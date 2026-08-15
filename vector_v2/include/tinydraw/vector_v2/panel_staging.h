#ifndef TINYDRAW_VECTOR_V2_PANEL_STAGING_H
#define TINYDRAW_VECTOR_V2_PANEL_STAGING_H

#include <cstdint>
#include <cstring>
#include <span>

namespace tinydraw::vector_v2 {

// Byte-swap staging shared by the CO5300 transport and its host tests: the
// panel wire format is big-endian RGB565, host pixels are little-endian.
// All multi-byte access goes through std::memcpy so the staging math stays
// free of strict-aliasing undefined behavior on every toolchain.

constexpr std::uint32_t swap_pixel_pair(std::uint16_t first, std::uint16_t second) {
  const std::uint32_t pixels =
      static_cast<std::uint32_t>(first) | (static_cast<std::uint32_t>(second) << 16U);
  return ((pixels >> 8U) & 0x00FF00FFU) | ((pixels << 8U) & 0xFF00FF00U);
}

static_assert(swap_pixel_pair(0x1234U, 0xABCDU) == 0xCDAB3412U);

constexpr std::uint32_t swap_pixel_word(std::uint32_t pair) {
  return ((pair >> 8U) & 0x00FF00FFU) | ((pair << 8U) & 0xFF00FF00U);
}

static_assert(swap_pixel_word(0xABCD1234U) == swap_pixel_pair(0x1234U, 0xABCDU));

// Stages one run of an even number of pixels into wire byte order using
// plain uint16 element access: aliasing-legal on every toolchain, and the
// PSRAM cache line, not the load width, dominates staging cost (the 32-bit
// word-fusion variant measured only ~10% faster on hardware, and its
// memcpy-per-word aliasing-safe form measured 5x SLOWER on Xtensa because
// the 4-byte memcpy calls did not inline).
inline void stage_pixels_swapped(const std::uint16_t* source, std::uint16_t* destination,
                                 int width) {
  for (int column = 0; column < width; column += 2) {
    const std::uint16_t first = source[column];
    const std::uint16_t second = source[column + 1];
    destination[column] = static_cast<std::uint16_t>((first >> 8U) | (first << 8U));
    destination[column + 1] = static_cast<std::uint16_t>((second >> 8U) | (second << 8U));
  }
}

// Stages one ring-addressed panel row: panel column x reads buffer column
// (x + shift_x) % area_width of source_row. The even-run fast path stages
// contiguous chunks; a pair that straddles the ring wrap moves explicitly.
// Preconditions (validated by the transport): 0 <= shift_x < area_width,
// x + width <= area_width, width even.
inline void copy_ring_row(const std::uint16_t* source_row, int area_width, int shift_x, int x,
                          int width, std::uint16_t* destination) {
  int source_column = x + shift_x;
  if (source_column >= area_width) {
    source_column -= area_width;
  }
  int written = 0;
  while (written < width) {
    const int chunk =
        area_width - source_column < width - written ? area_width - source_column : width - written;
    for (int column = 0; column < chunk; ++column) {
      destination[written + column] = source_row[source_column + column];
    }
    written += chunk;
    source_column = 0;
  }
}

inline void swap_pixels_in_place(std::span<std::uint16_t> pixels) {
  for (std::uint16_t& pixel : pixels) {
    pixel = static_cast<std::uint16_t>((pixel >> 8U) | (pixel << 8U));
  }
}

inline void stage_ring_row(const std::uint16_t* source_row, int area_width, int shift_x, int x,
                           int width, std::uint16_t* destination) {
  const auto swap16 = [](std::uint16_t value) {
    return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
  };
  int source_column = x + shift_x;
  if (source_column >= area_width) {
    source_column -= area_width;
  }
  int written = 0;
  while (written < width) {
    const int chunk =
        area_width - source_column < width - written ? area_width - source_column : width - written;
    const int even_chunk = chunk & ~1;
    stage_pixels_swapped(source_row + source_column, destination + written, even_chunk);
    written += even_chunk;
    source_column += even_chunk;
    if (source_column >= area_width) {
      source_column = 0;
    }
    if (chunk != even_chunk && written < width) {
      // The remaining pair straddles the ring wrap.
      destination[written] = swap16(source_row[area_width - 1]);
      destination[written + 1] = swap16(source_row[0]);
      written += 2;
      source_column = 1;
    }
  }
}

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_PANEL_STAGING_H
