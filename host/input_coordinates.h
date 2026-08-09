#pragma once

#include <optional>

#include "tinydraw/geometry.h"

namespace tinydraw::host {

// With SDL_RenderSetLogicalSize, sdl2-compat reports mouse events in renderer-
// logical coordinates. Scaling them by the Retina window size would scale twice.
[[nodiscard]] inline std::optional<Point> event_to_logical(Point event_point) {
  if (event_point.x < 0.0F || event_point.x >= static_cast<float>(kCanvasWidth) ||
      event_point.y < 0.0F || event_point.y >= static_cast<float>(kCanvasHeight)) {
    return std::nullopt;
  }
  return event_point;
}

}  // namespace tinydraw::host
