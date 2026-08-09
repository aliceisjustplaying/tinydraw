#include "tinydraw/ui/toolbar.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace tinydraw {
namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kInk = 0x2104U;
constexpr std::uint16_t kMuted = 0x8410U;
constexpr std::uint16_t kBorder = 0xDEDBU;
constexpr std::uint16_t kShadow = 0xBDF7U;
constexpr std::uint16_t kSelected = 0x349FU;
constexpr int kMainTop = 386;
constexpr int kMainBottom = 442;
constexpr int kAuxTop = 326;
constexpr int kAuxBottom = 378;

void set_pixel(std::span<std::uint16_t> canvas, int width, int height, int x, int y,
               std::uint16_t color) {
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return;
  }
  canvas[static_cast<std::size_t>(y * width + x)] = color;
}

void fill_rect(std::span<std::uint16_t> canvas, int width, int height, int x0, int y0, int x1,
               int y1, std::uint16_t color) {
  for (int y = std::max(0, y0); y < std::min(height, y1); ++y) {
    for (int x = std::max(0, x0); x < std::min(width, x1); ++x) {
      set_pixel(canvas, width, height, x, y, color);
    }
  }
}

void fill_circle(std::span<std::uint16_t> canvas, int width, int height, int center_x, int center_y,
                 int radius, std::uint16_t color) {
  const int radius_squared = radius * radius;
  for (int y = center_y - radius; y <= center_y + radius; ++y) {
    for (int x = center_x - radius; x <= center_x + radius; ++x) {
      const int dx = x - center_x;
      const int dy = y - center_y;
      if (dx * dx + dy * dy <= radius_squared) {
        set_pixel(canvas, width, height, x, y, color);
      }
    }
  }
}

void rounded_rect(std::span<std::uint16_t> canvas, int width, int height, int x0, int y0, int x1,
                  int y1, int radius, std::uint16_t color) {
  fill_rect(canvas, width, height, x0 + radius, y0, x1 - radius, y1, color);
  fill_rect(canvas, width, height, x0, y0 + radius, x1, y1 - radius, color);
  fill_circle(canvas, width, height, x0 + radius, y0 + radius, radius, color);
  fill_circle(canvas, width, height, x1 - radius - 1, y0 + radius, radius, color);
  fill_circle(canvas, width, height, x0 + radius, y1 - radius - 1, radius, color);
  fill_circle(canvas, width, height, x1 - radius - 1, y1 - radius - 1, radius, color);
}

void line(std::span<std::uint16_t> canvas, int width, int height, int x0, int y0, int x1, int y1,
          std::uint16_t color, int thickness = 2) {
  const int dx = std::abs(x1 - x0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int step_y = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    fill_circle(canvas, width, height, x0, y0, thickness, color);
    if (x0 == x1 && y0 == y1) {
      return;
    }
    const int twice_error = 2 * error;
    if (twice_error >= dy) {
      error += dy;
      x0 += step_x;
    }
    if (twice_error <= dx) {
      error += dx;
      y0 += step_y;
    }
  }
}

bool inside(Point point, float x0, float y0, float x1, float y1) {
  return point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1;
}

void draw_dock(std::span<std::uint16_t> canvas, int width, int height, int x0, int y0, int x1,
               int y1) {
  rounded_rect(canvas, width, height, x0 + 2, y0 + 3, x1 + 2, y1 + 3, 10, kShadow);
  rounded_rect(canvas, width, height, x0 - 1, y0 - 1, x1 + 1, y1 + 1, 10, kBorder);
  rounded_rect(canvas, width, height, x0, y0, x1, y1, 9, kWhite);
}

void draw_pen(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 27, 424, 43, 403, color, 2);
  line(canvas, width, height, 27, 424, 31, 412, color, 2);
  line(canvas, width, height, 31, 412, 43, 403, color, 2);
}

void draw_eraser(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 73, 421, 89, 403, color, 2);
  line(canvas, width, height, 89, 403, 97, 411, color, 2);
  line(canvas, width, height, 97, 411, 81, 429, color, 2);
  line(canvas, width, height, 81, 429, 73, 421, color, 2);
  line(canvas, width, height, 77, 425, 85, 429, color, 2);
}

}  // namespace

bool toolbar_contains(Point point) {
  return inside(point, 8.0F, static_cast<float>(kMainTop), 318.0F,
                static_cast<float>(kMainBottom)) ||
         inside(point, 8.0F, static_cast<float>(kAuxTop), 112.0F, static_cast<float>(kAuxBottom));
}

