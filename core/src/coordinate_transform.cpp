#include "tinydraw/platform/coordinate_transform.h"

#include <algorithm>
#include <utility>

namespace tinydraw {
namespace {

float scale_axis(float value, int source_extent, int destination_extent) {
  if (source_extent <= 1 || destination_extent <= 1) {
    return 0.0F;
  }
  const float maximum = static_cast<float>(source_extent - 1);
  const float clamped = std::clamp(value, 0.0F, maximum);
  return clamped * static_cast<float>(destination_extent - 1) / maximum;
}

}  // namespace

Point touch_to_logical(Point raw, int raw_width, int raw_height, TouchTransform transform) {
  if (transform.swap_xy) {
    std::swap(raw.x, raw.y);
    std::swap(raw_width, raw_height);
  }

  if (transform.mirror_x) {
    raw.x = static_cast<float>(raw_width - 1) - raw.x;
  }
  if (transform.mirror_y) {
    raw.y = static_cast<float>(raw_height - 1) - raw.y;
  }

  return {
      .x = scale_axis(raw.x, raw_width, kCanvasWidth),
      .y = scale_axis(raw.y, raw_height, kCanvasHeight),
  };
}

}  // namespace tinydraw
