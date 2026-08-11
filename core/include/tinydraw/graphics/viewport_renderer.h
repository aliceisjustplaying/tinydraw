#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"
#include "tinydraw/graphics/camera.h"
#include "tinydraw/graphics/coverage_tile.h"
#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw {

struct ViewportRenderOptions {
  std::uint16_t background = 0xFFFFU;
  float minimum_screen_radius = 0.0F;
  void (*yield)(void*) = nullptr;
  void* yield_context = nullptr;
  std::uint32_t yield_every_tiles = 0;
  // Optional monotonic tick source (e.g. CPU cycle counter). Wrap-safe deltas
  // accumulate into the phase tick counters below; nullptr disables timing.
  std::uint32_t (*now)() = nullptr;
  // Optional two-lane executor for tile compositing. It must invoke
  // work(work_context, 0) and work(work_context, 1), potentially concurrently,
  // and return only after both complete. Tiles are partitioned by lane, so
  // output is identical to sequential rendering. nullptr runs sequentially.
  void (*execute)(void* context, void (*work)(void*, int lane), void* work_context) = nullptr;
  void* execute_context = nullptr;
};

struct ViewportRenderStats {
  std::uint32_t strokes_tested = 0;
  std::uint32_t strokes_intersecting = 0;
  std::uint32_t samples_processed = 0;
  std::uint32_t primitives_rasterized = 0;
  std::uint32_t primitive_tile_visits = 0;
  std::uint32_t tiles_composited = 0;
  std::uint32_t batches = 0;
  std::uint64_t clear_ticks = 0;
  std::uint64_t geometry_ticks = 0;
  std::uint64_t raster_ticks = 0;
  std::uint64_t composite_ticks = 0;
  bool complete = true;
};

// Rebuilds one raster viewport without display transfers or live-stroke state.
// Strokes stream through a bounded primitive arena in document-order batches;
// each batch is composited tile-major so every touched tile costs one
// destination read and one write per batch instead of one per stroke.
class ViewportRenderer {
 public:
  static constexpr std::size_t kPixelCount = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);
  static constexpr std::size_t kScratchBytes = kPixelCount;

  explicit ViewportRenderer(std::span<std::uint8_t> scratch);

  [[nodiscard]] bool valid() const;
  [[nodiscard]] std::size_t primitive_capacity() const;
  [[nodiscard]] ViewportRenderStats render(const VectorDocument& document, Camera camera,
                                           std::span<std::uint16_t> destination,
                                           ViewportRenderOptions options = {});

  // Rebuilds only a half-open screen-space region and leaves every pixel
  // outside it untouched. This is the seam used by cached pan strips and
  // progressive refinement; output inside the region is identical to a full
  // render at the same camera.
  [[nodiscard]] ViewportRenderStats render_region(const VectorDocument& document, Camera camera,
                                                  std::span<std::uint16_t> destination, Rect region,
                                                  ViewportRenderOptions options = {});

 private:
  static constexpr int kTileSize = kStrokeTileSize;
  static constexpr int kTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
  static constexpr int kTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
  static constexpr int kTileCount = kTilesAcross * kTilesDown;
  static constexpr std::size_t kBatchPrimitiveCapacity = 1'536U;
  static constexpr std::size_t kBatchEntryCapacity = 24'576U;

  // Clamped tile-index rectangle for one primitive; x0 > x1 marks offscreen.
  struct TileRect {
    std::uint8_t x0 = 1;
    std::uint8_t y0 = 1;
    std::uint8_t x1 = 0;
    std::uint8_t y1 = 0;
  };

  struct StrokeRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint16_t color = 0;
  };

  struct Batch {
    std::size_t primitive_count = 0;
    std::size_t range_count = 0;
    std::size_t entry_count = 0;
  };

  static constexpr int kLanes = 2;

  struct LaneBuffers {
    CoverageTile coverage{0, 0, kTileSize, kTileSize};
    std::array<std::uint16_t, kTileSize * kTileSize> working{};
  };

  struct TileWork {
    ViewportRenderer* renderer = nullptr;
    const Batch* batch = nullptr;
    std::span<std::uint16_t> destination;
    const ViewportRenderOptions* options = nullptr;
    Rect region{};
    int lane_count = 1;
    std::array<ViewportRenderStats, kLanes> lane_stats{};
  };

  [[nodiscard]] bool render_stroke_geometry(const VectorStroke& stroke,
                                            std::span<const StrokeSample> samples, Camera camera,
                                            Rect region, const ViewportRenderOptions& options,
                                            Batch& batch, ViewportRenderStats& stats);
  void composite_batch(std::span<std::uint16_t> destination, const Batch& batch, Rect region,
                       const ViewportRenderOptions& options, ViewportRenderStats& stats);
  static void composite_lane(void* raw, int lane);

  [[nodiscard]] std::span<RibbonPrimitive> primitives();
  [[nodiscard]] std::span<TileRect> tile_rects();
  [[nodiscard]] std::span<StrokeRange> ranges();
  [[nodiscard]] std::span<std::uint16_t> entries();

  std::span<std::uint8_t> scratch_;
  std::array<std::uint16_t, kTileCount> tile_counts_{};
  std::array<std::uint16_t, kTileCount> tile_offsets_{};
  std::array<LaneBuffers, kLanes> lanes_{};
};

}  // namespace tinydraw
