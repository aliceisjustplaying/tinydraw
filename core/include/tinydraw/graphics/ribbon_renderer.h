#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/graphics/coverage_tile.h"
#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw {

struct RibbonRenderStats {
  std::uint32_t tiles_rasterized = 0;
  std::uint32_t pixels_considered = 0;
};

// Owns the 12 KiB tile scratch arena so callers can place it explicitly in
// internal SRAM rather than consuming an embedded task's stack.
class RibbonRenderer {
 public:
  [[nodiscard]] RibbonRenderStats render(std::span<const RibbonPrimitive> primitives,
                                         std::span<std::uint16_t> canvas, int width, int height,
                                         std::uint16_t color);

 private:
  CoverageTile coverage_{0, 0};
  std::array<std::uint16_t, kTileSize * kTileSize> working_{};
};

}  // namespace tinydraw
