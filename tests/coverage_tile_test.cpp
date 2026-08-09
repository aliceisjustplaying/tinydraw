#include "tinydraw/graphics/coverage_tile.h"

#include <doctest.h>

#include <array>

TEST_CASE("coverage pieces union without accumulating overlap") {
  tinydraw::CoverageTile tile(0, 0, 64, 64);
  tile.rasterize_circle({.x = 20.0F, .y = 20.0F}, 4.0F);
  const auto once = tile.coverage_at(20, 20);

  tile.rasterize_circle({.x = 20.0F, .y = 20.0F}, 4.0F);

  CHECK(once == 255U);
  CHECK(tile.coverage_at(20, 20) == once);
}

TEST_CASE("antialiased circle edges have partial coverage") {
  tinydraw::CoverageTile tile(0, 0, 64, 64);
  tile.rasterize_circle({.x = 20.0F, .y = 20.0F}, 3.0F);

  CHECK(tile.coverage_at(20, 20) == 255U);
  CHECK(tile.coverage_at(22, 21) > 0U);
  CHECK(tile.coverage_at(22, 21) < 255U);
  CHECK(tile.coverage_at(24, 24) == 0U);
}

TEST_CASE("convex ribbon pieces rasterize with antialiased edges") {
  tinydraw::CoverageTile tile(0, 0, 64, 64);
  constexpr std::array diamond{
      tinydraw::Point{10.0F, 15.0F},
      tinydraw::Point{15.0F, 10.0F},
      tinydraw::Point{20.0F, 15.0F},
      tinydraw::Point{15.0F, 20.0F},
  };

  tile.rasterize_convex(diamond);

  CHECK(tile.coverage_at(15, 15) == 255U);
  CHECK(tile.coverage_at(10, 14) > 0U);
  CHECK(tile.coverage_at(10, 14) < 255U);
  CHECK(tile.coverage_at(8, 8) == 0U);
}

TEST_CASE("RGB565 coverage composites once in stored channel space") {
  tinydraw::CoverageTile tile(0, 0, 2, 1);
  tile.union_coverage(0, 0, 255U);
  tile.union_coverage(1, 0, 128U);
  std::array<std::uint16_t, 2> pixels{0xFFFFU, 0xFFFFU};

  tinydraw::composite_rgb565(tile, 0x001FU, pixels);

  CHECK(pixels[0] == 0x001FU);
  CHECK(pixels[1] != 0xFFFFU);
  CHECK(pixels[1] != 0x001FU);
}
