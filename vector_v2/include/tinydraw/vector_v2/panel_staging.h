#ifndef TINYDRAW_VECTOR_V2_PANEL_STAGING_H
#define TINYDRAW_VECTOR_V2_PANEL_STAGING_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/pixel_memory.h"

namespace tinydraw::vector_v2 {

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    !defined(TINYDRAW_PANEL_PROBE) && !defined(TINYDRAW_QEMU) &&   \
    !defined(TINYDRAW_DISABLE_PIE_STAGING)
extern "C" void tinydraw_stage_pixels_swapped_pie(const std::uint16_t* source,
                                                  std::uint16_t* destination, int blocks);
extern "C" void tinydraw_stage_pixels_swapped_unaligned_pie(const std::uint16_t* source,
                                                            std::uint16_t* destination, int blocks);
extern "C" void tinydraw_stage_full_ring_rows_swapped_pie(const std::uint16_t* source,
                                                          std::uint16_t* destination, int first_row,
                                                          int rows, int ring_height, int shift_x);
#endif

// Byte-swap staging shared by the CO5300 transport and its host tests: the
// panel wire format is big-endian RGB565, host pixels are little-endian.

// Stages one run of an even number of pixels into wire byte order using
// plain uint16 element access: aliasing-legal on every toolchain, and the
// PSRAM cache line, not the load width, dominates staging cost (the 32-bit
// word-fusion variant measured only ~10% faster on hardware, and its
// memcpy-per-word aliasing-safe form measured 5x SLOWER on Xtensa because
// the 4-byte memcpy calls did not inline).
inline void stage_pixels_swapped(const std::uint16_t* source, std::uint16_t* destination,
                                 int width) {
#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    !defined(TINYDRAW_PANEL_PROBE) && !defined(TINYDRAW_QEMU) &&   \
    !defined(TINYDRAW_DISABLE_PIE_STAGING)
  if (width <= 0) {
    return;
  }

  // PIE stores round down to 16 bytes, so scalarize only enough output to
  // align the destination. EE.LD.128.USAR.IP then preserves an arbitrary
  // source phase in SAR_BYTE while its paired SRC stream reconstructs each
  // unaligned vector.
  const int prefix = static_cast<int>(
      ((16U - (reinterpret_cast<std::uintptr_t>(destination) & 0x0FU)) & 0x0FU) / 2U);
  const int scalar_prefix = prefix < width ? prefix : width;
  for (int column = 0; column < scalar_prefix; ++column) {
    const std::uint16_t pixel = source[column];
    destination[column] = static_cast<std::uint16_t>((pixel >> 8U) | (pixel << 8U));
  }
  source += scalar_prefix;
  destination += scalar_prefix;
  width -= scalar_prefix;

  constexpr int kPixelsPerBlock = 16;
  if ((reinterpret_cast<std::uintptr_t>(source) & 0x0FU) == 0U) {
    const int blocks = width / kPixelsPerBlock;
    if (blocks > 0) {
      tinydraw_stage_pixels_swapped_pie(source, destination, blocks);
      const int vector_pixels = blocks * kPixelsPerBlock;
      source += vector_pixels;
      destination += vector_pixels;
      width -= vector_pixels;
    }
  } else {
    // The final unaligned output vector needs the following aligned source
    // vector for SRC. Keeping eight requested pixels scalar guarantees those
    // bytes are readable without assuming allocation padding.
    constexpr int kReadableTailPixels = 8;
    const int blocks =
        width > kReadableTailPixels ? (width - kReadableTailPixels) / kPixelsPerBlock : 0;
    if (blocks > 0) {
      tinydraw_stage_pixels_swapped_unaligned_pie(source, destination, blocks);
      const int vector_pixels = blocks * kPixelsPerBlock;
      source += vector_pixels;
      destination += vector_pixels;
      width -= vector_pixels;
    }
  }

  for (int column = 0; column < width; ++column) {
    const std::uint16_t pixel = source[column];
    destination[column] = static_cast<std::uint16_t>((pixel >> 8U) | (pixel << 8U));
  }
#else
  for (int column = 0; column < width; column += 2) {
    const std::uint16_t first = source[column];
    const std::uint16_t second = source[column + 1];
    destination[column] = static_cast<std::uint16_t>((first >> 8U) | (first << 8U));
    destination[column + 1] = static_cast<std::uint16_t>((second >> 8U) | (second << 8U));
  }
#endif
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

// Writes one logical panel run back into its ring-addressed backing row.
// This is the inverse addressing operation of copy_ring_row and keeps local
// canvas/ink damage in the toroidal frame without materializing the frame.
inline void copy_to_ring_row(const std::uint16_t* source, int width, std::uint16_t* destination_row,
                             int area_width, int shift_x, int x) {
  int destination_column = x + shift_x;
  if (destination_column >= area_width) {
    destination_column -= area_width;
  }
  int read = 0;
  while (read < width) {
    const int chunk = area_width - destination_column < width - read
                          ? area_width - destination_column
                          : width - read;
    copy_pixel_rows_disjoint(source + read, chunk, destination_row + destination_column, chunk,
                             chunk, 1);
    read += chunk;
    destination_column = 0;
  }
}

// Writes a source rectangle into a toroidal RGB565 frame in at most four 2D
// copies (the Cartesian product of its horizontal and vertical seam splits).
// Overlap is rejected before the first write so callers can retain any legacy
// in-place behavior in their scalar fallback.
[[nodiscard]] inline bool copy_pixel_rows_to_ring(const std::uint16_t* source, int source_stride,
                                                  int width, int height, std::uint16_t* destination,
                                                  int area_width, int area_height,
                                                  int first_destination_row, int shift_x, int x) {
  if (width < 0 || height < 0 || source_stride < width || area_width <= 0 || area_height <= 0 ||
      first_destination_row < 0 || first_destination_row >= area_height || shift_x < 0 ||
      shift_x >= area_width || x < 0 || x + width > area_width) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;
  }
  if (source == nullptr || destination == nullptr || height > area_height) {
    return false;
  }
  const std::size_t source_extent =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(source_stride) +
      static_cast<std::size_t>(width);
  const std::size_t destination_extent =
      static_cast<std::size_t>(area_width) * static_cast<std::size_t>(area_height);
  if (storage_overlaps(std::as_bytes(std::span(source, source_extent)),
                       std::as_bytes(std::span(destination, destination_extent)))) {
    return false;
  }

  int destination_x = x + shift_x;
  if (destination_x >= area_width) {
    destination_x -= area_width;
  }
  int destination_y = first_destination_row;
  int copied_rows = 0;
  while (copied_rows < height) {
    const int rows = std::min(height - copied_rows, area_height - destination_y);
    int row_destination_x = destination_x;
    int copied_columns = 0;
    while (copied_columns < width) {
      const int columns = std::min(width - copied_columns, area_width - row_destination_x);
      copy_pixel_rows_disjoint(
          source + static_cast<std::ptrdiff_t>(copied_rows) * source_stride + copied_columns,
          source_stride,
          destination + static_cast<std::ptrdiff_t>(destination_y) * area_width + row_destination_x,
          area_width, columns, rows);
      copied_columns += columns;
      row_destination_x = 0;
    }
    copied_rows += rows;
    destination_y = 0;
  }
  return true;
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

// Stages complete ring rows into consecutive destination rows. The ESP32-S3
// production path is specialized for the 368-pixel panel: every ring row and
// transfer row is 16-byte aligned. Eight-pixel-aligned shifts use the original
// paired-vector loop; other shifts use USAR/SRC for 352 pixels and scalarize
// exactly 16 pixels around the cyclic seam, so no allocation padding is read.
// Other geometries and pointer phases retain the scalar/reference row path.
inline bool stage_full_ring_rows_swapped(const std::uint16_t* source, int area_width, int first_row,
                                         int rows, int ring_height, int shift_x,
                                         std::uint16_t* destination, int destination_stride) {
  if (source == nullptr || destination == nullptr || area_width <= 0 || ring_height <= 0 ||
      first_row < 0 || first_row >= ring_height || rows < 0 || rows > ring_height || shift_x < 0 ||
      shift_x >= area_width || destination_stride < area_width || (area_width & 1) != 0) {
    return false;
  }
  if (rows == 0) {
    return true;
  }

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    !defined(TINYDRAW_PANEL_PROBE) && !defined(TINYDRAW_QEMU) &&   \
    !defined(TINYDRAW_DISABLE_PIE_STAGING)
  constexpr int kPiePanelWidth = 368;
  if (area_width == kPiePanelWidth && destination_stride == kPiePanelWidth &&
      ((reinterpret_cast<std::uintptr_t>(source) | reinterpret_cast<std::uintptr_t>(destination)) &
       0x0FU) == 0U) {
    tinydraw_stage_full_ring_rows_swapped_pie(source, destination, first_row, rows, ring_height,
                                              shift_x);
    return true;
  }
#endif

  int source_row = first_row;
  for (int row = 0; row < rows; ++row) {
    stage_ring_row(source + static_cast<std::ptrdiff_t>(source_row) * area_width, area_width,
                   shift_x, 0, area_width,
                   destination + static_cast<std::ptrdiff_t>(row) * destination_stride);
    if (++source_row == ring_height) {
      source_row = 0;
    }
  }
  return true;
}

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_PANEL_STAGING_H
