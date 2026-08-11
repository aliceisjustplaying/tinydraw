#include "tinydraw/graphics/stroke_raster.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "tinydraw/graphics/tile_undo_history.h"

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

void include(Bounds& destination, const RibbonPrimitiveBatch& primitives) {
  for (const RibbonPrimitive& primitive : primitives) {
    const Bounds source = bounds_of(primitive);
    destination.minimum_x = std::min(destination.minimum_x, source.minimum_x);
    destination.minimum_y = std::min(destination.minimum_y, source.minimum_y);
    destination.maximum_x = std::max(destination.maximum_x, source.maximum_x);
    destination.maximum_y = std::max(destination.maximum_y, source.maximum_y);
  }
}

struct Region {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

Region region_of(Bounds bounds) {
  if (!std::isfinite(bounds.minimum_x) || !std::isfinite(bounds.minimum_y) ||
      !std::isfinite(bounds.maximum_x) || !std::isfinite(bounds.maximum_y)) {
    return {};
  }
  const int x = std::max(0, static_cast<int>(std::floor(bounds.minimum_x))) & ~1;
  const int y = std::max(0, static_cast<int>(std::floor(bounds.minimum_y))) & ~1;
  const int right =
      std::min(kCanvasWidth, (static_cast<int>(std::ceil(bounds.maximum_x)) + 2) & ~1);
  const int bottom =
      std::min(kCanvasHeight, (static_cast<int>(std::ceil(bounds.maximum_y)) + 2) & ~1);
  return {.x = x, .y = y, .width = right - x, .height = bottom - y};
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

StrokeRaster::StrokeRaster(std::span<std::uint16_t> committed,
                           std::span<std::uint8_t> active_coverage, DisplayBackend& display)
    : committed_(committed), active_coverage_(active_coverage), display_(&display) {
  constexpr auto pixel_count = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);
  valid_ = committed_.size() >= pixel_count && active_coverage_.size() >= pixel_count;
  assert(valid_);
}

StrokeRaster::StrokeRaster(std::span<std::uint16_t> committed, std::span<std::uint16_t> visible,
                           std::span<std::uint8_t> active_coverage, DisplayBackend& display)
    : committed_(committed),
      visible_(visible),
      active_coverage_(active_coverage),
      display_(&display) {
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
    const auto flag_index = static_cast<std::size_t>(tile_index);
    const bool has_committed = committed_tiles[flag_index];
    const bool is_dirty = dirty_tiles[flag_index];
    if (!has_committed && !is_dirty) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y, stats);
    if (has_committed) {
      rasterize(update.committed, stats);
      store_coverage_tile(tile_x, tile_y, stats);
      touched_[flag_index] = true;
    }
    if (is_dirty) {
      rasterize(update.provisional, stats);
      compose_visible_tile(tile_x, tile_y, color, stats);
    }
  }

  Bounds presentation_bounds;
  include(presentation_bounds, provisional_);
  include(presentation_bounds, update.committed);
  include(presentation_bounds, update.provisional);
  const Region presentation = region_of(presentation_bounds);
  present_region(presentation.x, presentation.y, presentation.width, presentation.height);
  provisional_ = update.provisional;
  return stats;
}

StrokeRasterStats StrokeRaster::finish(const RibbonUpdate& update, std::uint16_t color,
                                       TileUndoHistory* history, ViewOrigin origin) {
  assert(update.provisional.empty());
  StrokeRasterStats stats;
  if (!valid_ || !update.provisional.empty()) {
    return stats;
  }
  if (history != nullptr) {
    history->begin_entry(origin);
  }
  TileFlags committed_tiles{};
  mark_tiles(update.committed, committed_tiles);

  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!committed_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y, stats);
    rasterize(update.committed, stats);
    store_coverage_tile(tile_x, tile_y, stats);
    touched_[static_cast<std::size_t>(tile_index)] = true;
  }

  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!touched_[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    load_coverage_tile(tile_x, tile_y, stats);
    compose_committed_tile(tile_x, tile_y, color, stats, history);
    touched_[static_cast<std::size_t>(tile_index)] = false;
  }

  Bounds presentation_bounds;
  include(presentation_bounds, update.committed);
  const Region presentation = region_of(presentation_bounds);
  present_region(presentation.x, presentation.y, presentation.width, presentation.height);
  if (history != nullptr) {
    static_cast<void>(history->commit_entry());
  }
  provisional_ = {};
  return stats;
}

void StrokeRaster::cancel() {
  if (!valid_) {
    return;
  }
  TileFlags dirty_tiles = touched_;
  mark_tiles(provisional_, dirty_tiles);
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!dirty_tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    const int tile_width = std::min(kTileSize, kCanvasWidth - tile_x);
    const int tile_height = std::min(kTileSize, kCanvasHeight - tile_y);
    for (int y = 0; y < tile_height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      const auto tile_index_in_working = static_cast<std::size_t>(y * tile_width);
      std::fill_n(active_coverage_.begin() + static_cast<std::ptrdiff_t>(canvas_index), tile_width,
                  0U);
      std::copy_n(committed_.begin() + static_cast<std::ptrdiff_t>(canvas_index), tile_width,
                  working_.begin() + static_cast<std::ptrdiff_t>(tile_index_in_working));
      if (!visible_.empty()) {
        std::copy_n(committed_.begin() + static_cast<std::ptrdiff_t>(canvas_index), tile_width,
                    visible_.begin() + static_cast<std::ptrdiff_t>(canvas_index));
      }
    }
    if (display_ != nullptr && visible_.empty()) {
      display_->push_rect(tile_x, tile_y, tile_width, tile_height, working_.data());
    }
    touched_[static_cast<std::size_t>(tile_index)] = false;
  }
  present_tiles(dirty_tiles);
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

