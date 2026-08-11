#include "tinydraw/graphics/viewport_renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tinydraw {
namespace {

struct PrimitiveBounds {
  float x0 = std::numeric_limits<float>::max();
  float y0 = std::numeric_limits<float>::max();
  float x1 = std::numeric_limits<float>::lowest();
  float y1 = std::numeric_limits<float>::lowest();
};

void include(PrimitiveBounds& bounds, Point point, float padding = 0.0F) {
  bounds.x0 = std::min(bounds.x0, point.x - padding);
  bounds.y0 = std::min(bounds.y0, point.y - padding);
  bounds.x1 = std::max(bounds.x1, point.x + padding);
  bounds.y1 = std::max(bounds.y1, point.y + padding);
}

PrimitiveBounds bounds_of(const RibbonPrimitive& primitive) {
  PrimitiveBounds bounds;
  if (primitive.kind == RibbonPrimitiveKind::kCircle) {
    include(bounds, primitive.center, primitive.radius + 1.0F);
  } else {
    for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
      include(bounds, primitive.points[index], 1.0F);
    }
  }
  return bounds;
}

}  // namespace

ViewportRenderer::ViewportRenderer(std::span<std::uint8_t> scratch) : scratch_(scratch) {}

ViewportRenderStats ViewportRenderer::render(const VectorDocument& document, Camera camera,
                                             std::span<std::uint16_t> destination,
                                             ViewportRenderOptions options) {
  ViewportRenderStats stats;
  if (!valid() || destination.size() < kPixelCount || !camera_valid(camera) ||
      !std::isfinite(options.minimum_screen_radius) || options.minimum_screen_radius < 0.0F) {
    return stats;
  }

  std::fill_n(destination.begin(), kPixelCount, options.background);
  std::fill_n(scratch_.begin(), kScratchBytes, 0U);
  const RectF viewport = camera_world_viewport(camera);

  for (const VectorStroke& stroke : document.strokes()) {
    ++stats.strokes_tested;
    if (!rects_intersect(stroke.bounds, viewport)) {
      continue;
    }
    const auto samples = document.samples(stroke);
    if (samples.empty()) {
      continue;
    }
    ++stats.strokes_intersecting;

    TileFlags stroke_tiles{};
    CurvedRibbonStream ribbon;
    for (std::size_t index = 0; index < samples.size(); ++index) {
      const StrokeSample sample = samples[index];
      const InkPoint point{
          .position = camera_project(camera, sample.x, sample.y),
          .radius = camera_project_radius(camera, sample.radius, options.minimum_screen_radius),
      };
      const bool final = index + 1U == samples.size();
      const RibbonUpdate update = final ? ribbon.finish(point) : ribbon.append(point);
      rasterize(update.committed, stroke_tiles, stats);
      ++stats.samples_processed;
    }

    const std::uint16_t color =
        stroke.tool == VectorTool::kEraser ? options.background : stroke.color;
    composite_stroke(destination, stroke_tiles, color, stats);
  }
  return stats;
}

void ViewportRenderer::rasterize(const RibbonPrimitiveBatch& primitives, TileFlags& stroke_tiles,
                                 ViewportRenderStats& stats) {
  TileFlags batch_tiles{};
  for (const RibbonPrimitive& primitive : primitives) {
    const PrimitiveBounds bounds = bounds_of(primitive);
    if (!std::isfinite(bounds.x0) || !std::isfinite(bounds.y0) || !std::isfinite(bounds.x1) ||
        !std::isfinite(bounds.y1) || bounds.x1 < 0.0F || bounds.y1 < 0.0F ||
        bounds.x0 >= static_cast<float>(kCanvasWidth) ||
        bounds.y0 >= static_cast<float>(kCanvasHeight)) {
      continue;
    }
    const int first_x = std::clamp(static_cast<int>(std::floor(bounds.x0)), 0, kCanvasWidth - 1);
    const int first_y = std::clamp(static_cast<int>(std::floor(bounds.y0)), 0, kCanvasHeight - 1);
    const int last_x = std::clamp(static_cast<int>(std::ceil(bounds.x1)), 0, kCanvasWidth - 1);
    const int last_y = std::clamp(static_cast<int>(std::ceil(bounds.y1)), 0, kCanvasHeight - 1);
    for (int tile_y = first_y / kTileSize; tile_y <= last_y / kTileSize; ++tile_y) {
      for (int tile_x = first_x / kTileSize; tile_x <= last_x / kTileSize; ++tile_x) {
        batch_tiles[static_cast<std::size_t>(tile_y * kTilesAcross + tile_x)] = true;
      }
    }
  }

  stats.primitives_rasterized += static_cast<std::uint32_t>(primitives.size());
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!batch_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_tile(tile_x, tile_y);
    for (const RibbonPrimitive& primitive : primitives) {
      if (primitive.kind == RibbonPrimitiveKind::kCircle) {
        coverage_.rasterize_circle(primitive.center, primitive.radius);
      } else {
        coverage_.rasterize_convex(std::span(primitive.points.data(), primitive.point_count));
      }
      ++stats.primitive_tile_visits;
    }
    store_tile(tile_x, tile_y);
    stroke_tiles[static_cast<std::size_t>(tile_index)] = true;
  }
}

void ViewportRenderer::load_tile(int tile_x, int tile_y) {
  const int width = std::min(kTileSize, kCanvasWidth - tile_x);
  const int height = std::min(kTileSize, kCanvasHeight - tile_y);
  coverage_.reset(tile_x, tile_y, width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      coverage_.union_coverage(tile_x + x, tile_y + y, scratch_[index]);
    }
  }
}

void ViewportRenderer::store_tile(int tile_x, int tile_y) {
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      scratch_[index] = coverage_.coverage_at(tile_x + x, tile_y + y);
    }
  }
}

void ViewportRenderer::composite_stroke(std::span<std::uint16_t> destination,
                                        const TileFlags& stroke_tiles, std::uint16_t color,
                                        ViewportRenderStats& stats) {
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!stroke_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_tile(tile_x, tile_y);
    const int width = coverage_.width();
    const int height = coverage_.height();
    for (int y = 0; y < height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      const auto tile_offset = static_cast<std::size_t>(y * width);
      std::copy_n(destination.begin() + static_cast<std::ptrdiff_t>(canvas_index), width,
                  working_.begin() + static_cast<std::ptrdiff_t>(tile_offset));
    }
    composite_rgb565(coverage_, color,
                     std::span(working_.data(), static_cast<std::size_t>(width * height)));
    for (int y = 0; y < height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      const auto tile_offset = static_cast<std::size_t>(y * width);
      std::copy_n(working_.begin() + static_cast<std::ptrdiff_t>(tile_offset), width,
                  destination.begin() + static_cast<std::ptrdiff_t>(canvas_index));
      std::fill_n(scratch_.begin() + static_cast<std::ptrdiff_t>(canvas_index), width, 0U);
    }
    ++stats.tiles_composited;
  }
}

}  // namespace tinydraw
