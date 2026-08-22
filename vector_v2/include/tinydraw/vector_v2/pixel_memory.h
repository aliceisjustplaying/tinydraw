#ifndef TINYDRAW_VECTOR_V2_PIXEL_MEMORY_H
#define TINYDRAW_VECTOR_V2_PIXEL_MEMORY_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_PIXEL_MEMORY)
extern "C" void tinydraw_copy_pixel_rows_pie(const std::uint16_t* source,
                                             std::uint16_t* destination, int source_stride,
                                             int destination_stride, int width, int height);
extern "C" void tinydraw_fill_pixel_rows_pie(std::uint16_t* destination,
                                             std::uint32_t replicated_color, int destination_stride,
                                             int width, int height);
#endif

// Unchecked hot-path primitive. Callers establish positive geometry, adequate
// strides, and disjoint storage. Every halfword pointer phase is accepted.
inline void copy_pixel_rows_disjoint(const std::uint16_t* source, int source_stride,
                                     std::uint16_t* destination, int destination_stride, int width,
                                     int height) {
#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_PIXEL_MEMORY)
  // The assembly also contains an exact scalar row body for differing pointer
  // phases; keeping all geometry in one call removes the per-row ROM memcpy.
  tinydraw_copy_pixel_rows_pie(source, destination, source_stride, destination_stride, width,
                               height);
#else
  for (int row = 0; row < height; ++row) {
    std::copy_n(source + static_cast<std::ptrdiff_t>(row) * source_stride, width,
                destination + static_cast<std::ptrdiff_t>(row) * destination_stride);
  }
#endif
}

// Checked entry point for external geometry and native-kernel oracles. It
// rejects overlap before writing so a caller can retain its original scalar
// overlap semantics.
[[nodiscard]] inline bool copy_pixel_rows_nonoverlapping(const std::uint16_t* source,
                                                         int source_stride,
                                                         std::uint16_t* destination,
                                                         int destination_stride, int width,
                                                         int height) {
  if (width < 0 || height < 0 || source_stride < width || destination_stride < width) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;
  }
  if (source == nullptr || destination == nullptr) {
    return false;
  }
  const std::size_t source_extent =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(source_stride) +
      static_cast<std::size_t>(width);
  const std::size_t destination_extent =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(destination_stride) +
      static_cast<std::size_t>(width);
  if (storage_overlaps(std::as_bytes(std::span(source, source_extent)),
                       std::as_bytes(std::span(destination, destination_extent)))) {
    return false;
  }
  copy_pixel_rows_disjoint(source, source_stride, destination, destination_stride, width, height);
  return true;
}

// Unchecked hot-path fill. Row padding is never touched.
inline void fill_pixel_rows_unchecked(std::uint16_t* destination, int destination_stride, int width,
                                      int height, std::uint16_t color) {
#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_PIXEL_MEMORY)
  const std::uint32_t replicated_color = static_cast<std::uint32_t>(color) * 0x00010001U;
  tinydraw_fill_pixel_rows_pie(destination, replicated_color, destination_stride, width, height);
#else
  for (int row = 0; row < height; ++row) {
    std::fill_n(destination + static_cast<std::ptrdiff_t>(row) * destination_stride, width, color);
  }
#endif
}

[[nodiscard]] inline bool fill_pixel_rows(std::uint16_t* destination, int destination_stride,
                                          int width, int height, std::uint16_t color) {
  if (width < 0 || height < 0 || destination_stride < width) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;
  }
  if (destination == nullptr) {
    return false;
  }
  fill_pixel_rows_unchecked(destination, destination_stride, width, height, color);
  return true;
}

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_PIXEL_MEMORY_H
