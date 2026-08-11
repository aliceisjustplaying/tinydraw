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
  std::uint32_t yield_every_strokes = 0;
};

struct ViewportRenderStats {
  std::uint32_t strokes_tested = 0;
  std::uint32_t strokes_intersecting = 0;
  std::uint32_t samples_processed = 0;
  std::uint32_t primitives_rasterized = 0;
  std::uint32_t primitive_tile_visits = 0;
  std::uint32_t tiles_composited = 0;
  bool complete = true;
};

// Rebuilds one raster viewport without display transfers or live-stroke state.
// Scratch is a bounded temporary RibbonPrimitive arena; no coverage plane or
// generated geometry becomes part of the document.
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

 private:
  static constexpr int kTileSize = kStrokeTileSize;
  static constexpr int kTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
  static constexpr int kTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
  static constexpr int kTileCount = kTilesAcross * kTilesDown;
  using TileFlags = std::array<bool, kTileCount>;

  [[nodiscard]] bool append(const RibbonPrimitiveBatch& batch, std::size_t& primitive_count,
                            TileFlags& stroke_tiles);
  void composite_stroke(std::span<std::uint16_t> destination,
                        std::span<const RibbonPrimitive> primitives, const TileFlags& stroke_tiles,
                        std::uint16_t color, ViewportRenderStats& stats);
  [[nodiscard]] std::span<RibbonPrimitive> primitive_arena();

  std::span<std::uint8_t> scratch_;
  CoverageTile coverage_{0, 0, kTileSize, kTileSize};
  std::array<std::uint16_t, kTileSize * kTileSize> working_{};
};

}  // namespace tinydraw
