#pragma once

#include <algorithm>
#include <optional>

#include "tinydraw/geometry.h"

namespace tinydraw::host {

// SDL mouse events and SDL_GetWindowSize use window points on macOS. Do not use
// renderer output pixels here: Retina drawable pixels have a different scale.
[[nodiscard]] inline std::optional<Point> window_to_logical(Point window_point, int window_width,
                                                            int window_height) {
  if (window_width <= 0 || window_height <= 0) {
    return std::nullopt;
  }

  const float scale_x = static_cast<float>(window_width) / static_cast<float>(kCanvasWidth);
  const float scale_y = static_cast<float>(window_height) / static_cast<float>(kCanvasHeight);
  const float scale = std::min(scale_x, scale_y);
  const float rendered_width = static_cast<float>(kCanvasWidth) * scale;
  const float rendered_height = static_cast<float>(kCanvasHeight) * scale;
  const float offset_x = (static_cast<float>(window_width) - rendered_width) * 0.5F;
  const float offset_y = (static_cast<float>(window_height) - rendered_height) * 0.5F;

  if (window_point.x < offset_x || window_point.y < offset_y ||
      window_point.x > offset_x + rendered_width || window_point.y > offset_y + rendered_height) {
    return std::nullopt;
  }

  return Point{
      .x = std::clamp((window_point.x - offset_x) / scale, 0.0F,
                      static_cast<float>(kCanvasWidth - 1)),
      .y = std::clamp((window_point.y - offset_y) / scale, 0.0F,
                      static_cast<float>(kCanvasHeight - 1)),
  };
}

}  // namespace tinydraw::host
