#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/coverage_tile.h"

namespace tinydraw {

inline constexpr int kViewportTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
inline constexpr int kViewportTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
inline constexpr std::size_t kMaxViewportUpdateRegions =
    static_cast<std::size_t>(kViewportTilesAcross * kViewportTilesDown);

struct ViewportUpdateStats {
  std::size_t regions = 0;
  std::size_t pixels = 0;
  bool complete = false;
};

// Finds tile-aligned runs that differ between the displayed and next viewport.
// Vertically identical runs are merged. Callers own the bounded region storage.
[[nodiscard]] ViewportUpdateStats plan_viewport_updates(std::span<const std::uint16_t> displayed,
                                                        std::span<const std::uint16_t> next,
                                                        int bottom, std::span<Rect> regions);

void sync_viewport_updates(std::span<const std::uint16_t> source,
                           std::span<std::uint16_t> destination, std::span<const Rect> regions);

}  // namespace tinydraw
