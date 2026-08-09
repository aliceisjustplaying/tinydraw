#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw {

struct RibbonRenderStats {
  std::uint32_t tiles_rasterized = 0;
  std::uint32_t pixels_considered = 0;
};

// Rasterize every piece into 8-bit tile coverage, union overlaps, then composite
// the logical stroke exactly once into the RGB565 canvas.
[[nodiscard]] RibbonRenderStats render_ribbon(std::span<const RibbonPrimitive> primitives,
                                              std::span<std::uint16_t> canvas, int width,
                                              int height, std::uint16_t color);

}  // namespace tinydraw
