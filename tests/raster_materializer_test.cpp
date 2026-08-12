#include "tinydraw/graphics/raster_materializer.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kBlack = 0x0000U;

}  // namespace

TEST_CASE("valid raster fallback preserves known pixels and fills unknown world white") {
  constexpr int kWidth = 4;
  constexpr int kHeight = 4;
  std::array<std::uint16_t, kWidth * kHeight> source{};
  for (int index = 0; index < kWidth * kHeight; ++index) {
    source[static_cast<std::size_t>(index)] = static_cast<std::uint16_t>(index + 1);
  }
  std::array<std::uint16_t, kWidth * kHeight> destination{};

  tinydraw::resample_valid_raster(source, kWidth, kHeight, {.x = 0.0, .y = 0.0, .zoom = 1.0F},
                                  destination, kWidth, kHeight,
                                  {.x = -1.0, .y = 0.0, .zoom = 1.0F});

  CHECK(destination[0] == kWhite);
  CHECK(destination[1] == source[0]);
  CHECK(destination[2] == source[1]);
  CHECK(destination[3] == source[2]);
}

TEST_CASE("regional fallback changes only the requested destination rectangle") {
  constexpr int kWidth = 4;
  constexpr int kHeight = 4;
  std::array<std::uint16_t, kWidth * kHeight> source{};
  for (int index = 0; index < kWidth * kHeight; ++index) {
    source[static_cast<std::size_t>(index)] = static_cast<std::uint16_t>(index + 1);
  }
  std::array<std::uint16_t, kWidth * kHeight> destination{};
  destination.fill(0xABCDU);

  tinydraw::resample_valid_raster_region(source, kWidth, kHeight, {.zoom = 1.0F}, destination,
                                         kWidth, kHeight, kWidth, {.zoom = 1.0F},
                                         {.x0 = 1, .y0 = 1, .x1 = 3, .y1 = 3});

  CHECK(destination[0] == 0xABCDU);
  CHECK(destination[5] == source[5]);
  CHECK(destination[6] == source[6]);
  CHECK(destination[15] == 0xABCDU);
}

TEST_CASE("bilinear zoom fallback blends RGB565 channels") {
  constexpr std::array<std::uint16_t, 4> source{kBlack, 0xF800U, 0x07E0U, kWhite};
  std::array<std::uint16_t, 16> destination{};

  tinydraw::resample_bilinear_rgb565_region(source, 2, 2, {.zoom = 1.0F}, destination, 4, 4, 4,
                                            {.zoom = 2.0F}, {.x0 = 0, .y0 = 0, .x1 = 4, .y1 = 4});

  CHECK(destination[0] != destination[1]);
  CHECK(destination[5] != kBlack);
  CHECK(destination[5] != kWhite);
}

TEST_CASE("two by two RGB565 box downsample averages channels") {
  constexpr std::array<std::uint16_t, 4> source{0xF800U, 0x07E0U, 0x001FU, kWhite};
  std::array<std::uint16_t, 1> destination{};

  tinydraw::downsample_rgb565_2x(source, 2, 2, destination, 1);

  // Rounded average: R=16, G=32, B=16.
  CHECK(destination[0] == static_cast<std::uint16_t>((16U << 11U) | (32U << 5U) | 16U));
}

TEST_CASE("nearest two-times upsample duplicates source pixels") {
  constexpr std::array<std::uint16_t, 2> source{0x1234U, 0xABCDU};
  std::array<std::uint16_t, 8> destination{};

  tinydraw::upsample_rgb565_2x(source, 2, 1, 2, destination, 4);

  constexpr std::array<std::uint16_t, 8> expected{
      0x1234U, 0x1234U, 0xABCDU, 0xABCDU, 0x1234U, 0x1234U, 0xABCDU, 0xABCDU,
  };
  CHECK(destination == expected);
}

TEST_CASE("odd-sized downsample uses only available edge samples") {
  constexpr std::array<std::uint16_t, 9> source{
      kBlack, kBlack, 0xF800U, kBlack, kBlack, 0xF800U, 0x07E0U, 0x07E0U, kWhite,
  };
  std::array<std::uint16_t, 4> destination{};

  tinydraw::downsample_rgb565_2x(source, 3, 3, destination, 2);

  CHECK(destination[0] == kBlack);
  CHECK(destination[1] == 0xF800U);
  CHECK(destination[2] == 0x07E0U);
  CHECK(destination[3] == kWhite);
}
