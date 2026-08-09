#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "tinydraw/platform/coordinate_transform.h"

#include <doctest.h>

#include "tinydraw/geometry.h"

TEST_CASE("touch corners map to the logical canvas") {
  const auto origin = tinydraw::touch_to_logical({0.0F, 0.0F}, 368, 448, {});
  CHECK(origin.x == doctest::Approx(0.0F));
  CHECK(origin.y == doctest::Approx(0.0F));

  const auto far_corner = tinydraw::touch_to_logical({367.0F, 447.0F}, 368, 448, {});
  CHECK(far_corner.x == doctest::Approx(367.0F));
  CHECK(far_corner.y == doctest::Approx(447.0F));
}

TEST_CASE("touch transform order is swap then mirror then scale") {
  const tinydraw::TouchTransform transform{
      .swap_xy = true,
      .mirror_x = true,
      .mirror_y = false,
  };

  const auto point = tinydraw::touch_to_logical({10.0F, 20.0F}, 100, 200, transform);
  const float expected_x = (199.0F - 20.0F) * 367.0F / 199.0F;
  const float expected_y = 10.0F * 447.0F / 99.0F;
  CHECK(point.x == doctest::Approx(expected_x));
  CHECK(point.y == doctest::Approx(expected_y));
}

TEST_CASE("touch input is clamped before scaling") {
  const auto point = tinydraw::touch_to_logical({-20.0F, 900.0F}, 368, 448, {});
  CHECK(point.x == doctest::Approx(0.0F));
  CHECK(point.y == doctest::Approx(447.0F));
}

TEST_CASE("panel offsets apply after logical geometry") {
  constexpr tinydraw::Rect logical{.x0 = 3, .y0 = 5, .x1 = 13, .y1 = 21};
  constexpr tinydraw::PanelGeometry panel{.x_offset = 7, .y_offset = 11};
  constexpr auto result = tinydraw::logical_to_panel(logical, panel);
  static_assert(result.x0 == 10);
  static_assert(result.y0 == 16);
  static_assert(result.x1 == 20);
  static_assert(result.y1 == 32);
  CHECK(result.x0 == 10);
}
