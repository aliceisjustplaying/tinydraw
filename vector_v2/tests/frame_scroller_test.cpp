#include "tinydraw/vector_v2/frame_scroller.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace vector_v2 = tinydraw::vector_v2;

namespace {

constexpr int kWidth = 8;
constexpr int kHeight = 6;

std::array<std::uint16_t, kWidth * kHeight> numbered_frame() {
  std::array<std::uint16_t, kWidth * kHeight> frame{};
  for (std::size_t index = 0; index < frame.size(); ++index) {
    frame[index] = static_cast<std::uint16_t>(index);
  }
  return frame;
}

}  // namespace

TEST_CASE("frame scroll moves overlap with memmove semantics") {
  auto frame = numbered_frame();
  const auto before = frame;
  const vector_v2::PixelRect area{1, 1, 7, 5};

  const auto result = vector_v2::scroll_frame(frame, kWidth, area, 2, 1);
  REQUIRE(result.has_value());
  REQUIRE(result->exposed_count == 2U);
  CHECK(result->exposed[0] == vector_v2::PixelRect{5, 1, 7, 5});
  CHECK(result->exposed[1] == vector_v2::PixelRect{1, 4, 5, 5});
  for (int y = 1; y < 4; ++y) {
    for (int x = 1; x < 5; ++x) {
      CHECK(frame[static_cast<std::size_t>(y * kWidth + x)] ==
            before[static_cast<std::size_t>((y + 1) * kWidth + x + 2)]);
    }
  }
  CHECK(frame.front() == before.front());
}

TEST_CASE("frame scroll handles reverse motion") {
  auto frame = numbered_frame();
  const auto before = frame;
  const vector_v2::PixelRect area{0, 0, kWidth, kHeight};

  const auto result = vector_v2::scroll_frame(frame, kWidth, area, -1, -2);
  REQUIRE(result.has_value());
  REQUIRE(result->exposed_count == 2U);
  CHECK(result->exposed[0] == vector_v2::PixelRect{0, 0, 1, kHeight});
  CHECK(result->exposed[1] == vector_v2::PixelRect{1, 0, kWidth, 2});
  for (int y = 2; y < kHeight; ++y) {
    for (int x = 1; x < kWidth; ++x) {
      CHECK(frame[static_cast<std::size_t>(y * kWidth + x)] ==
            before[static_cast<std::size_t>((y - 2) * kWidth + x - 1)]);
    }
  }
}

TEST_CASE("frame scroll returns one exposed rectangle for single-axis motion") {
  auto frame = numbered_frame();
  const auto before = frame;
  const auto result = vector_v2::scroll_frame(frame, kWidth, {0, 0, kWidth, kHeight}, 2, 0);
  REQUIRE(result.has_value());
  REQUIRE(result->exposed_count == 1U);
  CHECK(result->exposed[0] == vector_v2::PixelRect{kWidth - 2, 0, kWidth, kHeight});
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth - 2; ++x) {
      CHECK(frame[static_cast<std::size_t>(y * kWidth + x)] ==
            before[static_cast<std::size_t>(y * kWidth + x + 2)]);
    }
  }
}

TEST_CASE("frame scroll rejects invalid geometry without mutation") {
  auto frame = numbered_frame();
  const auto before = frame;
  CHECK_FALSE(vector_v2::scroll_frame(frame, kWidth, {0, 0, kWidth, kHeight}, kWidth, 0));
  CHECK_FALSE(vector_v2::scroll_frame(frame, kWidth - 1, {0, 0, kWidth, kHeight}, 1, 0));
  CHECK(frame == before);
}

TEST_CASE("ring scroll stays equivalent to physical scrolling across a drag") {
  // The ring never moves pixels; consumers de-rotate through ring_row and
  // ring_column. Drive identical pan sequences through scroll_frame (physical
  // memmove) and through the ring, writing the same sentinel values into each
  // exposed partition, and require every panel pixel to agree at every step.
  constexpr int kW = 16;
  constexpr int kH = 12;
  const vector_v2::PixelRect area{0, 0, kW, kH};
  std::vector<std::uint16_t> physical(static_cast<std::size_t>(kW) * kH);
  std::vector<std::uint16_t> ring_pixels(physical.size());
  for (std::size_t index = 0; index < physical.size(); ++index) {
    physical[index] = static_cast<std::uint16_t>(index);
    ring_pixels[index] = static_cast<std::uint16_t>(index);
  }
  vector_v2::RingFrame ring{};
  std::uint16_t sentinel = 1'000;
  constexpr std::array<std::array<int, 2>, 6> kDeltas{
      {{3, 2}, {-5, 4}, {0, -7}, {15, 11}, {-15, -11}, {6, 0}}};
  for (const auto delta : kDeltas) {
    const auto physical_result = vector_v2::scroll_frame(physical, kW, area, delta[0], delta[1]);
    const auto ring_result = vector_v2::ring_scroll(ring, area, delta[0], delta[1]);
    REQUIRE(physical_result.has_value());
    REQUIRE(ring_result.has_value());
    REQUIRE(physical_result->exposed_count == ring_result->exposed_count);
    for (std::size_t index = 0; index < ring_result->exposed_count; ++index) {
      CHECK(physical_result->exposed[index] == ring_result->exposed[index]);
      const auto exposed = ring_result->exposed[index];
      for (int y = exposed.y0; y < exposed.y1; ++y) {
        for (int x = exposed.x0; x < exposed.x1; ++x) {
          ++sentinel;
          physical[static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)] = sentinel;
          ring_pixels[static_cast<std::size_t>(vector_v2::ring_row(ring, area, y)) * kW +
                      static_cast<std::size_t>(vector_v2::ring_column(ring, area, x))] = sentinel;
        }
      }
    }
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        CHECK(physical[static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)] ==
              ring_pixels[static_cast<std::size_t>(vector_v2::ring_row(ring, area, y)) * kW +
                          static_cast<std::size_t>(vector_v2::ring_column(ring, area, x))]);
      }
    }
  }
  CHECK(ring.active());
  CHECK_FALSE(vector_v2::ring_scroll(ring, area, kW, 0).has_value());
}
