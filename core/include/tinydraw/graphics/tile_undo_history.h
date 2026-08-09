#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/coverage_tile.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw {

struct TileUndoStats {
  std::uint32_t tiles_restored = 0;
  std::uint32_t history_bytes_read = 0;
  std::uint32_t canvas_bytes_written = 0;
  std::uint32_t display_bytes = 0;
};

// Ten deterministic undo slots. Each slot has room for every 64x64 tile, but
// only tiles changed by an operation are copied. This keeps capture and restore
// traffic proportional to dirty area without a variable-size arena.
class TileUndoHistory {
 public:
  static constexpr std::size_t kMaxEntries = 10U;
  static constexpr int kTilesAcross = (kCanvasWidth + kTileSize - 1) / kTileSize;
  static constexpr int kTilesDown = (kCanvasHeight + kTileSize - 1) / kTileSize;
  static constexpr int kTileCount = kTilesAcross * kTilesDown;
  static constexpr std::size_t kTilePixels = static_cast<std::size_t>(kTileSize * kTileSize);
  static constexpr std::size_t kRequiredPixels = kMaxEntries * kTileCount * kTilePixels;

  explicit TileUndoHistory(std::span<std::uint16_t> storage);

  [[nodiscard]] bool valid() const { return valid_; }
  [[nodiscard]] bool can_undo() const { return count_ > 0U; }
  [[nodiscard]] std::size_t entry_count() const { return count_; }

  void begin_entry();
  void capture_tile(int x, int y, int width, int height,
                    std::span<const std::uint16_t> packed_pixels);
  void capture_canvas(std::span<const std::uint16_t> canvas);
  [[nodiscard]] std::uint32_t commit_entry();
  void clear();

  [[nodiscard]] TileUndoStats undo(std::span<std::uint16_t> committed,
                                   std::span<std::uint16_t> visible = {},
                                   DisplayBackend* display = nullptr);

 private:
  using TileFlags = std::array<bool, kTileCount>;

  [[nodiscard]] std::span<std::uint16_t> tile_storage(std::size_t entry, int tile_index);
  [[nodiscard]] std::span<const std::uint16_t> tile_storage(std::size_t entry,
                                                            int tile_index) const;

  std::span<std::uint16_t> storage_;
  std::array<std::uint16_t, kTilePixels> working_{};
  std::array<TileFlags, kMaxEntries> tiles_{};
  std::size_t next_ = 0U;
  std::size_t count_ = 0U;
  bool entry_open_ = false;
  bool entry_has_tiles_ = false;
  bool valid_ = false;
};

}  // namespace tinydraw
