#include "tinydraw/vector_v2/panel_staging.h"

#include <doctest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/frame_scroller.h"

namespace {

using tinydraw::vector_v2::copy_ring_row;
using tinydraw::vector_v2::copy_to_ring_row;
using tinydraw::vector_v2::PixelRect;
using tinydraw::vector_v2::RingFrame;
using tinydraw::vector_v2::stage_full_ring_rows_swapped;
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

constexpr int kOracleWidth = 12;
constexpr int kOracleHeight = 10;
constexpr PixelRect kOracleArea{0, 0, kOracleWidth, kOracleHeight};

std::size_t oracle_index(int x, int y) {
  return static_cast<std::size_t>(y) * kOracleWidth + static_cast<std::size_t>(x);
}

std::uint16_t oracle_pixel(std::uint16_t generation, int x, int y) {
  return static_cast<std::uint16_t>(generation | static_cast<std::uint16_t>(y << 4U) |
                                    static_cast<std::uint16_t>(x));
}

void paint_rect(std::span<std::uint16_t> pixels, PixelRect bounds, std::uint16_t generation) {
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    for (int x = bounds.x0; x < bounds.x1; ++x) {
      pixels[oracle_index(x, y)] = oracle_pixel(generation, x, y);
    }
  }
}

void copy_rect_to_ring(std::span<const std::uint16_t> source, std::span<std::uint16_t> ring_pixels,
                       const RingFrame& ring, PixelRect bounds) {
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const int ring_y = tinydraw::vector_v2::ring_row(ring, kOracleArea, y);
    copy_to_ring_row(source.data() + oracle_index(bounds.x0, y), bounds.x1 - bounds.x0,
                     ring_pixels.data() + oracle_index(0, ring_y), kOracleWidth, ring.shift_x,
                     bounds.x0);
  }
}

std::vector<std::uint16_t> shifted_oracle(std::span<const std::uint16_t> before, int delta_x,
                                          int delta_y) {
  std::vector<std::uint16_t> after(before.size());
  for (int y = 0; y < kOracleHeight; ++y) {
    for (int x = 0; x < kOracleWidth; ++x) {
      after[oracle_index(x, y)] =
          before[oracle_index((x + delta_x) % kOracleWidth, (y + delta_y) % kOracleHeight)];
    }
  }
  return after;
}

void paint_staged_rect(std::span<std::uint16_t> staged, PixelRect bounds, std::uint16_t color,
                       bool byte_swapped_surface) {
  const std::uint16_t staged_color = byte_swapped_surface ? byte_swapped(color) : color;
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    for (int x = bounds.x0; x < bounds.x1; ++x) {
      staged[oracle_index(x, y)] = staged_color;
    }
  }
}

std::vector<std::uint16_t> stage_oracle_frame(std::span<const std::uint16_t> ring_pixels,
                                              const RingFrame& ring, bool byte_swapped_surface,
                                              PixelRect chrome, std::uint16_t chrome_color,
                                              PixelRect provisional = {},
                                              std::uint16_t provisional_color = 0U) {
  std::vector<std::uint16_t> staged(ring_pixels.size());
  for (int y = 0; y < kOracleHeight; ++y) {
    const int ring_y = tinydraw::vector_v2::ring_row(ring, kOracleArea, y);
    const auto* source = ring_pixels.data() + oracle_index(0, ring_y);
    auto* destination = staged.data() + oracle_index(0, y);
    if (byte_swapped_surface) {
      stage_ring_row(source, kOracleWidth, ring.shift_x, 0, kOracleWidth, destination);
    } else {
      copy_ring_row(source, kOracleWidth, ring.shift_x, 0, kOracleWidth, destination);
    }
  }
  paint_staged_rect(staged, chrome, chrome_color, byte_swapped_surface);
  if (provisional.x0 < provisional.x1 && provisional.y0 < provisional.y1) {
    paint_staged_rect(staged, provisional, provisional_color, byte_swapped_surface);
  }
  if (!byte_swapped_surface) {
    swap_pixels_in_place(staged);
  }
  return staged;
}

