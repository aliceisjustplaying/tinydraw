#include "tinydraw/vector_v2/tile_producer.h"

#include <algorithm>
#include <array>
#include <limits>
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
#include <chrono>
#endif

#include "tinydraw/vector_v2/raster_census.h"
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
#include "tinydraw/vector_v2/rerender_ledger.h"
#endif
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
                           DocumentRevision uniform_baseline_revision, std::uint16_t baseline_color)
    : log_(log),
      canvas_(canvas),
      workspace_(workspace),
      summary_(workspace.summary_row_unset, workspace.summary_saturated_words),
      baseline_revision_(uniform_baseline_revision),
      baseline_color_(baseline_color) {}

bool TileProducer::ready() const {
  const std::array<std::span<const std::byte>, 5> workspaces{
      std::as_bytes(workspace_.supertask_pixels), std::as_bytes(workspace_.finalized_pixels),
      std::as_bytes(workspace_.summary_row_unset),
      std::as_bytes(workspace_.summary_saturated_words),
      std::span<const std::byte>(workspace_.operation_chord_plans)};
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
  return !workspace_invalid && log_.ready() && canvas_.ready() &&
         workspace_.supertask_pixels.size() >= kTileProducerPixels &&
         workspace_.finalized_pixels.size() >= kTileProducerMaskBytes &&
         workspace_.summary_row_unset.size() >= kTileProducerSummaryRows &&
         workspace_.summary_saturated_words.size() >= kTileProducerSummaryWords &&
         workspace_.operation_chord_plans.size() >= kOperationChordStorageBytes;
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
         source->revision == canvas_.current_revision() &&
         static_cast<int>(source->quality) >= static_cast<int>(quality);
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
  const auto replay = log_.active_replay_range(epoch);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0 || !replay.has_value() ||
      revision != canvas_.current_revision()) {
    return false;
  }
  auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  std::fill(surface.begin(), surface.end(), baseline_color_);
  std::fill_n(workspace_.finalized_pixels.begin(), kTileProducerMaskBytes, std::uint8_t{0});
  summary_.reset(bounds.y1 - bounds.y0, bounds.x1 - bounds.x0);
  active_group_ = {.view = view,
                   .origin = group_origin,
                   .bounds = bounds,
                   .epoch = epoch,
                   .revision = revision,
                   .first_operation = replay->first_operation,
                   .next_operation = replay->first_operation + replay->operation_count,
                   .next_sample = 0,
                   .active = true};
  return true;
}

bool TileProducer::active_group_has_work() const {
  return active_group_.cached_operation_index != kNoCachedOperation ||
         active_group_.next_operation > active_group_.first_operation;
}

void TileProducer::discard_active_group() { active_group_ = {}; }

void TileProducer::consume_active_operation(TileProductionStep&, std::size_t& operations_consumed) {
  active_group_.next_sample = 0U;
  active_group_.batch_active = false;
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
  if (active_group_.next_operation <= active_group_.first_operation) {
    return OperationGate::kConsumed;
  }
  const std::size_t operation_index = --active_group_.next_operation;
  const auto stored = log_.operation(operation_index);
  if (!stored.has_value()) {
    return OperationGate::kFailed;
  }
  ++result.operations_scanned;
  const PixelRect operation_bounds =
      operation_level_bounds(stored->world_bounds, active_group_.view.zoom);
  if (!intersects(operation_bounds, active_group_.bounds)) {
    TINYDRAW_V2_CENSUS_ADD(operations_bbox_rejected, 1);
    consume_active_operation(result, operations_consumed);
    return OperationGate::kConsumed;
  }
  ++result.operations_intersecting;
  if (summary_.rows_saturated(
          std::max(operation_bounds.y0, active_group_.bounds.y0) - active_group_.bounds.y0,
          std::min(operation_bounds.y1, active_group_.bounds.y1) - 1 - active_group_.bounds.y0)) {
    // Every pixel this operation could touch inside the group is already
    // finalized by newer paint, so replaying it is a provable no-op.
    TINYDRAW_V2_CENSUS_ADD(operations_saturation_skipped, 1);
    consume_active_operation(result, operations_consumed);
    return OperationGate::kConsumed;
  }
  active_group_.cached_operation_index = operation_index;
  active_group_.cached_operation = *stored;
  return OperationGate::kReady;
}

void TileProducer::finish_active_batch(TileProductionStep& result,
                                       std::size_t& operations_consumed) {
  active_group_.batch_active = false;
  if (active_group_.batch_next_endpoint == 0U) {
    consume_active_operation(result, operations_consumed);
  } else {
    active_group_.next_sample = active_group_.batch_next_endpoint;
  }
}

