#include "tinydraw/graphics/camera.h"

#include <algorithm>
#include <cmath>

namespace tinydraw {

bool camera_valid(Camera camera) {
  return std::isfinite(camera.x) && std::isfinite(camera.y) && std::isfinite(camera.zoom) &&
         camera.zoom > 0.0F;
}

Point camera_project(Camera camera, double world_x, double world_y) {
  if (!camera_valid(camera) || !std::isfinite(world_x) || !std::isfinite(world_y)) {
    return {};
  }
  const double zoom = camera.zoom;
  return {
      .x = static_cast<float>((world_x - camera.x) * zoom),
      .y = static_cast<float>((world_y - camera.y) * zoom),
  };
}

float camera_project_radius(Camera camera, float world_radius, float minimum_screen_radius) {
  if (!camera_valid(camera) || !std::isfinite(world_radius) || world_radius < 0.0F ||
      !std::isfinite(minimum_screen_radius) || minimum_screen_radius < 0.0F) {
    return 0.0F;
  }
  return std::max(world_radius * camera.zoom, minimum_screen_radius);
}

RectF camera_world_viewport(Camera camera) {
  if (!camera_valid(camera)) {
    return {};
  }
  return {
      .x0 = static_cast<float>(camera.x),
      .y0 = static_cast<float>(camera.y),
      .x1 = static_cast<float>(camera.x + static_cast<double>(kCanvasWidth) / camera.zoom),
      .y1 = static_cast<float>(camera.y + static_cast<double>(kCanvasHeight) / camera.zoom),
  };
}

bool rects_intersect(RectF left, RectF right) {
  return left.x0 <= right.x1 && left.x1 >= right.x0 && left.y0 <= right.y1 && left.y1 >= right.y0;
}

}  // namespace tinydraw
