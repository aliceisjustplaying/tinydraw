#include "tinydraw/graphics/ribbon_renderer.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <vector>

TEST_CASE("overlapping primitives composite only once") {
  constexpr int width = 64;
  constexpr int height = 64;
  const tinydraw::RibbonPrimitive circle{
      .kind = tinydraw::RibbonPrimitiveKind::kCircle,
      .center = {.x = 20.0F, .y = 20.0F},
      .radius = 5.0F,
  };
  const std::array one{circle};
  const std::array duplicated{circle, circle};
  std::vector<std::uint16_t> once(width * height, 0xFFFFU);
  std::vector<std::uint16_t> twice = once;
  tinydraw::RibbonRenderer renderer;

  static_cast<void>(renderer.render(one, once, width, height, 0x001FU));
  static_cast<void>(renderer.render(duplicated, twice, width, height, 0x001FU));

  CHECK(once == twice);
}

TEST_CASE("geometry outside the canvas does no tile work") {
  constexpr int width = 64;
  constexpr int height = 64;
  const std::array primitives{tinydraw::RibbonPrimitive{
      .kind = tinydraw::RibbonPrimitiveKind::kCircle,
      .center = {.x = -100.0F, .y = -100.0F},
      .radius = 5.0F,
  }};
  std::vector<std::uint16_t> canvas(width * height, 0xFFFFU);
  tinydraw::RibbonRenderer renderer;

  const auto stats = renderer.render(primitives, canvas, width, height, 0x001FU);

  CHECK(stats.tiles_rasterized == 0U);
  CHECK(std::all_of(canvas.begin(), canvas.end(),
                    [](std::uint16_t pixel) { return pixel == 0xFFFFU; }));
}

TEST_CASE("extreme finite coordinates are rejected before integer conversion") {
  constexpr int width = 64;
  constexpr int height = 64;
  const std::array primitives{tinydraw::RibbonPrimitive{
      .kind = tinydraw::RibbonPrimitiveKind::kConvex,
      .points = {tinydraw::Point{-1.0e30F, 10.0F}, tinydraw::Point{10.0F, 10.0F},
                 tinydraw::Point{10.0F, 20.0F}, tinydraw::Point{-1.0e30F, 20.0F}},
      .point_count = 4U,
  }};
  std::vector<std::uint16_t> canvas(width * height, 0xFFFFU);
  tinydraw::RibbonRenderer renderer;

  const auto stats = renderer.render(primitives, canvas, width, height, 0x001FU);

  CHECK(stats.tiles_rasterized == 0U);
}

TEST_CASE("malformed convex primitives are ignored safely") {
  constexpr int width = 64;
  constexpr int height = 64;
  const std::array primitives{tinydraw::RibbonPrimitive{
      .kind = tinydraw::RibbonPrimitiveKind::kConvex,
      .point_count = 5U,
  }};
  std::vector<std::uint16_t> canvas(width * height, 0xFFFFU);
  tinydraw::RibbonRenderer renderer;

  const auto stats = renderer.render(primitives, canvas, width, height, 0x001FU);

  CHECK(stats.tiles_rasterized == 0U);
}

TEST_CASE("dirty ribbon bounds enumerate every crossed tile") {
  constexpr int width = 128;
  constexpr int height = 64;
  const std::array primitives{tinydraw::RibbonPrimitive{
      .kind = tinydraw::RibbonPrimitiveKind::kCircle,
      .center = {.x = 64.0F, .y = 32.0F},
      .radius = 5.0F,
  }};
  std::vector<std::uint16_t> canvas(width * height, 0xFFFFU);
  tinydraw::RibbonRenderer renderer;

  const auto stats = renderer.render(primitives, canvas, width, height, 0x001FU);

  CHECK(stats.tiles_rasterized == 2U);
  CHECK(stats.pixels_considered == 2U * 64U * 64U);
  CHECK(std::count(canvas.begin(), canvas.end(), 0x001FU) > 0);
}
