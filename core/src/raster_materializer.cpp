#include "tinydraw/graphics/raster_materializer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tinydraw {
namespace {

constexpr std::int64_t kFixedOne = 1LL << 16;
constexpr std::int64_t kFixedHalf = kFixedOne / 2;

std::uint16_t average_rgb565(std::span<const std::uint16_t> pixels) {
  unsigned red = 0;
  unsigned green = 0;
  unsigned blue = 0;
  for (const std::uint16_t pixel : pixels) {
    red += (pixel >> 11U) & 0x1FU;
    green += (pixel >> 5U) & 0x3FU;
    blue += pixel & 0x1FU;
  }
  const unsigned count = static_cast<unsigned>(pixels.size());
  red = (red + count / 2U) / count;
  green = (green + count / 2U) / count;
  blue = (blue + count / 2U) / count;
  return static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
}

}  // namespace

void resample_valid_raster(std::span<const std::uint16_t> source, int source_width,
                           int source_height, Camera source_camera,
                           std::span<std::uint16_t> destination, int destination_width,
                           int destination_height, Camera destination_camera,
                           std::uint16_t background) {
  const std::size_t source_pixels = static_cast<std::size_t>(std::max(source_width, 0)) *
                                    static_cast<std::size_t>(std::max(source_height, 0));
  const std::size_t destination_pixels = static_cast<std::size_t>(std::max(destination_width, 0)) *
                                         static_cast<std::size_t>(std::max(destination_height, 0));
  if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
      destination_height <= 0 || source.size() < source_pixels ||
      destination.size() < destination_pixels || !camera_valid(source_camera) ||
      !camera_valid(destination_camera)) {
    return;
  }

  const double source_step =
      static_cast<double>(source_camera.zoom) / static_cast<double>(destination_camera.zoom);
  const double source_start_x =
      (destination_camera.x - source_camera.x) * source_camera.zoom + source_step * 0.5 - 0.5;
  const double source_start_y =
      (destination_camera.y - source_camera.y) * source_camera.zoom + source_step * 0.5 - 0.5;
  const auto step = static_cast<std::int64_t>(std::llround(source_step * kFixedOne));
  const auto start_x = static_cast<std::int64_t>(std::llround(source_start_x * kFixedOne));
  std::int64_t source_y_fixed = static_cast<std::int64_t>(std::llround(source_start_y * kFixedOne));
  for (int y = 0; y < destination_height; ++y) {
    const int source_y = static_cast<int>((source_y_fixed + kFixedHalf) >> 16);
    std::int64_t source_x_fixed = start_x;
    for (int x = 0; x < destination_width; ++x) {
      const int source_x = static_cast<int>((source_x_fixed + kFixedHalf) >> 16);
      const auto destination_index = static_cast<std::size_t>(y * destination_width + x);
      if (source_x >= 0 && source_x < source_width && source_y >= 0 && source_y < source_height) {
        destination[destination_index] =
            source[static_cast<std::size_t>(source_y * source_width + source_x)];
      } else {
        destination[destination_index] = background;
      }
      source_x_fixed += step;
    }
    source_y_fixed += step;
  }
}

void upsample_rgb565_2x(std::span<const std::uint16_t> source, int source_width, int source_height,
                        int source_stride, std::span<std::uint16_t> destination,
                        int destination_stride) {
  if (source_width <= 0 || source_height <= 0 || source_stride < source_width ||
      destination_stride < source_width * 2 ||
      source.size() <
          static_cast<std::size_t>(source_stride) * static_cast<std::size_t>(source_height) ||
      destination.size() < static_cast<std::size_t>(destination_stride) *
                               static_cast<std::size_t>(source_height) * 2U) {
    return;
  }
  for (int source_y = 0; source_y < source_height; ++source_y) {
    for (int source_x = 0; source_x < source_width; ++source_x) {
      const std::uint16_t pixel =
          source[static_cast<std::size_t>(source_y * source_stride + source_x)];
      const int destination_x = source_x * 2;
      const int destination_y = source_y * 2;
      for (int offset_y = 0; offset_y < 2; ++offset_y) {
        auto* output = destination.data() +
                       static_cast<std::ptrdiff_t>((destination_y + offset_y) * destination_stride +
                                                   destination_x);
        output[0] = pixel;
        output[1] = pixel;
      }
    }
  }
}

void downsample_rgb565_2x(std::span<const std::uint16_t> source, int source_width,
                          int source_height, std::span<std::uint16_t> destination,
                          int destination_stride) {
  if (source_width <= 0 || source_height <= 0 || destination_stride <= 0 ||
      source.size() <
          static_cast<std::size_t>(source_width) * static_cast<std::size_t>(source_height)) {
    return;
  }
  const int output_width = (source_width + 1) / 2;
  const int output_height = (source_height + 1) / 2;
  if (destination_stride < output_width ||
      destination.size() <
          static_cast<std::size_t>(destination_stride) * static_cast<std::size_t>(output_height)) {
    return;
  }

  for (int output_y = 0; output_y < output_height; ++output_y) {
    for (int output_x = 0; output_x < output_width; ++output_x) {
      std::uint16_t samples[4];
      std::size_t count = 0;
      for (int offset_y = 0; offset_y < 2; ++offset_y) {
        const int source_y = output_y * 2 + offset_y;
        if (source_y >= source_height) {
          continue;
        }
        for (int offset_x = 0; offset_x < 2; ++offset_x) {
          const int source_x = output_x * 2 + offset_x;
          if (source_x < source_width) {
            samples[count++] = source[static_cast<std::size_t>(source_y * source_width + source_x)];
          }
        }
      }
      destination[static_cast<std::size_t>(output_y * destination_stride + output_x)] =
          average_rgb565(std::span(samples, count));
    }
  }
}

}  // namespace tinydraw
