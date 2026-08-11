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
};

struct ViewportRenderStats {
  std::uint32_t strokes_tested = 0;
  std::uint32_t strokes_intersecting = 0;
  std::uint32_t samples_processed = 0;
  std::uint32_t primitives_rasterized = 0;
  std::uint32_t primitive_tile_visits = 0;
  std::uint32_t tiles_composited = 0;
};

// Rebuilds one raster viewport without display transfers or live-stroke state.
// Scratch holds one byte of coverage per viewport pixel and may live in PSRAM.
class ViewportRenderer {
 public:
  static constexpr std::size_t kPixelCount = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);
  static constexpr std::size_t kScratchBytes = kPixelCount;

  explicit ViewportRenderer(std::span<std::uint8_t> scratch);

  [[nodiscard]] bool valid() const { return scratch_.size() >= kScratchBytes; }
  [[nodiscard]] ViewportRenderStats render(const VectorDocument& document, Camera camera,
                                           std::span<std::uint16_t> destination,
                                           ViewportRenderOptions options = {});

 private:
  static constexpr int kTileSize = kStrokeTileSize;
  static constexpr int kTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
  static constexpr int kTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
  static constexpr int kTileCount = kTilesAcross * kTilesDown;
  using TileFlags = std::array<bool, kTileCount>;

  void rasterize(const RibbonPrimitiveBatch& primitives, TileFlags& stroke_tiles,
                 ViewportRenderStats& stats);
  void load_tile(int tile_x, int tile_y);
  void store_tile(int tile_x, int tile_y);
  void composite_stroke(std::span<std::uint16_t> destination, const TileFlags& stroke_tiles,
                        std::uint16_t color, ViewportRenderStats& stats);

  std::span<std::uint8_t> scratch_;
  CoverageTile coverage_{0, 0, kTileSize, kTileSize};
  std::array<std::uint16_t, kTileSize * kTileSize> working_{};
};

}  // namespace tinydraw
