#ifndef TINYDRAW_PRODUCTION_TILE_PAYLOAD_ANALYSIS_H
#define TINYDRAW_PRODUCTION_TILE_PAYLOAD_ANALYSIS_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/materialized_canvas.h"

namespace tinydraw::production {

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

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_TILE_PAYLOAD_ANALYSIS_H
