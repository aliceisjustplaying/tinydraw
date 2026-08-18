#include "tinydraw/vector_v2/tile_uniform.h"

#include <algorithm>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

std::optional<std::uint16_t> tile_uniform_color(std::span<const std::uint16_t> pixels, int width,
                                                int height, std::size_t source_stride) {
  if (width <= 0 || height <= 0 || width > kTileWidth || height > kTileHeight ||
      source_stride < static_cast<std::size_t>(width)) {
    return std::nullopt;
  }
  const std::size_t expected =
      static_cast<std::size_t>(height - 1) * source_stride + static_cast<std::size_t>(width);
  if (pixels.size() != expected || pixels.empty()) {
    return std::nullopt;
  }
  const std::uint16_t color = pixels.front();
  for (int row = 0; row < height; ++row) {
    const auto first = pixels.begin() + static_cast<std::ptrdiff_t>(row) *
                                            static_cast<std::ptrdiff_t>(source_stride);
    if (!std::all_of(first, first + width,
                     [color](std::uint16_t pixel) { return pixel == color; })) {
      return std::nullopt;
    }
  }
  return color;
}

}  // namespace tinydraw::vector_v2