ToolbarAction toolbar_action_at(Point point) {
  if (inside(point, 14.0F, 392.0F, 60.0F, 436.0F)) {
    return ToolbarAction::kSelectPen;
  }
  if (inside(point, 62.0F, 392.0F, 108.0F, 436.0F)) {
    return ToolbarAction::kSelectEraser;
  }
  constexpr std::array actions{ToolbarAction::kSelectBlack, ToolbarAction::kSelectBlue,
                               ToolbarAction::kSelectRed, ToolbarAction::kSelectGreen};
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const float x0 = 112.0F + static_cast<float>(index) * 36.0F;
    if (inside(point, x0, 392.0F, x0 + 36.0F, 436.0F)) {
      return actions[index];
    }
  }
  if (inside(point, 262.0F, 392.0F, 310.0F, 436.0F)) {
    return ToolbarAction::kCycleSize;
  }
  if (inside(point, 14.0F, 330.0F, 60.0F, 374.0F)) {
    return ToolbarAction::kUndo;
  }
  if (inside(point, 62.0F, 330.0F, 108.0F, 374.0F)) {
    return ToolbarAction::kNewDrawing;
  }
  return ToolbarAction::kNone;
}

std::uint16_t rgb565(InkColor color) {
  switch (color) {
    case InkColor::kBlack:
      return 0x0000U;
    case InkColor::kBlue:
      return 0x001FU;
    case InkColor::kRed:
      return 0xF800U;
    case InkColor::kGreen:
      return 0x07E0U;
  }
  return 0x0000U;
}

float brush_size(PenSize size) {
  switch (size) {
    case PenSize::kSmall:
      return 3.5F;
    case PenSize::kMedium:
      return 6.0F;
    case PenSize::kLarge:
      return 11.0F;
  }
  return 6.0F;
}

PenSize next_pen_size(PenSize size) {
  switch (size) {
    case PenSize::kSmall:
      return PenSize::kMedium;
    case PenSize::kMedium:
      return PenSize::kLarge;
    case PenSize::kLarge:
      return PenSize::kSmall;
  }
  return PenSize::kMedium;
}

void draw_toolbar(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state) {
  if (width <= 0 || height <= 0 || canvas.size() < static_cast<std::size_t>(width * height)) {
    return;
  }
  draw_dock(canvas, width, height, 8, kMainTop, 318, kMainBottom);
  draw_dock(canvas, width, height, 8, kAuxTop, 112, kAuxBottom);

  if (state.tool == DrawingTool::kPen) {
    rounded_rect(canvas, width, height, 14, 392, 60, 436, 8, kSelected);
  } else {
    rounded_rect(canvas, width, height, 62, 392, 108, 436, 8, kSelected);
  }
  draw_pen(canvas, width, height, state.tool == DrawingTool::kPen ? kWhite : kInk);
  draw_eraser(canvas, width, height, state.tool == DrawingTool::kEraser ? kWhite : kInk);

  constexpr std::array colors{InkColor::kBlack, InkColor::kBlue, InkColor::kRed, InkColor::kGreen};
  constexpr std::array centers{130, 166, 202, 238};
  for (std::size_t index = 0; index < colors.size(); ++index) {
    if (state.color == colors[index]) {
      fill_circle(canvas, width, height, centers[index], 414, 12, kSelected);
      fill_circle(canvas, width, height, centers[index], 414, 9, kWhite);
    }
    fill_circle(canvas, width, height, centers[index], 414, 7, rgb565(colors[index]));
  }

  constexpr std::array sizes{PenSize::kSmall, PenSize::kMedium, PenSize::kLarge};
  constexpr std::array size_centers{275, 286, 298};
  constexpr std::array radii{2, 4, 6};
  for (std::size_t index = 0; index < sizes.size(); ++index) {
    fill_circle(canvas, width, height, size_centers[index], 414, radii[index],
                state.size == sizes[index] ? kSelected : kMuted);
  }

  const std::uint16_t undo_color = state.can_undo ? kInk : kMuted;
  line(canvas, width, height, 29, 352, 47, 352, undo_color, 2);
  line(canvas, width, height, 29, 352, 36, 345, undo_color, 2);
  line(canvas, width, height, 29, 352, 36, 359, undo_color, 2);

  // New drawing: page outline and plus sign.
  line(canvas, width, height, 73, 339, 93, 339, kInk, 1);
  line(canvas, width, height, 93, 339, 93, 365, kInk, 1);
  line(canvas, width, height, 93, 365, 73, 365, kInk, 1);
  line(canvas, width, height, 73, 365, 73, 339, kInk, 1);
  line(canvas, width, height, 79, 352, 87, 352, kInk, 1);
  line(canvas, width, height, 83, 348, 83, 356, kInk, 1);
}

}  // namespace tinydraw
