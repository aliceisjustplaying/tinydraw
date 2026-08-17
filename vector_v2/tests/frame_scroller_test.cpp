#include "tinydraw/vector_v2/frame_scroller.h"

#include <doctest.h>

#include <array>

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
