#include "tinydraw/graphics/viewport_updates.h"

#include <algorithm>

namespace tinydraw {
namespace {

constexpr std::size_t kViewportPixels = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);

bool tile_changed(std::span<const std::uint16_t> displayed, std::span<const std::uint16_t> next,
                  int x0, int y0, int x1, int y1) {
  for (int y = y0; y < y1; ++y) {
    const auto row = static_cast<std::size_t>(y * kCanvasWidth);
    for (int x = x0; x < x1; ++x) {
      const auto index = row + static_cast<std::size_t>(x);
      if (displayed[index] != next[index]) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

ViewportUpdateStats plan_viewport_updates(std::span<const std::uint16_t> displayed,
                                          std::span<const std::uint16_t> next, int bottom,
                                          std::span<Rect> regions) {
  ViewportUpdateStats stats;
  if (displayed.size() < kViewportPixels || next.size() < kViewportPixels || bottom < 0 ||
      bottom > kCanvasHeight || regions.empty()) {
    return stats;
  }

  for (int y = 0; y < bottom; y += kTileSize) {
    const int y1 = std::min(y + kTileSize, bottom);
    int run_start = -1;
    for (int x = 0; x <= kCanvasWidth; x += kTileSize) {
      const bool changed =
          x < kCanvasWidth &&
          tile_changed(displayed, next, x, y, std::min(x + kTileSize, kCanvasWidth), y1);
      if (changed && run_start < 0) {
        run_start = x;
      }
      if ((!changed || x >= kCanvasWidth) && run_start >= 0) {
        const int run_end = std::min(x, kCanvasWidth);
        stats.pixels += static_cast<std::size_t>((run_end - run_start) * (y1 - y));
        if (stats.regions > 0U && regions[stats.regions - 1U].x0 == run_start &&
            regions[stats.regions - 1U].x1 == run_end && regions[stats.regions - 1U].y1 == y) {
          regions[stats.regions - 1U].y1 = y1;
        } else {
          if (stats.regions >= regions.size()) {
            return stats;
          }
          regions[stats.regions++] = {.x0 = run_start, .y0 = y, .x1 = run_end, .y1 = y1};
        }
        run_start = -1;
      }
    }
  }
  stats.complete = true;
  return stats;
}

void sync_viewport_updates(std::span<const std::uint16_t> source,
                           std::span<std::uint16_t> destination, std::span<const Rect> regions) {
  if (source.size() < kViewportPixels || destination.size() < kViewportPixels) {
    return;
  }
  for (const Rect region : regions) {
    if (region.x0 < 0 || region.y0 < 0 || region.x1 > kCanvasWidth || region.y1 > kCanvasHeight ||
        region.x0 >= region.x1 || region.y0 >= region.y1) {
      continue;
    }
    for (int y = region.y0; y < region.y1; ++y) {
      const auto offset = static_cast<std::size_t>(y * kCanvasWidth + region.x0);
      std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(offset), region.x1 - region.x0,
                  destination.begin() + static_cast<std::ptrdiff_t>(offset));
    }
  }
}

}  // namespace tinydraw
