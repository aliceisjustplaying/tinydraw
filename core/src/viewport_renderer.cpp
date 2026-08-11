#include "tinydraw/graphics/viewport_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

std::uint32_t tick(std::uint32_t (*now)()) { return now != nullptr ? now() : 0U; }

constexpr std::size_t align_up(std::size_t value, std::size_t alignment) {
  return (value + alignment - 1U) / alignment * alignment;
}

}  // namespace

// Scratch layout: primitives, tile rects, stroke ranges, then bin entries.
namespace layout {
constexpr std::size_t kPrimitivesOffset = 0;
constexpr std::size_t kPrimitivesBytes = 1'536U * sizeof(RibbonPrimitive);
constexpr std::size_t kRectsOffset = align_up(kPrimitivesOffset + kPrimitivesBytes, 4U);
constexpr std::size_t kRectsBytes = 1'536U * 4U;
constexpr std::size_t kRangesOffset = align_up(kRectsOffset + kRectsBytes, 4U);
constexpr std::size_t kRangesBytes = 1'536U * 12U;
constexpr std::size_t kEntriesOffset = align_up(kRangesOffset + kRangesBytes, 2U);
constexpr std::size_t kEntriesBytes = 24'576U * 2U;
constexpr std::size_t kTotalBytes = kEntriesOffset + kEntriesBytes;
}  // namespace layout

ViewportRenderer::ViewportRenderer(std::span<std::uint8_t> scratch) : scratch_(scratch) {
  static_assert(layout::kTotalBytes <= kScratchBytes);
  static_assert(layout::kPrimitivesBytes == kBatchPrimitiveCapacity * sizeof(RibbonPrimitive));
  static_assert(layout::kEntriesBytes == kBatchEntryCapacity * sizeof(std::uint16_t));
  static_assert(sizeof(StrokeRange) <= 12U);
}

bool ViewportRenderer::valid() const {
  return scratch_.size() >= layout::kTotalBytes &&
         reinterpret_cast<std::uintptr_t>(scratch_.data()) % alignof(RibbonPrimitive) == 0U;
}

std::size_t ViewportRenderer::primitive_capacity() const { return kBatchPrimitiveCapacity; }

std::span<RibbonPrimitive> ViewportRenderer::primitives() {
  return {reinterpret_cast<RibbonPrimitive*>(scratch_.data() + layout::kPrimitivesOffset),
          kBatchPrimitiveCapacity};
}

std::span<ViewportRenderer::TileRect> ViewportRenderer::tile_rects() {
  return {reinterpret_cast<TileRect*>(scratch_.data() + layout::kRectsOffset),
          kBatchPrimitiveCapacity};
}

std::span<ViewportRenderer::StrokeRange> ViewportRenderer::ranges() {
  return {reinterpret_cast<StrokeRange*>(scratch_.data() + layout::kRangesOffset),
          kBatchPrimitiveCapacity};
}

std::span<std::uint16_t> ViewportRenderer::entries() {
  return {reinterpret_cast<std::uint16_t*>(scratch_.data() + layout::kEntriesOffset),
          kBatchEntryCapacity};
}

ViewportRenderStats ViewportRenderer::render(const VectorDocument& document, Camera camera,
                                             std::span<std::uint16_t> destination,
                                             ViewportRenderOptions options) {
  ViewportRenderStats stats;
  if (!valid() || destination.size() < kPixelCount || !camera_valid(camera) ||
      !std::isfinite(options.minimum_screen_radius) || options.minimum_screen_radius < 0.0F) {
    stats.complete = false;
    return stats;
  }

  const std::uint32_t clear_started = tick(options.now);
  std::fill_n(destination.begin(), kPixelCount, options.background);
  stats.clear_ticks += static_cast<std::uint32_t>(tick(options.now) - clear_started);
  const RectF viewport = camera_world_viewport(camera);

  Batch batch;
  const auto strokes = document.strokes();
  std::size_t stroke_index = 0;
  while (stroke_index < strokes.size()) {
    const VectorStroke& stroke = strokes[stroke_index];
    ++stats.strokes_tested;
    if (!rects_intersect(stroke.bounds, viewport)) {
      ++stroke_index;
      continue;
    }
    const auto samples = document.samples(stroke);
    if (samples.empty()) {
      ++stroke_index;
      continue;
    }

    const Batch checkpoint = batch;
    if (render_stroke_geometry(stroke, samples, camera, options, batch, stats)) {
      ++stats.strokes_intersecting;
      stats.samples_processed += static_cast<std::uint32_t>(samples.size());
      ++stroke_index;
      continue;
    }

    // The stroke did not fit. Flush every completed stroke and retry it with
    // an empty arena; a stroke too large for the whole arena fails the render.
    if (checkpoint.range_count == 0U) {
      stats.complete = false;
      return stats;
    }
    batch = checkpoint;
    composite_batch(destination, batch, options, stats);
    batch = {};
  }
  composite_batch(destination, batch, options, stats);
  return stats;
}

