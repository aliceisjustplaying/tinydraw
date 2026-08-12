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

bool cancelled(const ViewportRenderOptions& options) {
  return options.cancelled != nullptr && options.cancelled(options.cancellation_context);
}

constexpr std::size_t kCancellationSampleInterval = 16U;
constexpr float kCurveOverlapPixels = 0.75F;
constexpr float kAntialiasPaddingPixels = 1.0F;

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
  return render_region(document, camera, destination,
                       {.x0 = 0, .y0 = 0, .x1 = kCanvasWidth, .y1 = kCanvasHeight}, options);
}

ViewportRenderStats ViewportRenderer::render_region(const VectorDocument& document, Camera camera,
                                                    std::span<std::uint16_t> destination,
                                                    Rect region, ViewportRenderOptions options) {
  ViewportRenderStats stats;
  const bool region_valid = region.x0 >= 0 && region.y0 >= 0 && region.x1 <= kCanvasWidth &&
                            region.y1 <= kCanvasHeight && region.x0 <= region.x1 &&
                            region.y0 <= region.y1;
  if (!valid() || destination.size() < kPixelCount || !camera_valid(camera) || !region_valid ||
      !std::isfinite(options.minimum_screen_radius) || options.minimum_screen_radius < 0.0F) {
    stats.complete = false;
    return stats;
  }
  if (region.x0 == region.x1 || region.y0 == region.y1) {
    return stats;
  }
  if (cancelled(options)) {
    stats.complete = false;
    return stats;
  }

  const std::uint32_t clear_started = tick(options.now);
  for (int y = region.y0; y < region.y1; ++y) {
    const auto row =
        destination.begin() + static_cast<std::ptrdiff_t>(y * kCanvasWidth + region.x0);
    std::fill_n(row, static_cast<std::size_t>(region.x1 - region.x0), options.background);
  }
  stats.clear_ticks += static_cast<std::uint32_t>(tick(options.now) - clear_started);
  const double inverse_zoom = 1.0 / static_cast<double>(camera.zoom);
  const double screen_halo = static_cast<double>(options.minimum_screen_radius) +
                             static_cast<double>(kCurveOverlapPixels + kAntialiasPaddingPixels);
  const double world_halo = screen_halo * inverse_zoom;
  const RectF viewport{
      .x0 =
          static_cast<float>(camera.x + static_cast<double>(region.x0) * inverse_zoom - world_halo),
      .y0 =
          static_cast<float>(camera.y + static_cast<double>(region.y0) * inverse_zoom - world_halo),
      .x1 =
          static_cast<float>(camera.x + static_cast<double>(region.x1) * inverse_zoom + world_halo),
      .y1 =
          static_cast<float>(camera.y + static_cast<double>(region.y1) * inverse_zoom + world_halo),
  };

  Batch batch;
  const auto strokes = document.strokes();
  std::size_t stroke_index = 0;
  while (stroke_index < strokes.size()) {
    if (!options.candidate_strokes.empty()) {
      const std::size_t word = stroke_index / 64U;
      const std::uint64_t bit = std::uint64_t{1} << (stroke_index % 64U);
      if (word >= options.candidate_strokes.size() ||
          (options.candidate_strokes[word] & bit) == 0U) {
        ++stroke_index;
        continue;
      }
    }
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
    const GeometryResult geometry =
        render_stroke_geometry(stroke, samples, camera, region, options, batch, stats);
    if (geometry == GeometryResult::kComplete) {
      ++stats.strokes_intersecting;
      stats.samples_processed += static_cast<std::uint32_t>(samples.size());
      ++stroke_index;
      continue;
    }
    if (geometry == GeometryResult::kCancelled) {
      stats.complete = false;
      return stats;
    }

    // Flush every completed stroke and retry with an empty arena. If the
    // stroke alone exceeds the arena, stream it tile-by-tile so all of its
    // primitives still union into one coverage mask before the single blend.
    batch = checkpoint;
    if (checkpoint.range_count != 0U) {
      composite_batch(destination, batch, region, options, stats);
      batch = {};
      continue;
    }
    if (!render_large_stroke(stroke, samples, camera, destination, region, options, stats)) {
      stats.complete = false;
      return stats;
    }
    ++stats.strokes_intersecting;
    stats.samples_processed += static_cast<std::uint32_t>(samples.size());
    ++stroke_index;
  }
  if (cancelled(options)) {
    stats.complete = false;
    return stats;
  }
  composite_batch(destination, batch, region, options, stats);
  return stats;
}

