#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"

namespace tinydraw {

inline constexpr int kTileSize = 64;

class CoverageTile {
 public:
  CoverageTile(int origin_x, int origin_y, int width = kTileSize, int height = kTileSize);

  void reset(int origin_x, int origin_y, int width = kTileSize, int height = kTileSize);
  void clear();
  void union_coverage(int x, int y, std::uint8_t coverage);
  [[nodiscard]] std::uint8_t coverage_at(int x, int y) const;

  void rasterize_circle(Point center, float radius);
  void rasterize_convex(std::span<const Point> polygon);

  [[nodiscard]] int origin_x() const { return origin_x_; }
  [[nodiscard]] int origin_y() const { return origin_y_; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] const std::uint8_t* data() const { return coverage_.data(); }

 private:
  [[nodiscard]] bool contains(int x, int y) const;
  [[nodiscard]] std::size_t index_of(int x, int y) const;

  int origin_x_;
  int origin_y_;
  int width_;
  int height_;
  std::array<std::uint8_t, kTileSize * kTileSize> coverage_{};
};

// Blend directly in stored sRGB-like RGB565 channel space. The destination is
// a contiguous tile with coverage.width() × coverage.height() pixels.
void composite_rgb565(const CoverageTile& coverage, std::uint16_t source,
                      std::span<std::uint16_t> destination);

}  // namespace tinydraw