bool ViewportRenderer::render_stroke_geometry(const VectorStroke& stroke,
                                              std::span<const StrokeSample> samples, Camera camera,
                                              const ViewportRenderOptions& options, Batch& batch,
                                              ViewportRenderStats& stats) {
  const std::uint32_t geometry_started = tick(options.now);
  auto arena = primitives();
  auto rects = tile_rects();
  CurvedRibbonStream ribbon;
  const std::size_t first_primitive = batch.primitive_count;

  for (std::size_t index = 0; index < samples.size(); ++index) {
    const StrokeSample sample = samples[index];
    const InkPoint point{
        .position = camera_project(camera, sample.x, sample.y),
        .pressure = 0.0F,
        .radius = camera_project_radius(camera, sample.radius, options.minimum_screen_radius),
        .distance = 0.0F,
        .running_length = 0.0F,
        .timestamp_us = 0U,
    };
    const bool final = index + 1U == samples.size();
    const RibbonUpdate update = final ? ribbon.finish(point) : ribbon.append(point);
    if (update.committed.size() > arena.size() - batch.primitive_count) {
      stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
      return false;
    }
    for (const RibbonPrimitive& primitive : update.committed) {
      const std::size_t slot = batch.primitive_count;
      arena[slot] = primitive;
      TileRect rect;
      const PrimitiveBounds bounds = bounds_of(primitive);
      if (std::isfinite(bounds.x0) && std::isfinite(bounds.y0) && std::isfinite(bounds.x1) &&
          std::isfinite(bounds.y1) && bounds.x1 >= 0.0F && bounds.y1 >= 0.0F &&
          bounds.x0 < static_cast<float>(kCanvasWidth) &&
          bounds.y0 < static_cast<float>(kCanvasHeight)) {
        const int first_x =
            std::clamp(static_cast<int>(std::floor(bounds.x0)), 0, kCanvasWidth - 1);
        const int first_y =
            std::clamp(static_cast<int>(std::floor(bounds.y0)), 0, kCanvasHeight - 1);
        const int last_x = std::clamp(static_cast<int>(std::ceil(bounds.x1)), 0, kCanvasWidth - 1);
        const int last_y = std::clamp(static_cast<int>(std::ceil(bounds.y1)), 0, kCanvasHeight - 1);
        rect.x0 = static_cast<std::uint8_t>(first_x / kTileSize);
        rect.y0 = static_cast<std::uint8_t>(first_y / kTileSize);
        rect.x1 = static_cast<std::uint8_t>(last_x / kTileSize);
        rect.y1 = static_cast<std::uint8_t>(last_y / kTileSize);
        const std::size_t touched = static_cast<std::size_t>(rect.x1 - rect.x0 + 1) *
                                    static_cast<std::size_t>(rect.y1 - rect.y0 + 1);
        if (touched > kBatchEntryCapacity - batch.entry_count) {
          stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
          return false;
        }
        batch.entry_count += touched;
      }
      rects[slot] = rect;
      ++batch.primitive_count;
    }
  }

  if (batch.range_count >= ranges().size()) {
    stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
    return false;
  }
  const std::uint16_t color =
      stroke.tool == VectorTool::kEraser ? options.background : stroke.color;
  ranges()[batch.range_count++] = {
      .first = static_cast<std::uint32_t>(first_primitive),
      .count = static_cast<std::uint32_t>(batch.primitive_count - first_primitive),
      .color = color,
  };
  stats.primitives_rasterized +=
      static_cast<std::uint32_t>(batch.primitive_count - first_primitive);
  stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
  return true;
}

