#include "tinydraw/graphics/stroke_raster.h"

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

void include(Bounds& bounds, Point point, float padding = 0.0F) {
  bounds.minimum_x = std::min(bounds.minimum_x, point.x - padding);
  bounds.minimum_y = std::min(bounds.minimum_y, point.y - padding);
  bounds.maximum_x = std::max(bounds.maximum_x, point.x + padding);
  bounds.maximum_y = std::max(bounds.maximum_y, point.y + padding);
}

Bounds bounds_of(const RibbonPrimitive& primitive) {
  Bounds bounds;
  if (primitive.kind == RibbonPrimitiveKind::kCircle) {
    include(bounds, primitive.center, primitive.radius + 1.0F);
    return bounds;
  }
  for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
    include(bounds, primitive.points[index], 1.0F);
  }
  return bounds;
}

}  // namespace

StrokeRaster::StrokeRaster(std::span<std::uint16_t> committed, std::span<std::uint16_t> visible,
                           std::span<std::uint8_t> active_coverage)
    : committed_(committed), visible_(visible), active_coverage_(active_coverage) {
  constexpr auto pixel_count = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);
  valid_ = committed_.size() >= pixel_count && visible_.size() >= pixel_count &&
           active_coverage_.size() >= pixel_count;
  assert(valid_);
}

StrokeRasterStats StrokeRaster::update(const RibbonUpdate& update, std::uint16_t color) {
  StrokeRasterStats stats;
  if (!valid_) {
    return stats;
  }
  TileFlags committed_tiles{};
  TileFlags dirty_tiles{};
  mark_tiles(provisional_, dirty_tiles);
  mark_tiles(update.committed, committed_tiles);
  mark_tiles(update.committed, dirty_tiles);
  mark_tiles(update.provisional, dirty_tiles);

  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!committed_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y);
    rasterize(update.committed, stats);
    store_coverage_tile(tile_x, tile_y);
    touched_[static_cast<std::size_t>(tile_index)] = true;
  }

  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!dirty_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y);
    rasterize(update.provisional, stats);
    compose_visible_tile(tile_x, tile_y, color, stats);
  }

  provisional_ = update.provisional;
  return stats;
}

StrokeRasterStats StrokeRaster::finish(const RibbonUpdate& update, std::uint16_t color) {
  assert(update.provisional.empty());
  StrokeRasterStats stats;
  if (!valid_ || !update.provisional.empty()) {
    return stats;
  }
  TileFlags committed_tiles{};
  mark_tiles(update.committed, committed_tiles);

  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!committed_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y);
    rasterize(update.committed, stats);
    store_coverage_tile(tile_x, tile_y);
    touched_[static_cast<std::size_t>(tile_index)] = true;
  }

  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!touched_[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y);
    compose_committed_tile(tile_x, tile_y, color, stats);
    touched_[static_cast<std::size_t>(tile_index)] = false;
  }

  provisional_ = {};
  return stats;
}

void StrokeRaster::cancel() {
  if (!valid_) {
    return;
  }
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!touched_[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    const int tile_width = std::min(kTileSize, kCanvasWidth - tile_x);
    const int tile_height = std::min(kTileSize, kCanvasHeight - tile_y);
    for (int y = 0; y < tile_height; ++y) {
      for (int x = 0; x < tile_width; ++x) {
        const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
        active_coverage_[index] = 0U;
      }
    }
    touched_[static_cast<std::size_t>(tile_index)] = false;
  }
  std::copy_n(committed_.begin(), static_cast<std::size_t>(kCanvasWidth * kCanvasHeight),
              visible_.begin());
  provisional_ = {};
}

void StrokeRaster::mark_tiles(const RibbonPrimitiveBatch& primitives, TileFlags& flags) const {
  for (const RibbonPrimitive& primitive : primitives) {
    const Bounds bounds = bounds_of(primitive);
    if (!std::isfinite(bounds.minimum_x) || !std::isfinite(bounds.minimum_y) ||
        !std::isfinite(bounds.maximum_x) || !std::isfinite(bounds.maximum_y) ||
        bounds.maximum_x < 0.0F || bounds.maximum_y < 0.0F ||
        bounds.minimum_x >= static_cast<float>(kCanvasWidth) ||
        bounds.minimum_y >= static_cast<float>(kCanvasHeight)) {
      continue;
    }
    const float maximum_x = static_cast<float>(kCanvasWidth - 1);
    const float maximum_y = static_cast<float>(kCanvasHeight - 1);
    const int first_tile_x =
        static_cast<int>(std::floor(std::clamp(bounds.minimum_x, 0.0F, maximum_x))) / kTileSize;
    const int first_tile_y =
        static_cast<int>(std::floor(std::clamp(bounds.minimum_y, 0.0F, maximum_y))) / kTileSize;
    const int last_tile_x =
        static_cast<int>(std::ceil(std::clamp(bounds.maximum_x, 0.0F, maximum_x))) / kTileSize;
    const int last_tile_y =
        static_cast<int>(std::ceil(std::clamp(bounds.maximum_y, 0.0F, maximum_y))) / kTileSize;
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
      for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
        flags[static_cast<std::size_t>(tile_y * kTilesAcross + tile_x)] = true;
      }
    }
  }
}

void StrokeRaster::load_coverage_tile(int tile_x, int tile_y) {
  const int tile_width = std::min(kTileSize, kCanvasWidth - tile_x);
  const int tile_height = std::min(kTileSize, kCanvasHeight - tile_y);
  coverage_.reset(tile_x, tile_y, tile_width, tile_height);
  for (int y = 0; y < tile_height; ++y) {
    for (int x = 0; x < tile_width; ++x) {
      const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      coverage_.union_coverage(tile_x + x, tile_y + y, active_coverage_[index]);
    }
  }
}

void StrokeRaster::store_coverage_tile(int tile_x, int tile_y) {
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      active_coverage_[index] = coverage_.coverage_at(tile_x + x, tile_y + y);
    }
  }
}

void StrokeRaster::rasterize(const RibbonPrimitiveBatch& primitives, StrokeRasterStats& stats) {
  for (const RibbonPrimitive& primitive : primitives) {
    ++stats.primitive_tile_visits;
    if (primitive.kind == RibbonPrimitiveKind::kCircle) {
      coverage_.rasterize_circle(primitive.center, primitive.radius);
    } else {
      coverage_.rasterize_convex(std::span(primitive.points.data(), primitive.point_count));
    }
  }
}

void StrokeRaster::compose_visible_tile(int tile_x, int tile_y, std::uint16_t color,
                                        StrokeRasterStats& stats) {
  const std::size_t tile_pixels = static_cast<std::size_t>(coverage_.width() * coverage_.height());
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      const auto tile_index = static_cast<std::size_t>(y * coverage_.width() + x);
      working_[tile_index] = committed_[canvas_index];
    }
  }
  composite_rgb565(coverage_, color, std::span(working_.data(), tile_pixels));
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      const auto tile_index = static_cast<std::size_t>(y * coverage_.width() + x);
      visible_[canvas_index] = working_[tile_index];
    }
  }
  ++stats.tiles_updated;
  stats.pixels_composited += static_cast<std::uint32_t>(tile_pixels);
}

void StrokeRaster::compose_committed_tile(int tile_x, int tile_y, std::uint16_t color,
                                          StrokeRasterStats& stats) {
  compose_visible_tile(tile_x, tile_y, color, stats);
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      committed_[canvas_index] = visible_[canvas_index];
      active_coverage_[canvas_index] = 0U;
    }
  }
}

}  // namespace tinydraw
