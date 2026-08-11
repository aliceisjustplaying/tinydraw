#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/world_canvas.h"

namespace tinydraw {

// A row-major canvas shadow with a tile-major flash seam. Two 32x32 RGB565
// tiles occupy one 4 KiB erase sector, so saving remains proportional to changed ink.
class DrawingSnapshot {
 public:
  static constexpr int kTileSize = 32;
  static constexpr int kTilesAcross = WorldCanvas::kWidth / kTileSize;
  static constexpr int kTilesDown = WorldCanvas::kHeight / kTileSize;
  static constexpr std::size_t kTilePixels = static_cast<std::size_t>(kTileSize * kTileSize);
  static constexpr std::size_t kTileCount = static_cast<std::size_t>(kTilesAcross * kTilesDown);
  static constexpr std::size_t kTilesPerSector = 2U;
  static constexpr std::size_t kSectorPixels = kTilePixels * kTilesPerSector;
  static constexpr std::size_t kSectorBytes = kSectorPixels * sizeof(std::uint16_t);
  static constexpr std::size_t kSectorCount = kTileCount / kTilesPerSector;
  static constexpr std::size_t kRequiredPixels = kTileCount * kTilePixels;

  explicit DrawingSnapshot(std::span<std::uint16_t> storage);

  [[nodiscard]] bool valid() const { return valid_; }
  void include_all();
  void include_viewport(ViewOrigin origin);
  void include_segment(Point from, Point to, float radius, ViewOrigin origin);
  [[nodiscard]] std::size_t capture(std::span<const std::uint16_t> world, ViewOrigin origin);
  [[nodiscard]] bool restore(std::span<std::uint16_t> world, ViewOrigin& origin) const;

  void initialize_blank();
  [[nodiscard]] bool copy_sector(std::size_t index, std::span<std::uint16_t> output) const;
  [[nodiscard]] bool load_sector(std::size_t index, std::span<const std::uint16_t> input);
  [[nodiscard]] bool sector_matches(std::size_t index,
                                    std::span<const std::uint16_t> serialized) const;
  [[nodiscard]] bool tile_included(std::size_t index) const;
  [[nodiscard]] bool sector_pending(std::size_t index) const;
  [[nodiscard]] std::size_t pending_sector_count() const;
  void acknowledge_sector(std::size_t index);

  [[nodiscard]] ViewOrigin origin() const { return origin_; }
  void load_origin(ViewOrigin origin);
  [[nodiscard]] bool metadata_pending() const { return metadata_pending_; }
  void acknowledge_metadata() { metadata_pending_ = false; }

 private:
  void include_world_rect(int x0, int y0, int x1, int y1);

  std::span<std::uint16_t> storage_;
  std::array<bool, kTileCount> included_tiles_{};
  std::array<bool, kSectorCount> pending_sectors_{};
  ViewOrigin origin_{kCanvasWidth / 2, kCanvasHeight / 2};
  bool metadata_pending_ = false;
  bool valid_ = false;
};

static_assert(DrawingSnapshot::kRequiredPixels == WorldCanvas::kRequiredPixels);
static_assert(DrawingSnapshot::kSectorBytes == 4096U);

}  // namespace tinydraw