ViewportRenderer::GeometryResult ViewportRenderer::render_stroke_geometry(
    const VectorStroke& stroke, std::span<const StrokeSample> samples, Camera camera, Rect region,
    const ViewportRenderOptions& options, Batch& batch, ViewportRenderStats& stats) {
  const std::uint32_t geometry_started = tick(options.now);
  auto arena = primitives();
  auto rects = tile_rects();
  CurvedRibbonStream ribbon;
  const std::size_t first_primitive = batch.primitive_count;

  for (std::size_t index = 0; index < samples.size(); ++index) {
    if (index % kCancellationSampleInterval == 0U && cancelled(options)) {
      stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
      return GeometryResult::kCancelled;
    }
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
    const RibbonUpdate update = final ? ribbon.finish(point) : ribbon.append(point, false);
    for (const RibbonPrimitive& primitive : update.committed) {
      const PrimitiveBounds bounds = bounds_of(primitive);
      if (!std::isfinite(bounds.x0) || !std::isfinite(bounds.y0) ||
          !std::isfinite(bounds.x1) || !std::isfinite(bounds.y1) ||
          bounds.x1 < static_cast<float>(region.x0) ||
          bounds.y1 < static_cast<float>(region.y0) ||
          bounds.x0 >= static_cast<float>(region.x1) ||
          bounds.y0 >= static_cast<float>(region.y1)) {
        continue;
      }

      const int first_x =
          std::clamp(static_cast<int>(std::floor(bounds.x0)), 0, kCanvasWidth - 1);
      const int first_y =
          std::clamp(static_cast<int>(std::floor(bounds.y0)), 0, kCanvasHeight - 1);
      const int last_x = std::clamp(static_cast<int>(std::ceil(bounds.x1)), 0, kCanvasWidth - 1);
      const int last_y = std::clamp(static_cast<int>(std::ceil(bounds.y1)), 0, kCanvasHeight - 1);
      const int first_tile_x = std::max(first_x / kTileSize, region.x0 / kTileSize);
      const int first_tile_y = std::max(first_y / kTileSize, region.y0 / kTileSize);
      const int last_tile_x = std::min(last_x / kTileSize, (region.x1 - 1) / kTileSize);
      const int last_tile_y = std::min(last_y / kTileSize, (region.y1 - 1) / kTileSize);
      if (first_tile_x > last_tile_x || first_tile_y > last_tile_y) {
        continue;
      }

      const std::size_t touched = static_cast<std::size_t>(last_tile_x - first_tile_x + 1) *
                                  static_cast<std::size_t>(last_tile_y - first_tile_y + 1);
      if (batch.primitive_count >= arena.size() ||
          touched > kBatchEntryCapacity - batch.entry_count) {
        stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
        return GeometryResult::kCapacity;
      }

      const std::size_t slot = batch.primitive_count++;
      arena[slot] = primitive;
      rects[slot] = {
          .x0 = static_cast<std::uint8_t>(first_tile_x),
          .y0 = static_cast<std::uint8_t>(first_tile_y),
          .x1 = static_cast<std::uint8_t>(last_tile_x),
          .y1 = static_cast<std::uint8_t>(last_tile_y),
      };
      batch.entry_count += touched;
    }
  }

  if (batch.primitive_count == first_primitive) {
    stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
    return GeometryResult::kComplete;
  }
  if (batch.range_count >= ranges().size()) {
    stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
    return GeometryResult::kCapacity;
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
  return GeometryResult::kComplete;
}

bool ViewportRenderer::render_large_stroke(const VectorStroke& stroke,
                                           std::span<const StrokeSample> samples, Camera camera,
                                           std::span<std::uint16_t> destination, Rect region,
                                           const ViewportRenderOptions& options,
                                           ViewportRenderStats& stats) {
  const std::uint32_t geometry_started = tick(options.now);
  const std::uint16_t color =
      stroke.tool == VectorTool::kEraser ? options.background : stroke.color;
  LaneBuffers& lane = lanes_[0];
  std::uint32_t primitive_count = 0U;

  // Restrict the exceptional path to tiles the stroke can conservatively
  // reach. Stored bounds already include world-space radii; the additional
  // screen halo covers minimum-radius enlargement, curve overlap, and AA.
  const float screen_halo =
      options.minimum_screen_radius + kCurveOverlapPixels + kAntialiasPaddingPixels;
  const float screen_x0 =
      static_cast<float>((static_cast<double>(stroke.bounds.x0) - camera.x) * camera.zoom) -
      screen_halo;
  const float screen_y0 =
      static_cast<float>((static_cast<double>(stroke.bounds.y0) - camera.y) * camera.zoom) -
      screen_halo;
  const float screen_x1 =
      static_cast<float>((static_cast<double>(stroke.bounds.x1) - camera.x) * camera.zoom) +
      screen_halo;
  const float screen_y1 =
      static_cast<float>((static_cast<double>(stroke.bounds.y1) - camera.y) * camera.zoom) +
      screen_halo;
  const int clipped_x0 = std::max(region.x0, static_cast<int>(std::floor(screen_x0)));
  const int clipped_y0 = std::max(region.y0, static_cast<int>(std::floor(screen_y0)));
  const int clipped_x1 = std::min(region.x1, static_cast<int>(std::ceil(screen_x1)));
  const int clipped_y1 = std::min(region.y1, static_cast<int>(std::ceil(screen_y1)));
  if (clipped_x0 >= clipped_x1 || clipped_y0 >= clipped_y1) {
    stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
    return true;
  }

  const int first_tile_x = clipped_x0 / kTileSize;
  const int first_tile_y = clipped_y0 / kTileSize;
  const int last_tile_x = (clipped_x1 - 1) / kTileSize;
  const int last_tile_y = (clipped_y1 - 1) / kTileSize;
  for (int tile_y_index = first_tile_y; tile_y_index <= last_tile_y; ++tile_y_index) {
    for (int tile_x_index = first_tile_x; tile_x_index <= last_tile_x; ++tile_x_index) {
      const int grid_x = tile_x_index * kTileSize;
      const int grid_y = tile_y_index * kTileSize;
      const int tile_x = std::max(grid_x, region.x0);
      const int tile_y = std::max(grid_y, region.y0);
      const int tile_right = std::min({grid_x + kTileSize, kCanvasWidth, region.x1});
      const int tile_bottom = std::min({grid_y + kTileSize, kCanvasHeight, region.y1});
      const int width = tile_right - tile_x;
      const int height = tile_bottom - tile_y;
      lane.coverage.reset(tile_x, tile_y, width, height);

      CurvedRibbonStream ribbon;
      std::uint32_t tile_primitive_count = 0U;
      for (std::size_t index = 0; index < samples.size(); ++index) {
        if (index % kCancellationSampleInterval == 0U && cancelled(options)) {
          stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
          return false;
        }
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
        const RibbonUpdate update = final ? ribbon.finish(point) : ribbon.append(point, false);
        for (const RibbonPrimitive& primitive : update.committed) {
          ++tile_primitive_count;
          const PrimitiveBounds bounds = bounds_of(primitive);
          if (std::isfinite(bounds.x0) && std::isfinite(bounds.y0) && std::isfinite(bounds.x1) &&
              std::isfinite(bounds.y1) && bounds.x1 >= static_cast<float>(tile_x) &&
              bounds.y1 >= static_cast<float>(tile_y) &&
              bounds.x0 < static_cast<float>(tile_right) &&
              bounds.y0 < static_cast<float>(tile_bottom)) {
            if (primitive.kind == RibbonPrimitiveKind::kCircle) {
              lane.coverage.rasterize_circle(primitive.center, primitive.radius);
            } else {
              lane.coverage.rasterize_convex(
                  std::span(primitive.points.data(), primitive.point_count));
            }
            ++stats.primitive_tile_visits;
          }
        }
      }
      if (primitive_count == 0U) {
        primitive_count = tile_primitive_count;
      }
      if (lane.coverage.dirty_right() < lane.coverage.dirty_left()) {
        continue;
      }

      const std::uint32_t composite_started = tick(options.now);
      for (int y = 0; y < height; ++y) {
        const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
        std::copy_n(destination.begin() + static_cast<std::ptrdiff_t>(canvas_index), width,
                    lane.working.begin() + static_cast<std::ptrdiff_t>(y * width));
      }
      composite_rgb565(lane.coverage, color,
                       std::span(lane.working.data(), static_cast<std::size_t>(width * height)));
      for (int y = 0; y < height; ++y) {
        const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
        std::copy_n(lane.working.begin() + static_cast<std::ptrdiff_t>(y * width), width,
                    destination.begin() + static_cast<std::ptrdiff_t>(canvas_index));
      }
      stats.composite_ticks += static_cast<std::uint32_t>(tick(options.now) - composite_started);
      ++stats.tiles_composited;
    }
  }
  stats.primitives_rasterized += primitive_count;
  stats.geometry_ticks += static_cast<std::uint32_t>(tick(options.now) - geometry_started);
  ++stats.batches;
  return true;
}

void ViewportRenderer::composite_batch(std::span<std::uint16_t> destination, const Batch& batch,
                                       Rect region, const ViewportRenderOptions& options,
                                       ViewportRenderStats& stats) {
  if (batch.range_count == 0U) {
    return;
  }
  ++stats.batches;
  auto rects = tile_rects();
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

  TileWork work{
      .renderer = this,
      .batch = &batch,
      .destination = destination,
      .options = &options,
      .region = region,
      .lane_count = options.execute != nullptr ? kLanes : 1,
  };
  if (options.execute != nullptr) {
    options.execute(options.execute_context, &ViewportRenderer::composite_lane, &work);
  } else {
    composite_lane(&work, 0);
  }
  for (const ViewportRenderStats& lane : work.lane_stats) {
    stats.primitive_tile_visits += lane.primitive_tile_visits;
    stats.tiles_composited += lane.tiles_composited;
    stats.raster_ticks += lane.raster_ticks;
    stats.composite_ticks += lane.composite_ticks;
  }
}

void ViewportRenderer::composite_lane(void* raw, int lane) {
  auto* work = static_cast<TileWork*>(raw);
  ViewportRenderer& renderer = *work->renderer;
  const Batch& batch = *work->batch;
  const ViewportRenderOptions& options = *work->options;
  std::span<std::uint16_t> destination = work->destination;
  ViewportRenderStats& stats = work->lane_stats[static_cast<std::size_t>(lane)];
  auto arena = renderer.primitives();
  auto range_list = renderer.ranges();
  auto entry_pool = renderer.entries();
  CoverageTile& coverage = renderer.lanes_[static_cast<std::size_t>(lane)].coverage;
  auto& working = renderer.lanes_[static_cast<std::size_t>(lane)].working;
  static_cast<void>(batch);

  std::uint32_t tiles_since_yield = 0U;
  for (int tile = lane; tile < kTileCount; tile += work->lane_count) {
    const std::uint16_t count = renderer.tile_counts_[static_cast<std::size_t>(tile)];
    if (count == 0U) {
      continue;
    }
    const std::uint16_t offset = renderer.tile_offsets_[static_cast<std::size_t>(tile)];
    const int grid_x = tile % kTilesAcross * kTileSize;
    const int grid_y = tile / kTilesAcross * kTileSize;
    const int tile_x = std::max(grid_x, work->region.x0);
    const int tile_y = std::max(grid_y, work->region.y0);
    const int tile_right = std::min({grid_x + kTileSize, kCanvasWidth, work->region.x1});
    const int tile_bottom = std::min({grid_y + kTileSize, kCanvasHeight, work->region.y1});
    const int width = tile_right - tile_x;
    const int height = tile_bottom - tile_y;

    const std::uint32_t read_started = tick(options.now);
    for (int y = 0; y < height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      std::copy_n(destination.begin() + static_cast<std::ptrdiff_t>(canvas_index), width,
                  working.begin() + static_cast<std::ptrdiff_t>(y * width));
    }
    stats.composite_ticks += static_cast<std::uint32_t>(tick(options.now) - read_started);

    // Walk this tile's primitives in document order, grouping runs that belong
    // to the same stroke so ordered colors and eraser strokes stay correct.
    std::size_t range_index = 0;
    std::size_t entry = offset;
    const std::size_t entry_end = static_cast<std::size_t>(offset) + count;
    coverage.reset(tile_x, tile_y, width, height);
    bool first_range = true;
    while (entry < entry_end) {
      if (!first_range) {
        coverage.clear();
      }
      first_range = false;
      const std::uint16_t first_slot = entry_pool[entry];
      while (first_slot >= range_list[range_index].first + range_list[range_index].count) {
        ++range_index;
      }
      const StrokeRange range = range_list[range_index];
      const std::uint32_t raster_started = tick(options.now);
      while (entry < entry_end && entry_pool[entry] < range.first + range.count) {
        const RibbonPrimitive& primitive = arena[entry_pool[entry]];
        if (primitive.kind == RibbonPrimitiveKind::kCircle) {
          coverage.rasterize_circle(primitive.center, primitive.radius);
        } else {
          coverage.rasterize_convex(std::span(primitive.points.data(), primitive.point_count));
        }
        ++stats.primitive_tile_visits;
        ++entry;
      }
      const std::uint32_t blend_started = tick(options.now);
      stats.raster_ticks += static_cast<std::uint32_t>(blend_started - raster_started);
      composite_rgb565(coverage, range.color,
                       std::span(working.data(), static_cast<std::size_t>(width * height)));
      stats.composite_ticks += static_cast<std::uint32_t>(tick(options.now) - blend_started);
    }

    const std::uint32_t write_started = tick(options.now);
    for (int y = 0; y < height; ++y) {
      const auto canvas_index = static_cast<std::size_t>((tile_y + y) * kCanvasWidth + tile_x);
      std::copy_n(working.begin() + static_cast<std::ptrdiff_t>(y * width), width,
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
