#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_INTERNAL_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_INTERNAL_H

#include <array>

#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2::incremental_document_internal {

inline bool intersects(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

inline bool valid_priority_view(const std::optional<ViewRequest>& view) {
  if (!view.has_value()) {
    return true;
  }
  const PixelRect bounds = view->level_pixels;
  return view->zoom != ZoomLevel::k25Percent && bounds.x0 >= 0 && bounds.y0 >= 0 &&
         bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kWorldWidth * zoom_percent(view->zoom) / 100 &&
         bounds.y1 <= kWorldHeight * zoom_percent(view->zoom) / 100;
}

inline bool in_priority_view(TileKey key, const std::optional<ViewRequest>& view) {
  return view.has_value() && key.zoom == view->zoom &&
         intersects(tile_pixel_bounds(key), view->level_pixels);
}

inline bool valid_in_place_workspace(const OperationLog& log, const MaterializedCanvas& canvas,
                                     const InPlaceAppendWorkspace& workspace) {
  const std::array<std::span<const std::byte>, 4> workspaces{
      std::as_bytes(workspace.overview_scratch), std::as_bytes(workspace.affected_keys),
      std::as_bytes(workspace.tile_mask), workspace.operation_chord_plans};
  bool invalid = workspace.tile_mask.size() < kInPlaceTileMaskBytes;
  for (std::size_t left = 0; left < workspaces.size(); ++left) {
    invalid = invalid || !canvas.accepts_external_workspace(workspaces[left]) ||
              log.workspace_overlaps_storage(workspaces[left]);
    for (std::size_t right = left + 1U; right < workspaces.size(); ++right) {
      invalid = invalid || storage_overlaps(workspaces[left], workspaces[right]);
    }
  }
  return !invalid;
}

inline bool in_recent_view(const MaterializedCanvas& canvas, TileKey key) {
  for (const ViewFootprint& view : canvas.recent_views()) {
    if (!view.valid || view.zoom != key.zoom) {
      continue;
    }
    const PixelRect bounds = tile_pixel_bounds(key);
    if (bounds.x0 < view.level_pixels.x1 && view.level_pixels.x0 < bounds.x1 &&
        bounds.y0 < view.level_pixels.y1 && view.level_pixels.y0 < bounds.y1) {
      return true;
    }
  }
  return false;
}

}  // namespace tinydraw::vector_v2::incremental_document_internal

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_INTERNAL_H
