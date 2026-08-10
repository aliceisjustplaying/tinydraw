#include "tinydraw/ink/ribbon_geometry.h"

#include "tinydraw/graphics/coverage_tile.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

void check_primitive(const tinydraw::RibbonPrimitive& actual,
                     const tinydraw::RibbonPrimitive& expected) {
  CHECK(actual.kind == expected.kind);
  CHECK(actual.point_count == expected.point_count);
  CHECK(actual.center.x == doctest::Approx(expected.center.x));
  CHECK(actual.center.y == doctest::Approx(expected.center.y));
  CHECK(actual.radius == doctest::Approx(expected.radius));
  for (std::size_t index = 0; index < expected.point_count; ++index) {
    CHECK(actual.points[index].x == doctest::Approx(expected.points[index].x));
    CHECK(actual.points[index].y == doctest::Approx(expected.points[index].y));
  }
}

}  // namespace

TEST_CASE("streaming ribbon reproduces each growing batch without changing committed pieces") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(20.0F, 20.0F, 3.0F),
                          ink_point(25.0F, 30.0F, 4.0F), ink_point(35.0F, 25.0F, 2.5F)};
  tinydraw::RibbonStream stream;
  std::vector<tinydraw::RibbonPrimitive> committed;

  for (std::size_t point_count = 1; point_count <= points.size(); ++point_count) {
    const auto update = stream.append(points[point_count - 1U]);
    committed.insert(committed.end(), update.committed.begin(), update.committed.end());

    std::vector<tinydraw::RibbonPrimitive> visible = committed;
    visible.insert(visible.end(), update.provisional.begin(), update.provisional.end());
    const auto batch =
        tinydraw::build_pf_ribbon(std::span<const tinydraw::InkPoint>{points}.first(point_count));

    REQUIRE(visible.size() == batch.size());
    for (std::size_t index = 0; index < batch.size(); ++index) {
      check_primitive(visible[index], batch[index]);
    }
  }
}

TEST_CASE("finishing a streaming ribbon commits the exact final batch and resets the stream") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(20.0F, 20.0F, 3.0F),
                          ink_point(25.0F, 30.0F, 4.0F)};
  tinydraw::RibbonStream stream;
  std::vector<tinydraw::RibbonPrimitive> committed;

  for (std::size_t index = 0; index + 1U < points.size(); ++index) {
    const auto update = stream.append(points[index]);
    committed.insert(committed.end(), update.committed.begin(), update.committed.end());
  }
  const auto final_update = stream.finish(points.back());
  committed.insert(committed.end(), final_update.committed.begin(), final_update.committed.end());

  const auto batch = tinydraw::build_pf_ribbon(points);
  REQUIRE(committed.size() == batch.size());
  CHECK(final_update.provisional.empty());
  CHECK_FALSE(stream.active());
  for (std::size_t index = 0; index < batch.size(); ++index) {
    check_primitive(committed[index], batch[index]);
  }
}

TEST_CASE("streaming ribbon output stays bounded during a long stroke") {
  tinydraw::RibbonStream stream;
  std::size_t committed_count = 0U;

  for (int index = 0; index < 1'000; ++index) {
    const float coordinate = static_cast<float>(index);
    const auto update = stream.append(ink_point(coordinate, std::fmod(coordinate, 17.0F), 3.0F));
    CHECK(update.committed.size() <= 4U);
    CHECK(update.provisional.size() <= 4U);
    committed_count += update.committed.size();
  }

  CHECK(committed_count > 900U);
  CHECK(stream.active());
}

TEST_CASE("duplicate streaming points do not grow or alter the ribbon") {
  tinydraw::RibbonStream stream;
  const auto point = ink_point(10.0F, 20.0F, 2.0F);
  const auto first = stream.append(point);
  const auto duplicate = stream.append(point);

  REQUIRE(first.provisional.size() == duplicate.provisional.size());
  REQUIRE(duplicate.committed.empty());
  for (std::size_t index = 0; index < first.provisional.size(); ++index) {
    check_primitive(duplicate.provisional.begin()[index], first.provisional.begin()[index]);
  }
}

