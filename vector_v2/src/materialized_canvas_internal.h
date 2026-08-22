#ifndef TINYDRAW_VECTOR_V2_MATERIALIZED_CANVAS_INTERNAL_H
#define TINYDRAW_VECTOR_V2_MATERIALIZED_CANVAS_INTERNAL_H

#include <array>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {
namespace materialized_canvas_detail {

// Zero-byte transient mark in the raw-slot directory during retained-uniform
// staging. The adjacent value remains the serialized no-slot sentinel.
inline constexpr std::uint16_t kRetainedUniformSlot = 0xFFFEU;

// Fixed geometry lets cache probes validate a key and derive its dense
// identity with two indexed loads. Keep this scalar: std::optional's Xtensa
// windowed-ABI return forces otherwise-leaf probes through stack temporaries.
inline constexpr std::array<TileGrid, 5> kTileGrids{{
    {6, 7},
    {12, 14},
    {23, 28},
    {46, 56},
    {92, 112},
}};
inline constexpr std::array<std::uint16_t, kTiledZoomCount> kTileIdentityOffsets{0U, 168U, 812U,
                                                                                 3'388U};

[[nodiscard, gnu::always_inline]] inline std::uint16_t tile_identity_or_no_slot(TileKey key) {
  const unsigned zoom = static_cast<unsigned>(key.zoom);
  if (zoom == 0U || zoom >= kTileGrids.size()) {
    return kNoRawSlot;
  }
  const TileGrid grid = kTileGrids[zoom];
  if (key.column >= static_cast<unsigned>(grid.columns) ||
      key.row >= static_cast<unsigned>(grid.rows)) {
    return kNoRawSlot;
  }
  return static_cast<std::uint16_t>(
      kTileIdentityOffsets[zoom - 1U] +
      static_cast<unsigned>(key.row) * static_cast<unsigned>(grid.columns) + key.column);
}

[[nodiscard]] constexpr int ceil_div(int numerator, int denominator) {
  return (numerator + denominator - 1) / denominator;
}

[[nodiscard]] inline int scaled_extent(int world_extent, ZoomLevel zoom) {
  return world_extent * zoom_percent(zoom) / 100;
}

[[nodiscard]] constexpr bool rectangles_intersect(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

[[nodiscard]] inline PixelRect tile_world_bounds(TileKey key) {
  const PixelRect level = tile_pixel_bounds(key);
  const int percent = zoom_percent(key.zoom);
  return {
      .x0 = level.x0 * 100 / percent,
      .y0 = level.y0 * 100 / percent,
      .x1 = ceil_div(level.x1 * 100, percent),
      .y1 = ceil_div(level.y1 * 100, percent),
  };
}

[[nodiscard]] constexpr bool valid_world_bounds(PixelRect bounds) {
  return bounds.x0 >= 0 && bounds.y0 >= 0 && bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kWorldWidth && bounds.y1 <= kWorldHeight;
}

}  // namespace materialized_canvas_detail
}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_MATERIALIZED_CANVAS_INTERNAL_H
