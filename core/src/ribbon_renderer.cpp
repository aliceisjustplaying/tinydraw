#include "tinydraw/graphics/ribbon_renderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace tinydraw {
namespace {

constexpr float kMaximumCoordinateMagnitude = 1'000'000.0F;

struct Bounds {
  float minimum_x = std::numeric_limits<float>::max();
  float minimum_y = std::numeric_limits<float>::max();
  float maximum_x = std::numeric_limits<float>::lowest();
  float maximum_y = std::numeric_limits<float>::lowest();
};

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
  const std::size_t required =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(stride) +
      static_cast<std::size_t>(width);
  assert(surface.size() >= required);
  if (surface.size() < required) {
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
      coverage_.reset(tile_x, tile_y, tile_width, tile_height);
      for (const RibbonPrimitive& primitive : primitives) {
        if (!valid(primitive)) {
          continue;
        }
        if (primitive.kind == RibbonPrimitiveKind::kCircle) {
          coverage_.rasterize_circle(primitive.center, primitive.radius);
        } else {
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