TEST_CASE("curved ribbon stabilizes midpoint curves while its tail reaches the latest point") {
  tinydraw::CurvedRibbonStream stream;
  const auto first = stream.append(ink_point(10.0F, 30.0F, 2.0F));
  const auto second = stream.append(ink_point(30.0F, 10.0F, 4.0F));
  const auto third = stream.append(ink_point(50.0F, 30.0F, 6.0F));

  CHECK(first.committed.empty());
  CHECK(second.committed.empty());
  CHECK_FALSE(third.committed.empty());
  CHECK_FALSE(third.provisional.empty());
  CHECK(third.provisional.end()[-1].kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(third.provisional.end()[-1].center.x == doctest::Approx(50.0F));
  CHECK(third.provisional.end()[-1].center.y == doctest::Approx(30.0F));
  CHECK(third.provisional.end()[-1].radius == doctest::Approx(6.0F));
}

TEST_CASE("curved ribbon pieces fully cover their internal join") {
  tinydraw::CurvedRibbonStream stream;
  static_cast<void>(stream.append(ink_point(5.0F, 50.0F, 8.0F)));
  static_cast<void>(stream.append(ink_point(25.0F, 5.0F, 8.0F)));
  const auto update = stream.append(ink_point(55.0F, 50.0F, 8.0F));
  tinydraw::CoverageTile coverage(0, 0);
  for (const auto& primitive : update.committed) {
    if (primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
      coverage.rasterize_circle(primitive.center, primitive.radius);
    } else {
      coverage.rasterize_convex(
          std::span(primitive.points.data(), primitive.point_count));
    }
  }

  CHECK(coverage.coverage_at(24, 23) == 255U);
}

TEST_CASE("curved ribbon finish commits its visible tail and resets") {
  tinydraw::CurvedRibbonStream stream;
  const auto first = ink_point(10.0F, 20.0F, 2.0F);
  const auto last = ink_point(40.0F, 50.0F, 4.0F);
  static_cast<void>(stream.append(first));
  static_cast<void>(stream.append(last));

  const auto update = stream.finish(last);

  CHECK_FALSE(update.committed.empty());
  CHECK(update.provisional.empty());
  CHECK_FALSE(stream.active());
  CHECK(update.committed.end()[-1].kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(update.committed.end()[-1].center.x == doctest::Approx(last.position.x));
  CHECK(update.committed.end()[-1].center.y == doctest::Approx(last.position.y));
}

TEST_CASE("curved ribbon output remains bounded for long sparse strokes") {
  tinydraw::CurvedRibbonStream stream;
  for (int index = 0; index < 1'000; ++index) {
    const float coordinate = static_cast<float>(index);
    const auto update = stream.append(ink_point(coordinate, std::fmod(coordinate, 31.0F), 8.0F));
    CHECK(update.committed.size() <= 5U);
    CHECK(update.provisional.size() <= 3U);
  }
  CHECK(stream.active());
}

TEST_CASE("PF ribbon emits unionable triangles and round caps") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(20.0F, 20.0F, 3.0F),
                          ink_point(30.0F, 20.0F, 4.0F)};

  const auto primitives = tinydraw::build_pf_ribbon(points);

  REQUIRE(primitives.size() == 5U);
  CHECK(primitives.front().kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(primitives.front().radius == doctest::Approx(2.0F));
  CHECK(primitives[2].kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(primitives[2].center.x == doctest::Approx(20.0F));
  CHECK(primitives.back().kind == tinydraw::RibbonPrimitiveKind::kCircle);
  CHECK(primitives.back().radius == doctest::Approx(4.0F));
  CHECK(primitives[1].kind == tinydraw::RibbonPrimitiveKind::kConvex);
  CHECK(primitives[1].point_count == 4);
  CHECK(primitives[3].kind == tinydraw::RibbonPrimitiveKind::kConvex);
  CHECK(primitives[3].point_count == 4);
}

TEST_CASE("a reversing ribbon adds explicit corner coverage") {
  const std::array points{ink_point(10.0F, 20.0F, 2.0F), ink_point(20.0F, 20.0F, 3.0F),
                          ink_point(10.0F, 20.0F, 2.0F)};

  const auto primitives = tinydraw::build_pf_ribbon(points);

  const auto corner = std::find_if(
      primitives.begin() + 1, primitives.end() - 1, [](const tinydraw::RibbonPrimitive& primitive) {
        return primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle &&
               primitive.center.x == 20.0F;
      });
  REQUIRE(corner != primitives.end() - 1);
  CHECK(corner->center.x == doctest::Approx(20.0F));
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
