#include "tinydraw/storage/drawing_snapshot.h"

#include <algorithm>
#include <cmath>

namespace tinydraw {

DrawingSnapshot::DrawingSnapshot(std::span<std::uint16_t> storage) : storage_(storage) {
  valid_ = storage_.size() >= kRequiredPixels;
}

void DrawingSnapshot::include_all() { included_tiles_.fill(true); }

void DrawingSnapshot::include_viewport(ViewOrigin origin) {
  include_world_rect(origin.x, origin.y, origin.x + kCanvasWidth - 1, origin.y + kCanvasHeight - 1);
}

void DrawingSnapshot::include_segment(Point from, Point to, float radius, ViewOrigin origin) {
  const float safe_radius = std::max(0.0F, radius);
  const int x0 = static_cast<int>(
      std::floor(std::min(from.x, to.x) + static_cast<float>(origin.x) - safe_radius));
  const int y0 = static_cast<int>(
      std::floor(std::min(from.y, to.y) + static_cast<float>(origin.y) - safe_radius));
  const int x1 = static_cast<int>(
      std::ceil(std::max(from.x, to.x) + static_cast<float>(origin.x) + safe_radius));
  const int y1 = static_cast<int>(
      std::ceil(std::max(from.y, to.y) + static_cast<float>(origin.y) + safe_radius));
  include_world_rect(x0, y0, x1, y1);
}

std::size_t DrawingSnapshot::capture(std::span<const std::uint16_t> world, ViewOrigin origin) {
  if (!valid_ || world.size() < WorldCanvas::kRequiredPixels) {
    return 0U;
  }
  std::size_t captured = 0;
  for (std::size_t tile = 0; tile < kTileCount; ++tile) {
    if (!included_tiles_[tile]) {
      continue;
    }
    const int tile_x = static_cast<int>(tile % static_cast<std::size_t>(kTilesAcross));
    const int tile_y = static_cast<int>(tile / static_cast<std::size_t>(kTilesAcross));
    for (int row = 0; row < kTileSize; ++row) {
      const auto offset = static_cast<std::ptrdiff_t>(
          (tile_y * kTileSize + row) * WorldCanvas::kWidth + tile_x * kTileSize);
      std::copy_n(world.begin() + offset, kTileSize, storage_.begin() + offset);
    }
    pending_sectors_[tile / kTilesPerSector] = true;
    included_tiles_[tile] = false;
    ++captured;
  }
  if (origin != origin_) {
    origin_ = origin;
    metadata_pending_ = true;
  }
  return captured;
}

bool DrawingSnapshot::restore(std::span<std::uint16_t> world, ViewOrigin& origin) const {
  if (!valid_ || world.size() < WorldCanvas::kRequiredPixels) {
    return false;
  }
  std::copy_n(storage_.begin(), kRequiredPixels, world.begin());
  origin = origin_;
  return true;
}

void DrawingSnapshot::initialize_blank() {
  if (!valid_) {
    return;
  }
  std::fill_n(storage_.begin(), kRequiredPixels, 0xFFFFU);
  included_tiles_.fill(false);
  pending_sectors_.fill(false);
  origin_ = {kCanvasWidth / 2, kCanvasHeight / 2};
  metadata_pending_ = false;
}

bool DrawingSnapshot::copy_sector(std::size_t index, std::span<std::uint16_t> output) const {
  if (!valid_ || index >= kSectorCount || output.size() < kSectorPixels) {
    return false;
  }
  for (std::size_t tile_in_sector = 0; tile_in_sector < kTilesPerSector; ++tile_in_sector) {
    const std::size_t tile = index * kTilesPerSector + tile_in_sector;
    const int tile_x = static_cast<int>(tile % static_cast<std::size_t>(kTilesAcross));
    const int tile_y = static_cast<int>(tile / static_cast<std::size_t>(kTilesAcross));
    auto destination = output.begin() + static_cast<std::ptrdiff_t>(tile_in_sector * kTilePixels);
    for (int row = 0; row < kTileSize; ++row) {
      const auto source = storage_.begin() + static_cast<std::ptrdiff_t>(
                                                 (tile_y * kTileSize + row) * WorldCanvas::kWidth +
                                                 tile_x * kTileSize);
      std::copy_n(source, kTileSize, destination + static_cast<std::ptrdiff_t>(row * kTileSize));
    }
  }
  return true;
}

bool DrawingSnapshot::load_sector(std::size_t index, std::span<const std::uint16_t> input) {
  if (!valid_ || index >= kSectorCount || input.size() < kSectorPixels) {
    return false;
  }
  for (std::size_t tile_in_sector = 0; tile_in_sector < kTilesPerSector; ++tile_in_sector) {
    const std::size_t tile = index * kTilesPerSector + tile_in_sector;
    const int tile_x = static_cast<int>(tile % static_cast<std::size_t>(kTilesAcross));
    const int tile_y = static_cast<int>(tile / static_cast<std::size_t>(kTilesAcross));
    const auto source = input.begin() + static_cast<std::ptrdiff_t>(tile_in_sector * kTilePixels);
    for (int row = 0; row < kTileSize; ++row) {
      auto destination = storage_.begin() +
                         static_cast<std::ptrdiff_t>(
                             (tile_y * kTileSize + row) * WorldCanvas::kWidth + tile_x * kTileSize);
      std::copy_n(source + static_cast<std::ptrdiff_t>(row * kTileSize), kTileSize, destination);
    }
  }
  return true;
}

bool DrawingSnapshot::sector_matches(std::size_t index,
                                     std::span<const std::uint16_t> serialized) const {
  if (!valid_ || index >= kSectorCount || serialized.size() < kSectorPixels) {
    return false;
  }
  for (std::size_t tile_in_sector = 0; tile_in_sector < kTilesPerSector; ++tile_in_sector) {
    const std::size_t tile = index * kTilesPerSector + tile_in_sector;
    const int tile_x = static_cast<int>(tile % static_cast<std::size_t>(kTilesAcross));
    const int tile_y = static_cast<int>(tile / static_cast<std::size_t>(kTilesAcross));
    const auto expected =
        serialized.begin() + static_cast<std::ptrdiff_t>(tile_in_sector * kTilePixels);
    for (int row = 0; row < kTileSize; ++row) {
      const auto current = storage_.begin() + static_cast<std::ptrdiff_t>(
                                                  (tile_y * kTileSize + row) * WorldCanvas::kWidth +
                                                  tile_x * kTileSize);
      if (!std::equal(current, current + kTileSize,
                      expected + static_cast<std::ptrdiff_t>(row * kTileSize))) {
        return false;
      }
    }
  }
  return true;
}

bool DrawingSnapshot::tile_included(std::size_t index) const {
  return index < kTileCount && included_tiles_[index];
}

bool DrawingSnapshot::sector_pending(std::size_t index) const {
  return index < kSectorCount && pending_sectors_[index];
}

std::size_t DrawingSnapshot::pending_sector_count() const {
  return static_cast<std::size_t>(
      std::count(pending_sectors_.begin(), pending_sectors_.end(), true));
}

void DrawingSnapshot::acknowledge_sector(std::size_t index) {
  if (index < kSectorCount) {
    pending_sectors_[index] = false;
  }
}

void DrawingSnapshot::load_origin(ViewOrigin origin) {
  origin_ = {
      .x = std::clamp(origin.x, 0, WorldCanvas::kWidth - kCanvasWidth),
      .y = std::clamp(origin.y, 0, WorldCanvas::kHeight - kCanvasHeight),
  };
  metadata_pending_ = false;
}

void DrawingSnapshot::include_world_rect(int x0, int y0, int x1, int y1) {
  if (x1 < 0 || y1 < 0 || x0 >= WorldCanvas::kWidth || y0 >= WorldCanvas::kHeight) {
    return;
  }
  x0 = std::clamp(x0, 0, WorldCanvas::kWidth - 1);
  y0 = std::clamp(y0, 0, WorldCanvas::kHeight - 1);
  x1 = std::clamp(x1, 0, WorldCanvas::kWidth - 1);
  y1 = std::clamp(y1, 0, WorldCanvas::kHeight - 1);
  for (int tile_y = y0 / kTileSize; tile_y <= y1 / kTileSize; ++tile_y) {
    for (int tile_x = x0 / kTileSize; tile_x <= x1 / kTileSize; ++tile_x) {
      included_tiles_[static_cast<std::size_t>(tile_y * kTilesAcross + tile_x)] = true;
    }
  }
}

}  // namespace tinydraw
