#include "tinydraw/vector_v2/tile_producer.h"

#include <algorithm>
#include <array>
#include <limits>
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
#include <chrono>
#endif

#include "tinydraw/vector_v2/raster_census.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "tinydraw/vector_v2/storage_overlap.h"
#include "tinydraw/vector_v2/tile_payload_analysis.h"

namespace tinydraw::vector_v2 {
namespace {

bool intersects(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

std::size_t distance_squared(int x, int y, int center_x, int center_y) {
  const auto delta_x = static_cast<std::int64_t>(x) - center_x;
  const auto delta_y = static_cast<std::int64_t>(y) - center_y;
  return static_cast<std::size_t>(delta_x * delta_x + delta_y * delta_y);
}

}  // namespace

TileProducer::TileProducer(OperationLog& log, MaterializedCanvas& canvas,
                           TileProducerWorkspace workspace,
                           DocumentRevision uniform_baseline_revision, std::uint16_t baseline_color,
                           RenderAccounting* render_accounting)
    : log_(log),
      canvas_(canvas),
      workspace_(workspace),
      summary_(workspace.summary_row_unset, workspace.summary_saturated_words),
      replay_index_(workspace.replay_index_words),
      render_accounting_(render_accounting),
      baseline_revision_(uniform_baseline_revision),
      baseline_color_(baseline_color) {}

bool TileProducer::ready() const {
  const std::array<std::span<const std::byte>, 5> workspaces{
      std::as_bytes(workspace_.supertask_pixels), std::as_bytes(workspace_.packed_tile_pixels),
      std::as_bytes(workspace_.finalized_pixels), std::as_bytes(workspace_.summary_row_unset),
      std::as_bytes(workspace_.summary_saturated_words)};
  bool workspace_invalid = false;
  for (std::size_t left = 0; left < workspaces.size(); ++left) {
    workspace_invalid = workspace_invalid ||
                        !canvas_.accepts_external_workspace(workspaces[left]) ||
                        log_.workspace_overlaps_storage(workspaces[left]);
    for (std::size_t right = left + 1U; right < workspaces.size(); ++right) {
      workspace_invalid =
          workspace_invalid || storage_overlaps(workspaces[left], workspaces[right]);
    }
  }
  if (!workspace_.replay_index_words.empty()) {
    const auto replay_bytes = std::as_bytes(workspace_.replay_index_words);
    workspace_invalid = workspace_invalid || !replay_index_.ready() ||
                        !canvas_.accepts_external_workspace(replay_bytes) ||
                        log_.workspace_overlaps_storage(replay_bytes);
    for (const auto existing : workspaces) {
      workspace_invalid = workspace_invalid || storage_overlaps(existing, replay_bytes);
    }
  }
  return !workspace_invalid && log_.ready() && canvas_.ready() &&
         workspace_.supertask_pixels.size() >= kTileProducerPixels &&
         workspace_.packed_tile_pixels.size() >= kTilePixels &&
         workspace_.finalized_pixels.size() >= kTileProducerMaskBytes &&
         workspace_.summary_row_unset.size() >= kTileProducerSummaryRows &&
         workspace_.summary_saturated_words.size() >= kTileProducerSummaryWords;
}

const CandidateDiscoveryCounters& TileProducer::candidate_counters() const {
  return candidate_counters_;
}

void TileProducer::reset_candidate_counters() { candidate_counters_ = {}; }

bool TileProducer::sync_replay_index() {
  return !replay_index_.ready() || replay_index_.sync(log_);
}

bool TileProducer::valid_view(const ViewRequest& view) {
  if (view.zoom == ZoomLevel::k25Percent) {
    return false;
  }
  const int level_width = kWorldWidth * zoom_percent(view.zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(view.zoom) / 100;
  return view.level_pixels.x0 >= 0 && view.level_pixels.y0 >= 0 &&
         view.level_pixels.x1 > view.level_pixels.x0 &&
         view.level_pixels.y1 > view.level_pixels.y0 && view.level_pixels.x1 <= level_width &&
         view.level_pixels.y1 <= level_height;
}

bool TileProducer::tile_satisfies(TileKey key, MaterializationQuality quality) const {
  const auto source = canvas_.lookup(key);
  return source.has_value() && source->kind != SourceKind::kOverview &&
         source->identity.revision == canvas_.current_revision() &&
         static_cast<int>(source->identity.quality) >= static_cast<int>(quality);
}

std::optional<std::size_t> TileProducer::visible_tiles_remaining(const ViewRequest& view) const {
  return visible_tiles_remaining(view, MaterializationQuality::kImmediate);
}

std::optional<std::size_t> TileProducer::visible_tiles_remaining(
    const ViewRequest& view, MaterializationQuality quality) const {
  if (!ready() || !valid_view(view)) {
    return std::nullopt;
  }
  const PixelRect rect = view.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  std::size_t remaining = 0;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      remaining += !tile_satisfies(
          {view.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)},
          quality);
    }
  }
  return remaining;
}

