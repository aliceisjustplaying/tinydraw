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
    if (radius < 0) {
      return;
    }
    const int squared_radius = radius * radius;
    int half_width = 0;
    // Walking from the cap toward the center makes the accepted half-width
    // monotonic. Every row becomes one clipped contiguous fill instead of a
    // bounding-square scan with a branch and bounds check for every pixel.
    for (int offset_y = radius; offset_y >= 0; --offset_y) {
      while (half_width < radius &&
             (half_width + 1) * (half_width + 1) + offset_y * offset_y <= squared_radius) {
        ++half_width;
      }
      horizontal_span(center_x - half_width, center_x + half_width + 1, center_y - offset_y, color);
      if (offset_y != 0) {
        horizontal_span(center_x - half_width, center_x + half_width + 1, center_y + offset_y,
                        color);
      }
    }
  }

  void rounded(UiPixelRect bounds, int radius, std::uint16_t color) {
    if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0 || radius < 0) {
      return;
    }
    radius = std::min({radius, (bounds.x1 - bounds.x0 - 1) / 2, (bounds.y1 - bounds.y0 - 1) / 2});
    const int squared_radius = radius * radius;
    int half_width = 0;
    for (int offset_y = radius; offset_y >= 0; --offset_y) {
      while (half_width < radius &&
             (half_width + 1) * (half_width + 1) + offset_y * offset_y <= squared_radius) {
        ++half_width;
      }
      const int x0 = bounds.x0 + radius - half_width;
      const int x1 = bounds.x1 - radius + half_width;
      horizontal_span(x0, x1, bounds.y0 + radius - offset_y, color);
      const int bottom_y = bounds.y1 - radius - 1 + offset_y;
      if (bottom_y != bounds.y0 + radius - offset_y) {
        horizontal_span(x0, x1, bottom_y, color);
      }
    }
    for (int y = bounds.y0 + radius + 1; y < bounds.y1 - radius - 1; ++y) {
      horizontal_span(bounds.x0, bounds.x1, y, color);
    }
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
  void horizontal_span(int x0, int x1, int y, std::uint16_t color) {
    y -= origin_y_;
    if (y < 0 || y >= height_) {
      return;
    }
    x0 = std::clamp(x0 - origin_x_, 0, width_);
    x1 = std::clamp(x1 - origin_x_, x0, width_);
    const auto start = pixels_.begin() + static_cast<std::ptrdiff_t>(y) * width_ + x0;
    std::fill(start, start + (x1 - x0), color);
  }

  [[nodiscard]] static std::array<std::uint8_t, 7> glyph_rows(char character) {
    switch (character) {
      case 'A':
        return {0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U};
      case 'B':
        return {0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU};
      case 'C':
        return {0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU};
      case 'D':
        return {0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU};
      case 'E':
        return {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU};
      case 'F':
        return {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U};
      case 'G':
        return {0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0EU};
      case 'I':
        return {0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x1FU};
      case 'L':
        return {0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1FU};
      case 'M':
        return {0x11U, 0x1BU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U};
      case 'N':
        return {0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U};
      case 'O':
        return {0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU};
      case 'P':
        return {0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U};
      case 'R':
        return {0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U};
      case 'S':
        return {0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU};
      case 'T':
        return {0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U};
      case 'U':
        return {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU};
      case 'V':
        return {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U};
      case 'W':
        return {0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x1BU, 0x11U};
      case 'X':
        return {0x11U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U};
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
      case '-':
        return {0x00U, 0x00U, 0x00U, 0x1FU, 0x00U, 0x00U, 0x00U};
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
