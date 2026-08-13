#include "tinydraw/production/incremental_document.h"

#include <algorithm>
#include <functional>

namespace tinydraw::production {
namespace {

template <typename Left, typename Right>
bool spans_overlap(std::span<Left> left, std::span<Right> right) {
  const auto* left_begin = reinterpret_cast<const std::byte*>(left.data());
  const auto* left_end = left_begin + left.size_bytes();
  const auto* right_begin = reinterpret_cast<const std::byte*>(right.data());
  const auto* right_end = right_begin + right.size_bytes();
  const std::less<const std::byte*> less;
  return less(left_begin, right_end) && less(right_begin, left_end);
}

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
  if (!canvas.ready() || !log.ready() || canvas.overview_pixels().size() != kOverviewPixels ||
      workspace.next_overview.size() != kOverviewPixels ||
      !canvas.accepts_external_workspace(workspace.next_overview) ||
      !canvas.accepts_external_workspace(workspace.tile_scratch) ||
      log.workspace_overlaps_storage(workspace.next_overview) ||
      log.workspace_overlaps_storage(workspace.tile_scratch) ||
      spans_overlap(workspace.next_overview, workspace.tile_scratch) ||
      workspace.publications.size() > workspace.tile_scratch.size() / kTilePixels ||
      workspace.affected_keys.size() < workspace.publications.size() ||
      log.current_revision() != canvas.current_revision()) {
    return std::nullopt;
  }
  auto prepared = log.prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const StoredOperation& stored = prepared->operation();
  const OperationIdentity identity = stored.identity;
  const IncrementalOperation operation{
      .tool = stored.tool, .color = stored.color, .samples = stored.samples};
  std::copy_n(canvas.overview_pixels().begin(), kOverviewPixels, workspace.next_overview.begin());
  const bool overview_ready = apply_incremental_operation(
      operation, {.zoom = ZoomLevel::k25Percent,
                  .level_bounds = {0, 0, kOverviewWidth, kOverviewHeight},
                  .pixels = workspace.next_overview,
                  .stride = kOverviewWidth});
  const auto resident_count =
      canvas.resident_tiles_intersecting(stored.world_bounds, workspace.affected_keys);
  if (!overview_ready || !resident_count.has_value()) {
    prepared->cancel();
    return std::nullopt;
  }
  const std::size_t publication_count = std::min(*resident_count, workspace.publications.size());
  for (std::size_t index = 0; index < publication_count; ++index) {
    auto scratch = workspace.tile_scratch.subspan(index * kTilePixels, kTilePixels);
    if (!prepare_tile(canvas, operation, workspace.affected_keys[index], scratch,
                      workspace.publications[index])) {
      prepared->cancel();
      return std::nullopt;
    }
  }
  if (!canvas.commit_incremental_revision(stored.identity.revision, workspace.next_overview,
                                          stored.world_bounds,
                                          workspace.publications.first(publication_count))) {
    prepared->cancel();
    return std::nullopt;
  }
  prepared->publish();
  return IncrementalAppendResult{.identity = identity,
                                 .affected_resident_tiles = *resident_count,
                                 .published_tiles = publication_count,
                                 .fallback_tiles = *resident_count - publication_count};
}

}  // namespace tinydraw::production
