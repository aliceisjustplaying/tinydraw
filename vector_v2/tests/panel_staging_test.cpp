#include "tinydraw/vector_v2/panel_staging.h"

#include <doctest.h>

#include <cstdint>
#include <vector>

namespace {

using tinydraw::vector_v2::copy_ring_row;
using tinydraw::vector_v2::stage_pixels_swapped;
using tinydraw::vector_v2::stage_ring_row;
using tinydraw::vector_v2::swap_pixels_in_place;

std::uint16_t byte_swapped(std::uint16_t pixel) {
  return static_cast<std::uint16_t>(((pixel >> 8U) & 0xFFU) | ((pixel & 0xFFU) << 8U));
}

std::vector<std::uint16_t> pattern(std::size_t count, std::uint32_t seed) {
  std::vector<std::uint16_t> pixels(count);
  std::uint32_t state = seed;
  for (auto& pixel : pixels) {
    state = state * 1'664'525U + 1'013'904'223U;
    pixel = static_cast<std::uint16_t>(state >> 16U);
  }
  return pixels;
}

TEST_CASE("staged runs equal the naive byte-swap model at aligned and odd sources") {
  for (const int width : {2, 6, 8, 44, 368}) {
    // One leading pixel of padding lets the same buffer provide a 2-byte
    // misaligned source view.
    const auto source =
        pattern(static_cast<std::size_t>(width) + 1U, 0x1234U + static_cast<std::uint32_t>(width));
    for (const int offset : {0, 1}) {
      std::vector<std::uint16_t> staged(static_cast<std::size_t>(width), 0U);
      stage_pixels_swapped(source.data() + offset, staged.data(), width);
      for (int column = 0; column < width; ++column) {
        CAPTURE(width);
        CAPTURE(offset);
        CAPTURE(column);
        CHECK(staged[static_cast<std::size_t>(column)] ==
              byte_swapped(source[static_cast<std::size_t>(column + offset)]));
      }
    }
  }
}

TEST_CASE("host-order ring copy can be patched before one in-place byte swap") {
  constexpr int kAreaWidth = 368;
  const auto source_row = pattern(kAreaWidth, 0xCAFEU);
  for (const int shift_x : {0, 3, 17, 367}) {
    std::vector<std::uint16_t> staged(kAreaWidth, 0U);
    copy_ring_row(source_row.data(), kAreaWidth, shift_x, 0, kAreaWidth, staged.data());
    staged[0] = 0x1234U;
    staged[183] = 0xABCDU;
    staged[367] = 0x55AAU;
    swap_pixels_in_place(staged);

    CHECK(staged.front() == byte_swapped(0x1234U));
    CHECK(staged[183] == byte_swapped(0xABCDU));
    CHECK(staged.back() == byte_swapped(0x55AAU));
    for (int column = 1; column < kAreaWidth - 1; ++column) {
      if (column == 183) {
        continue;
      }
      CAPTURE(shift_x);
      CAPTURE(column);
      CHECK(staged[static_cast<std::size_t>(column)] ==
            byte_swapped(source_row[static_cast<std::size_t>((column + shift_x) % kAreaWidth)]));
    }
  }
}

TEST_CASE("ring row staging equals the naive wrap model including edge columns") {
  constexpr int kAreaWidth = 368;
  const auto source_row = pattern(kAreaWidth, 0xBEEFU);
  const int shifts[] = {0, 2, 3, 17, 366, 367};
  struct Window {
    int x;
    int width;
  };
  // Windows chosen to cover no-wrap, wrap mid-run, wrap at the pair special
  // case, and the full row.
  const Window windows[] = {{0, kAreaWidth}, {0, 2},   {0, 44},  {320, 48},
                            {360, 8},        {180, 6}, {364, 4}, {2, 364}};
  for (const int shift_x : shifts) {
    for (const auto& window : windows) {
      std::vector<std::uint16_t> staged(static_cast<std::size_t>(window.width), 0U);
      stage_ring_row(source_row.data(), kAreaWidth, shift_x, window.x, window.width, staged.data());
      for (int column = 0; column < window.width; ++column) {
        const int source_column = (window.x + column + shift_x) % kAreaWidth;
        CAPTURE(shift_x);
        CAPTURE(window.x);
        CAPTURE(window.width);
        CAPTURE(column);
        CHECK(staged[static_cast<std::size_t>(column)] ==
              byte_swapped(source_row[static_cast<std::size_t>(source_column)]));
      }
      // Edge columns explicitly: first and last staged pixels.
      CHECK(staged.front() ==
            byte_swapped(source_row[static_cast<std::size_t>((window.x + shift_x) % kAreaWidth)]));
      CHECK(staged.back() == byte_swapped(source_row[static_cast<std::size_t>(
                                 (window.x + window.width - 1 + shift_x) % kAreaWidth)]));
    }
  }
}

TEST_CASE("44-row strip staging equals single-pass staging across seams") {
  constexpr int kWidth = 368;
  constexpr int kHeight = 372;
  constexpr int kRowsPerStrip = 44;
  const auto frame = pattern(static_cast<std::size_t>(kWidth) * kHeight, 0xF00DU);
  std::vector<std::uint16_t> single(frame.size(), 0U);
  for (int row = 0; row < kHeight; ++row) {
    stage_pixels_swapped(frame.data() + static_cast<std::ptrdiff_t>(row) * kWidth,
                         single.data() + static_cast<std::ptrdiff_t>(row) * kWidth, kWidth);
  }
  std::vector<std::uint16_t> stripped(frame.size(), 0U);
  for (int strip = 0; strip < kHeight; strip += kRowsPerStrip) {
    const int rows = std::min(kRowsPerStrip, kHeight - strip);
    for (int row = 0; row < rows; ++row) {
      const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(strip + row) * kWidth;
      stage_pixels_swapped(frame.data() + offset, stripped.data() + offset, kWidth);
    }
  }
  CHECK(single == stripped);
  // Strip-seam rows explicitly, every column including both edges.
  for (const int row : {43, 44, 87, 88}) {
    for (int column = 0; column < kWidth; ++column) {
      const std::size_t index =
          static_cast<std::size_t>(row) * kWidth + static_cast<std::size_t>(column);
      CAPTURE(row);
      CAPTURE(column);
      CHECK(stripped[index] == byte_swapped(frame[index]));
    }
  }
}

}  // namespace
