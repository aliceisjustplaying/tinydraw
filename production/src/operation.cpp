#include "tinydraw/production/operation.h"

#include <algorithm>

namespace tinydraw::production {
namespace {

// Immediate rendering keeps a 0.75 screen-pixel minimum radius. At the
// coarsest tiled level (50%), that halo is 1.5 world units, or 6 quarter units.
// World bounds must include it so intersecting resident tiles cannot be carried
// forward stale when a very thin operation crosses a tile cut.
constexpr int kMinimumTiledRadiusQuarter = 6;

}  // namespace

std::optional<PixelRect> operation_world_bounds(std::span<const CompactOperationSample> samples) {
  if (samples.empty()) {
    return std::nullopt;
  }
  int minimum_x = kWorldWidth * 4;
  int minimum_y = kWorldHeight * 4;
  int maximum_x = 0;
  int maximum_y = 0;
  for (const CompactOperationSample sample : samples) {
    if (sample.x_quarter > kWorldWidth * 4 || sample.y_quarter > kWorldHeight * 4 ||
        sample.radius_256 == 0U) {
      return std::nullopt;
    }
    const int radius_quarter =
        std::max((static_cast<int>(sample.radius_256) + 63) / 64, kMinimumTiledRadiusQuarter);
    minimum_x = std::min(minimum_x, static_cast<int>(sample.x_quarter) - radius_quarter);
    minimum_y = std::min(minimum_y, static_cast<int>(sample.y_quarter) - radius_quarter);
    maximum_x = std::max(maximum_x, static_cast<int>(sample.x_quarter) + radius_quarter);
    maximum_y = std::max(maximum_y, static_cast<int>(sample.y_quarter) + radius_quarter);
  }
  return PixelRect{
      .x0 = std::max(0, minimum_x) / 4,
      .y0 = std::max(0, minimum_y) / 4,
      .x1 = std::min(kWorldWidth, (maximum_x + 3) / 4),
      .y1 = std::min(kWorldHeight, (maximum_y + 3) / 4),
  };
}

}  // namespace tinydraw::production
