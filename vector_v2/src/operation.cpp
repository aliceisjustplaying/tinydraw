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

std::optional<PixelRect> operation_sample_world_bounds(CompactOperationSample sample) {
  if (sample.x_quarter > kWorldWidth * kSampleUnitsPerWorldUnit ||
      sample.y_quarter > kWorldHeight * kSampleUnitsPerWorldUnit || sample.radius_256 == 0U) {
    return std::nullopt;
  }
  // 256 radius units per world unit over kSampleUnitsPerWorldUnit: 16 per
  // sample unit, rounded up.
  const int radius_units =
      std::max((static_cast<int>(sample.radius_256) + 15) / 16, kMinimumTiledRadiusUnits);
  return PixelRect{
      .x0 =
          std::max(0, static_cast<int>(sample.x_quarter) - radius_units) / kSampleUnitsPerWorldUnit,
      .y0 =
          std::max(0, static_cast<int>(sample.y_quarter) - radius_units) / kSampleUnitsPerWorldUnit,
      .x1 = std::min(kWorldWidth, (static_cast<int>(sample.x_quarter) + radius_units +
                                   kSampleUnitsPerWorldUnit - 1) /
                                      kSampleUnitsPerWorldUnit),
      .y1 = std::min(kWorldHeight, (static_cast<int>(sample.y_quarter) + radius_units +
                                    kSampleUnitsPerWorldUnit - 1) /
                                       kSampleUnitsPerWorldUnit),
  };
}

std::optional<PixelRect> operation_world_bounds(std::span<const CompactOperationSample> samples) {
  std::optional<PixelRect> bounds;
  for (const CompactOperationSample sample : samples) {
    const auto sample_bounds = operation_sample_world_bounds(sample);
    if (!sample_bounds.has_value()) {
      return std::nullopt;
    }
    if (!bounds.has_value()) {
      bounds = sample_bounds;
    } else {
      bounds->x0 = std::min(bounds->x0, sample_bounds->x0);
      bounds->y0 = std::min(bounds->y0, sample_bounds->y0);
      bounds->x1 = std::max(bounds->x1, sample_bounds->x1);
      bounds->y1 = std::max(bounds->y1, sample_bounds->y1);
    }
  }
  return bounds;
}

}  // namespace tinydraw::vector_v2
