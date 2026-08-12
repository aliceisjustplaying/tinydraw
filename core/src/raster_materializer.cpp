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

bool valid_resample(std::span<const std::uint16_t> source, int source_width, int source_height,
                    std::span<std::uint16_t> destination, int destination_width,
                    int destination_height, int destination_stride, Camera source_camera,
                    Camera destination_camera, Rect region) {
  const std::size_t source_pixels = static_cast<std::size_t>(std::max(source_width, 0)) *
                                    static_cast<std::size_t>(std::max(source_height, 0));
  const std::size_t destination_pixels = static_cast<std::size_t>(std::max(destination_stride, 0)) *
                                         static_cast<std::size_t>(std::max(destination_height, 0));
  return source_width > 0 && source_height > 0 && destination_width > 0 && destination_height > 0 &&
         destination_stride >= destination_width && source.size() >= source_pixels &&
         destination.size() >= destination_pixels && camera_valid(source_camera) &&
         camera_valid(destination_camera) && region.x0 >= 0 && region.y0 >= 0 &&
         region.x1 >= region.x0 && region.y1 >= region.y0 && region.x1 <= destination_width &&
         region.y1 <= destination_height;
}

struct Mapping {
  std::int64_t step = 0;
  std::int64_t start_x = 0;
  std::int64_t start_y = 0;
};

Mapping mapping(Camera source_camera, Camera destination_camera) {
  const double source_step =
      static_cast<double>(source_camera.zoom) / static_cast<double>(destination_camera.zoom);
  const double source_start_x =
      (destination_camera.x - source_camera.x) * source_camera.zoom + source_step * 0.5 - 0.5;
  const double source_start_y =
      (destination_camera.y - source_camera.y) * source_camera.zoom + source_step * 0.5 - 0.5;
  return {
      .step = static_cast<std::int64_t>(std::llround(source_step * kFixedOne)),
      .start_x = static_cast<std::int64_t>(std::llround(source_start_x * kFixedOne)),
      .start_y = static_cast<std::int64_t>(std::llround(source_start_y * kFixedOne)),
  };
}

std::uint16_t bilinear_rgb565(std::uint16_t p00, std::uint16_t p10, std::uint16_t p01,
                              std::uint16_t p11, std::uint32_t fx, std::uint32_t fy) {
  constexpr std::uint32_t kWeight = 256U;
  const std::uint32_t wx0 = kWeight - fx;
  const std::uint32_t wy0 = kWeight - fy;
  const std::uint32_t w00 = wx0 * wy0;
  const std::uint32_t w10 = fx * wy0;
  const std::uint32_t w01 = wx0 * fy;
  const std::uint32_t w11 = fx * fy;
  const auto channel = [&](unsigned shift, std::uint16_t mask) {
    const std::uint32_t value = ((p00 >> shift) & mask) * w00 + ((p10 >> shift) & mask) * w10 +
                                ((p01 >> shift) & mask) * w01 + ((p11 >> shift) & mask) * w11;
    return (value + (kWeight * kWeight) / 2U) / (kWeight * kWeight);
  };
  return static_cast<std::uint16_t>((channel(11U, 0x1FU) << 11U) | (channel(5U, 0x3FU) << 5U) |
                                    channel(0U, 0x1FU));
}

}  // namespace

void resample_valid_raster(std::span<const std::uint16_t> source, int source_width,
                           int source_height, Camera source_camera,
                           std::span<std::uint16_t> destination, int destination_width,
                           int destination_height, Camera destination_camera,
                           std::uint16_t background) {
  resample_valid_raster_region(source, source_width, source_height, source_camera, destination,
                               destination_width, destination_height, destination_width,
                               destination_camera, {0, 0, destination_width, destination_height},
                               background);
}

void resample_valid_raster_region(std::span<const std::uint16_t> source, int source_width,
                                  int source_height, Camera source_camera,
                                  std::span<std::uint16_t> destination, int destination_width,
                                  int destination_height, int destination_stride,
                                  Camera destination_camera, Rect region,
                                  std::uint16_t background) {
  if (!valid_resample(source, source_width, source_height, destination, destination_width,
                      destination_height, destination_stride, source_camera, destination_camera,
                      region)) {
    return;
  }
  const Mapping transform = mapping(source_camera, destination_camera);
  std::int64_t source_y_fixed =
      transform.start_y + static_cast<std::int64_t>(region.y0) * transform.step;
  for (int y = region.y0; y < region.y1; ++y) {
    const int source_y = static_cast<int>((source_y_fixed + kFixedHalf) >> 16);
    std::int64_t source_x_fixed =
        transform.start_x + static_cast<std::int64_t>(region.x0) * transform.step;
    for (int x = region.x0; x < region.x1; ++x) {
      const int source_x = static_cast<int>((source_x_fixed + kFixedHalf) >> 16);
      const auto destination_index = static_cast<std::size_t>(y * destination_stride + x);
      if (source_x >= 0 && source_x < source_width && source_y >= 0 && source_y < source_height) {
        destination[destination_index] =
            source[static_cast<std::size_t>(source_y * source_width + source_x)];
      } else {
        destination[destination_index] = background;
      }
      source_x_fixed += transform.step;
    }
    source_y_fixed += transform.step;
  }
}

void resample_bilinear_rgb565_region(std::span<const std::uint16_t> source, int source_width,
                                     int source_height, Camera source_camera,
                                     std::span<std::uint16_t> destination, int destination_width,
                                     int destination_height, int destination_stride,
                                     Camera destination_camera, Rect region,
                                     std::uint16_t background) {
  if (!valid_resample(source, source_width, source_height, destination, destination_width,
                      destination_height, destination_stride, source_camera, destination_camera,
                      region)) {
    return;
  }
  const Mapping transform = mapping(source_camera, destination_camera);
  std::int64_t source_y_fixed =
      transform.start_y + static_cast<std::int64_t>(region.y0) * transform.step;
  for (int y = region.y0; y < region.y1; ++y) {
    const int y0 = static_cast<int>(source_y_fixed >> 16);
    const std::uint32_t fy = static_cast<std::uint32_t>(source_y_fixed & 0xFFFF) >> 8U;
    std::int64_t source_x_fixed =
        transform.start_x + static_cast<std::int64_t>(region.x0) * transform.step;
    for (int x = region.x0; x < region.x1; ++x) {
      const int x0 = static_cast<int>(source_x_fixed >> 16);
      const std::uint32_t fx = static_cast<std::uint32_t>(source_x_fixed & 0xFFFF) >> 8U;
      const auto sample = [&](int sx, int sy) {
        return sx >= 0 && sx < source_width && sy >= 0 && sy < source_height
                   ? source[static_cast<std::size_t>(sy * source_width + sx)]
                   : background;
      };
      destination[static_cast<std::size_t>(y * destination_stride + x)] = bilinear_rgb565(
          sample(x0, y0), sample(x0 + 1, y0), sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), fx, fy);
      source_x_fixed += transform.step;
    }
    source_y_fixed += transform.step;
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
