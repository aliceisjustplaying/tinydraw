#include "tinydraw/production/frame_scroller.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace production = tinydraw::production;

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
  const production::PixelRect area{1, 1, 7, 5};

  const auto result = production::scroll_frame(frame, kWidth, area, 2, 1);
  REQUIRE(result.has_value());
  REQUIRE(result->exposed_count == 2U);
  CHECK(result->exposed[0] == production::PixelRect{5, 1, 7, 5});
  CHECK(result->exposed[1] == production::PixelRect{1, 4, 5, 5});
  for (int y = 1; y < 4; ++y) {
    for (int x = 1; x < 5; ++x) {
      CHECK(frame[static_cast<std::size_t>(y * kWidth + x)] ==
            before[static_cast<std::size_t>((y + 1) * kWidth + x + 2)]);
    }
  }
  CHECK(frame.front() == before.front());
}

TEST_CASE("frame scroll handles reverse and single-axis motion") {
  auto frame = numbered_frame();
  const auto before = frame;
  const production::PixelRect area{0, 0, kWidth, kHeight};

  const auto result = production::scroll_frame(frame, kWidth, area, -1, -2);
  REQUIRE(result.has_value());
  REQUIRE(result->exposed_count == 2U);
  CHECK(result->exposed[0] == production::PixelRect{0, 0, 1, kHeight});
  CHECK(result->exposed[1] == production::PixelRect{1, 0, kWidth, 2});
  for (int y = 2; y < kHeight; ++y) {
    for (int x = 1; x < kWidth; ++x) {
      CHECK(frame[static_cast<std::size_t>(y * kWidth + x)] ==
            before[static_cast<std::size_t>((y - 2) * kWidth + x - 1)]);
    }
  }
}

TEST_CASE("frame scroll rejects invalid geometry without mutation") {
  auto frame = numbered_frame();
  const auto before = frame;
  CHECK_FALSE(production::scroll_frame(frame, kWidth, {0, 0, kWidth, kHeight}, kWidth, 0));
  CHECK_FALSE(production::scroll_frame(frame, kWidth - 1, {0, 0, kWidth, kHeight}, 1, 0));
  CHECK(frame == before);
}
