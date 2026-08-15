#ifndef TINYDRAW_VECTOR_V2_PANEL_STAGING_H
#define TINYDRAW_VECTOR_V2_PANEL_STAGING_H

#include <cstdint>
#include <cstring>

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

// Stages one run of an even number of pixels into wire byte order. When the
// source pair is 4-byte aligned, each pair moves with a single 32-bit load
// (the PSRAM read transactions dominate staging cost); memcpy of four bytes
// compiles to that same load without aliasing the uint16 buffer as uint32.
inline void stage_pixels_swapped(const std::uint16_t* source, std::uint16_t* destination,
                                 int width) {
  const auto load_word = [](const std::uint16_t* from) {
    std::uint32_t word;
    std::memcpy(&word, from, sizeof(word));
    return word;
  };
  const auto store_word = [](std::uint16_t* to, std::uint32_t word) {
    std::memcpy(to, &word, sizeof(word));
  };
  if ((reinterpret_cast<std::uintptr_t>(source) & 3U) == 0U) {
    const int pairs = width / 2;
    int pair = 0;
    for (; pair + 4 <= pairs; pair += 4) {
      const std::uint32_t first = load_word(source + 2 * pair);
      const std::uint32_t second = load_word(source + 2 * (pair + 1));
      const std::uint32_t third = load_word(source + 2 * (pair + 2));
      const std::uint32_t fourth = load_word(source + 2 * (pair + 3));
      store_word(destination + 2 * pair, swap_pixel_word(first));
      store_word(destination + 2 * (pair + 1), swap_pixel_word(second));
      store_word(destination + 2 * (pair + 2), swap_pixel_word(third));
      store_word(destination + 2 * (pair + 3), swap_pixel_word(fourth));
    }
    for (; pair < pairs; ++pair) {
      store_word(destination + 2 * pair, swap_pixel_word(load_word(source + 2 * pair)));
    }
    return;
  }
  for (int column = 0; column < width; column += 2) {
    store_word(destination + column, swap_pixel_pair(source[column], source[column + 1]));
  }
}

// Stages one ring-addressed panel row: panel column x reads buffer column
// (x + shift_x) % area_width of source_row. The even-run fast path stages
// contiguous chunks; a pair that straddles the ring wrap moves explicitly.
// Preconditions (validated by the transport): 0 <= shift_x < area_width,
// x + width <= area_width, width even.
inline void stage_ring_row(const std::uint16_t* source_row, int area_width, int shift_x, int x,
                           int width, std::uint16_t* destination) {
  const auto store_word = [](std::uint16_t* to, std::uint32_t word) {
    std::memcpy(to, &word, sizeof(word));
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
      store_word(destination + written, swap_pixel_pair(source_row[area_width - 1], source_row[0]));
      written += 2;
      source_column = 1;
    }
  }
}

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_PANEL_STAGING_H
