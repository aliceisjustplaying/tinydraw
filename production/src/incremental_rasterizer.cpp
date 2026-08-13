#include "tinydraw/production/incremental_rasterizer.h"

#include <algorithm>
#include <cmath>

namespace tinydraw::production {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
// A center on a pixel-grid intersection is sqrt(0.5) pixels from the nearest
// pixel center. Keep thin projected operations above that distance so stroke
// presence survives every committed zoom.
constexpr float kMinimumScreenRadius = 0.75F;
constexpr float kMaximumRasterStep = 32.0F;

struct Sample {
  float x = 0;
  float y = 0;
  float radius = 0;
};

struct Segment {
  Sample first{};
  Sample second{};
  float delta_x = 0;
  float delta_y = 0;
  float length_squared = 0;
};

Segment make_segment(const Sample& first, const Sample& second) {
  return {
      .first = first,
      .second = second,
      .delta_x = second.x - first.x,
      .delta_y = second.y - first.y,
      .length_squared =
          (second.x - first.x) * (second.x - first.x) + (second.y - first.y) * (second.y - first.y),
  };
}

Sample scaled_sample(CompactOperationSample sample, ZoomLevel zoom) {
  const float scale = static_cast<float>(zoom_percent(zoom)) / 100.0F;
  return {
      .x = static_cast<float>(sample.x_quarter) * 0.25F * scale,
      .y = static_cast<float>(sample.y_quarter) * 0.25F * scale,
      .radius =
          std::max(static_cast<float>(sample.radius_256) / 256.0F * scale, kMinimumScreenRadius),
  };
}

bool valid_surface(const RasterSurface& surface) {
  const int width = surface.level_bounds.x1 - surface.level_bounds.x0;
  const int height = surface.level_bounds.y1 - surface.level_bounds.y0;
  const int level_width = kWorldWidth * zoom_percent(surface.zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(surface.zoom) / 100;
  const std::size_t required =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(surface.stride) +
      static_cast<std::size_t>(width);
  return width > 0 && height > 0 && surface.level_bounds.x0 >= 0 && surface.level_bounds.y0 >= 0 &&
         surface.level_bounds.x1 <= level_width && surface.level_bounds.y1 <= level_height &&
         surface.stride >= width && surface.pixels.size() >= required;
}

PixelRect segment_bounds(const Segment& segment, const RasterSurface& surface) {
  const float minimum_x =
      std::min(segment.first.x - segment.first.radius, segment.second.x - segment.second.radius);
  const float minimum_y =
      std::min(segment.first.y - segment.first.radius, segment.second.y - segment.second.radius);
  const float maximum_x =
      std::max(segment.first.x + segment.first.radius, segment.second.x + segment.second.radius);
  const float maximum_y =
      std::max(segment.first.y + segment.first.radius, segment.second.y + segment.second.radius);
  return {
      .x0 = std::max(surface.level_bounds.x0, static_cast<int>(std::floor(minimum_x))),
      .y0 = std::max(surface.level_bounds.y0, static_cast<int>(std::floor(minimum_y))),
      .x1 = std::min(surface.level_bounds.x1, static_cast<int>(std::ceil(maximum_x))),
      .y1 = std::min(surface.level_bounds.y1, static_cast<int>(std::ceil(maximum_y))),
  };
}

bool covers_pixel(const Segment& segment, float pixel_x, float pixel_y) {
  const float projection = segment.length_squared > 0.0F
                               ? ((pixel_x - segment.first.x) * segment.delta_x +
                                  (pixel_y - segment.first.y) * segment.delta_y) /
                                     segment.length_squared
                               : 0.0F;
  const float amount = std::clamp(projection, 0.0F, 1.0F);
  const float center_x = segment.first.x + amount * segment.delta_x;
  const float center_y = segment.first.y + amount * segment.delta_y;
  const float radius =
      segment.first.radius + amount * (segment.second.radius - segment.first.radius);
  const float distance_x = pixel_x - center_x;
  const float distance_y = pixel_y - center_y;
  return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

void paint_segment(const Sample& first, const Sample& second, std::uint16_t color,
                   const RasterSurface& surface) {
  const Segment segment = make_segment(first, second);
  const PixelRect bounds = segment_bounds(segment, surface);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    for (int x = bounds.x0; x < bounds.x1; ++x) {
      if (covers_pixel(segment, static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F)) {
        const std::size_t offset = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                                       static_cast<std::size_t>(surface.stride) +
                                   static_cast<std::size_t>(x - surface.level_bounds.x0);
        surface.pixels[offset] = color;
      }
    }
  }
}

Sample interpolate(const Sample& first, const Sample& second, float amount) {
  return {
      .x = first.x + amount * (second.x - first.x),
      .y = first.y + amount * (second.y - first.y),
      .radius = first.radius + amount * (second.radius - first.radius),
  };
}

void paint_bounded_segment(const Sample& first, const Sample& second, std::uint16_t color,
                           const RasterSurface& surface) {
  const float span = std::max(std::abs(second.x - first.x), std::abs(second.y - first.y));
  const int steps = std::max(1, static_cast<int>(std::ceil(span / kMaximumRasterStep)));
  Sample prior = first;
  for (int step = 1; step <= steps; ++step) {
    const Sample next =
        interpolate(first, second, static_cast<float>(step) / static_cast<float>(steps));
    paint_segment(prior, next, color, surface);
    prior = next;
  }
}

PixelRect operation_bounds(const OperationAppend& operation, ZoomLevel zoom) {
  const auto world = operation_world_bounds(operation.samples);
  if (!world.has_value()) {
    return {};
  }
  const int percent = zoom_percent(zoom);
  return {
      .x0 = world->x0 * percent / 100,
      .y0 = world->y0 * percent / 100,
      .x1 = std::min(kWorldWidth * percent / 100, (world->x1 * percent + 99) / 100),
      .y1 = std::min(kWorldHeight * percent / 100, (world->y1 * percent + 99) / 100),
  };
}

}  // namespace

bool apply_incremental_operation(const OperationAppend& operation, const RasterSurface& surface) {
  if (!valid_surface(surface) || operation.samples.empty()) {
    return false;
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  if (operation.samples.size() == 1U) {
    const Sample sample = scaled_sample(operation.samples.front(), surface.zoom);
    paint_bounded_segment(sample, sample, color, surface);
    return true;
  }
  for (std::size_t index = 1; index < operation.samples.size(); ++index) {
    paint_bounded_segment(scaled_sample(operation.samples[index - 1U], surface.zoom),
                          scaled_sample(operation.samples[index], surface.zoom), color, surface);
  }
  return true;
}

std::optional<AffectedTileResult> affected_tiles(const OperationAppend& operation, ZoomLevel zoom,
                                                 std::span<TileKey> output) {
  if (zoom == ZoomLevel::k25Percent || operation.samples.empty()) {
    return std::nullopt;
  }
  const PixelRect bounds = operation_bounds(operation, zoom);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return AffectedTileResult{};
  }
  const int first_column = bounds.x0 / kTileWidth;
  const int last_column = (bounds.x1 - 1) / kTileWidth;
  const int first_row = bounds.y0 / kTileHeight;
  const int last_row = (bounds.y1 - 1) / kTileHeight;
  const std::size_t required = static_cast<std::size_t>(last_column - first_column + 1) *
                               static_cast<std::size_t>(last_row - first_row + 1);
  std::size_t written = 0;
  for (int row = first_row; row <= last_row && written < output.size(); ++row) {
    for (int column = first_column; column <= last_column && written < output.size(); ++column) {
      output[written++] = {zoom, static_cast<std::uint16_t>(column),
                           static_cast<std::uint16_t>(row)};
    }
  }
  return AffectedTileResult{.required = required, .written = written};
}

}  // namespace tinydraw::production
