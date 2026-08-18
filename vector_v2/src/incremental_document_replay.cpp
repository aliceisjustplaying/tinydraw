#include <algorithm>

#include "tinydraw/vector_v2/incremental_document.h"

namespace tinydraw::vector_v2 {

std::optional<HistoryChange> move_history_incrementally(OperationLog& log,
                                                        MaterializedCanvas& canvas,
                                                        HistoryDirection direction,
                                                        std::span<std::uint16_t> overview_scratch) {
  if (!log.ready() || !canvas.ready() || log.current_revision() != canvas.current_revision() ||
      !canvas.accepts_external_workspace(std::as_bytes(overview_scratch)) ||
      log.workspace_overlaps_storage(std::as_bytes(overview_scratch))) {
    return std::nullopt;
  }
  auto prepared = direction == HistoryDirection::kUndo ? log.prepare_undo() : log.prepare_redo();
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const HistoryChange change = prepared->change();
  const PixelRect overview_bounds = overview_bounds_for_world(change.affected_world_bounds);
  const int width = overview_bounds.x1 - overview_bounds.x0;
  const int height = overview_bounds.y1 - overview_bounds.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (width <= 0 || height <= 0 || overview_scratch.size() < pixel_count) {
    prepared->cancel();
    return std::nullopt;
  }
  auto pixels = overview_scratch.first(pixel_count);
  std::fill(pixels.begin(), pixels.end(), 0xFFFFU);
  const RasterSurface surface{
      .zoom = ZoomLevel::k25Percent,
      .level_bounds = overview_bounds,
      .pixels = pixels,
      .stride = width,
  };
  const std::span<std::uint16_t> candidate_workspace = overview_scratch.subspan(pixel_count);
  const PixelRect query_world_bounds{
      .x0 = overview_bounds.x0 * 4,
      .y0 = overview_bounds.y0 * 4,
      .x1 = overview_bounds.x1 * 4,
      .y1 = overview_bounds.y1 * 4,
  };
  const auto candidate_count =
      prepared->query_target_spatial(query_world_bounds, candidate_workspace);
  const std::size_t replay_count = candidate_count.value_or(change.active_operation_count);
  for (std::size_t offset = 0; offset < replay_count; ++offset) {
    const std::size_t index =
        candidate_count.has_value() ? candidate_workspace[*candidate_count - offset - 1U] : offset;
    const auto stored = prepared->target_operation(index);
    if (!stored.has_value()) {
      prepared->cancel();
      return std::nullopt;
    }
    const PixelRect level_bounds =
        operation_level_bounds(stored->world_bounds, ZoomLevel::k25Percent);
    const bool intersects =
        level_bounds.x0 < overview_bounds.x1 && overview_bounds.x0 < level_bounds.x1 &&
        level_bounds.y0 < overview_bounds.y1 && overview_bounds.y0 < level_bounds.y1;
    if (!intersects) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples}, surface)) {
      prepared->cancel();
      return std::nullopt;
    }
  }
  if (!canvas.commit_history_revision(
          change.generation, {.bounds = overview_bounds, .pixels = pixels},
          change.affected_world_bounds, log.history_timeline(),
          static_cast<std::uint16_t>(change.previous_active_operation_count),
          static_cast<std::uint16_t>(change.active_operation_count))) {
    prepared->cancel();
    return std::nullopt;
  }
  prepared->publish();
  auto map_storage = std::as_writable_bytes(overview_scratch);
  if (map_storage.size() >= kOccupancyBytes) {
    std::span<std::uint8_t> tiled_may_ink{reinterpret_cast<std::uint8_t*>(map_storage.data()),
                                          kOccupancyBytes};
    if (build_tiled_may_ink(log, tiled_may_ink)) {
      static_cast<void>(canvas.replace_tiled_may_ink(change.generation, tiled_may_ink));
    }
  }
  return change;
}

std::size_t pending_operation_count(const OperationLog& log, const MaterializedCanvas& canvas) {
  if (!log.ready() || !canvas.ready()) {
    return 0;
  }
  const auto range =
      log.replay_range(log.epoch(), canvas.current_revision(), log.current_revision());
  return range.has_value() ? range->operation_count : 0;
}

