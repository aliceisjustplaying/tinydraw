#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/graphics/camera.h"

namespace tinydraw {

enum class RasterQuality : std::uint8_t {
  kInvalid,
  kDerived,
  kSettled,
  kExact,
};

struct RasterMaterialization {
  Camera camera{};
  std::uint32_t revision = 0;
  RasterQuality quality = RasterQuality::kInvalid;
};

// Builds a valid nearest-neighbor fallback for a camera change. Pixels outside
// the source materialization are known-white rather than undefined.
void resample_valid_raster(std::span<const std::uint16_t> source, int source_width,
                           int source_height, Camera source_camera,
                           std::span<std::uint16_t> destination, int destination_width,
                           int destination_height, Camera destination_camera,
                           std::uint16_t background = 0xFFFFU);

// Camera-aware regional variants. Destination coordinates are in the full
// destination camera; pixels outside region remain untouched.
void resample_valid_raster_region(std::span<const std::uint16_t> source, int source_width,
                                  int source_height, Camera source_camera,
                                  std::span<std::uint16_t> destination, int destination_width,
                                  int destination_height, int destination_stride,
                                  Camera destination_camera, Rect region,
                                  std::uint16_t background = 0xFFFFU);

// Filtered fallback used as the inexpensive settled zoom-in materialization.
// RGB565 channels are interpolated independently with fixed-point weights.
void resample_bilinear_rgb565_region(std::span<const std::uint16_t> source, int source_width,
                                     int source_height, Camera source_camera,
                                     std::span<std::uint16_t> destination, int destination_width,
                                     int destination_height, int destination_stride,
                                     Camera destination_camera, Rect region,
                                     std::uint16_t background = 0xFFFFU);

// Produces one 2x zoom-out level using an RGB565 box filter. Odd edge pixels
// are averaged from the samples that exist. This is derived-valid output, not
// canonical low-zoom LOD.
void downsample_rgb565_2x(std::span<const std::uint16_t> source, int source_width,
                          int source_height, std::span<std::uint16_t> destination,
                          int destination_stride);

// Nearest-neighbor 2x enlargement from a packed or strided source. Intended
// for a deliberately cheap settled pass before canonical refinement.
void upsample_rgb565_2x(std::span<const std::uint16_t> source, int source_width, int source_height,
                        int source_stride, std::span<std::uint16_t> destination,
                        int destination_stride);

}  // namespace tinydraw
