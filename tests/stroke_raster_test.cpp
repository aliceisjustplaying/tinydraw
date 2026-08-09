#include "tinydraw/graphics/stroke_raster.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "tinydraw/graphics/ribbon_renderer.h"
#include "tinydraw/ink/ink_stream.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kInk = 0x001FU;
constexpr std::size_t kPixelCount =
    static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);

struct RasterFixture {
  std::vector<std::uint16_t> committed = std::vector<std::uint16_t>(kPixelCount, kBackground);
  std::vector<std::uint16_t> visible = committed;
  std::vector<std::uint8_t> coverage = std::vector<std::uint8_t>(kPixelCount, 0U);
  tinydraw::StrokeRaster raster{committed, visible, coverage};
};

std::size_t changed_pixels(const std::vector<std::uint16_t>& pixels) {
  return static_cast<std::size_t>(std::count_if(
      pixels.begin(), pixels.end(), [](std::uint16_t pixel) { return pixel != kBackground; }));
}

void append_geometry(std::vector<tinydraw::RibbonPrimitive>& geometry, std::size_t& committed_count,
                     const tinydraw::RibbonUpdate& update) {
  geometry.resize(committed_count);
  geometry.insert(geometry.end(), update.committed.begin(), update.committed.end());
  committed_count = geometry.size();
  geometry.insert(geometry.end(), update.provisional.begin(), update.provisional.end());
}

}  // namespace

TEST_CASE("provisional stroke pixels never enter the persistent canvas") {
  RasterFixture fixture;
  tinydraw::RibbonStream ribbon;
  const tinydraw::InkPoint first{.position = {40.0F, 80.0F}, .radius = 8.0F};
  const tinydraw::InkPoint second{.position = {80.0F, 90.0F}, .radius = 8.0F};

  static_cast<void>(fixture.raster.update(ribbon.append(first), kInk));
  static_cast<void>(fixture.raster.update(ribbon.append(second), kInk));

  CHECK(changed_pixels(fixture.visible) > 0U);
  CHECK(changed_pixels(fixture.committed) == 0U);

  static_cast<void>(fixture.raster.finish(ribbon.finish(second), kInk));

  CHECK(fixture.visible == fixture.committed);
  CHECK(changed_pixels(fixture.committed) > 0U);
  CHECK(std::all_of(fixture.coverage.begin(), fixture.coverage.end(),
                    [](std::uint8_t value) { return value == 0U; }));
}

TEST_CASE("incremental stroke raster matches one-pass coverage union") {
  RasterFixture fixture;
  tinydraw::RibbonStream ribbon;
  std::vector<tinydraw::RibbonPrimitive> geometry;
  std::size_t committed_count = 0U;

  for (int index = 0; index < 80; ++index) {
    const float step = static_cast<float>(index);
    const tinydraw::InkPoint point{
        .position = {.x = 30.0F + step * 3.0F, .y = 150.0F + 50.0F * std::sin(step * 0.18F)},
        .radius = 7.0F,
    };
    const auto update = index + 1 == 80 ? ribbon.finish(point) : ribbon.append(point);
    append_geometry(geometry, committed_count, update);
    if (index + 1 == 80) {
      static_cast<void>(fixture.raster.finish(update, kInk));
    } else {
      static_cast<void>(fixture.raster.update(update, kInk));
    }
  }

  std::vector<std::uint16_t> expected(kPixelCount, kBackground);
  tinydraw::RibbonRenderer renderer;
  static_cast<void>(
      renderer.render(geometry, expected, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, kInk));

  CHECK(fixture.committed == expected);
}

TEST_CASE("XL stroke update work stays bounded as the gesture grows") {
  RasterFixture fixture;
  tinydraw::InkConfig config;
  config.size = 20.0F;
  tinydraw::InkStream ink(config);
  tinydraw::RibbonStream ribbon;
  std::uint32_t maximum_tile_visits = 0U;
  std::uint32_t maximum_tiles = 0U;
  bool display_counts_match = true;

  for (int index = 0; index < 500; ++index) {
    const float step = static_cast<float>(index);
    const tinydraw::TouchPoint touch{
        .x = 30.0F + std::fmod(step * 2.3F, 300.0F),
        .y = 200.0F + 120.0F * std::sin(step * 0.09F),
        .timestamp_us = static_cast<std::uint32_t>(index * 8'000),
    };
    const auto point = index == 0 ? ink.begin(touch) : ink.update(touch);
    const auto stats = fixture.raster.update(ribbon.append(point), kInk);
    maximum_tile_visits = std::max(maximum_tile_visits, stats.primitive_tile_visits);
    maximum_tiles = std::max(maximum_tiles, stats.tiles_updated);
    display_counts_match =
        display_counts_match && stats.display_bytes == stats.pixels_composited * 2U;
  }

  CHECK(maximum_tile_visits <= 100U);
  CHECK(maximum_tiles <= 20U);
  CHECK(display_counts_match);
}
