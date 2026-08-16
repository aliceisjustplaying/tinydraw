#include "tinydraw/graphics/ribbon_renderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "tinydraw/checked_surface.h"

namespace tinydraw {
namespace {

constexpr float kMaximumCoordinateMagnitude = 1'000'000.0F;

struct Bounds {
  float minimum_x = std::numeric_limits<float>::max();
  float minimum_y = std::numeric_limits<float>::max();
  float maximum_x = std::numeric_limits<float>::lowest();
  float maximum_y = std::numeric_limits<float>::lowest();
};

struct PreparedTaperedSegment {
  Point first{};
  float first_radius = 0.0F;
  float delta_x = 0.0F;
  float delta_y = 0.0F;
  float second_radius = 0.0F;
  float inverse_length_squared = 0.0F;
};

PreparedTaperedSegment prepare_tapered(const RibbonPrimitive& primitive) {
  const Point second = tapered_ribbon_second_center(primitive);
  const float delta_x = second.x - primitive.center.x;
  const float delta_y = second.y - primitive.center.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  return {
      .first = primitive.center,
      .first_radius = primitive.radius,
      .delta_x = delta_x,
      .delta_y = delta_y,
      .second_radius = tapered_ribbon_second_radius(primitive),
      .inverse_length_squared = length_squared == 0.0F ? 0.0F : 1.0F / length_squared,
  };
}

bool covers(const PreparedTaperedSegment& segment, Point point) {
  if (segment.inverse_length_squared == 0.0F) {
    const float radius = std::max(segment.first_radius, segment.second_radius);
    const float distance_x = point.x - segment.first.x;
    const float distance_y = point.y - segment.first.y;
    return distance_x * distance_x + distance_y * distance_y <= radius * radius;
  }
  const float projection = ((point.x - segment.first.x) * segment.delta_x +
                            (point.y - segment.first.y) * segment.delta_y) *
                           segment.inverse_length_squared;
  const float amount = std::clamp(projection, 0.0F, 1.0F);
  const float center_x = segment.first.x + amount * segment.delta_x;
  const float center_y = segment.first.y + amount * segment.delta_y;
  const float radius =
      segment.first_radius + amount * (segment.second_radius - segment.first_radius);
  const float distance_x = point.x - center_x;
  const float distance_y = point.y - center_y;
  return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

bool safe(Point point) {
  return std::isfinite(point.x) && std::isfinite(point.y) &&
         std::abs(point.x) <= kMaximumCoordinateMagnitude &&
         std::abs(point.y) <= kMaximumCoordinateMagnitude;
}

bool valid(const RibbonPrimitive& primitive) {
  if (primitive.kind == RibbonPrimitiveKind::kCircle) {
    return safe(primitive.center) && std::isfinite(primitive.radius) && primitive.radius > 0.0F &&
           primitive.radius <= kMaximumCoordinateMagnitude;
  }
  if (primitive.kind == RibbonPrimitiveKind::kTaperedSegment) {
    const float second_radius = tapered_ribbon_second_radius(primitive);
    return safe(primitive.center) && safe(tapered_ribbon_second_center(primitive)) &&
           std::isfinite(primitive.radius) && std::isfinite(second_radius) &&
           primitive.radius > 0.0F && second_radius > 0.0F &&
           primitive.radius <= kMaximumCoordinateMagnitude &&
           second_radius <= kMaximumCoordinateMagnitude;
  }
  if (primitive.point_count < 3U || primitive.point_count > primitive.points.size()) {
    return false;
  }
  for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
    if (!safe(primitive.points[index])) {
      return false;
    }
  }
  return true;
}

void include(Bounds& bounds, Point point, float padding = 0.0F) {
  bounds.minimum_x = std::min(bounds.minimum_x, point.x - padding);
  bounds.minimum_y = std::min(bounds.minimum_y, point.y - padding);
  bounds.maximum_x = std::max(bounds.maximum_x, point.x + padding);
  bounds.maximum_y = std::max(bounds.maximum_y, point.y + padding);
}

Bounds primitive_bounds(std::span<const RibbonPrimitive> primitives) {
  Bounds bounds;
  for (const RibbonPrimitive& primitive : primitives) {
    if (!valid(primitive)) {
      continue;
    }
    if (primitive.kind == RibbonPrimitiveKind::kCircle) {
      include(bounds, primitive.center, primitive.radius + 1.0F);
    } else if (primitive.kind == RibbonPrimitiveKind::kTaperedSegment) {
      include(bounds, primitive.center, primitive.radius + 1.0F);
      include(bounds, tapered_ribbon_second_center(primitive),
              tapered_ribbon_second_radius(primitive) + 1.0F);
    } else {
      for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
        include(bounds, primitive.points[index], 1.0F);
      }
    }
  }
  return bounds;
}

}  // namespace

RibbonRenderStats RibbonRenderer::render(std::span<const RibbonPrimitive> primitives,
                                         std::span<std::uint16_t> canvas, int width, int height,
                                         std::uint16_t color) {
  return render_surface(primitives, canvas, width, height, width, 0, 0, color);
}

