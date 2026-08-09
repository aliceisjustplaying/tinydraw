#include <doctest.h>

#include "input_coordinates.h"

TEST_CASE("mouse coordinates map from window points to the logical canvas") {
  const auto center = tinydraw::host::window_to_logical({368.0F, 448.0F}, 736, 896);

  REQUIRE(center.has_value());
  CHECK(center->x == doctest::Approx(184.0F));
  CHECK(center->y == doctest::Approx(224.0F));
}

TEST_CASE("mouse mapping accounts for letterboxing after resize") {
  const auto top_left = tinydraw::host::window_to_logical({132.0F, 0.0F}, 1'000, 896);
  const auto bottom_right = tinydraw::host::window_to_logical({868.0F, 896.0F}, 1'000, 896);
  const auto outside = tinydraw::host::window_to_logical({50.0F, 100.0F}, 1'000, 896);

  REQUIRE(top_left.has_value());
  REQUIRE(bottom_right.has_value());
  CHECK(top_left->x == doctest::Approx(0.0F));
  CHECK(top_left->y == doctest::Approx(0.0F));
  CHECK(bottom_right->x == doctest::Approx(367.0F));
  CHECK(bottom_right->y == doctest::Approx(447.0F));
  CHECK_FALSE(outside.has_value());
}