void StrokeRaster::load_coverage_tile(int tile_x, int tile_y, StrokeRasterStats& stats) {
  const int tile_width = std::min(kTileSize, kCanvasWidth - tile_x);
  const int tile_height = std::min(kTileSize, kCanvasHeight - tile_y);
  coverage_.reset(tile_x, tile_y, tile_width, tile_height);
  for (int y = 0; y < tile_height; ++y) {
    const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
    std::copy_n(active_coverage_.begin() + static_cast<std::ptrdiff_t>(index), tile_width,
                coverage_.row(y));
    stats.coverage_bytes_read += static_cast<std::uint32_t>(tile_width);
  }
}

void StrokeRaster::store_coverage_tile(int tile_x, int tile_y, StrokeRasterStats& stats) {
  for (int y = 0; y < coverage_.height(); ++y) {
    const auto index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
    std::copy_n(coverage_.row(y), coverage_.width(),
                active_coverage_.begin() + static_cast<std::ptrdiff_t>(index));
    stats.coverage_bytes_written += static_cast<std::uint32_t>(coverage_.width());
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

void StrokeRaster::present_region(int x, int y, int width, int height) {
  if (display_ == nullptr || visible_.empty() || width <= 0 || height <= 0) {
    return;
  }
  const auto offset = static_cast<std::size_t>(y * kCanvasWidth + x);
  display_->push_rect(x, y, width, height, visible_.data() + offset, kCanvasWidth);
}

void StrokeRaster::present_tiles(const TileFlags& tiles) {
  if (display_ == nullptr || visible_.empty()) {
    return;
  }
  int first_x = kCanvasWidth;
  int first_y = kCanvasHeight;
  int last_x = 0;
  int last_y = 0;
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!tiles[static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int tile_x = tile_index % kTilesAcross * kTileSize;
    const int tile_y = tile_index / kTilesAcross * kTileSize;
    first_x = std::min(first_x, tile_x);
    first_y = std::min(first_y, tile_y);
    last_x = std::max(last_x, std::min(tile_x + kTileSize, kCanvasWidth));
    last_y = std::max(last_y, std::min(tile_y + kTileSize, kCanvasHeight));
  }
  if (first_x >= last_x || first_y >= last_y) {
    return;
  }
  present_region(first_x, first_y, last_x - first_x, last_y - first_y);
}

void StrokeRaster::compose_visible_tile(int tile_x, int tile_y, std::uint16_t color,
                                        StrokeRasterStats& stats, TileUndoHistory* history) {
  const std::size_t tile_pixels = static_cast<std::size_t>(coverage_.width() * coverage_.height());
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      const auto tile_index = static_cast<std::size_t>(y * coverage_.width() + x);
      working_[tile_index] = committed_[canvas_index];
      stats.committed_bytes_read += sizeof(std::uint16_t);
    }
  }
  if (history != nullptr && history->valid()) {
    history->capture_tile(tile_x, tile_y, coverage_.width(), coverage_.height(),
                          std::span<const std::uint16_t>(working_.data(), tile_pixels));
    stats.history_bytes_written += static_cast<std::uint32_t>(tile_pixels * sizeof(std::uint16_t));
  }
  composite_rgb565(coverage_, color, std::span(working_.data(), tile_pixels));
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      const auto tile_index = static_cast<std::size_t>(y * coverage_.width() + x);
      if (!visible_.empty()) {
        visible_[canvas_index] = working_[tile_index];
      }
    }
  }
  if (display_ != nullptr && visible_.empty()) {
    display_->push_rect(tile_x, tile_y, coverage_.width(), coverage_.height(), working_.data());
  }
  ++stats.tiles_updated;
  stats.pixels_composited += static_cast<std::uint32_t>(tile_pixels);
  stats.display_bytes += static_cast<std::uint32_t>(tile_pixels * sizeof(std::uint16_t));
}

void StrokeRaster::compose_committed_tile(int tile_x, int tile_y, std::uint16_t color,
                                          StrokeRasterStats& stats, TileUndoHistory* history) {
  compose_visible_tile(tile_x, tile_y, color, stats, history);
  for (int y = 0; y < coverage_.height(); ++y) {
    for (int x = 0; x < coverage_.width(); ++x) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x + x);
      committed_[canvas_index] = working_[static_cast<std::size_t>(y * coverage_.width() + x)];
      active_coverage_[canvas_index] = 0U;
      stats.committed_bytes_written += sizeof(std::uint16_t);
      ++stats.coverage_bytes_written;
    }
  }
}

}  // namespace tinydraw
