#include "tinydraw/vector_v2/incremental_document.h"

#include <algorithm>
#include <array>

#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {
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

bool intersects(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

bool valid_priority_view(const std::optional<ViewRequest>& view) {
  if (!view.has_value()) {
    return true;
  }
  const PixelRect bounds = view->level_pixels;
  return view->zoom != ZoomLevel::k25Percent && bounds.x0 >= 0 && bounds.y0 >= 0 &&
         bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kWorldWidth * zoom_percent(view->zoom) / 100 &&
         bounds.y1 <= kWorldHeight * zoom_percent(view->zoom) / 100;
}

bool in_priority_view(TileKey key, const std::optional<ViewRequest>& view) {
  return view.has_value() && key.zoom == view->zoom &&
         intersects(tile_pixel_bounds(key), view->level_pixels);
}

void prioritize_view(std::span<TileKey> keys, const std::optional<ViewRequest>& view) {
  std::size_t destination = 0;
  for (std::size_t candidate = 0; candidate < keys.size(); ++candidate) {
    if (in_priority_view(keys[candidate], view)) {
      std::swap(keys[destination], keys[candidate]);
      ++destination;
    }
  }
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
      .quality = MaterializationQuality::kImmediate,
      .pixels = pixels,
  };
  return true;
}

}  // namespace

std::optional<IncrementalAppendResult> append_incrementally(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const IncrementalDocumentWorkspace& workspace, IncrementalAppendOptions options) {
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
      log.current_revision() != canvas.current_revision() ||
      !valid_priority_view(options.priority_view)) {
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
  const auto resident_count = canvas.materialized_tiles_intersecting(
      stored.world_bounds, workspace.affected_keys, options.priority_view,
      options.publication_scope == IncrementalPublicationScope::kPriorityView);
  if (!overview_ready || !resident_count.has_value()) {
    prepared->cancel();
    return std::nullopt;
  }
  const std::size_t publication_capacity =
      std::min(workspace.publications.size(), workspace.tile_scratch.size() / kTilePixels);
  const std::size_t publication_count = std::min(*resident_count, publication_capacity);
  if (options.priority_view.has_value() && publication_count < *resident_count) {
    prioritize_view(workspace.affected_keys.first(*resident_count), options.priority_view);
  }
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
                                 .affected_world_bounds = stored.world_bounds,
                                 .affected_resident_tiles = *resident_count,
                                 .published_tiles = publication_count,
                                 .fallback_tiles = *resident_count - publication_count};
}

