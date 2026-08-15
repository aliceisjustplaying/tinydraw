#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

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
  // A painter whose surface's (0, 0) sits at logical coordinate
  // (origin_x, origin_y): callers keep drawing in absolute coordinates and
  // every primitive translates and clips. This lets full-frame drawing code
  // render into small offset scratch surfaces unchanged.
  PixelPainter(std::span<std::uint16_t> pixels, int width, int height, int origin_x, int origin_y)
      : pixels_(pixels), width_(width), height_(height), origin_x_(origin_x), origin_y_(origin_y) {}

  void pixel(int x, int y, std::uint16_t color) {
    x -= origin_x_;
    y -= origin_y_;
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
      pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
              static_cast<std::size_t>(x)] = color;
    }
  }

  void rect(UiPixelRect bounds, std::uint16_t color) {
    const int x0 = std::clamp(bounds.x0 - origin_x_, 0, width_);
    const int x1 = std::clamp(bounds.x1 - origin_x_, x0, width_);
    for (int y = std::clamp(bounds.y0 - origin_y_, 0, height_);
         y < std::clamp(bounds.y1 - origin_y_, 0, height_); ++y) {
      const auto start = pixels_.begin() + static_cast<std::ptrdiff_t>(y) * width_ + x0;
      std::fill(start, start + (x1 - x0), color);
    }
  }

  void circle(int center_x, int center_y, int radius, std::uint16_t color) {
    const int first_y = std::max(center_y - radius, origin_y_);
    const int last_y = std::min(center_y + radius, origin_y_ + height_ - 1);
    const int first_x = std::max(center_x - radius, origin_x_);
    const int last_x = std::min(center_x + radius, origin_x_ + width_ - 1);
    for (int y = first_y; y <= last_y; ++y) {
      for (int x = first_x; x <= last_x; ++x) {
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

  void text(int x, int y, std::string_view value, std::uint16_t color, int scale = 2,
            int scale_denominator = 1) {
    if (scale <= 0 || scale_denominator <= 0) {
      return;
    }
    for (const char character : value) {
      const auto rows = glyph_rows(character);
      for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
          if ((rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) != 0U) {
            rect({x + column * scale / scale_denominator, y + row * scale / scale_denominator,
                  x + (column + 1) * scale / scale_denominator,
                  y + (row + 1) * scale / scale_denominator},
                 color);
          }
        }
      }
      x += 6 * scale / scale_denominator;
    }
  }

 private:
  [[nodiscard]] static std::array<std::uint8_t, 7> glyph_rows(char character) {
    switch (character) {
      case 'A':
        return {0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U};
      case 'D':
        return {0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU};
      case 'E':
        return {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU};
      case 'G':
        return {0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0EU};
      case 'I':
        return {0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x1FU};
      case 'N':
        return {0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U};
      case 'O':
        return {0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU};
      case 'R':
        return {0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U};
      case 'S':
        return {0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU};
      case 'V':
        return {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U};
      case 'W':
        return {0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x1BU, 0x11U};
      case 'Y':
        return {0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U, 0x04U};
      case '0':
        return {0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU};
      case '1':
        return {0x04U, 0x0CU, 0x14U, 0x04U, 0x04U, 0x04U, 0x1FU};
      case '2':
        return {0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU};
      case '3':
        return {0x1EU, 0x01U, 0x01U, 0x0EU, 0x01U, 0x01U, 0x1EU};
      case '4':
        return {0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U};
      case '5':
        return {0x1FU, 0x10U, 0x10U, 0x1EU, 0x01U, 0x01U, 0x1EU};
      case '6':
        return {0x0EU, 0x10U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU};
      case '7':
        return {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U};
      case '8':
        return {0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU};
      case '9':
        return {0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x01U, 0x0EU};
      case '%':
        return {0x19U, 0x1AU, 0x04U, 0x04U, 0x0BU, 0x13U, 0x00U};
      case '?':
        return {0x0EU, 0x11U, 0x02U, 0x04U, 0x04U, 0x00U, 0x04U};
      default:
        return {};
    }
  }

  std::span<std::uint16_t> pixels_;
  int width_;
  int height_;
  int origin_x_ = 0;
  int origin_y_ = 0;
};

}  // namespace tinydraw