std::optional<TileKey> TileProducer::choose_missing_group(const ViewRequest& view) const {
  const PixelRect rect = view.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  const int center_x = (rect.x0 + rect.x1) / 2;
  const int center_y = (rect.y0 + rect.y1) / 2;
  std::optional<TileKey> selected;
  std::size_t best_distance = std::numeric_limits<std::size_t>::max();
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      if (tile_satisfies(key, MaterializationQuality::kImmediate)) {
        continue;
      }
      const int group_column = column & ~1;
      const int group_row = row & ~1;
      const std::size_t candidate_distance =
          distance_squared(group_column * kTileWidth + kTileProducerWidth / 2,
                           group_row * kTileHeight + kTileProducerHeight / 2, center_x, center_y);
      if (!selected.has_value() || candidate_distance < best_distance) {
        selected = {view.zoom, static_cast<std::uint16_t>(group_column),
                    static_cast<std::uint16_t>(group_row)};
        best_distance = candidate_distance;
      }
    }
  }
  return selected;
}

std::optional<TileKey> TileProducer::choose_certain_paper_group(const ViewRequest& view) const {
  if (!ready() || !valid_view(view)) {
    return std::nullopt;
  }
  const PixelRect rect = view.level_pixels;
  for (int row = rect.y0 / kTileHeight; row <= (rect.y1 - 1) / kTileHeight; ++row) {
    for (int column = rect.x0 / kTileWidth; column <= (rect.x1 - 1) / kTileWidth; ++column) {
      const TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      if (!tile_satisfies(key, MaterializationQuality::kImmediate) &&
          canvas_.certainly_paper(key)) {
        return TileKey{view.zoom, static_cast<std::uint16_t>(column & ~1),
                       static_cast<std::uint16_t>(row & ~1)};
      }
    }
  }
  return std::nullopt;
}

std::optional<TileProductionStep> TileProducer::publish_certain_paper_group(const ViewRequest& view,
                                                                            TileKey origin) {
  const TileGrid grid = tile_grid(view.zoom);
  GroupPublication publication{};
  for (int row = origin.row; row < std::min(grid.rows, static_cast<int>(origin.row) + 2); ++row) {
    for (int column = origin.column;
         column < std::min(grid.columns, static_cast<int>(origin.column) + 2); ++column) {
      const TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      const PixelRect bounds = tile_pixel_bounds(key);
      if (!intersects(bounds, view.level_pixels) ||
          tile_satisfies(key, MaterializationQuality::kImmediate) ||
          !canvas_.certainly_paper(key)) {
        continue;
      }
      if (!canvas_.publish_uniform(key, canvas_.current_revision(),
                                   MaterializationQuality::kImmediate, baseline_color_)) {
        return std::nullopt;
      }
      include_bounds(bounds, publication);
    }
  }
  const auto remaining = visible_tiles_remaining(view);
  if (publication.tiles_published == 0U || !remaining.has_value()) {
    return std::nullopt;
  }
  ++candidate_counters_.groups_published;
  return TileProductionStep{.level_bounds = publication.level_bounds,
                            .groups_published = 1,
                            .tiles_published = publication.tiles_published,
                            .visible_tiles_remaining = *remaining,
                            .complete = *remaining == 0U};
}

