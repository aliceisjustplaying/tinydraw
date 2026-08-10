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
constexpr int kPaletteTop = 296;
constexpr int kPaletteBottom = 366;
constexpr int kHitSlop = 8;
constexpr int kMainHitTop = kMainTop - kHitSlop;
constexpr int kPaletteHitTop = kPaletteTop - kHitSlop;

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

void draw_undo(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 24, 410, 54, 410, color, 2);
  line(canvas, width, height, 24, 410, 36, 398, color, 2);
  line(canvas, width, height, 24, 410, 36, 422, color, 2);
}

void draw_pen(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 87, 419, 104, 398, color, 1);
  line(canvas, width, height, 93, 425, 110, 404, color, 1);
  line(canvas, width, height, 104, 398, 110, 404, color, 1);
  line(canvas, width, height, 87, 419, 83, 429, color, 1);
  line(canvas, width, height, 93, 425, 83, 429, color, 1);
  line(canvas, width, height, 87, 419, 93, 425, color, 1);
}

void draw_eraser(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  // Keep the same two-part silhouette as tldraw's eraser: a tilted block plus
  // the exposed lower rubber, separated across the short axis.
  line(canvas, width, height, 147, 409, 158, 398, color, 1);
  line(canvas, width, height, 158, 398, 168, 408, color, 1);
  line(canvas, width, height, 168, 408, 157, 419, color, 1);
  line(canvas, width, height, 147, 409, 157, 419, color, 1);
  line(canvas, width, height, 147, 409, 143, 413, color, 1);
  line(canvas, width, height, 143, 413, 143, 416, color, 1);
  line(canvas, width, height, 143, 416, 149, 422, color, 1);
  line(canvas, width, height, 149, 422, 153, 423, color, 1);
  line(canvas, width, height, 153, 423, 157, 419, color, 1);
}

void draw_new(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 318, 393, 344, 393, color, 1);
  line(canvas, width, height, 344, 393, 344, 427, color, 1);
  line(canvas, width, height, 344, 427, 318, 427, color, 1);
  line(canvas, width, height, 318, 427, 318, 393, color, 1);
  line(canvas, width, height, 324, 410, 338, 410, color, 1);
  line(canvas, width, height, 331, 403, 331, 417, color, 1);
}

int size_radius(PenSize size) {
  switch (size) {
    case PenSize::kSmall:
      return 6;
    case PenSize::kMedium:
      return 10;
    case PenSize::kLarge:
      return 14;
    case PenSize::kExtraLarge:
      return 18;
  }
  return 9;
}

}  // namespace

bool toolbar_contains(Point point, const ToolbarState& state) {
  const bool main_row = inside(point, 0.0F, static_cast<float>(kMainHitTop),
                               static_cast<float>(kCanvasWidth), static_cast<float>(kCanvasHeight));
  return main_row || ((state.colors_open || state.sizes_open) &&
                      inside(point, 0.0F, static_cast<float>(kPaletteHitTop),
                             static_cast<float>(kCanvasWidth), static_cast<float>(kMainTop)));
}

ToolbarAction toolbar_action_at(Point point, const ToolbarState& state) {
  if ((state.sizes_open || state.colors_open) &&
      inside(point, 0.0F, static_cast<float>(kPaletteHitTop), static_cast<float>(kCanvasWidth),
             static_cast<float>(kMainTop))) {
    const auto index =
        std::min(static_cast<std::size_t>(point.x * 4.0F / kCanvasWidth), std::size_t{3});
    if (state.sizes_open) {
      constexpr std::array actions{ToolbarAction::kSelectSmall, ToolbarAction::kSelectMedium,
                                   ToolbarAction::kSelectLarge, ToolbarAction::kSelectExtraLarge};
      return actions[index];
    }
    constexpr std::array actions{ToolbarAction::kSelectBlack, ToolbarAction::kSelectBlue,
                                 ToolbarAction::kSelectRed, ToolbarAction::kSelectGreen};
    return actions[index];
  }
  if (!inside(point, 0.0F, static_cast<float>(kMainHitTop), static_cast<float>(kCanvasWidth),
              static_cast<float>(kCanvasHeight))) {
    return ToolbarAction::kNone;
  }
  constexpr std::array actions{
      ToolbarAction::kUndo,         ToolbarAction::kSelectPen,   ToolbarAction::kSelectEraser,
      ToolbarAction::kToggleColors, ToolbarAction::kToggleSizes, ToolbarAction::kNewDrawing,
  };
  const auto index =
      std::min(static_cast<std::size_t>(point.x * 6.0F / kCanvasWidth), std::size_t{5});
  return actions[index];
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
      return 5.0F;
    case PenSize::kMedium:
      return 8.0F;
    case PenSize::kLarge:
      return 13.0F;
    case PenSize::kExtraLarge:
      return 20.0F;
  }
  return 8.0F;
}

void draw_toolbar(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state) {
  if (width <= 0 || height <= 0 || canvas.size() < static_cast<std::size_t>(width * height)) {
    return;
  }
  draw_dock(canvas, width, height, 4, kMainTop, 364, kMainBottom);

  if (state.tool == DrawingTool::kPen) {
    rounded_rect(canvas, width, height, 67, 380, 126, 440, 9, kSelected);
  } else {
    rounded_rect(canvas, width, height, 126, 380, 184, 440, 9, kSelected);
  }

  draw_undo(canvas, width, height, state.can_undo ? kInk : kMuted);
  draw_pen(canvas, width, height, state.tool == DrawingTool::kPen ? kWhite : kInk);
  draw_eraser(canvas, width, height, state.tool == DrawingTool::kEraser ? kWhite : kInk);

  fill_circle(canvas, width, height, 213, 410, 18, kSelected);
  fill_circle(canvas, width, height, 213, 410, 15, kWhite);
  fill_circle(canvas, width, height, 213, 410, 13, rgb565(state.color));

  const int selected_size_radius = size_radius(state.size);
  fill_circle(canvas, width, height, 272, 410, selected_size_radius + 4, kSelected);
  fill_circle(canvas, width, height, 272, 410, selected_size_radius + 2, kWhite);
  fill_circle(canvas, width, height, 272, 410, selected_size_radius, kInk);
  draw_new(canvas, width, height, kInk);

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
        fill_circle(canvas, width, height, centers[index], 331, 21, kSelected);
        fill_circle(canvas, width, height, centers[index], 331, 18, kWhite);
      }
      fill_circle(canvas, width, height, centers[index], 331, 15, rgb565(colors[index]));
    }
    return;
  }

  constexpr std::array palette_sizes{PenSize::kSmall, PenSize::kMedium, PenSize::kLarge,
                                     PenSize::kExtraLarge};
  constexpr std::array centers{52, 140, 228, 316};
  for (std::size_t index = 0; index < palette_sizes.size(); ++index) {
    const int radius = size_radius(palette_sizes[index]);
    if (state.size == palette_sizes[index]) {
      fill_circle(canvas, width, height, centers[index], 331, radius + 5, kSelected);
      fill_circle(canvas, width, height, centers[index], 331, radius + 2, kWhite);
    }
    fill_circle(canvas, width, height, centers[index], 331, radius, kInk);
  }
}

}  // namespace tinydraw
