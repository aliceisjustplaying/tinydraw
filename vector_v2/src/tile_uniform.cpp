#include "tinydraw/vector_v2/tile_uniform.h"

#include <cstring>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_TILE_UNIFORM)
extern "C" bool tinydraw_tile_uniform_color_pie(const std::uint16_t* pixels, int width, int height,
                                                std::size_t source_stride, std::uint16_t color);
#endif

namespace {

[[nodiscard]] bool pixels_match_color(const std::uint16_t* pixels, int width, int height,
                                      std::size_t source_stride, std::uint16_t color) {
#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_TILE_UNIFORM)
  return tinydraw_tile_uniform_color_pie(pixels, width, height, source_stride, color);
#else
  const std::uint32_t replicated_color = static_cast<std::uint32_t>(color) * 0x00010001U;
  for (int row = 0; row < height; ++row) {
    const std::uint16_t* current =
        pixels + static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(source_stride);
    int remaining = width;

    // A halfword prefix makes every word load naturally aligned. The fixed-size
    // memcpy is recognized as a single load by Clang and GCC and remains valid
    // under strict aliasing.
    if ((reinterpret_cast<std::uintptr_t>(current) & 3U) != 0U) {
      if (*current != color) {
        return false;
      }
      ++current;
      --remaining;
    }
    while (remaining >= 2) {
      std::uint32_t pair = 0U;
      std::memcpy(&pair, current, sizeof(pair));
      if (pair != replicated_color) {
        return false;
      }
      current += 2;
      remaining -= 2;
    }
    if (remaining != 0 && *current != color) {
      return false;
    }
  }
  return true;
#endif
}

}  // namespace

std::optional<std::uint16_t> tile_uniform_color(std::span<const std::uint16_t> pixels, int width,
                                                int height, std::size_t source_stride) {
  if (width <= 0 || height <= 0 || width > kTileWidth || height > kTileHeight ||
      source_stride < static_cast<std::size_t>(width)) {
    return std::nullopt;
  }
  const std::size_t expected =
      static_cast<std::size_t>(height - 1) * source_stride + static_cast<std::size_t>(width);
  if (pixels.size() != expected || pixels.empty()) {
    return std::nullopt;
  }
  const std::uint16_t color = pixels.front();
  if (!pixels_match_color(pixels.data(), width, height, source_stride, color)) {
    return std::nullopt;
  }
  return color;
}

}  // namespace tinydraw::vector_v2