bool overlay_pending_operations(const OperationLog& log, const MaterializedCanvas& canvas,
                                const RasterSurface& surface) {
  if (!log.ready() || !canvas.ready() || surface.pixels.empty() ||
      surface.stride < surface.level_bounds.x1 - surface.level_bounds.x0) {
    return false;
  }
  const auto range =
      log.replay_range(log.epoch(), canvas.current_revision(), log.current_revision());
  if (!range.has_value()) {
    return false;
  }
  for (std::size_t offset = 0; offset < range->operation_count; ++offset) {
    const auto stored = log.operation(range->first_operation + offset);
    if (!stored.has_value()) {
      return false;
    }
    const PixelRect bounds = operation_level_bounds(stored->world_bounds, surface.zoom);
    const bool intersects =
        bounds.x0 < surface.level_bounds.x1 && surface.level_bounds.x0 < bounds.x1 &&
        bounds.y0 < surface.level_bounds.y1 && surface.level_bounds.y0 < bounds.y1;
    if (!intersects) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples}, surface)) {
      return false;
    }
  }
  return true;
}

bool replay_active_overview(const OperationLog& log, std::span<std::uint16_t> output) {
  if (!log.ready() || output.size() != kOverviewPixels ||
      log.workspace_overlaps_storage(std::as_bytes(output))) {
    return false;
  }
  const AuthorityReadView view = log.read_view();
  std::fill(output.begin(), output.end(), 0xFFFFU);
  const RasterSurface surface{
      .zoom = ZoomLevel::k25Percent,
      .level_bounds = {0, 0, kOverviewWidth, kOverviewHeight},
      .pixels = output,
      .stride = kOverviewWidth,
  };
  for (std::size_t index = 0; index < view.active_operation_count; ++index) {
    const auto operation = log.operation(index);
    if (!operation.has_value() ||
        !apply_incremental_operation(
            {.tool = operation->tool, .color = operation->color, .samples = operation->samples},
            surface)) {
      return false;
    }
  }
  return true;
}

bool build_tiled_may_ink(const OperationLog& log, std::span<std::uint8_t> output) {
  if (!log.ready() || output.size() != kOccupancyBytes ||
      log.workspace_overlaps_storage(std::as_bytes(output))) {
    return false;
  }
  std::fill(output.begin(), output.end(), 0U);
  const AuthorityReadView view = log.read_view();
  for (std::size_t index = 0; index < view.active_operation_count; ++index) {
    const auto operation = log.operation(index);
    if (!operation.has_value()) {
      return false;
    }
    if (operation->tool == OperationTool::kEraser) {
      continue;
    }
    const PixelRect bounds = operation->world_bounds;
    const int first_column = bounds.x0 / kOccupancyCellWorldSize;
    const int last_column = (bounds.x1 - 1) / kOccupancyCellWorldSize;
    const int first_row = bounds.y0 / kOccupancyCellWorldSize;
    const int last_row = (bounds.y1 - 1) / kOccupancyCellWorldSize;
    for (int row = first_row; row <= last_row; ++row) {
      for (int column = first_column; column <= last_column; ++column) {
        const std::size_t bit =
            static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
        output[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
      }
    }
  }
  return true;
}

bool restore_document_snapshot(OperationLog& log, MaterializedCanvas& canvas,
                               DocumentRevision revision,
                               std::span<const std::uint16_t> overview_pixels) {
  if (!log.ready() || !canvas.ready() || !log.can_reset() ||
      overview_pixels.size() != kOverviewPixels ||
      !canvas.accepts_external_workspace(std::as_bytes(overview_pixels)) ||
      log.workspace_overlaps_storage(std::as_bytes(overview_pixels))) {
    return false;
  }
  // restore_snapshot cannot fail after the checks above under the serialized
  // ownership contract. Reset is called second so a failed canvas validation
  // cannot discard document authority.
  if (!canvas.restore_snapshot(revision, overview_pixels)) {
    return false;
  }
  return log.reset(revision);
}

bool reset_blank_document(OperationLog& log, MaterializedCanvas& canvas,
                          DocumentRevision revision) {
  if (!log.ready() || !canvas.ready() || !log.can_reset()) {
    return false;
  }
  // reset_blank cannot fail after the readiness check under the serialized
  // ownership contract. Reset is called second so validation cannot discard
  // document authority.
  if (!canvas.reset_blank(revision)) {
    return false;
  }
  return log.reset(revision);
}

}  // namespace tinydraw::vector_v2