std::optional<TileProductionStep> TileProducer::produce_next(const ViewRequest& view) {
  if (active_group_.active && active_group_.epoch == log_.epoch() &&
      active_group_.revision == log_.current_revision() &&
      active_group_.revision == canvas_.current_revision() &&
      active_group_.view.zoom == view.zoom &&
      active_group_.view.level_pixels == view.level_pixels) {
    // The active group is current for this exact view. No tile can be
    // published until its newest-first replay completes, so the visible
    // missing count cannot change; skip the per-slice remaining scan that
    // walks every visible tile through the PSRAM slot directory.
    return render_active_batch();
  }
  if (!active_group_.active) {
    if (const auto paper = choose_certain_paper_group(view); paper.has_value()) {
      active_group_ = {};
      return publish_certain_paper_group(view, *paper);
    }
  }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  const auto remaining_scan_started = std::chrono::steady_clock::now();
#endif
  const auto remaining = visible_tiles_remaining(view);
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  TINYDRAW_V2_CENSUS_ADD(remaining_scans, 1);
  TINYDRAW_V2_CENSUS_ADD(
      remaining_scan_ns,
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - remaining_scan_started)
                                     .count()));
#endif
  if (!remaining.has_value()) {
    discard_active_group();
    return std::nullopt;
  }
  if (*remaining == 0U) {
    discard_active_group();
    record_view_reuses(view);
    return TileProductionStep{.complete = true};
  }
  const bool active_group_is_current = active_group_.active &&
                                       active_group_.epoch == log_.epoch() &&
                                       active_group_.revision == log_.current_revision() &&
                                       active_group_.revision == canvas_.current_revision();
  if (!active_group_is_current || !(active_group_.view.zoom == view.zoom &&
                                    active_group_.view.level_pixels == view.level_pixels)) {
    const auto group = choose_missing_group(view);
    discard_active_group();
    if (!group.has_value() || !start_group(view, *group)) {
      active_group_ = {};
      return std::nullopt;
    }
  }
  return render_active_batch();
}

bool TileProducer::reset_uniform_baseline(DocumentRevision revision, std::uint16_t color) {
  if (log_.operation_count() != 0U || log_.current_revision() != revision ||
      canvas_.current_revision() != revision) {
    return false;
  }
  baseline_revision_ = revision;
  baseline_color_ = color;
  discard_active_group();
  return true;
}

bool TileProducer::start_group(const ViewRequest& view, TileKey group_origin) {
  const TileGrid grid = tile_grid(view.zoom);
  const int first_column = group_origin.column;
  const int first_row = group_origin.row;
  const int last_column = std::min(grid.columns, first_column + kTileProducerColumns);
  const int last_row = std::min(grid.rows, first_row + kTileProducerRows);
  const int level_width = kWorldWidth * zoom_percent(view.zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(view.zoom) / 100;
  const PixelRect bounds{
      .x0 = first_column * kTileWidth,
      .y0 = first_row * kTileHeight,
      .x1 = std::min(level_width, last_column * kTileWidth),
      .y1 = std::min(level_height, last_row * kTileHeight),
  };
  const OperationLogEpoch epoch = log_.epoch();
  const DocumentRevision revision = log_.current_revision();
  const auto replay = log_.replay_range(epoch, baseline_revision_, revision);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0 || !replay.has_value() ||
      revision != canvas_.current_revision()) {
    return false;
  }
  auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  std::fill(surface.begin(), surface.end(), baseline_color_);
  std::fill_n(workspace_.finalized_pixels.begin(), kTileProducerMaskBytes, std::uint8_t{0});
  summary_.reset(bounds.y1 - bounds.y0, bounds.x1 - bounds.x0);
  const int percent = zoom_percent(view.zoom);
  const PixelRect world_query{
      .x0 = bounds.x0 * 100 / percent,
      .y0 = bounds.y0 * 100 / percent,
      .x1 = (bounds.x1 * 100 + percent - 1) / percent,
      .y1 = (bounds.y1 * 100 + percent - 1) / percent,
  };
  const bool indexed = replay_index_.ready() && replay_index_.sync(log_);
  const ReplayBlockIndex linear_fallback;
  active_group_ = {
      .view = view,
      .origin = group_origin,
      .bounds = bounds,
      .epoch = epoch,
      .revision = revision,
      .candidates = (indexed ? replay_index_ : linear_fallback)
                        .query(world_query, replay->first_operation, replay->operation_count),
      .next_sample = 0,
      .active = true};
  if (render_accounting_ != nullptr) {
    render_accounting_->record_attempt(active_group_key());
  }
  return true;
}

