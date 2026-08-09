#include "tinydraw/ink/ribbon_geometry.h"

#include <doctest.h>

#include <array>
#include <cmath>

namespace {

tinydraw::InkPoint ink_point(float x, float y, float radius) {
  return {
      .position = {.x = x, .y = y},
      .pressure = 0.5F,
      .radius = radius,
      .distance = 0.0F,
      .running_length = 0.0F,
      .timestamp_us = 0U,
  };
}

}  // namespace

TEST_CASE("PF ribbon emits unionable triangles and round caps") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(20.0F, 20.0F, 3.0F),
                          ink_point(30.0F, 20.0F, 4.0F)};

  const auto primitives = tinydraw::build_pf_ribbon(points);

  REQUIRE(primitives.size() == 6U);
  CHECK(primitives.front().kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(primitives.front().radius == doctest::Approx(2.0F));
  CHECK(primitives.back().kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(primitives.back().radius == doctest::Approx(4.0F));
  for (std::size_t index = 1; index + 1U < primitives.size(); ++index) {
    CHECK(primitives[index].kind == tinydraw::RibbonPrimitiveKind::kConvex);
    CHECK(primitives[index].point_count == 3);
  }
}

TEST_CASE("a reversing ribbon adds explicit corner coverage") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(20.0F, 20.0F, 3.0F),
                          ink_point(10.0F, 20.0F, 2.0F)};

  const auto primitives = tinydraw::build_pf_ribbon(points);

  REQUIRE(primitives.size() == 7U);
  CHECK(primitives[3].kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(primitives[3].center.x == doctest::Approx(20.0F));
}

TEST_CASE("ribbon geometry remains finite for duplicate points") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(10.0F, 20.0F, 2.0F),
                          ink_point(20.0F, 20.0F, 2.0F)};

  const auto primitives = tinydraw::build_pf_ribbon(points);

  for (const auto& primitive : primitives) {
    CHECK(std::isfinite(primitive.center.x));
    CHECK(std::isfinite(primitive.center.y));
    for (int index = 0; index < primitive.point_count; ++index) {
      CHECK(std::isfinite(primitive.points[static_cast<std::size_t>(index)].x));
      CHECK(std::isfinite(primitive.points[static_cast<std::size_t>(index)].y));
    }
  }
}
