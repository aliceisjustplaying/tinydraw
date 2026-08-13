#include "tinydraw/production/incremental_document.h"

#include <algorithm>
#include <array>

#include "tinydraw/production/storage_overlap.h"

namespace tinydraw::production {
namespace {

bool prepare_overview(const MaterializedCanvas& canvas, const OperationAppend& operation,
                      PixelRect world_bounds, std::span<std::uint16_t> scratch,
                      OverviewRevisionPublication& publication) {
  const PixelRect bounds = overview_bounds_for_world(world_bounds);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (scratch.size() < pixel_count) {
    return false;
  }
  const auto pixels = scratch.first(pixel_count);
  for (int row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::ptrdiff_t>(bounds.y0 + row) * kOverviewWidth +
                               static_cast<std::ptrdiff_t>(bounds.x0);
    const auto destination_offset =
        static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(width);
    const auto source = canvas.overview_pixels().begin() + source_offset;
    std::copy_n(source, width, pixels.begin() + destination_offset);
  }
  if (!apply_incremental_operation(operation, {.zoom = ZoomLevel::k25Percent,
                                               .level_bounds = bounds,
                                               .pixels = pixels,
                                               .stride = width})) {
    return false;
  }
  publication = {.bounds = bounds, .pixels = pixels};
  return true;
}

bool prepare_tile(const MaterializedCanvas& canvas, const OperationAppend& operation, TileKey key,
                  std::span<std::uint16_t> scratch, TileRevisionPublication& publication) {
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
  const std::array<std::span<const std::byte>, 4> workspaces{
      std::as_bytes(workspace.overview_scratch), std::as_bytes(workspace.tile_scratch),
      std::as_bytes(workspace.publications), std::as_bytes(workspace.affected_keys)};
  bool workspaces_overlap = false;
  for (std::size_t left = 0; left < workspaces.size(); ++left) {
    for (std::size_t right = left + 1U; right < workspaces.size(); ++right) {
      workspaces_overlap =
          workspaces_overlap || storage_overlaps(workspaces[left], workspaces[right]);
    }
  }
  const bool workspace_aliases_owned_storage =
      std::any_of(workspaces.begin(), workspaces.end(), [&](const auto workspace_bytes) {
        return !canvas.accepts_external_workspace(workspace_bytes) ||
               log.workspace_overlaps_storage(workspace_bytes);
      });
  if (!canvas.ready() || !log.ready() || canvas.overview_pixels().size() != kOverviewPixels ||
      workspaces_overlap || workspace_aliases_owned_storage ||
      log.current_revision() != canvas.current_revision()) {
    return std::nullopt;
  }
  auto prepared = log.prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const StoredOperation& stored = prepared->operation();
  const OperationIdentity identity = stored.identity;
  const OperationAppend operation{
      .tool = stored.tool, .color = stored.color, .samples = stored.samples};
  OverviewRevisionPublication overview_publication{};
  const bool overview_ready = prepare_overview(canvas, operation, stored.world_bounds,
                                               workspace.overview_scratch, overview_publication);
  const auto resident_count =
      canvas.resident_tiles_intersecting(stored.world_bounds, workspace.affected_keys);
  if (!overview_ready || !resident_count.has_value()) {
    prepared->cancel();
    return std::nullopt;
  }
  const std::size_t publication_capacity =
      std::min(workspace.publications.size(), workspace.tile_scratch.size() / kTilePixels);
  const std::size_t publication_count = std::min(*resident_count, publication_capacity);
  for (std::size_t index = 0; index < publication_count; ++index) {
    auto scratch = workspace.tile_scratch.subspan(index * kTilePixels, kTilePixels);
    if (!prepare_tile(canvas, operation, workspace.affected_keys[index], scratch,
                      workspace.publications[index])) {
      prepared->cancel();
      return std::nullopt;
    }
  }
  if (!canvas.commit_incremental_revision(stored.identity.revision, overview_publication,
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

}  // namespace tinydraw::production