bool TileProducer::active_group_has_work() const {
  return active_group_.cached_operation_index != kNoCachedOperation ||
         active_group_.candidates.next_operation > active_group_.candidates.first_operation;
}

RenderGroupKey TileProducer::active_group_key() const {
  return {.revision = active_group_.revision,
          .zoom = active_group_.view.zoom,
          .group_column = active_group_.origin.column,
          .group_row = active_group_.origin.row};
}

void TileProducer::discard_active_group() {
  if (active_group_.active && render_accounting_ != nullptr) {
    render_accounting_->record_discard(active_group_key());
  }
  active_group_ = {};
}

void TileProducer::record_view_reuses(const ViewRequest& view) {
  if (render_accounting_ == nullptr) {
    return;
  }
  const int first_column = (view.level_pixels.x0 / kTileWidth) & ~1;
  const int last_column = ((view.level_pixels.x1 - 1) / kTileWidth) & ~1;
  const int first_row = (view.level_pixels.y0 / kTileHeight) & ~1;
  const int last_row = ((view.level_pixels.y1 - 1) / kTileHeight) & ~1;
  for (int row = first_row; row <= last_row; row += kTileProducerRows) {
    for (int column = first_column; column <= last_column; column += kTileProducerColumns) {
      render_accounting_->record_reuse({.revision = canvas_.current_revision(),
                                        .zoom = view.zoom,
                                        .group_column = static_cast<std::uint16_t>(column),
                                        .group_row = static_cast<std::uint16_t>(row)});
    }
  }
}

void TileProducer::consume_active_operation(TileProductionStep&, std::size_t& operations_consumed) {
  active_group_.next_sample = 0U;
  active_group_.next_curve_step = 0U;
  active_group_.cached_operation_index = kNoCachedOperation;
  ++operations_consumed;
}

// Runs the operation-level visibility and saturation gates exactly once per
// operation and caches the passing fetch, so per-segment replay pays neither
// the log lookup nor the operation-rectangle math again.
TileProducer::OperationGate TileProducer::gate_active_operation(TileProductionStep& result,
                                                                std::size_t& operations_consumed) {
  if (active_group_.cached_operation_index != kNoCachedOperation) {
    return OperationGate::kReady;
  }
  const auto operation_index = ReplayBlockIndex::previous(active_group_.candidates);
  if (!operation_index.has_value()) {
    return OperationGate::kConsumed;
  }
  const auto stored = log_.operation(*operation_index);
  if (!stored.has_value()) {
    return OperationGate::kFailed;
  }
  ++result.operations_scanned;
  ++candidate_counters_.operations_scanned;
  const PixelRect operation_bounds =
      operation_level_bounds(stored->world_bounds, active_group_.view.zoom);
  if (!intersects(operation_bounds, active_group_.bounds)) {
    TINYDRAW_V2_CENSUS_ADD(operations_bbox_rejected, 1);
    consume_active_operation(result, operations_consumed);
    return OperationGate::kConsumed;
  }
  ++result.operations_intersecting;
  ++candidate_counters_.operations_intersecting;
  if (summary_.rows_saturated(
          std::max(operation_bounds.y0, active_group_.bounds.y0) - active_group_.bounds.y0,
          std::min(operation_bounds.y1, active_group_.bounds.y1) - 1 - active_group_.bounds.y0)) {
    // Every pixel this operation could touch inside the group is already
    // finalized by newer paint, so replaying it is a provable no-op.
    TINYDRAW_V2_CENSUS_ADD(operations_saturation_skipped, 1);
    consume_active_operation(result, operations_consumed);
    return OperationGate::kConsumed;
  }
  active_group_.cached_operation_index = *operation_index;
  active_group_.cached_operation = *stored;
  return OperationGate::kReady;
}

