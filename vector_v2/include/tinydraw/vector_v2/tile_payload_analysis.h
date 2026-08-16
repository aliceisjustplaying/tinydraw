#ifndef TINYDRAW_VECTOR_V2_TILE_PAYLOAD_ANALYSIS_H
#define TINYDRAW_VECTOR_V2_TILE_PAYLOAD_ANALYSIS_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

// Measurement-only estimate for a tightly packed tile payload. A hypothetical
// row-run stream needs one byte for a 1..64-pixel run length and two bytes for
// its RGB565 value. No compressed storage policy depends on this estimate.
inline constexpr std::size_t kEstimatedRowRunBytes = 3;

struct TilePayloadAnalysis {
  std::size_t pixel_count = 0;
  std::size_t row_runs = 0;
  std::size_t raw_bytes = 0;
  std::size_t estimated_row_rle_bytes = 0;
  std::uint16_t uniform_color = 0;
  bool uniform = false;
};

// Analyzes a tightly packed row-major payload. Edge tiles may be smaller than
// 64x64. Invalid dimensions or a mismatched payload size return nullopt.
[[nodiscard]] std::optional<TilePayloadAnalysis> analyze_tile_payload(
    std::span<const std::uint16_t> pixels, int width, int height);

// Strided variant: reads a width x height window out of a larger row-major
// surface whose rows are source_stride pixels apart. The span must cover
// exactly (height - 1) * source_stride + width elements starting at the
// window origin, and source_stride must be >= width. pixel_count/raw_bytes
// describe the logical window (width * height), not the strided span.
[[nodiscard]] std::optional<TilePayloadAnalysis> analyze_tile_payload(
    std::span<const std::uint16_t> pixels, int width, int height, std::size_t source_stride);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_TILE_PAYLOAD_ANALYSIS_H
