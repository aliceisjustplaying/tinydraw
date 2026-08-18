#include "tinydraw/vector_v2/tile_payload_analysis.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("uniform tile classification exits without measurement data") {
  constexpr std::array<std::uint16_t, 8> pixels{9, 9, 0, 9, 9, 0, 9, 9};
  CHECK(vector_v2::tile_uniform_color(pixels, 2, 3, 3) == 9U);
  CHECK_FALSE(vector_v2::tile_uniform_color(pixels, 3, 2, 3));
  CHECK_FALSE(vector_v2::tile_uniform_color(pixels, 2, 3, 2));
}

TEST_CASE("tile payload analysis rejects malformed input") {
  std::array<std::uint16_t, 4> pixels{};
  CHECK_FALSE(vector_v2::analyze_tile_payload(pixels, 0, 4));
  CHECK_FALSE(vector_v2::analyze_tile_payload(pixels, 2, 0));
  CHECK_FALSE(vector_v2::analyze_tile_payload(pixels, vector_v2::kTileWidth + 1, 1));
  CHECK_FALSE(vector_v2::analyze_tile_payload(pixels, 2, 3));
}

TEST_CASE("uniform tile has one row run per row") {
  constexpr std::array<std::uint16_t, 12> pixels{
      0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
      0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
  };
  const auto analysis = vector_v2::analyze_tile_payload(pixels, 4, 3);
  REQUIRE(analysis.has_value());
  CHECK(analysis->uniform);
  CHECK(analysis->uniform_color == 0xFFFFU);
  CHECK(analysis->pixel_count == 12U);
  CHECK(analysis->raw_bytes == 24U);
  CHECK(analysis->row_runs == 3U);
  CHECK(analysis->estimated_row_rle_bytes == 9U);
}

TEST_CASE("row run estimate resets at every row") {
  constexpr std::array<std::uint16_t, 8> pixels{
      1, 1, 2, 2, 2, 2, 1, 1,
  };
  const auto analysis = vector_v2::analyze_tile_payload(pixels, 4, 2);
  REQUIRE(analysis.has_value());
  CHECK_FALSE(analysis->uniform);
  CHECK(analysis->uniform_color == 1U);
  CHECK(analysis->row_runs == 4U);
  CHECK(analysis->estimated_row_rle_bytes == 12U);
}

TEST_CASE("alternating pixels expose row RLE expansion") {
  constexpr std::array<std::uint16_t, 8> pixels{1, 2, 1, 2, 2, 1, 2, 1};
  const auto analysis = vector_v2::analyze_tile_payload(pixels, 4, 2);
  REQUIRE(analysis.has_value());
  CHECK_FALSE(analysis->uniform);
  CHECK(analysis->row_runs == pixels.size());
  CHECK(analysis->estimated_row_rle_bytes == 24U);
  CHECK(analysis->estimated_row_rle_bytes > analysis->raw_bytes);
}