namespace {

// Paints exactly the operation's covered pixels into one tile. The operation
// is a single tool and color, so its segments can replay newest-first through
// a finalized mask: every covered pixel is written exactly once and the
// resulting pixel union is identical to forward per-segment painting.
bool paint_operation_into_tile(const OperationAppend& operation, const InPlaceTileEdit& edit,
                               std::span<std::uint8_t> tile_mask) {
  const RasterSurface surface{
      .zoom = edit.key.zoom,
      .level_bounds = edit.bounds,
      .pixels = edit.pixels.first(static_cast<std::size_t>(edit.bounds.y1 - edit.bounds.y0 - 1) *
                                      kTileWidth +
                                  static_cast<std::size_t>(edit.bounds.x1 - edit.bounds.x0)),
      .stride = kTileWidth,
  };
  const std::size_t mask_bytes = (surface.pixels.size() + 7U) / 8U;
  std::fill_n(tile_mask.begin(), mask_bytes, std::uint8_t{0});
  if (operation.samples.size() == 1U) {
    return apply_masked_incremental_segment({.tool = operation.tool,
                                             .color = operation.color,
                                             .first = operation.samples.front(),
                                             .second = operation.samples.front()},
                                            surface, tile_mask);
  }
  for (std::size_t index = operation.samples.size(); index-- > 1U;) {
    const PixelRect segment_bounds = incremental_segment_level_bounds(
        operation.samples[index - 1U], operation.samples[index], edit.key.zoom);
    if (!intersects(segment_bounds, edit.bounds)) {
      continue;
    }
    if (!apply_masked_incremental_segment({.tool = operation.tool,
                                           .color = operation.color,
                                           .first = operation.samples[index - 1U],
                                           .second = operation.samples[index]},
                                          surface, tile_mask)) {
      return false;
    }
  }
  return true;
}

bool valid_in_place_workspace(const OperationLog& log, const MaterializedCanvas& canvas,
                              const InPlaceAppendWorkspace& workspace) {
  const std::array<std::span<const std::byte>, 3> workspaces{
      std::as_bytes(workspace.overview_scratch), std::as_bytes(workspace.affected_keys),
      std::as_bytes(workspace.tile_mask)};
  bool workspace_invalid = workspace.tile_mask.size() < kInPlaceTileMaskBytes;
  for (std::size_t left = 0; left < workspaces.size(); ++left) {
    workspace_invalid = workspace_invalid || !canvas.accepts_external_workspace(workspaces[left]) ||
                        log.workspace_overlaps_storage(workspaces[left]);
    for (std::size_t right = left + 1U; right < workspaces.size(); ++right) {
      workspace_invalid =
          workspace_invalid || storage_overlaps(workspaces[left], workspaces[right]);
    }
  }
  return !workspace_invalid;
}

// Mutation phase of the in-place append: no step may abandon the commit. A
// tile that cannot be updated is simply not retained and becomes correct
// overview fallback. Retained keys are swap-partitioned into the prefix of
// the enumeration so no key is lost or visited twice; uniform conversions run
// first so their slot eviction can never pick a raw tile edited earlier in
// this same commit. Returns the retained-prefix length.
std::size_t retain_affected_tiles(MaterializedCanvas& canvas, const OperationAppend& operation,
                                  std::uint16_t painted_color,
                                  const std::optional<ViewRequest>& priority_view,
                                  std::span<TileKey> affected, std::span<std::uint8_t> tile_mask) {
  std::size_t retained = 0;
  for (std::size_t index = 0; index < affected.size(); ++index) {
    const TileKey key = affected[index];
    const auto color = canvas.uniform_color(key);
    if (!color.has_value()) {
      continue;
    }
    bool keep = false;
    if (*color == painted_color) {
      // Painting this color over an identical uniform is a no-op; retain it.
      keep = true;
    } else if (in_priority_view(key, priority_view)) {
      const auto edit = canvas.materialize_uniform_as_raw(key);
      if (edit.has_value() && paint_operation_into_tile(operation, *edit, tile_mask)) {
        keep = true;
      } else if (edit.has_value()) {
        canvas.invalidate_identity(key);
      }
    }
    if (keep) {
      std::swap(affected[index], affected[retained]);
      ++retained;
    }
  }
  for (std::size_t index = retained; index < affected.size(); ++index) {
    const TileKey key = affected[index];
    const auto edit = canvas.edit_resident_tile(key);
    if (!edit.has_value()) {
      continue;
    }
    if (paint_operation_into_tile(operation, *edit, tile_mask)) {
      std::swap(affected[index], affected[retained]);
      ++retained;
    } else {
      canvas.invalidate_identity(key);
    }
  }
  return retained;
}

}  // namespace

std::optional<IncrementalAppendResult> append_incrementally_in_place(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const InPlaceAppendWorkspace& workspace, std::optional<ViewRequest> priority_view) {
  if (!canvas.ready() || !log.ready() || !valid_in_place_workspace(log, canvas, workspace) ||
      canvas.overview_pixels().size() != kOverviewPixels ||
      log.current_revision() != canvas.current_revision() || !valid_priority_view(priority_view)) {
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
  const auto resident_count = canvas.materialized_tiles_intersecting(
      stored.world_bounds, workspace.affected_keys, priority_view, false);
  if (!overview_ready || !resident_count.has_value() ||
      !canvas.can_edit_in_place_revision(identity.revision, overview_publication,
                                         stored.world_bounds)) {
    prepared->cancel();
    return std::nullopt;
  }

  const auto affected = workspace.affected_keys.first(*resident_count);
  const std::uint16_t painted_color =
      stored.tool == OperationTool::kEraser ? 0xFFFFU : stored.color;
  const std::size_t retained = retain_affected_tiles(canvas, operation, painted_color,
                                                     priority_view, affected, workspace.tile_mask);
  if (!canvas.commit_in_place_revision(identity.revision, overview_publication, stored.world_bounds,
                                       affected.first(retained))) {
    for (const TileKey key : affected.first(retained)) {
      canvas.invalidate_identity(key);
    }
    prepared->cancel();
    return std::nullopt;
  }
  prepared->publish();
  return IncrementalAppendResult{.identity = identity,
                                 .affected_world_bounds = stored.world_bounds,
                                 .affected_resident_tiles = *resident_count,
                                 .published_tiles = retained,
                                 .fallback_tiles = *resident_count - retained};
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

}  // namespace tinydraw::vector_v2
