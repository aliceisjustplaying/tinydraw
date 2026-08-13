#include "tinydraw/vector_v2/tile_payload_analysis.h"

#include <limits>

namespace tinydraw::vector_v2 {

std::optional<TilePayloadAnalysis> analyze_tile_payload(std::span<const std::uint16_t> pixels,
                                                        int width, int height) {
  if (width <= 0 || height <= 0 || width > kTileWidth || height > kTileHeight) {
    return std::nullopt;
  }
  const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixels.size() != expected || pixels.empty()) {
    return std::nullopt;
  }

  TilePayloadAnalysis result{
      .pixel_count = pixels.size(),
      .raw_bytes = pixels.size_bytes(),
      .uniform_color = pixels.front(),
      .uniform = true,
  };
  for (int row = 0; row < height; ++row) {
    const std::size_t row_start = static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    ++result.row_runs;
    std::uint16_t previous = pixels[row_start];
    result.uniform = result.uniform && previous == result.uniform_color;
    for (int column = 1; column < width; ++column) {
      const std::uint16_t current = pixels[row_start + static_cast<std::size_t>(column)];
      result.row_runs += current != previous;
      result.uniform = result.uniform && current == result.uniform_color;
      previous = current;
    }
  }
  if (result.row_runs > std::numeric_limits<std::size_t>::max() / kEstimatedRowRunBytes) {
    return std::nullopt;
  }
  result.estimated_row_rle_bytes = result.row_runs * kEstimatedRowRunBytes;
  return result;
}

}  // namespace tinydraw::vector_v2
