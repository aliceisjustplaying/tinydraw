#include "tinydraw/vector_v2/operation.h"

#include <algorithm>

namespace tinydraw::vector_v2 {
namespace {

// Immediate rendering keeps a center-sampled 0.75 screen-pixel minimum radius.
// At the coarsest tiled level (50%), that halo is 1.5 world units, or 24
// sample units. The same halo leaves 0.375 overview pixels around a thin
// centerline; center sampling can paint only 0.25 pixels past that centerline
// at 25%, so overview_bounds_for_world remains a complete clipping region. If
// raster coverage or its minimum radius changes, preserve both contracts.
constexpr int kMinimumTiledRadiusUnits = 24;
static_assert(kMinimumTiledRadiusUnits == kSampleUnitsPerWorldUnit * 3 / 2);

}  // namespace

std::optional<PixelRect> operation_world_bounds(std::span<const CompactOperationSample> samples) {
  if (samples.empty()) {
    return std::nullopt;
  }
  int minimum_x = kWorldWidth * kSampleUnitsPerWorldUnit;
  int minimum_y = kWorldHeight * kSampleUnitsPerWorldUnit;
  int maximum_x = 0;
  int maximum_y = 0;
  for (const CompactOperationSample sample : samples) {
    if (sample.x_quarter > kWorldWidth * kSampleUnitsPerWorldUnit ||
        sample.y_quarter > kWorldHeight * kSampleUnitsPerWorldUnit || sample.radius_256 == 0U) {
      return std::nullopt;
    }
    // 256 radius units per world unit over kSampleUnitsPerWorldUnit: 16 per
    // sample unit, rounded up.
    const int radius_units =
        std::max((static_cast<int>(sample.radius_256) + 15) / 16, kMinimumTiledRadiusUnits);
    minimum_x = std::min(minimum_x, static_cast<int>(sample.x_quarter) - radius_units);
    minimum_y = std::min(minimum_y, static_cast<int>(sample.y_quarter) - radius_units);
    maximum_x = std::max(maximum_x, static_cast<int>(sample.x_quarter) + radius_units);
    maximum_y = std::max(maximum_y, static_cast<int>(sample.y_quarter) + radius_units);
  }
  return PixelRect{
      .x0 = std::max(0, minimum_x) / kSampleUnitsPerWorldUnit,
      .y0 = std::max(0, minimum_y) / kSampleUnitsPerWorldUnit,
      .x1 = std::min(kWorldWidth,
                     (maximum_x + kSampleUnitsPerWorldUnit - 1) / kSampleUnitsPerWorldUnit),
      .y1 = std::min(kWorldHeight,
                     (maximum_y + kSampleUnitsPerWorldUnit - 1) / kSampleUnitsPerWorldUnit),
  };
}

}  // namespace tinydraw::vector_v2