bool TileProducer::render_active_operation_slice(TileProductionStep& result,
                                                 std::size_t& operations_consumed,
                                                 std::size_t& chords_consumed,
                                                 std::size_t& work_consumed) {
  const StoredOperation& operation = active_group_.cached_operation;
  if (!active_group_.batch_active) {
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    const std::uint32_t setup_started = raster_census_now();
#endif
    if (active_group_.next_sample == 0U) {
      active_group_.next_sample = operation.samples.size() - 1U;
    }
    const auto batch = prepare_operation_chord_batch(
        operation.samples, active_group_.next_sample, active_group_.view.zoom, active_group_.bounds,
        workspace_.operation_chord_plans.first(kOperationChordStorageBytes));
    if (!batch.has_value()) {
      return false;
    }
    // Preparation moves the endpoint cursor; count it against the per-call
    // chord budget even when every chord clipped away.
    chords_consumed += std::max<std::size_t>(batch->chord_count, 1U);
    active_group_.batch_chords = batch->chord_count;
    active_group_.batch_next_endpoint = batch->next_endpoint;
    active_group_.batch_bounds = batch->clipped_bounds;
    active_group_.batch_work = batch->raster_work;
    active_group_.batch_row = batch->clipped_bounds.y0;
    active_group_.batch_active = true;
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    TINYDRAW_V2_CENSUS_ADD(setup_ticks, raster_census_now() - setup_started);
#endif
    if (batch->chord_count == 0U) {
      TINYDRAW_V2_CENSUS_ADD(segments_bbox_rejected, 1);
      finish_active_batch(result, operations_consumed);
      return true;
    }
    if (summary_.rows_saturated(batch->clipped_bounds.y0 - active_group_.bounds.y0,
                                batch->clipped_bounds.y1 - 1 - active_group_.bounds.y0)) {
      // The whole batch footprint lies in saturated rows.
      TINYDRAW_V2_CENSUS_ADD(segments_saturation_skipped, 1);
      finish_active_batch(result, operations_consumed);
      return true;
    }
  }
  const auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  const std::size_t max_work =
      kTileProducerSweepWorkBatch - std::min(kTileProducerSweepWorkBatch, work_consumed);
  if (max_work == 0U) {
    return true;
  }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  const std::uint32_t paint_started = raster_census_now();
#endif
  const OperationChordBatch batch{
      .chord_count = active_group_.batch_chords,
      .next_endpoint = active_group_.batch_next_endpoint,
      .clipped_bounds = active_group_.batch_bounds,
      .raster_work = active_group_.batch_work,
  };
  OperationSweepSlice slice{};
  if (!apply_masked_operation_chord_rows(
          operation.tool, operation.color,
          workspace_.operation_chord_plans.first(kOperationChordStorageBytes), batch,
          active_group_.batch_row, max_work,
          {.zoom = active_group_.view.zoom,
           .level_bounds = active_group_.bounds,
           .pixels = surface,
           .stride = kTileProducerWidth},
          workspace_.finalized_pixels.first(kTileProducerMaskBytes), &summary_, slice)) {
    return false;
  }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  TINYDRAW_V2_CENSUS_ADD(paint_ticks, raster_census_now() - paint_started);
#endif
  work_consumed += slice.work_px;
  active_group_.batch_row = slice.next_row;
  if (slice.next_row >= active_group_.batch_bounds.y1) {
    TINYDRAW_V2_CENSUS_ADD(segments_painted, static_cast<std::uint64_t>(batch.chord_count));
    result.raster_steps += batch.chord_count;
    result.raster_work += active_group_.batch_work;
    ++result.operations_rendered;
    finish_active_batch(result, operations_consumed);
  }
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
  std::size_t chords_consumed = 0;
  std::size_t work_consumed = 0;
  while (active_group_has_work() && operations_consumed < kTileProducerOperationBatch &&
         chords_consumed < kTileProducerSampleBatch &&
         work_consumed < kTileProducerSweepWorkBatch) {
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    const std::uint32_t gate_started = raster_census_now();
#endif
    if (summary_.all_saturated()) {
      // Every pixel of the group surface is finalized; the remaining older
      // operations cannot change any pixel. Complete the replay immediately.
      TINYDRAW_V2_CENSUS_ADD(groups_saturated_early, 1);
      active_group_.next_operation = active_group_.first_operation;
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
    if (!render_active_operation_slice(result, operations_consumed, chords_consumed,
                                       work_consumed)) {
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
  if (work_consumed >= kTileProducerSweepWorkBatch) {
    // Preserve the interaction boundary after a slice-filling final sweep.
    // The completed group publishes on the next producer call.
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
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
    if (rerender_ledger_ != nullptr) {
      static_cast<void>(rerender_ledger_->record_group_render(
          active_group_.view.zoom, active_group_.origin.column, active_group_.origin.row,
          active_group_.revision));
    }
#endif
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
  constexpr auto kStride = static_cast<std::size_t>(kTileProducerWidth);
  const auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  // Publish straight from the supertask surface: one strided read replaces
  // the former supertask->packed->pool double copy.
  const auto origin = static_cast<std::size_t>(bounds.y0 - rendered_bounds.y0) * kStride +
                      static_cast<std::size_t>(bounds.x0 - rendered_bounds.x0);
  const auto strided = surface.subspan(
      origin, static_cast<std::size_t>(height - 1) * kStride + static_cast<std::size_t>(width));
  const auto analysis = analyze_tile_payload(strided, width, height, kStride);
  if (!analysis.has_value()) {
    return false;
  }
  if (analysis->uniform) {
    return canvas_
        .publish_uniform(key, revision, MaterializationQuality::kImmediate, analysis->uniform_color)
        .has_value();
  }
  return canvas_.publish_tile(key, revision, MaterializationQuality::kImmediate, strided, kStride)
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