void TileProducer::finish_active_segment(std::size_t endpoint, TileProductionStep& result,
                                         std::size_t& operations_consumed) {
  const StoredOperation& operation = active_group_.cached_operation;
  const bool operation_complete = operation.samples.size() <= 2U || endpoint == 2U;
  if (operation_complete) {
    consume_active_operation(result, operations_consumed);
  } else {
    active_group_.next_sample = endpoint - 1U;
    active_group_.next_curve_step = 0U;
  }
}

bool TileProducer::render_active_segment(TileProductionStep& result,
                                         std::size_t& operations_consumed,
                                         std::size_t& raster_steps_consumed,
                                         std::size_t& raster_work_consumed) {
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  const std::uint32_t setup_started = raster_census_now();
#endif
  const StoredOperation& operation = active_group_.cached_operation;
  if (active_group_.next_sample == 0U) {
    active_group_.next_sample = operation.samples.size() - 1U;
  }
  const std::size_t endpoint = active_group_.next_sample;
  if (active_group_.next_curve_step == 0U) {
    const auto prepared =
        prepare_incremental_curve_unit(operation.samples, endpoint, active_group_.view.zoom);
    if (!prepared.has_value() || prepared->step_count == 0U) {
      return false;
    }
    active_group_.prepared_unit = *prepared;
    active_group_.next_curve_step = prepared->step_count;
  }
  // The whole unit paints as one row sweep, so its chords' clipped bounds
  // union into one work estimate, one rejection, and one saturation gate.
  const std::size_t unit_steps = active_group_.next_curve_step;
  PixelRect clipped{active_group_.bounds.x1, active_group_.bounds.y1, active_group_.bounds.x0,
                    active_group_.bounds.y0};
  std::size_t raster_work = 0;
  for (std::size_t step_index = 0; step_index < unit_steps; ++step_index) {
    const auto step_bounds = prepared_curve_step_level_bounds(active_group_.prepared_unit,
                                                              step_index, active_group_.view.zoom);
    if (!step_bounds.has_value()) {
      continue;
    }
    const PixelRect step_clipped{
        .x0 = std::max(step_bounds->x0, active_group_.bounds.x0),
        .y0 = std::max(step_bounds->y0, active_group_.bounds.y0),
        .x1 = std::min(step_bounds->x1, active_group_.bounds.x1),
        .y1 = std::min(step_bounds->y1, active_group_.bounds.y1),
    };
    const std::size_t step_work =
        static_cast<std::size_t>(std::max(0, step_clipped.x1 - step_clipped.x0)) *
        static_cast<std::size_t>(std::max(0, step_clipped.y1 - step_clipped.y0));
    if (step_work == 0U) {
      TINYDRAW_V2_CENSUS_ADD(segments_bbox_rejected, 1);
      continue;
    }
    raster_work += step_work;
    clipped.x0 = std::min(clipped.x0, step_clipped.x0);
    clipped.y0 = std::min(clipped.y0, step_clipped.y0);
    clipped.x1 = std::max(clipped.x1, step_clipped.x1);
    clipped.y1 = std::max(clipped.y1, step_clipped.y1);
  }
  const auto finish_unit = [&] {
    active_group_.next_curve_step = 0U;
    finish_active_segment(endpoint, result, operations_consumed);
  };
  if (raster_work == 0U) {
    // Count a rejected unit against the per-call cursor budget.
    raster_steps_consumed += unit_steps;
    finish_unit();
    return true;
  }
  if (summary_.rows_saturated(clipped.y0 - active_group_.bounds.y0,
                              clipped.y1 - 1 - active_group_.bounds.y0)) {
    // The unit's clipped footprint lies entirely in saturated rows.
    TINYDRAW_V2_CENSUS_ADD(segments_saturation_skipped, 1);
    raster_steps_consumed += unit_steps;
    finish_unit();
    return true;
  }

  const auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  const std::uint32_t paint_started = raster_census_now();
  TINYDRAW_V2_CENSUS_ADD(setup_ticks, paint_started - setup_started);
#endif
  if (!apply_masked_prepared_curve_unit(
          operation.tool, operation.color, active_group_.prepared_unit,
          {.zoom = active_group_.view.zoom,
           .level_bounds = active_group_.bounds,
           .pixels = surface,
           .stride = kTileProducerWidth},
          workspace_.finalized_pixels.first(kTileProducerMaskBytes), &summary_)) {
    return false;
  }
  TINYDRAW_V2_CENSUS_ADD(segments_painted, 1);
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  TINYDRAW_V2_CENSUS_ADD(paint_ticks, raster_census_now() - paint_started);
#endif
  raster_steps_consumed += unit_steps;
  raster_work_consumed += raster_work;
  result.raster_steps += unit_steps;
  result.raster_work += raster_work;
  ++result.operations_rendered;
  finish_unit();
  return true;
}

