#include <doctest.h>

#include "input_coordinates.h"

TEST_CASE("SDL logical mouse coordinates are not scaled a second time") {
  const auto center = tinydraw::host::event_to_logical({184.0F, 224.0F});
  const auto bottom_right = tinydraw::host::event_to_logical({367.0F, 447.0F});

  REQUIRE(center.has_value());
  REQUIRE(bottom_right.has_value());
  CHECK(center->x == doctest::Approx(184.0F));
  CHECK(center->y == doctest::Approx(224.0F));
  CHECK(bottom_right->x == doctest::Approx(367.0F));
  CHECK(bottom_right->y == doctest::Approx(447.0F));
}

TEST_CASE("SDL events outside the logical canvas are ignored") {
  CHECK_FALSE(tinydraw::host::event_to_logical({-1.0F, 100.0F}).has_value());
  CHECK_FALSE(tinydraw::host::event_to_logical({100.0F, 448.0F}).has_value());
}
