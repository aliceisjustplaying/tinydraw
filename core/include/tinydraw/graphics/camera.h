#pragma once

#include "tinydraw/geometry.h"

namespace tinydraw {

// World values stay wide until after the camera origin is removed. Existing
// float raster geometry therefore only sees small viewport-local coordinates.
struct Camera {
  double x = 0.0;
  double y = 0.0;
  float zoom = 1.0F;
};

[[nodiscard]] bool camera_valid(Camera camera);
[[nodiscard]] Point camera_project(Camera camera, double world_x, double world_y);
[[nodiscard]] float camera_project_radius(Camera camera, float world_radius,
                                          float minimum_screen_radius = 0.0F);
[[nodiscard]] RectF camera_world_viewport(Camera camera);
[[nodiscard]] bool rects_intersect(RectF left, RectF right);

}  // namespace tinydraw
