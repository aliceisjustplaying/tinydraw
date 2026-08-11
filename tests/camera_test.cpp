#include "tinydraw/graphics/camera.h"

#include <doctest.h>

TEST_CASE("camera projects negative world coordinates and zoom") {
  const tinydraw::Camera camera{.x = -100.0, .y = -50.0, .zoom = 2.0F};
  const auto point = tinydraw::camera_project(camera, -90.0, -30.0);

  CHECK(point.x == doctest::Approx(20.0F));
  CHECK(point.y == doctest::Approx(40.0F));
  CHECK(tinydraw::camera_project_radius(camera, 3.0F) == doctest::Approx(6.0F));
  const auto viewport = tinydraw::camera_world_viewport(camera);
  CHECK(viewport.x0 == doctest::Approx(-100.0F));
  CHECK(viewport.y0 == doctest::Approx(-50.0F));
  CHECK(viewport.x1 == doctest::Approx(84.0F));
  CHECK(viewport.y1 == doctest::Approx(174.0F));
}

TEST_CASE("camera removes a large origin before converting to float") {
  constexpr double origin = 1'000'000'000'000.0;
  const tinydraw::Camera camera{.x = origin, .y = -origin, .zoom = 0.5F};
  const auto point = tinydraw::camera_project(camera, origin + 18.0, -origin + 30.0);

  CHECK(point.x == doctest::Approx(9.0F));
  CHECK(point.y == doctest::Approx(15.0F));
}

TEST_CASE("camera offers optional minimum visible radius") {
  const tinydraw::Camera camera{.zoom = 0.25F};
  CHECK(tinydraw::camera_project_radius(camera, 1.125F) == doctest::Approx(0.28125F));
  CHECK(tinydraw::camera_project_radius(camera, 1.125F, 0.45F) == doctest::Approx(0.45F));
}

TEST_CASE("rectangle intersection includes strokes touching a viewport edge") {
  CHECK(tinydraw::rects_intersect({.x0 = -5.0F, .y0 = 10.0F, .x1 = 0.0F, .y1 = 20.0F},
                                  {.x0 = 0.0F, .y0 = 0.0F, .x1 = 100.0F, .y1 = 100.0F}));
  CHECK_FALSE(tinydraw::rects_intersect({.x0 = -6.0F, .y0 = 10.0F, .x1 = -1.0F, .y1 = 20.0F},
                                        {.x0 = 0.0F, .y0 = 0.0F, .x1 = 100.0F, .y1 = 100.0F}));
}

TEST_CASE("invalid cameras do not project usable geometry") {
  CHECK_FALSE(tinydraw::camera_valid({.zoom = 0.0F}));
  CHECK_FALSE(tinydraw::camera_valid({.zoom = -1.0F}));
  CHECK(tinydraw::camera_project_radius({.zoom = 0.0F}, 2.0F) == 0.0F);
}