std::optional<TileProductionStep> TileProducer::render_active_batch() {
  if (!active_group_.active || log_.epoch() != active_group_.epoch ||
      log_.current_revision() != active_group_.revision ||
      canvas_.current_revision() != active_group_.revision) {
    discard_active_group();
    return std::nullopt;
  }
  TileProductionStep result{.level_bounds = active_group_.bounds};
  std::size_t operations_consumed = 0;
  std::size_t raster_steps_consumed = 0;
  std::size_t raster_work_consumed = 0;
  while (active_group_has_work() && operations_consumed < kTileProducerOperationBatch &&
         raster_steps_consumed < kTileProducerSampleBatch &&
         raster_work_consumed < kTileProducerRasterWorkBatch) {
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    const std::uint32_t gate_started = raster_census_now();
#endif
    if (summary_.all_saturated()) {
      // Every pixel of the group surface is finalized; the remaining older
      // operations cannot change any pixel. Complete the replay immediately.
      TINYDRAW_V2_CENSUS_ADD(groups_saturated_early, 1);
      active_group_.candidates.next_operation = active_group_.candidates.first_operation;
      active_group_.next_sample = 0U;
      active_group_.cached_operation_index = kNoCachedOperation;
      break;
    }
    const OperationGate gate = gate_active_operation(result, operations_consumed);
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    TINYDRAW_V2_CENSUS_ADD(gate_ticks, raster_census_now() - gate_started);
#endif
    if (gate == OperationGate::kFailed) {
      discard_active_group();
      return std::nullopt;
    }
    if (gate == OperationGate::kConsumed) {
      continue;
    }
    if (!render_active_segment(result, operations_consumed, raster_steps_consumed,
                               raster_work_consumed)) {
      discard_active_group();
      return std::nullopt;
    }
  }
  if (active_group_has_work()) {
    // No tile can be published until this exact newest-first group replay is
    // complete, so the visible missing count cannot change during a slice.
    // Avoid rescanning PSRAM slot metadata on every resumable batch.
    return result;
  }
  if (raster_work_consumed >= kTileProducerRasterWorkBatch) {
    // Preserve the interaction boundary after an oversized final unit. The
    // completed group publishes on the next producer call.
    return result;
  }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  const std::uint32_t publish_started = raster_census_now();
#endif
  const auto published = publish_group(active_group_.bounds, active_group_.view.level_pixels,
                                       active_group_.view.zoom, active_group_.revision);
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  TINYDRAW_V2_CENSUS_ADD(publish_ticks, raster_census_now() - publish_started);
#endif
  if (!published.has_value()) {
    discard_active_group();
    return std::nullopt;
  }
  if (published->tiles_published != 0U) {
    result.level_bounds = published->level_bounds;
    result.tiles_published = published->tiles_published;
    result.groups_published = 1U;
    ++candidate_counters_.groups_published;
    if (render_accounting_ != nullptr) {
      render_accounting_->record_completion(active_group_key());
    }
    if (rerender_ledger_ != nullptr) {
      static_cast<void>(rerender_ledger_->record_group_render(
          active_group_.view.zoom, active_group_.origin.column, active_group_.origin.row,
          active_group_.revision));
    }
  }
  const ViewRequest view = active_group_.view;
  active_group_ = {};
  const auto remaining = visible_tiles_remaining(view);
  if (!remaining.has_value()) {
    return std::nullopt;
  }
  result.visible_tiles_remaining = *remaining;
  result.complete = *remaining == 0U;
  return result;
}

