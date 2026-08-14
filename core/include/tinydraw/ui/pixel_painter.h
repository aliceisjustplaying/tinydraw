#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace tinydraw {

struct UiPixelRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
};

class PixelPainter {
 public:
  PixelPainter(std::span<std::uint16_t> pixels, int width, int height)
      : pixels_(pixels), width_(width), height_(height) {}

  void pixel(int x, int y, std::uint16_t color) {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
      pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
              static_cast<std::size_t>(x)] = color;
    }
  }

  void rect(UiPixelRect bounds, std::uint16_t color) {
    const int x0 = std::clamp(bounds.x0, 0, width_);
    const int x1 = std::clamp(bounds.x1, x0, width_);
    for (int y = std::clamp(bounds.y0, 0, height_); y < std::clamp(bounds.y1, 0, height_); ++y) {
      const auto start = pixels_.begin() + static_cast<std::ptrdiff_t>(y) * width_ + x0;
      std::fill(start, start + (x1 - x0), color);
    }
  }

  void circle(int center_x, int center_y, int radius, std::uint16_t color) {
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
      for (int x = center_x - radius; x <= center_x + radius; ++x) {
        const int dx = x - center_x;
        const int dy = y - center_y;
        if (dx * dx + dy * dy <= radius * radius) {
          pixel(x, y, color);
        }
      }
    }
  }

  void rounded(UiPixelRect bounds, int radius, std::uint16_t color) {
    rect({bounds.x0 + radius, bounds.y0, bounds.x1 - radius, bounds.y1}, color);
    rect({bounds.x0, bounds.y0 + radius, bounds.x1, bounds.y1 - radius}, color);
    circle(bounds.x0 + radius, bounds.y0 + radius, radius, color);
    circle(bounds.x1 - radius - 1, bounds.y0 + radius, radius, color);
    circle(bounds.x0 + radius, bounds.y1 - radius - 1, radius, color);
    circle(bounds.x1 - radius - 1, bounds.y1 - radius - 1, radius, color);
  }

  void line(UiPixelRect ends, std::uint16_t color, int thickness = 1) {
    int x = ends.x0;
    int y = ends.y0;
    const int dx = std::abs(ends.x1 - x);
    const int step_x = x < ends.x1 ? 1 : -1;
    const int dy = -std::abs(ends.y1 - y);
    const int step_y = y < ends.y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
      circle(x, y, thickness, color);
      if (x == ends.x1 && y == ends.y1) {
        return;
      }
      const int twice = 2 * error;
      if (twice >= dy) {
        error += dy;
        x += step_x;
      }
      if (twice <= dx) {
        error += dx;
        y += step_y;
      }
    }
  }

 private:
  std::span<std::uint16_t> pixels_;
  int width_;
  int height_;
};

}  // namespace tinydraw
