#include "tinydraw/graphics/viewport_updates.h"

#include <doctest.h>

#include <array>
#include <vector>

namespace {

constexpr std::size_t kPixels =
    static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kInk = 0x001FU;

std::size_t pixel(int x, int y) { return static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x); }

}  // namespace

TEST_CASE("identical viewports need no display updates") {
  const std::vector<std::uint16_t> displayed(kPixels, kWhite);
  std::array<tinydraw::Rect, tinydraw::kMaxViewportUpdateRegions> regions{};

  const auto stats = tinydraw::plan_viewport_updates(displayed, displayed, 372, regions);

  CHECK(stats.complete);
  CHECK(stats.regions == 0U);
  CHECK(stats.pixels == 0U);
}

TEST_CASE("sparse changes become bounded tile runs") {
  std::vector<std::uint16_t> displayed(kPixels, kWhite);
  auto next = displayed;
  next[pixel(10, 10)] = kInk;
  next[pixel(80, 10)] = kInk;
  next[pixel(10, 80)] = kInk;
  std::array<tinydraw::Rect, tinydraw::kMaxViewportUpdateRegions> regions{};

  const auto stats = tinydraw::plan_viewport_updates(displayed, next, 372, regions);

  REQUIRE(stats.complete);
  REQUIRE(stats.regions == 2U);
  CHECK(regions[0].x0 == 0);
  CHECK(regions[0].y0 == 0);
  CHECK(regions[0].x1 == 128);
  CHECK(regions[0].y1 == 64);
  CHECK(regions[1].x0 == 0);
  CHECK(regions[1].y0 == 64);
  CHECK(regions[1].x1 == 64);
  CHECK(regions[1].y1 == 128);
  CHECK(stats.pixels == 3U * 64U * 64U);
  CHECK(stats.pixels < static_cast<std::size_t>(tinydraw::kCanvasWidth * 372 / 10));

  tinydraw::sync_viewport_updates(next, displayed, std::span(regions).first(stats.regions));
  CHECK(displayed == next);
}

TEST_CASE("matching vertical runs merge into one transfer region") {
  std::vector<std::uint16_t> displayed(kPixels, kWhite);
  auto next = displayed;
  next[pixel(20, 20)] = kInk;
  next[pixel(20, 100)] = kInk;
  std::array<tinydraw::Rect, tinydraw::kMaxViewportUpdateRegions> regions{};

  const auto stats = tinydraw::plan_viewport_updates(displayed, next, 372, regions);

  REQUIRE(stats.complete);
  REQUIRE(stats.regions == 1U);
  CHECK(regions[0].x0 == 0);
  CHECK(regions[0].y0 == 0);
  CHECK(regions[0].x1 == 64);
  CHECK(regions[0].y1 == 128);
  CHECK(stats.pixels == 2U * 64U * 64U);
}

TEST_CASE("updates stop above the persistent toolbar") {
  std::vector<std::uint16_t> displayed(kPixels, kWhite);
  auto next = displayed;
  next[pixel(20, 380)] = kInk;
  std::array<tinydraw::Rect, tinydraw::kMaxViewportUpdateRegions> regions{};

  const auto stats = tinydraw::plan_viewport_updates(displayed, next, 372, regions);

  CHECK(stats.complete);
  CHECK(stats.regions == 0U);
  CHECK(stats.pixels == 0U);
}
