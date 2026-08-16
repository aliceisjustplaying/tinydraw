#include "tinydraw/graphics/coverage_tile.h"

#include <doctest.h>

#include <array>
#include <limits>

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

TEST_CASE("degenerate and non-finite shapes produce no coverage") {
  tinydraw::CoverageTile tile(0, 0, 64, 64);
  constexpr std::array repeated{
      tinydraw::Point{10.5F, 10.5F},
      tinydraw::Point{10.5F, 10.5F},
      tinydraw::Point{10.5F, 10.5F},
      tinydraw::Point{10.5F, 10.5F},
  };
  constexpr std::array collinear{
      tinydraw::Point{5.0F, 5.0F},
      tinydraw::Point{15.0F, 15.0F},
      tinydraw::Point{25.0F, 25.0F},
  };

  tile.rasterize_convex(repeated);
  tile.rasterize_convex(collinear);
  tile.rasterize_circle({.x = std::numeric_limits<float>::quiet_NaN(), .y = 10.0F}, 4.0F);

  CHECK(tile.coverage_at(10, 10) == 0U);
  CHECK(tile.coverage_at(15, 15) == 0U);
}

TEST_CASE("coverage tile rejects invalid dimensions without writing") {
  tinydraw::CoverageTile tile(0, 0, 2, 2);
  tile.union_coverage(0, 0, 255U);
  CHECK_FALSE(tile.reset(0, 0, tinydraw::kTileSize + 1, 1));
  CHECK(tile.width() == 0);
  CHECK(tile.height() == 0);
  tile.rasterize_circle({.x = 0.0F, .y = 0.0F}, 10.0F);
}

TEST_CASE("coverage tile rejects polygons larger than its edge workspace") {
  tinydraw::CoverageTile tile(0, 0, 32, 32);
  constexpr std::array pentagon{
      tinydraw::Point{16.0F, 2.0F}, tinydraw::Point{29.0F, 12.0F}, tinydraw::Point{24.0F, 28.0F},
      tinydraw::Point{8.0F, 28.0F}, tinydraw::Point{3.0F, 12.0F},
  };
  tile.rasterize_convex(pentagon);
  CHECK(tile.coverage_at(16, 16) == 0U);
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