RibbonRenderStats RibbonRenderer::render_surface(std::span<const RibbonPrimitive> primitives,
                                                 std::span<std::uint16_t> surface, int width,
                                                 int height, int stride, int origin_x, int origin_y,
                                                 std::uint16_t color) {
  // Validate at runtime before any arithmetic: in Release, negative
  // dimensions would underflow `height - 1` and an undersized surface would
  // be written out of bounds. Asserts keep development failures loud.
  assert(width > 0 && height > 0 && stride >= width);
  if (width <= 0 || height <= 0 || stride < width) {
    return {};
  }
  const auto required =
      checked_surface_extent(static_cast<std::size_t>(width), static_cast<std::size_t>(height),
                             static_cast<std::size_t>(stride));
  if (!required.has_value()) {
    return {};
  }
  assert(surface.size() >= *required);
  if (surface.size() < *required) {
    return {};
  }
  if (primitives.empty()) {
    return {};
  }

  const Bounds bounds = primitive_bounds(primitives);
  const float minimum_surface_x = static_cast<float>(origin_x);
  const float minimum_surface_y = static_cast<float>(origin_y);
  const float maximum_surface_x = static_cast<float>(origin_x + width - 1);
  const float maximum_surface_y = static_cast<float>(origin_y + height - 1);
  if (bounds.minimum_x == std::numeric_limits<float>::max() ||
      bounds.maximum_x < minimum_surface_x || bounds.maximum_y < minimum_surface_y ||
      bounds.minimum_x > maximum_surface_x || bounds.minimum_y > maximum_surface_y) {
    return {};
  }
  const int first_x = static_cast<int>(
      std::floor(std::clamp(bounds.minimum_x, minimum_surface_x, maximum_surface_x)));
  const int first_y = static_cast<int>(
      std::floor(std::clamp(bounds.minimum_y, minimum_surface_y, maximum_surface_y)));
  const int last_x = static_cast<int>(
      std::ceil(std::clamp(bounds.maximum_x, minimum_surface_x, maximum_surface_x)));
  const int last_y = static_cast<int>(
      std::ceil(std::clamp(bounds.maximum_y, minimum_surface_y, maximum_surface_y)));

  RibbonRenderStats stats;
  const int first_tile_x = origin_x + (first_x - origin_x) / kTileSize * kTileSize;
  const int first_tile_y = origin_y + (first_y - origin_y) / kTileSize * kTileSize;
  for (int tile_y = first_tile_y; tile_y <= last_y; tile_y += kTileSize) {
    for (int tile_x = first_tile_x; tile_x <= last_x; tile_x += kTileSize) {
      const int tile_width = std::min(kTileSize, origin_x + width - tile_x);
      const int tile_height = std::min(kTileSize, origin_y + height - tile_y);
      if (!coverage_.reset(tile_x, tile_y, tile_width, tile_height)) {
        continue;
      }
      for (const RibbonPrimitive& primitive : primitives) {
        if (!valid(primitive)) {
          continue;
        }
        if (primitive.kind == RibbonPrimitiveKind::kCircle) {
          coverage_.rasterize_circle(primitive.center, primitive.radius);
        } else if (primitive.kind == RibbonPrimitiveKind::kConvex) {
          coverage_.rasterize_convex(std::span(primitive.points.data(), primitive.point_count));
        }
      }

      for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
          const std::size_t canvas_index =
              static_cast<std::size_t>(tile_y + y - origin_y) * static_cast<std::size_t>(stride) +
              static_cast<std::size_t>(tile_x + x - origin_x);
          const std::size_t tile_index = static_cast<std::size_t>(y * tile_width + x);
          working_[tile_index] = surface[canvas_index];
        }
      }
      const std::size_t tile_pixels = static_cast<std::size_t>(tile_width * tile_height);
      composite_rgb565(coverage_, color, std::span(working_.data(), tile_pixels));
      for (const RibbonPrimitive& primitive : primitives) {
        if (!valid(primitive) || primitive.kind != RibbonPrimitiveKind::kTaperedSegment) {
          continue;
        }
        const PreparedTaperedSegment segment = prepare_tapered(primitive);
        for (int y = 0; y < tile_height; ++y) {
          for (int x = 0; x < tile_width; ++x) {
            const Point pixel_center{.x = static_cast<float>(tile_x + x) + 0.5F,
                                     .y = static_cast<float>(tile_y + y) + 0.5F};
            if (covers(segment, pixel_center)) {
              working_[static_cast<std::size_t>(y * tile_width + x)] = color;
            }
          }
        }
      }
      for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
          const std::size_t canvas_index =
              static_cast<std::size_t>(tile_y + y - origin_y) * static_cast<std::size_t>(stride) +
              static_cast<std::size_t>(tile_x + x - origin_x);
          const std::size_t tile_index = static_cast<std::size_t>(y * tile_width + x);
          surface[canvas_index] = working_[tile_index];
        }
      }
      ++stats.tiles_rasterized;
      stats.pixels_considered += static_cast<std::uint32_t>(tile_pixels);
    }
  }
  return stats;
}

}  // namespace tinydraw
