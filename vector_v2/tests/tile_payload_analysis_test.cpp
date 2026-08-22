#include "tinydraw/vector_v2/tile_payload_analysis.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("uniform tile classification exits without measurement data") {
  constexpr std::array<std::uint16_t, 8> pixels{9, 9, 0, 9, 9, 0, 9, 9};
  CHECK(vector_v2::tile_uniform_color(pixels, 2, 3, 3) == 9U);
  CHECK_FALSE(vector_v2::tile_uniform_color(pixels, 3, 2, 3));
  CHECK_FALSE(vector_v2::tile_uniform_color(pixels, 2, 3, 2));
}

TEST_CASE("uniform tile classification exhausts pointer phases and mismatch positions") {
  constexpr std::size_t kGuardPixels = 8U;
  constexpr std::array<std::uint16_t, 4> kColors{0x0000U, 0xFFFFU, 0x39E7U, 0xA55AU};
  constexpr std::uint16_t kGuard = 0xDEADU;
  std::vector<std::uint16_t> storage(kGuardPixels + 7U + vector_v2::kTilePixels + kGuardPixels,
                                     kGuard);

  for (const std::uint16_t color : kColors) {
    for (std::size_t phase = 0; phase < 8U; ++phase) {
      std::uint16_t* const pixels = storage.data() + kGuardPixels + phase;
      std::fill_n(pixels, vector_v2::kTilePixels, color);
      const std::span<const std::uint16_t> tile(pixels, vector_v2::kTilePixels);
      CHECK(vector_v2::tile_uniform_color(tile, vector_v2::kTileWidth, vector_v2::kTileHeight,
                                          vector_v2::kTileWidth) == color);

      for (std::size_t mismatch = 0; mismatch < vector_v2::kTilePixels; ++mismatch) {
        pixels[mismatch] = static_cast<std::uint16_t>(color ^ 0x0001U);
        CHECK_FALSE(vector_v2::tile_uniform_color(tile, vector_v2::kTileWidth,
                                                  vector_v2::kTileHeight, vector_v2::kTileWidth));
        pixels[mismatch] = color;
      }
      CHECK(std::all_of(storage.begin(),
                        storage.begin() + static_cast<std::ptrdiff_t>(kGuardPixels),
                        [](std::uint16_t pixel) { return pixel == kGuard; }));
      const auto suffix = storage.begin() + static_cast<std::ptrdiff_t>(kGuardPixels + phase +
                                                                        vector_v2::kTilePixels);
      CHECK(
          std::all_of(suffix, storage.end(), [](std::uint16_t pixel) { return pixel == kGuard; }));
      std::fill(storage.begin(), storage.end(), kGuard);
    }
  }
}

TEST_CASE("uniform tile classification exhausts row alignment prefixes and tails") {
  constexpr std::size_t kGuardPixels = 8U;
  constexpr std::array<int, 4> kHeights{1, 2, 7, vector_v2::kTileHeight};
  constexpr std::array<std::uint16_t, 2> kColors{0x0000U, 0x7BEFU};
  constexpr std::uint16_t kPadding = 0x1357U;

  for (const std::uint16_t color : kColors) {
    for (int width = 1; width <= vector_v2::kTileWidth; ++width) {
      for (const int height : kHeights) {
        for (std::size_t padding = 0; padding < 8U; ++padding) {
          const std::size_t stride = static_cast<std::size_t>(width) + padding;
          const std::size_t extent =
              static_cast<std::size_t>(height - 1) * stride + static_cast<std::size_t>(width);
          std::vector<std::uint16_t> storage(kGuardPixels + 7U + extent + kGuardPixels, kPadding);
          for (std::size_t phase = 0; phase < 8U; ++phase) {
            CAPTURE(color);
            CAPTURE(width);
            CAPTURE(height);
            CAPTURE(padding);
            CAPTURE(phase);
            std::fill(storage.begin(), storage.end(), kPadding);
            std::uint16_t* const pixels = storage.data() + kGuardPixels + phase;
            for (int row = 0; row < height; ++row) {
              std::fill_n(
                  pixels + static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(stride),
                  width, color);
            }
            const std::span<const std::uint16_t> window(pixels, extent);
            CHECK(vector_v2::tile_uniform_color(window, width, height, stride) == color);
            if (width * height > 1) {
              pixels[static_cast<std::ptrdiff_t>(height - 1) * static_cast<std::ptrdiff_t>(stride) +
                     width - 1] = static_cast<std::uint16_t>(color ^ 0x8000U);
              CHECK_FALSE(vector_v2::tile_uniform_color(window, width, height, stride));
            }
          }
        }
      }
    }
  }
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
