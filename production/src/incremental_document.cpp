#include "tinydraw/production/incremental_document.h"

#include <algorithm>

namespace tinydraw::production {
namespace {

bool prepare_tile(const MaterializedCanvas& canvas, const IncrementalOperation& operation,
                  TileKey key, std::span<std::uint16_t> scratch,
                  TileRevisionPublication& publication) {
  const PixelRect bounds = tile_pixel_bounds(key);
  const std::size_t pixel_count = static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                  static_cast<std::size_t>(bounds.y1 - bounds.y0);
  const auto pixels = scratch.first(pixel_count);
  if (!canvas.copy_resident_tile(key, pixels) ||
      !apply_incremental_operation(operation, {.zoom = key.zoom,
                                               .level_bounds = bounds,
                                               .pixels = pixels,
                                               .stride = bounds.x1 - bounds.x0})) {
    return false;
  }
  publication = {
      .key = key,
      .quality = MaterializationQuality::kSettled,
      .pixels = pixels,
  };
  return true;
}

}  // namespace

std::optional<IncrementalAppendResult> append_incrementally(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const IncrementalDocumentWorkspace& workspace) {
  if (workspace.next_overview.size() != kOverviewPixels ||
      !canvas.accepts_external_workspace(workspace.next_overview) ||
      !canvas.accepts_external_workspace(workspace.tile_scratch) ||
      workspace.tile_scratch.size() < workspace.publications.size() * kTilePixels ||
      workspace.affected_keys.size() < workspace.publications.size() ||
      log.current_revision() != canvas.current_revision()) {
    return std::nullopt;
  }
  const auto prepared = log.prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const StoredOperation& stored = prepared->operation();
  const IncrementalOperation operation{
      .tool = stored.tool, .color = stored.color, .samples = stored.samples};
  std::copy(canvas.overview_pixels().begin(), canvas.overview_pixels().end(),
            workspace.next_overview.begin());
  const bool overview_ready = apply_incremental_operation(
      operation, {.zoom = ZoomLevel::k25Percent,
                  .level_bounds = {0, 0, kOverviewWidth, kOverviewHeight},
                  .pixels = workspace.next_overview,
                  .stride = kOverviewWidth});
  const auto resident_count =
      canvas.resident_tiles_intersecting(stored.world_bounds, workspace.affected_keys);
  if (!overview_ready || !resident_count.has_value() ||
      *resident_count > workspace.publications.size()) {
    static_cast<void>(log.cancel(*prepared));
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *resident_count; ++index) {
    auto scratch = workspace.tile_scratch.subspan(index * kTilePixels, kTilePixels);
    if (!prepare_tile(canvas, operation, workspace.affected_keys[index], scratch,
                      workspace.publications[index])) {
      static_cast<void>(log.cancel(*prepared));
      return std::nullopt;
    }
  }
  if (!canvas.commit_incremental_revision(stored.identity.revision, workspace.next_overview,
                                          stored.world_bounds,
                                          workspace.publications.first(*resident_count))) {
    static_cast<void>(log.cancel(*prepared));
    return std::nullopt;
  }
  if (!log.publish(*prepared)) {
    return std::nullopt;
  }
  return IncrementalAppendResult{.identity = stored.identity,
                                 .affected_resident_tiles = *resident_count,
                                 .published_tiles = *resident_count};
}

}  // namespace tinydraw::production
