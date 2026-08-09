#include "tinydraw/graphics/ribbon_renderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace tinydraw {
namespace {

struct Bounds {
  float minimum_x = std::numeric_limits<float>::max();
  float minimum_y = std::numeric_limits<float>::max();
  float maximum_x = std::numeric_limits<float>::lowest();
  float maximum_y = std::numeric_limits<float>::lowest();
};

bool finite(Point point) { return std::isfinite(point.x) && std::isfinite(point.y); }

bool valid(const RibbonPrimitive& primitive) {
  if (primitive.kind == RibbonPrimitiveKind::kCircle) {
    return finite(primitive.center) && std::isfinite(primitive.radius) && primitive.radius > 0.0F;
  }
  if (primitive.point_count < 3U || primitive.point_count > primitive.points.size()) {
    return false;
  }
  for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
    if (!finite(primitive.points[index])) {
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
  assert(width > 0 && height > 0);
  assert(canvas.size() >= static_cast<std::size_t>(width * height));
  if (primitives.empty()) {
    return {};
  }

  const Bounds bounds = primitive_bounds(primitives);
  if (bounds.minimum_x == std::numeric_limits<float>::max() || bounds.maximum_x < 0.0F ||
      bounds.maximum_y < 0.0F || bounds.minimum_x >= static_cast<float>(width) ||
      bounds.minimum_y >= static_cast<float>(height)) {
    return {};
  }
  const int first_x = std::clamp(static_cast<int>(std::floor(bounds.minimum_x)), 0, width - 1);
  const int first_y = std::clamp(static_cast<int>(std::floor(bounds.minimum_y)), 0, height - 1);
  const int last_x = std::clamp(static_cast<int>(std::ceil(bounds.maximum_x)), 0, width - 1);
  const int last_y = std::clamp(static_cast<int>(std::ceil(bounds.maximum_y)), 0, height - 1);

  RibbonRenderStats stats;
  const int first_tile_x = first_x / kTileSize * kTileSize;
  const int first_tile_y = first_y / kTileSize * kTileSize;
  for (int tile_y = first_tile_y; tile_y <= last_y; tile_y += kTileSize) {
    for (int tile_x = first_tile_x; tile_x <= last_x; tile_x += kTileSize) {
      const int tile_width = std::min(kTileSize, width - tile_x);
      const int tile_height = std::min(kTileSize, height - tile_y);
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
              static_cast<std::size_t>((tile_y + y) * width + tile_x + x);
          const std::size_t tile_index = static_cast<std::size_t>(y * tile_width + x);
          working_[tile_index] = canvas[canvas_index];
        }
      }
      const std::size_t tile_pixels = static_cast<std::size_t>(tile_width * tile_height);
      composite_rgb565(coverage_, color, std::span(working_.data(), tile_pixels));
      for (int y = 0; y < tile_height; ++y) {
        for (int x = 0; x < tile_width; ++x) {
          const std::size_t canvas_index =
              static_cast<std::size_t>((tile_y + y) * width + tile_x + x);
          const std::size_t tile_index = static_cast<std::size_t>(y * tile_width + x);
          canvas[canvas_index] = working_[tile_index];
        }
      }
      ++stats.tiles_rasterized;
      stats.pixels_considered += static_cast<std::uint32_t>(tile_pixels);
    }
  }
  return stats;
}

}  // namespace tinydraw