void ViewportRenderer::composite_batch(std::span<std::uint16_t> destination, const Batch& batch,
                                       const ViewportRenderOptions& options,
                                       ViewportRenderStats& stats) {
  if (batch.range_count == 0U) {
    return;
  }
  ++stats.batches;
  auto arena = primitives();
  auto rects = tile_rects();
  auto range_list = ranges();
  auto entry_pool = entries();

  // Counting sort of primitive indices into per-tile bins, in primitive order.
  const std::uint32_t bin_started = tick(options.now);
  tile_counts_.fill(0U);
  for (std::size_t slot = 0; slot < batch.primitive_count; ++slot) {
    const TileRect rect = rects[slot];
    for (int tile_y = rect.y0; tile_y <= rect.y1; ++tile_y) {
      for (int tile_x = rect.x0; tile_x <= rect.x1; ++tile_x) {
        ++tile_counts_[static_cast<std::size_t>(tile_y * kTilesAcross + tile_x)];
      }
    }
  }
  std::uint16_t running = 0U;
  for (int tile = 0; tile < kTileCount; ++tile) {
    tile_offsets_[static_cast<std::size_t>(tile)] = running;
    running = static_cast<std::uint16_t>(running + tile_counts_[static_cast<std::size_t>(tile)]);
  }
  std::array<std::uint16_t, kTileCount> cursor = tile_offsets_;
  for (std::size_t slot = 0; slot < batch.primitive_count; ++slot) {
    const TileRect rect = rects[slot];
    for (int tile_y = rect.y0; tile_y <= rect.y1; ++tile_y) {
      for (int tile_x = rect.x0; tile_x <= rect.x1; ++tile_x) {
        const auto tile = static_cast<std::size_t>(tile_y * kTilesAcross + tile_x);
        entry_pool[cursor[tile]++] = static_cast<std::uint16_t>(slot);
      }
    }
  }
  stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - bin_started);

  std::uint32_t tiles_since_yield = 0U;
  for (int tile = 0; tile < kTileCount; ++tile) {
    const std::uint16_t count = tile_counts_[static_cast<std::size_t>(tile)];
    if (count == 0U) {
      continue;
    }
    const std::uint16_t offset = tile_offsets_[static_cast<std::size_t>(tile)];
    const int tile_x = tile % kTilesAcross * kTileSize;
    const int tile_y = tile / kTilesAcross * kTileSize;
    const int width = std::min(kTileSize, kCanvasWidth - tile_x);
    const int height = std::min(kTileSize, kCanvasHeight - tile_y);

    const std::uint32_t read_started = tick(options.now);
    for (int y = 0; y < height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      std::copy_n(destination.begin() + static_cast<std::ptrdiff_t>(canvas_index), width,
                  working_.begin() + static_cast<std::ptrdiff_t>(y * width));
    }
    stats.composite_ticks += static_cast<std::uint32_t>(tick(options.now) - read_started);

    // Walk this tile's primitives in document order, grouping runs that belong
    // to the same stroke so ordered colors and eraser strokes stay correct.
    std::size_t range_index = 0;
    std::size_t entry = offset;
    const std::size_t entry_end = static_cast<std::size_t>(offset) + count;
    while (entry < entry_end) {
      const std::uint16_t first_slot = entry_pool[entry];
      while (first_slot >= range_list[range_index].first + range_list[range_index].count) {
        ++range_index;
      }
      const StrokeRange range = range_list[range_index];
      const std::uint32_t raster_started = tick(options.now);
      coverage_.reset(tile_x, tile_y, width, height);
      while (entry < entry_end && entry_pool[entry] < range.first + range.count) {
        const RibbonPrimitive& primitive = arena[entry_pool[entry]];
        if (primitive.kind == RibbonPrimitiveKind::kCircle) {
          coverage_.rasterize_circle(primitive.center, primitive.radius);
        } else {
          coverage_.rasterize_convex(std::span(primitive.points.data(), primitive.point_count));
        }
        ++stats.primitive_tile_visits;
        ++entry;
      }
      const std::uint32_t blend_started = tick(options.now);
      stats.raster_ticks += static_cast<std::uint32_t>(blend_started - raster_started);
      composite_rgb565(coverage_, range.color,
                       std::span(working_.data(), static_cast<std::size_t>(width * height)));
      stats.composite_ticks += static_cast<std::uint32_t>(tick(options.now) - blend_started);
    }

    const std::uint32_t write_started = tick(options.now);
    for (int y = 0; y < height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      std::copy_n(working_.begin() + static_cast<std::ptrdiff_t>(y * width), width,
                  destination.begin() + static_cast<std::ptrdiff_t>(canvas_index));
    }
    stats.composite_ticks += static_cast<std::uint32_t>(tick(options.now) - write_started);
    ++stats.tiles_composited;
    if (options.yield != nullptr && options.yield_every_tiles > 0U &&
        ++tiles_since_yield >= options.yield_every_tiles) {
      tiles_since_yield = 0U;
      options.yield(options.yield_context);
    }
  }
}

}  // namespace tinydraw
