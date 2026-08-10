#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/coverage_tile.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw {

class TileUndoHistory;

struct StrokeRasterStats {
  std::uint32_t tiles_updated = 0;
  std::uint32_t primitive_tile_visits = 0;
  std::uint32_t pixels_composited = 0;
  std::uint32_t display_bytes = 0;
  std::uint32_t committed_bytes_read = 0;
  std::uint32_t committed_bytes_written = 0;
  std::uint32_t coverage_bytes_read = 0;
  std::uint32_t coverage_bytes_written = 0;
  std::uint32_t history_bytes_written = 0;
};

// Incrementally unions one active stroke into a coverage plane. The persistent
// RGB565 canvas changes only at finish(); visible receives dirty-tile previews.
class StrokeRaster {
 public:
  StrokeRaster(std::span<std::uint16_t> committed, std::span<std::uint16_t> visible,
               std::span<std::uint8_t> active_coverage);
  StrokeRaster(std::span<std::uint16_t> committed, std::span<std::uint8_t> active_coverage,
               DisplayBackend& display);
  StrokeRaster(std::span<std::uint16_t> committed, std::span<std::uint16_t> visible,
               std::span<std::uint8_t> active_coverage, DisplayBackend& display);

  [[nodiscard]] StrokeRasterStats update(const RibbonUpdate& update, std::uint16_t color);
  [[nodiscard]] StrokeRasterStats finish(const RibbonUpdate& update, std::uint16_t color,
                                         TileUndoHistory* history = nullptr);
  void cancel();

 private:
  static constexpr int kTileSize = kStrokeTileSize;
  static constexpr int kTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
  static constexpr int kTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
  static constexpr int kTileCount = kTilesAcross * kTilesDown;

  using TileFlags = std::array<bool, kTileCount>;

  void mark_tiles(const RibbonPrimitiveBatch& primitives, TileFlags& flags) const;
  void load_coverage_tile(int tile_x, int tile_y, StrokeRasterStats& stats);
  void store_coverage_tile(int tile_x, int tile_y, StrokeRasterStats& stats);
  void rasterize(const RibbonPrimitiveBatch& primitives, StrokeRasterStats& stats);
  void present_region(int x, int y, int width, int height);
  void present_tiles(const TileFlags& tiles);
  void compose_visible_tile(int tile_x, int tile_y, std::uint16_t color, StrokeRasterStats& stats,
                            TileUndoHistory* history = nullptr);
  void compose_committed_tile(int tile_x, int tile_y, std::uint16_t color, StrokeRasterStats& stats,
                              TileUndoHistory* history);

  std::span<std::uint16_t> committed_;
  std::span<std::uint16_t> visible_;
  std::span<std::uint8_t> active_coverage_;
  DisplayBackend* display_ = nullptr;
  bool valid_ = false;
  RibbonPrimitiveBatch provisional_;
  TileFlags touched_{};
  CoverageTile coverage_{0, 0};
  std::array<std::uint16_t, kTileSize * kTileSize> working_{};
};

}  // namespace tinydraw
