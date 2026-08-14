#include "tinydraw/vector_v2/tile_producer.h"

#include <algorithm>
#include <array>
#include <limits>
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
#include <chrono>
#endif

#include "tinydraw/vector_v2/raster_census.h"
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
      baseline_revision_(uniform_baseline_revision),
      baseline_color_(baseline_color) {}

bool TileProducer::ready() const {
  const auto supertask_bytes = std::as_bytes(workspace_.supertask_pixels);
  const auto packed_bytes = std::as_bytes(workspace_.packed_tile_pixels);
  const auto mask_bytes = std::as_bytes(workspace_.finalized_pixels);
  return log_.ready() && canvas_.ready() &&
         workspace_.supertask_pixels.size() >= kTileProducerPixels &&
         workspace_.packed_tile_pixels.size() >= kTilePixels &&
         workspace_.finalized_pixels.size() >= kTileProducerMaskBytes &&
         !storage_overlaps(supertask_bytes, packed_bytes) &&
         !storage_overlaps(supertask_bytes, mask_bytes) &&
         !storage_overlaps(packed_bytes, mask_bytes) &&
         canvas_.accepts_external_workspace(supertask_bytes) &&
         canvas_.accepts_external_workspace(packed_bytes) &&
         canvas_.accepts_external_workspace(mask_bytes) &&
         !log_.workspace_overlaps_storage(supertask_bytes) &&
         !log_.workspace_overlaps_storage(packed_bytes) &&
         !log_.workspace_overlaps_storage(mask_bytes);
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
  return TileProductionStep{.level_bounds = publication.level_bounds,
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
    active_group_ = {};
    return std::nullopt;
  }
  if (*remaining == 0U) {
    active_group_ = {};
    return TileProductionStep{.complete = true};
  }
  const bool active_group_is_current = active_group_.active &&
                                       active_group_.epoch == log_.epoch() &&
                                       active_group_.revision == log_.current_revision() &&
                                       active_group_.revision == canvas_.current_revision();
  if (!active_group_is_current || !(active_group_.view.zoom == view.zoom &&
                                    active_group_.view.level_pixels == view.level_pixels)) {
    const auto group = choose_missing_group(view);
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
  active_group_ = {};
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
  active_group_ = {.view = view,
                   .origin = group_origin,
                   .bounds = bounds,
                   .epoch = epoch,
                   .revision = revision,
                   .first_operation = replay->first_operation,
                   .operation_count = replay->operation_count,
                   .next_operation = replay->operation_count,
                   .next_sample = 0,
                   .next_segment_step = 0,
                   .active = true};
  return true;
}

TileProducer::RasterStepBatch TileProducer::choose_raster_step_batch(
    const IncrementalSegment& segment, std::size_t step_budget,
    std::size_t raster_work_budget) const {
  const std::size_t total_steps =
      incremental_segment_step_count(segment.first, segment.second, active_group_.view.zoom);
  RasterStepBatch batch{};
  while (active_group_.next_segment_step + batch.steps < total_steps && batch.steps < step_budget) {
    const std::size_t step = active_group_.next_segment_step + batch.steps;
    const std::size_t work = incremental_segment_step_work(
        segment.first, segment.second, active_group_.view.zoom, active_group_.bounds, step);
    if (batch.steps != 0U && (batch.raster_work >= raster_work_budget ||
                              work > raster_work_budget - batch.raster_work)) {
      break;
    }
    ++batch.steps;
    batch.raster_work += work;
  }
  return batch;
}

void TileProducer::finish_active_segment(const StoredOperation& operation, std::size_t startpoint,
                                         TileProductionStep& result,
                                         std::size_t& operations_consumed) {
  active_group_.next_segment_step = 0U;
  const bool operation_complete = operation.samples.size() == 1U || startpoint == 0U;
  if (operation_complete) {
    --active_group_.next_operation;
    active_group_.next_sample = 0U;
    ++operations_consumed;
    ++result.operations_scanned;
  } else {
    active_group_.next_sample = startpoint;
  }
}

bool TileProducer::render_active_operation(const StoredOperation& operation,
                                           TileProductionStep& result,
                                           std::size_t& operations_consumed,
                                           std::size_t& raster_steps_consumed,
                                           std::size_t& raster_work_consumed) {
  const bool visible =
      intersects(operation_level_bounds(operation.world_bounds, active_group_.view.zoom),
                 active_group_.bounds);
  if (!visible) {
    TINYDRAW_V2_CENSUS_ADD(operations_bbox_rejected, 1);
    --active_group_.next_operation;
    active_group_.next_sample = 0U;
    active_group_.next_segment_step = 0U;
    ++operations_consumed;
    ++result.operations_scanned;
    return true;
  }

  if (operation.samples.size() > 1U && active_group_.next_sample == 0U) {
    active_group_.next_sample = operation.samples.size() - 1U;
  }
  const std::size_t endpoint = active_group_.next_sample;
  // Replay one source segment per unit, exactly like forward painting. A
  // coalesced collinear capsule is equal to the per-segment union only in
  // real arithmetic; covers_pixel float rounding can flip boundary pixels
  // (caught by the collinear fuzz gate), so reverse replay must use the same
  // segment decomposition as the forward authority.
  const std::size_t startpoint = operation.samples.size() == 1U ? 0U : endpoint - 1U;
  const IncrementalSegment segment{
      .tool = operation.tool,
      .color = operation.color,
      .first = operation.samples[startpoint],
      .second = operation.samples[endpoint],
  };
  if (!intersects(
          incremental_segment_level_bounds(segment.first, segment.second, active_group_.view.zoom),
          active_group_.bounds)) {
    // Count a rejected segment against the per-call cursor budget so a single
    // giant operation cannot monopolize input polling even when it is distant.
    TINYDRAW_V2_CENSUS_ADD(segments_bbox_rejected, 1);
    ++raster_steps_consumed;
    finish_active_segment(operation, startpoint, result, operations_consumed);
    return true;
  }

  const std::size_t total_steps =
      incremental_segment_step_count(segment.first, segment.second, active_group_.view.zoom);
  const RasterStepBatch batch =
      choose_raster_step_batch(segment, kTileProducerSampleBatch - raster_steps_consumed,
                               kTileProducerRasterWorkBatch - raster_work_consumed);
  if (batch.steps == 0U) {
    return false;
  }
  const auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  if (!apply_masked_incremental_segment(
          segment,
          {.zoom = active_group_.view.zoom,
           .level_bounds = active_group_.bounds,
           .pixels = surface,
           .stride = kTileProducerWidth},
          workspace_.finalized_pixels.first(kTileProducerMaskBytes))) {
    return false;
  }
  TINYDRAW_V2_CENSUS_ADD(segments_painted, 1);
  active_group_.next_segment_step += batch.steps;
  raster_steps_consumed += batch.steps;
  raster_work_consumed += batch.raster_work;
  result.raster_steps += batch.steps;
  result.raster_work += batch.raster_work;
  ++result.operations_rendered;

  if (active_group_.next_segment_step < total_steps) {
    return true;
  }
  finish_active_segment(operation, startpoint, result, operations_consumed);
  return true;
}

std::optional<TileProductionStep> TileProducer::render_active_batch() {
  if (!active_group_.active || log_.epoch() != active_group_.epoch ||
      log_.current_revision() != active_group_.revision ||
      canvas_.current_revision() != active_group_.revision) {
    active_group_ = {};
    return std::nullopt;
  }
  TileProductionStep result{.level_bounds = active_group_.bounds};
  std::size_t operations_consumed = 0;
  std::size_t raster_steps_consumed = 0;
  std::size_t raster_work_consumed = 0;
  while (active_group_.next_operation != 0U && operations_consumed < kTileProducerOperationBatch &&
         raster_steps_consumed < kTileProducerSampleBatch &&
         raster_work_consumed < kTileProducerRasterWorkBatch) {
    const auto stored =
        log_.operation(active_group_.first_operation + active_group_.next_operation - 1U);
    if (!stored.has_value()) {
      active_group_ = {};
      return std::nullopt;
    }
    if (!render_active_operation(*stored, result, operations_consumed, raster_steps_consumed,
                                 raster_work_consumed)) {
      active_group_ = {};
      return std::nullopt;
    }
  }
  if (active_group_.next_operation != 0U) {
    // No tile can be published until this exact newest-first group replay is
    // complete, so the visible missing count cannot change during a slice.
    // Avoid rescanning PSRAM slot metadata on every resumable batch.
    return result;
  }
  const auto published = publish_group(active_group_.bounds, active_group_.view.level_pixels,
                                       active_group_.view.zoom, active_group_.revision);
  if (!published.has_value()) {
    active_group_ = {};
    return std::nullopt;
  }
  if (published->tiles_published != 0U) {
    result.level_bounds = published->level_bounds;
    result.tiles_published = published->tiles_published;
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
