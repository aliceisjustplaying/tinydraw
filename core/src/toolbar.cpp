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
constexpr int kMainTop = 374;
constexpr int kMainBottom = 444;
constexpr int kAuxTop = 296;
constexpr int kAuxBottom = 366;
constexpr int kPaletteTop = 218;
constexpr int kPaletteBottom = 288;

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
  rounded_rect(canvas, width, height, x0 + 2, y0 + 3, x1 + 2, y1 + 3, 11, kShadow);
  rounded_rect(canvas, width, height, x0 - 1, y0 - 1, x1 + 1, y1 + 1, 11, kBorder);
  rounded_rect(canvas, width, height, x0, y0, x1, y1, 10, kWhite);
}

void draw_pen(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 40, 423, 64, 394, color, 3);
  line(canvas, width, height, 40, 423, 45, 407, color, 3);
  line(canvas, width, height, 45, 407, 64, 394, color, 3);
}

void draw_eraser(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 124, 418, 146, 394, color, 3);
  line(canvas, width, height, 146, 394, 158, 406, color, 3);
  line(canvas, width, height, 158, 406, 136, 430, color, 3);
  line(canvas, width, height, 136, 430, 124, 418, color, 3);
  line(canvas, width, height, 130, 424, 142, 430, color, 3);
}

}  // namespace

bool toolbar_contains(Point point, const ToolbarState& state) {
  const bool fixed_dock =
      inside(point, 4.0F, static_cast<float>(kMainTop), 364.0F, static_cast<float>(kMainBottom)) ||
      inside(point, 4.0F, static_cast<float>(kAuxTop), 184.0F, static_cast<float>(kAuxBottom));
  return fixed_dock || ((state.colors_open || state.sizes_open) &&
                        inside(point, 4.0F, static_cast<float>(kPaletteTop), 364.0F,
                               static_cast<float>(kPaletteBottom)));
}

ToolbarAction toolbar_action_at(Point point, const ToolbarState& state) {
  if (state.sizes_open && inside(point, 8.0F, 224.0F, 360.0F, 282.0F)) {
    constexpr std::array actions{ToolbarAction::kSelectSmall, ToolbarAction::kSelectMedium,
                                 ToolbarAction::kSelectLarge};
    const auto index = static_cast<std::size_t>((point.x - 8.0F) / (352.0F / 3.0F));
    return actions[std::min(index, actions.size() - 1U)];
  }
  if (state.colors_open && inside(point, 8.0F, 224.0F, 360.0F, 282.0F)) {
    constexpr std::array actions{ToolbarAction::kSelectBlack, ToolbarAction::kSelectBlue,
                                 ToolbarAction::kSelectRed, ToolbarAction::kSelectGreen};
    const auto index = static_cast<std::size_t>((point.x - 8.0F) / 88.0F);
    return actions[std::min(index, actions.size() - 1U)];
  }
  if (inside(point, 8.0F, 380.0F, 96.0F, 440.0F)) {
    return ToolbarAction::kSelectPen;
  }
  if (inside(point, 96.0F, 380.0F, 184.0F, 440.0F)) {
    return ToolbarAction::kSelectEraser;
  }
  if (inside(point, 184.0F, 380.0F, 272.0F, 440.0F)) {
    return ToolbarAction::kToggleColors;
  }
  if (inside(point, 272.0F, 380.0F, 360.0F, 440.0F)) {
    return ToolbarAction::kToggleSizes;
  }
  if (inside(point, 8.0F, 302.0F, 96.0F, 360.0F)) {
    return ToolbarAction::kUndo;
  }
  if (inside(point, 96.0F, 302.0F, 180.0F, 360.0F)) {
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

void draw_toolbar(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state) {
  if (width <= 0 || height <= 0 || canvas.size() < static_cast<std::size_t>(width * height)) {
    return;
  }
  draw_dock(canvas, width, height, 4, kMainTop, 364, kMainBottom);
  draw_dock(canvas, width, height, 4, kAuxTop, 184, kAuxBottom);

  if (state.tool == DrawingTool::kPen) {
    rounded_rect(canvas, width, height, 8, 380, 96, 440, 9, kSelected);
  } else {
    rounded_rect(canvas, width, height, 96, 380, 184, 440, 9, kSelected);
  }
  draw_pen(canvas, width, height, state.tool == DrawingTool::kPen ? kWhite : kInk);
  draw_eraser(canvas, width, height, state.tool == DrawingTool::kEraser ? kWhite : kInk);

  fill_circle(canvas, width, height, 228, 410, 18, kSelected);
  fill_circle(canvas, width, height, 228, 410, 14, kWhite);
  fill_circle(canvas, width, height, 228, 410, 11, rgb565(state.color));

  constexpr std::array sizes{PenSize::kSmall, PenSize::kMedium, PenSize::kLarge};
  constexpr std::array size_centers{296, 316, 339};
  constexpr std::array radii{3, 6, 9};
  for (std::size_t index = 0; index < sizes.size(); ++index) {
    fill_circle(canvas, width, height, size_centers[index], 410, radii[index],
                state.size == sizes[index] ? kSelected : kMuted);
  }

  const std::uint16_t undo_color = state.can_undo ? kInk : kMuted;
  line(canvas, width, height, 34, 331, 70, 331, undo_color, 3);
  line(canvas, width, height, 34, 331, 46, 319, undo_color, 3);
  line(canvas, width, height, 34, 331, 46, 343, undo_color, 3);

  // New drawing: page outline and plus sign.
  line(canvas, width, height, 124, 313, 154, 313, kInk, 2);
  line(canvas, width, height, 154, 313, 154, 349, kInk, 2);
  line(canvas, width, height, 154, 349, 124, 349, kInk, 2);
  line(canvas, width, height, 124, 349, 124, 313, kInk, 2);
  line(canvas, width, height, 132, 331, 146, 331, kInk, 2);
  line(canvas, width, height, 139, 324, 139, 338, kInk, 2);

  if (!state.colors_open && !state.sizes_open) {
    return;
  }
  draw_dock(canvas, width, height, 4, kPaletteTop, 364, kPaletteBottom);
  if (state.colors_open) {
    constexpr std::array colors{InkColor::kBlack, InkColor::kBlue, InkColor::kRed,
                                InkColor::kGreen};
    constexpr std::array centers{52, 140, 228, 316};
    for (std::size_t index = 0; index < colors.size(); ++index) {
      if (state.color == colors[index]) {
        fill_circle(canvas, width, height, centers[index], 253, 21, kSelected);
        fill_circle(canvas, width, height, centers[index], 253, 18, kWhite);
      }
      fill_circle(canvas, width, height, centers[index], 253, 15, rgb565(colors[index]));
    }
    return;
  }

  constexpr std::array palette_sizes{PenSize::kSmall, PenSize::kMedium, PenSize::kLarge};
  constexpr std::array centers{66, 184, 302};
  constexpr std::array palette_radii{6, 11, 17};
  for (std::size_t index = 0; index < palette_sizes.size(); ++index) {
    if (state.size == palette_sizes[index]) {
      fill_circle(canvas, width, height, centers[index], 253, palette_radii[index] + 5, kSelected);
      fill_circle(canvas, width, height, centers[index], 253, palette_radii[index] + 2, kWhite);
    }
    fill_circle(canvas, width, height, centers[index], 253, palette_radii[index], kInk);
  }
}

}  // namespace tinydraw
