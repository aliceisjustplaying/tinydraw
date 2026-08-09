#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/coverage_tile.h"
#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw {

struct StrokeRasterStats {
  std::uint32_t tiles_updated = 0;
  std::uint32_t primitive_tile_visits = 0;
  std::uint32_t pixels_composited = 0;
  std::uint32_t display_bytes = 0;
};

// Incrementally unions one active stroke into a coverage plane. The persistent
// RGB565 canvas changes only at finish(); visible receives dirty-tile previews.
class StrokeRaster {
 public:
  StrokeRaster(std::span<std::uint16_t> committed, std::span<std::uint16_t> visible,
               std::span<std::uint8_t> active_coverage);

  [[nodiscard]] StrokeRasterStats update(const RibbonUpdate& update, std::uint16_t color);
  [[nodiscard]] StrokeRasterStats finish(const RibbonUpdate& update, std::uint16_t color);
  void cancel();

 private:
  static constexpr int kTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
  static constexpr int kTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
  static constexpr int kTileCount = kTilesAcross * kTilesDown;

  using TileFlags = std::array<bool, kTileCount>;

  void mark_tiles(const RibbonPrimitiveBatch& primitives, TileFlags& flags) const;
  void load_coverage_tile(int tile_x, int tile_y);
  void store_coverage_tile(int tile_x, int tile_y);
  void rasterize(const RibbonPrimitiveBatch& primitives, StrokeRasterStats& stats);
  void compose_visible_tile(int tile_x, int tile_y, std::uint16_t color, StrokeRasterStats& stats);
  void compose_committed_tile(int tile_x, int tile_y, std::uint16_t color,
                              StrokeRasterStats& stats);

  std::span<std::uint16_t> committed_;
  std::span<std::uint16_t> visible_;
  std::span<std::uint8_t> active_coverage_;
  bool valid_ = false;
  RibbonPrimitiveBatch provisional_;
  TileFlags touched_{};
  CoverageTile coverage_{0, 0};
  std::array<std::uint16_t, kTileSize * kTileSize> working_{};
};

}  // namespace tinydraw
