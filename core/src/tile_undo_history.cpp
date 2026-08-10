#include "tinydraw/graphics/tile_undo_history.h"

#include <algorithm>
#include <cassert>

namespace tinydraw {
namespace {

constexpr std::size_t kCanvasPixels = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);

}  // namespace

TileUndoHistory::TileUndoHistory(std::span<std::uint16_t> storage) : storage_(storage) {
  valid_ = storage_.size() >= kRequiredPixels;
  assert(valid_);
}

void TileUndoHistory::begin_entry() {
  if (!valid_ || entry_open_) {
    return;
  }
  entry_open_ = true;
  entry_has_tiles_ = false;
}

void TileUndoHistory::capture_tile(int x, int y, int width, int height,
                                   std::span<const std::uint16_t> packed_pixels) {
  if (!valid_ || !entry_open_ || x < 0 || y < 0 || x >= kCanvasWidth || y >= kCanvasHeight ||
      x % kTileSize != 0 || y % kTileSize != 0 || width != std::min(kTileSize, kCanvasWidth - x) ||
      height != std::min(kTileSize, kCanvasHeight - y) ||
      packed_pixels.size() < static_cast<std::size_t>(width * height)) {
    return;
  }

  if (!entry_has_tiles_) {
    if (count_ == kMaxEntries) {
      --count_;
    }
    tiles_[next_].fill(false);
    entry_has_tiles_ = true;
  }
  const int tile_index = y / kTileSize * kTilesAcross + x / kTileSize;
  if (tiles_[next_][static_cast<std::size_t>(tile_index)]) {
    return;
  }

  auto destination = tile_storage(next_, tile_index);
  std::copy_n(packed_pixels.begin(), static_cast<std::size_t>(width * height), destination.begin());
  tiles_[next_][static_cast<std::size_t>(tile_index)] = true;
}

void TileUndoHistory::capture_canvas(std::span<const std::uint16_t> canvas) {
  if (!valid_ || !entry_open_ || canvas.size() < kCanvasPixels) {
    return;
  }
  if (!entry_has_tiles_) {
    if (count_ == kMaxEntries) {
      --count_;
    }
    tiles_[next_].fill(false);
    entry_has_tiles_ = true;
  }
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    const int x = tile_index % kTilesAcross * kTileSize;
    const int y = tile_index / kTilesAcross * kTileSize;
    const int width = std::min(kTileSize, kCanvasWidth - x);
    const int height = std::min(kTileSize, kCanvasHeight - y);
    auto destination = tile_storage(next_, tile_index);
    for (int row = 0; row < height; ++row) {
      std::copy_n(canvas.begin() + (y + row) * kCanvasWidth + x, width,
                  destination.begin() + row * width);
    }
    tiles_[next_][static_cast<std::size_t>(tile_index)] = true;
  }
}

std::uint32_t TileUndoHistory::commit_entry() {
  if (!valid_ || !entry_open_) {
    return 0U;
  }
  entry_open_ = false;
  if (!entry_has_tiles_) {
    return 0U;
  }
  entry_has_tiles_ = false;

  const auto tile_count =
      static_cast<std::uint32_t>(std::count(tiles_[next_].begin(), tiles_[next_].end(), true));
  next_ = (next_ + 1U) % kMaxEntries;
  count_ = std::min(count_ + 1U, kMaxEntries);
  return tile_count;
}

void TileUndoHistory::clear() {
  count_ = 0U;
  next_ = 0U;
  entry_open_ = false;
  entry_has_tiles_ = false;
  for (auto& flags : tiles_) {
    flags.fill(false);
  }
}

TileUndoStats TileUndoHistory::undo(std::span<std::uint16_t> committed,
                                    std::span<std::uint16_t> visible, DisplayBackend* display) {
  TileUndoStats stats;
  if (!valid_ || entry_open_ || count_ == 0U || committed.size() < kCanvasPixels ||
      (!visible.empty() && visible.size() < kCanvasPixels)) {
    return stats;
  }

  next_ = (next_ + kMaxEntries - 1U) % kMaxEntries;
  for (int tile_index = 0; tile_index < kTileCount; ++tile_index) {
    if (!tiles_[next_][static_cast<std::size_t>(tile_index)]) {
      continue;
    }
    const int x = tile_index % kTilesAcross * kTileSize;
    const int y = tile_index / kTilesAcross * kTileSize;
    const int width = std::min(kTileSize, kCanvasWidth - x);
    const int height = std::min(kTileSize, kCanvasHeight - y);
    const auto source = tile_storage(next_, tile_index);
    const auto pixel_count = static_cast<std::size_t>(width * height);
    std::copy_n(source.begin(), pixel_count, working_.begin());
    for (int row = 0; row < height; ++row) {
      const auto working_row = working_.begin() + row * width;
      const auto canvas_row = committed.begin() + (y + row) * kCanvasWidth + x;
      std::copy_n(working_row, width, canvas_row);
      if (!visible.empty()) {
        std::copy_n(working_row, width, visible.begin() + (y + row) * kCanvasWidth + x);
      }
    }
    const auto bytes = static_cast<std::uint32_t>(width * height) * sizeof(std::uint16_t);
    ++stats.tiles_restored;
    stats.history_bytes_read += bytes;
    stats.canvas_bytes_written += bytes;
  }

  if (display != nullptr) {
    const std::span<const std::uint16_t> source = visible.empty() ? committed : visible;
    for (int tile_y = 0; tile_y < kTilesDown; ++tile_y) {
      int tile_x = 0;
      while (tile_x < kTilesAcross) {
        while (tile_x < kTilesAcross &&
               !tiles_[next_][static_cast<std::size_t>(tile_y * kTilesAcross + tile_x)]) {
          ++tile_x;
        }
        const int first_tile_x = tile_x;
        while (tile_x < kTilesAcross &&
               tiles_[next_][static_cast<std::size_t>(tile_y * kTilesAcross + tile_x)]) {
          ++tile_x;
        }
        if (first_tile_x == tile_x) {
          continue;
        }
        const int x = first_tile_x * kTileSize;
        const int y = tile_y * kTileSize;
        const int right = std::min(tile_x * kTileSize, kCanvasWidth);
        const int height = std::min(kTileSize, kCanvasHeight - y);
        const int width = right - x;
        display->push_rect(x, y, width, height,
                           source.data() + static_cast<std::size_t>(y * kCanvasWidth + x),
                           kCanvasWidth);
        stats.display_bytes +=
            static_cast<std::uint32_t>(width * height) * sizeof(std::uint16_t);
      }
    }
  }

  tiles_[next_].fill(false);
  --count_;
  return stats;
}

std::span<std::uint16_t> TileUndoHistory::tile_storage(std::size_t entry, int tile_index) {
  const std::size_t offset =
      (entry * kTileCount + static_cast<std::size_t>(tile_index)) * kTilePixels;
  return storage_.subspan(offset, kTilePixels);
}

std::span<const std::uint16_t> TileUndoHistory::tile_storage(std::size_t entry,
                                                             int tile_index) const {
  const std::size_t offset =
      (entry * kTileCount + static_cast<std::size_t>(tile_index)) * kTilePixels;
  return storage_.subspan(offset, kTilePixels);
}

}  // namespace tinydraw