void TileProducer::include_bounds(PixelRect bounds, GroupPublication& publication) {
  if (publication.tiles_published == 0U) {
    publication.level_bounds = bounds;
  } else {
    publication.level_bounds.x0 = std::min(publication.level_bounds.x0, bounds.x0);
    publication.level_bounds.y0 = std::min(publication.level_bounds.y0, bounds.y0);
    publication.level_bounds.x1 = std::max(publication.level_bounds.x1, bounds.x1);
    publication.level_bounds.y1 = std::max(publication.level_bounds.y1, bounds.y1);
  }
  ++publication.tiles_published;
}

bool TileProducer::publish_surface_tile(TileKey key, PixelRect rendered_bounds,
                                        DocumentRevision revision) {
  const PixelRect bounds = tile_pixel_bounds(key);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  auto packed = workspace_.packed_tile_pixels.first(count);
  for (int row = 0; row < height; ++row) {
    const auto source =
        static_cast<std::size_t>(bounds.y0 - rendered_bounds.y0 + row) * kTileProducerWidth +
        static_cast<std::size_t>(bounds.x0 - rendered_bounds.x0);
    const auto destination = static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    std::copy_n(surface.begin() + static_cast<std::ptrdiff_t>(source), width,
                packed.begin() + static_cast<std::ptrdiff_t>(destination));
  }
  const auto analysis = analyze_tile_payload(packed, width, height);
  if (!analysis.has_value()) {
    return false;
  }
  if (analysis->uniform && canvas_.uniform_capacity() != 0U) {
    return canvas_
        .publish_uniform(key, revision, MaterializationQuality::kImmediate, analysis->uniform_color)
        .has_value();
  }
  return canvas_.publish_tile(key, revision, MaterializationQuality::kImmediate, packed)
      .has_value();
}

std::optional<TileProducer::GroupPublication> TileProducer::publish_group(
    PixelRect rendered_bounds, PixelRect visible_bounds, ZoomLevel zoom,
    DocumentRevision revision) {
  const int first_column = std::max(rendered_bounds.x0, visible_bounds.x0) / kTileWidth;
  const int first_row = std::max(rendered_bounds.y0, visible_bounds.y0) / kTileHeight;
  const int last_column = (std::min(rendered_bounds.x1, visible_bounds.x1) - 1) / kTileWidth;
  const int last_row = (std::min(rendered_bounds.y1, visible_bounds.y1) - 1) / kTileHeight;
  GroupPublication publication{};
  if (canvas_.pins_outstanding() != 0U) {
    return std::nullopt;
  }
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)};
      if (tile_satisfies(key, MaterializationQuality::kImmediate)) {
        continue;
      }
      const PixelRect bounds = tile_pixel_bounds(key);
      if (!publish_surface_tile(key, rendered_bounds, revision)) {
        return std::nullopt;
      }
      include_bounds(bounds, publication);
    }
  }
  return publication;
}

}  // namespace tinydraw::vector_v2
