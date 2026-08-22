#include "tinydraw/vector_v2/frame_scroller.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <limits>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("ring scroll advances its origin and reports exposed partitions") {
  vector_v2::RingFrame ring{};
  const vector_v2::PixelRect area{0, 0, 16, 12};
  const auto result = vector_v2::ring_scroll(ring, area, 3, 2);
  REQUIRE(result.has_value());
  REQUIRE(result->exposed_count == 2U);
  CHECK(result->exposed[0] == vector_v2::PixelRect{13, 0, 16, 12});
  CHECK(result->exposed[1] == vector_v2::PixelRect{0, 10, 13, 12});
  CHECK(ring.shift_x == 3);
  CHECK(ring.shift_y == 2);
}

TEST_CASE("ring addressing remains stable across a drag") {
  constexpr int kW = 16;
  constexpr int kH = 12;
  const vector_v2::PixelRect area{0, 0, kW, kH};
  vector_v2::RingFrame ring{};
  int expected_x = 0;
  int expected_y = 0;
  constexpr std::array<std::array<int, 2>, 6> kDeltas{
      {{3, 2}, {-5, 4}, {0, -7}, {15, 11}, {-15, -11}, {6, 0}}};
  for (const auto delta : kDeltas) {
    const auto ring_result = vector_v2::ring_scroll(ring, area, delta[0], delta[1]);
    REQUIRE(ring_result.has_value());
    expected_x = ((expected_x + delta[0]) % kW + kW) % kW;
    expected_y = ((expected_y + delta[1]) % kH + kH) % kH;
    CHECK(ring.shift_x == expected_x);
    CHECK(ring.shift_y == expected_y);
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        CHECK(vector_v2::ring_row(ring, area, y) == (y + expected_y) % kH);
        CHECK(vector_v2::ring_column(ring, area, x) == (x + expected_x) % kW);
      }
    }
  }
  CHECK(ring.active());
  CHECK_FALSE(vector_v2::ring_scroll(ring, area, kW, 0).has_value());
}

TEST_CASE("ring scroll matches modulo for every bounded shift and delta") {
  constexpr int kMaximumExtent = 64;
  for (int extent = 1; extent <= kMaximumExtent; ++extent) {
    const vector_v2::PixelRect horizontal_area{19, -23, 19 + extent, -22};
    const vector_v2::PixelRect vertical_area{19, -23, 20, -23 + extent};
    for (int shift = 0; shift < extent; ++shift) {
      for (int delta = 1 - extent; delta < extent; ++delta) {
        const auto expected =
            static_cast<int>((static_cast<std::int64_t>(shift) + delta + extent) % extent);

        vector_v2::RingFrame horizontal{shift, 0};
        const auto horizontal_result =
            vector_v2::ring_scroll(horizontal, horizontal_area, delta, 0);
        REQUIRE(horizontal_result.has_value());
        CHECK(horizontal.shift_x == expected);
        CHECK(horizontal.shift_y == 0);
        CHECK(horizontal_result->exposed_count == (delta == 0 ? 0U : 1U));

        vector_v2::RingFrame vertical{0, shift};
        const auto vertical_result = vector_v2::ring_scroll(vertical, vertical_area, 0, delta);
        REQUIRE(vertical_result.has_value());
        CHECK(vertical.shift_x == 0);
        CHECK(vertical.shift_y == expected);
        CHECK(vertical_result->exposed_count == (delta == 0 ? 0U : 1U));
      }
    }
  }
}

TEST_CASE("ring scroll handles the extreme supported int deltas") {
  constexpr int kExtent = std::numeric_limits<int>::max();
  constexpr vector_v2::PixelRect kArea{0, 0, kExtent, kExtent};
  constexpr std::array<int, 5> kShiftsAndDeltas{0, 1, kExtent / 2, kExtent - 2, kExtent - 1};

  for (const int shift : kShiftsAndDeltas) {
    for (const int magnitude : kShiftsAndDeltas) {
      for (const int delta : {magnitude, -magnitude}) {
        vector_v2::RingFrame ring{shift, shift};
        const auto result = vector_v2::ring_scroll(ring, kArea, delta, delta);
        REQUIRE(result.has_value());
        const auto expected =
            static_cast<int>((static_cast<std::int64_t>(shift) + delta + kExtent) % kExtent);
        CHECK(ring.shift_x == expected);
        CHECK(ring.shift_y == expected);
      }
    }
  }

  for (const int invalid_delta : {std::numeric_limits<int>::min(), kExtent}) {
    vector_v2::RingFrame ring{17, 29};
    CHECK_FALSE(vector_v2::ring_scroll(ring, kArea, invalid_delta, 0).has_value());
    CHECK(ring.shift_x == 17);
    CHECK(ring.shift_y == 29);
  }
}