std::vector<std::uint16_t> wire_oracle(std::span<const std::uint16_t> canvas, PixelRect chrome,
                                       std::uint16_t chrome_color, PixelRect provisional = {},
                                       std::uint16_t provisional_color = 0U) {
  std::vector<std::uint16_t> expected(canvas.begin(), canvas.end());
  paint_staged_rect(expected, chrome, chrome_color, false);
  if (provisional.x0 < provisional.x1 && provisional.y0 < provisional.y1) {
    paint_staged_rect(expected, provisional, provisional_color, false);
  }
  swap_pixels_in_place(expected);
  return expected;
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

TEST_CASE("staged runs preserve byte order across every 16-byte pointer phase") {
  constexpr int kWidth = 368;
  const auto source = pattern(kWidth + 8, 0x51A6U);
  for (int source_offset = 0; source_offset < 8; ++source_offset) {
    for (int destination_offset = 0; destination_offset < 8; ++destination_offset) {
      std::vector<std::uint16_t> storage(kWidth + 16, 0xDEADU);
      auto* destination = storage.data() + destination_offset;
      for (const int width : {2, 6, 16, 18, 32, 34, kWidth}) {
        std::fill(storage.begin(), storage.end(), 0xDEADU);
        stage_pixels_swapped(source.data() + source_offset, destination, width);
        for (int column = 0; column < width; ++column) {
          CAPTURE(source_offset);
          CAPTURE(destination_offset);
          CAPTURE(width);
          CAPTURE(column);
          CHECK(destination[column] ==
                byte_swapped(source[static_cast<std::size_t>(source_offset + column)]));
        }
        CHECK(storage[static_cast<std::size_t>(destination_offset + width)] == 0xDEADU);
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

TEST_CASE("logical local patches round-trip through a wrapped ring row") {
  constexpr int kAreaWidth = 368;
  auto ring_row = pattern(kAreaWidth, 0xA11CEU);
  const auto original = ring_row;
  constexpr int kPatchX = 320;
  constexpr int kPatchWidth = 48;
  const auto patch = pattern(kPatchWidth, 0x10CA1U);

  for (const int shift_x : {0, 17, 367}) {
    ring_row = original;
    copy_to_ring_row(patch.data(), kPatchWidth, ring_row.data(), kAreaWidth, shift_x, kPatchX);

    auto expected = original;
    for (int column = 0; column < kPatchWidth; ++column) {
      expected[static_cast<std::size_t>((kPatchX + column + shift_x) % kAreaWidth)] =
          patch[static_cast<std::size_t>(column)];
    }
    CHECK(ring_row == expected);

    std::vector<std::uint16_t> copied(kPatchWidth, 0U);
    copy_ring_row(ring_row.data(), kAreaWidth, shift_x, kPatchX, kPatchWidth, copied.data());
    CHECK(copied == patch);

    std::vector<std::uint16_t> staged(kPatchWidth, 0U);
    stage_ring_row(ring_row.data(), kAreaWidth, shift_x, kPatchX, kPatchWidth, staged.data());
    for (int column = 0; column < kPatchWidth; ++column) {
      CAPTURE(shift_x);
      CAPTURE(column);
      CHECK(staged[static_cast<std::size_t>(column)] ==
            byte_swapped(patch[static_cast<std::size_t>(column)]));
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

TEST_CASE("full ring row staging covers horizontal and vertical wrap") {
  constexpr int kWidth = 368;
  constexpr int kHeight = 7;
  constexpr int kRows = 5;
  constexpr int kDestinationStride = kWidth + 4;
  const auto source = pattern(static_cast<std::size_t>(kWidth) * kHeight, 0x2D5EU);

  for (const int first_row : {0, 4, 6}) {
    for (const int shift_x : {0, 1, 3, 8, 24, 72, 360, 367}) {
      std::vector<std::uint16_t> destination(static_cast<std::size_t>(kDestinationStride) * kRows,
                                             0xDEADU);
      REQUIRE(stage_full_ring_rows_swapped(source.data(), kWidth, first_row, kRows, kHeight,
                                           shift_x, destination.data(), kDestinationStride));
      for (int row = 0; row < kRows; ++row) {
        const int source_row = (first_row + row) % kHeight;
        for (int column = 0; column < kWidth; ++column) {
          CAPTURE(first_row);
          CAPTURE(shift_x);
          CAPTURE(row);
          CAPTURE(column);
          const std::size_t source_index = static_cast<std::size_t>(source_row) * kWidth +
                                           static_cast<std::size_t>((column + shift_x) % kWidth);
          const std::size_t destination_index =
              static_cast<std::size_t>(row) * kDestinationStride + static_cast<std::size_t>(column);
          CHECK(destination[destination_index] == byte_swapped(source[source_index]));
        }
        for (int column = kWidth; column < kDestinationStride; ++column) {
          CHECK(destination[static_cast<std::size_t>(row) * kDestinationStride +
                            static_cast<std::size_t>(column)] == 0xDEADU);
        }
      }
    }
  }
}

TEST_CASE("full ring row staging rejects invalid geometry") {
  std::array<std::uint16_t, 32> source{};
  std::array<std::uint16_t, 32> destination{};
  CHECK_FALSE(stage_full_ring_rows_swapped(source.data(), 8, -1, 1, 4, 0, destination.data(), 8));
  CHECK_FALSE(stage_full_ring_rows_swapped(source.data(), 8, 0, 5, 4, 0, destination.data(), 8));
  CHECK_FALSE(stage_full_ring_rows_swapped(source.data(), 8, 0, 1, 4, 8, destination.data(), 8));
  CHECK_FALSE(stage_full_ring_rows_swapped(source.data(), 8, 0, 1, 4, 0, destination.data(), 7));
  CHECK(stage_full_ring_rows_swapped(source.data(), 8, 0, 0, 4, 0, destination.data(), 8));
}

TEST_CASE("ring-local presentation sequence matches the pixel oracle across x and y wraps") {
  constexpr PixelRect kLocalTile{0, 0, 4, 4};
  constexpr PixelRect kChrome{8, 0, 12, 2};
  constexpr PixelRect kProvisionalInk{2, 2, 6, 4};
  constexpr std::uint16_t kChromeColor = 0xE71CU;
  constexpr std::uint16_t kProvisionalColor = 0x001FU;
  constexpr std::uint16_t kCommittedColor = 0x07E0U;

  for (const bool byte_swapped_surface : {false, true}) {
    CAPTURE(byte_swapped_surface);
    std::vector<std::uint16_t> oracle(static_cast<std::size_t>(kOracleWidth) * kOracleHeight);
    paint_rect(oracle, kOracleArea, 0x1000U);
    std::vector<std::uint16_t> ring_pixels = oracle;
    RingFrame ring{};

    // The first pan places both axes two pixels before their physical wrap.
    // Its exposed strips are then composed directly into toroidal storage.
    constexpr int kFirstDeltaX = 10;
    constexpr int kFirstDeltaY = 8;
    const auto first_scroll =
        tinydraw::vector_v2::ring_scroll(ring, kOracleArea, kFirstDeltaX, kFirstDeltaY);
    REQUIRE(first_scroll.has_value());
    oracle = shifted_oracle(oracle, kFirstDeltaX, kFirstDeltaY);
    for (std::size_t index = 0; index < first_scroll->exposed_count; ++index) {
      paint_rect(oracle, first_scroll->exposed[index], 0x2000U);
      copy_rect_to_ring(oracle, ring_pixels, ring, first_scroll->exposed[index]);
    }
    CHECK(stage_oracle_frame(ring_pixels, ring, byte_swapped_surface, kChrome, kChromeColor) ==
          wire_oracle(oracle, kChrome, kChromeColor));

    // This local tile crosses both physical seams at shift (10, 8).
    paint_rect(oracle, kLocalTile, 0x3000U);
    copy_rect_to_ring(oracle, ring_pixels, ring, kLocalTile);
    CHECK(stage_oracle_frame(ring_pixels, ring, byte_swapped_surface, kChrome, kChromeColor) ==
          wire_oracle(oracle, kChrome, kChromeColor));

    // Chrome and provisional ink exist only in the transfer surface. The
    // committed pixels subsequently enter the ring at the same logical bounds.
    CHECK(stage_oracle_frame(ring_pixels, ring, byte_swapped_surface, kChrome, kChromeColor,
                             kProvisionalInk, kProvisionalColor) ==
          wire_oracle(oracle, kChrome, kChromeColor, kProvisionalInk, kProvisionalColor));
    paint_staged_rect(oracle, kProvisionalInk, kCommittedColor, false);
    copy_rect_to_ring(oracle, ring_pixels, ring, kProvisionalInk);
    CHECK(stage_oracle_frame(ring_pixels, ring, byte_swapped_surface, kChrome, kChromeColor) ==
          wire_oracle(oracle, kChrome, kChromeColor));

    // A second pan wraps both accumulated shifts and proves all local writes
    // remain correctly addressed in the next reusable frame.
    constexpr int kSecondDeltaX = 4;
    constexpr int kSecondDeltaY = 4;
    const auto second_scroll =
        tinydraw::vector_v2::ring_scroll(ring, kOracleArea, kSecondDeltaX, kSecondDeltaY);
    REQUIRE(second_scroll.has_value());
    oracle = shifted_oracle(oracle, kSecondDeltaX, kSecondDeltaY);
    for (std::size_t index = 0; index < second_scroll->exposed_count; ++index) {
      paint_rect(oracle, second_scroll->exposed[index], 0x4000U);
      copy_rect_to_ring(oracle, ring_pixels, ring, second_scroll->exposed[index]);
    }
    CHECK(ring.shift_x == 2);
    CHECK(ring.shift_y == 2);
    CHECK(stage_oracle_frame(ring_pixels, ring, byte_swapped_surface, kChrome, kChromeColor) ==
          wire_oracle(oracle, kChrome, kChromeColor));
  }
}

}  // namespace
