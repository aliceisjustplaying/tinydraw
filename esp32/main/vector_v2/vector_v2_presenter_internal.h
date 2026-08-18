#ifndef TINYDRAW_ESP32_VECTOR_V2_PRESENTER_INTERNAL_H
#define TINYDRAW_ESP32_VECTOR_V2_PRESENTER_INTERNAL_H

#include <algorithm>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::esp32::presenter_internal {

inline vector_v2::PixelRect align_bounds(vector_v2::PixelRect bounds) {
  bounds.x0 &= ~1;
  bounds.y0 &= ~1;
  bounds.x1 = (bounds.x1 + 1) & ~1;
  bounds.y1 = (bounds.y1 + 1) & ~1;
  bounds.x0 = std::clamp(bounds.x0, 0, vector_v2::kOverviewWidth);
  bounds.y0 = std::clamp(bounds.y0, 0, vector_v2::kOverviewHeight);
  bounds.x1 = std::clamp(bounds.x1, bounds.x0, vector_v2::kOverviewWidth);
  bounds.y1 = std::clamp(bounds.y1, bounds.y0, vector_v2::kOverviewHeight);
  return bounds;
}

}  // namespace tinydraw::esp32::presenter_internal

#endif  // TINYDRAW_ESP32_VECTOR_V2_PRESENTER_INTERNAL_H
