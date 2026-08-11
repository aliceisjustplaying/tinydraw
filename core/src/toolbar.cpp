#include "tinydraw/ui/toolbar.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

namespace tinydraw {
namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kInk = 0x2104U;
constexpr std::uint16_t kMuted = 0x8410U;
constexpr std::uint16_t kBorder = 0xDEDBU;
constexpr std::uint16_t kShadow = 0xBDF7U;
constexpr std::uint16_t kSelected = 0x349FU;
constexpr std::uint16_t kRecording = 0xE186U;
constexpr std::uint16_t kCharging = 0xF569U;
constexpr int kMainTop = 374;
constexpr int kMainBottom = 444;
constexpr int kColorPaletteTop = 180;
constexpr int kSizePaletteTop = 296;
constexpr int kPaletteBottom = 366;
constexpr int kDialogLeft = 28;
constexpr int kDialogTop = 126;
constexpr int kDialogRight = 340;
constexpr int kDialogBottom = 286;
constexpr int kBatteryLeft = 218;
constexpr int kBatteryTop = 22;
constexpr int kBatteryRight = 344;
constexpr int kBatteryBottom = 64;
constexpr int kHitSlop = 8;
constexpr int kMainHitTop = kMainTop - kHitSlop;
constexpr int kColorPaletteHitTop = kColorPaletteTop - kHitSlop;
constexpr int kSizePaletteHitTop = kSizePaletteTop - kHitSlop;
constexpr std::array kColors{
    InkColor::kBlack, InkColor::kGrey,       InkColor::kLightViolet, InkColor::kViolet,
    InkColor::kBlue,  InkColor::kLightBlue,  InkColor::kYellow,      InkColor::kOrange,
    InkColor::kGreen, InkColor::kLightGreen, InkColor::kLightRed,    InkColor::kRed,
};
constexpr std::array kPaletteCentersX{52, 140, 228, 316};
constexpr std::array kColorCentersY{207, 273, 339};

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
  line(canvas, width, height, 32, 410, 62, 410, color, 2);
  line(canvas, width, height, 32, 410, 44, 398, color, 2);
  line(canvas, width, height, 32, 410, 44, 422, color, 2);
}

void draw_pen(std::span<std::uint16_t> canvas, int width, int height, int center_x, int center_y,
              std::uint16_t color) {
  line(canvas, width, height, center_x - 10, center_y + 8, center_x + 7, center_y - 13, color, 1);
  line(canvas, width, height, center_x - 4, center_y + 14, center_x + 13, center_y - 7, color, 1);
  line(canvas, width, height, center_x + 7, center_y - 13, center_x + 13, center_y - 7, color, 1);
  line(canvas, width, height, center_x - 10, center_y + 8, center_x - 14, center_y + 18, color, 1);
  line(canvas, width, height, center_x - 4, center_y + 14, center_x - 14, center_y + 18, color, 1);
  line(canvas, width, height, center_x - 10, center_y + 8, center_x - 4, center_y + 14, color, 1);
}

void draw_hand(std::span<std::uint16_t> canvas, int width, int height, int center_x, int center_y,
               std::uint16_t color) {
  line(canvas, width, height, center_x - 10, center_y - 2, center_x - 10, center_y - 13, color, 1);
  line(canvas, width, height, center_x - 4, center_y - 4, center_x - 4, center_y - 17, color, 1);
  line(canvas, width, height, center_x + 2, center_y - 4, center_x + 2, center_y - 18, color, 1);
  line(canvas, width, height, center_x + 8, center_y - 2, center_x + 8, center_y - 14, color, 1);
  line(canvas, width, height, center_x - 10, center_y - 2, center_x - 16, center_y - 7, color, 1);
  line(canvas, width, height, center_x - 16, center_y - 7, center_x - 19, center_y - 3, color, 1);
  line(canvas, width, height, center_x - 19, center_y - 3, center_x - 8, center_y + 16, color, 1);
  line(canvas, width, height, center_x - 8, center_y + 16, center_x + 8, center_y + 16, color, 1);
  line(canvas, width, height, center_x + 8, center_y + 16, center_x + 12, center_y + 5, color, 1);
  line(canvas, width, height, center_x + 12, center_y + 5, center_x + 8, center_y - 2, color, 1);
}

void draw_eraser(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  // Keep the same two-part silhouette as tldraw's eraser: a tilted block plus
  // the exposed lower rubber, separated across the short axis.
  line(canvas, width, height, 148, 409, 159, 398, color, 1);
  line(canvas, width, height, 159, 398, 169, 408, color, 1);
  line(canvas, width, height, 169, 408, 158, 419, color, 1);
  line(canvas, width, height, 148, 409, 158, 419, color, 1);
  line(canvas, width, height, 148, 409, 144, 413, color, 1);
  line(canvas, width, height, 144, 413, 144, 416, color, 1);
  line(canvas, width, height, 144, 416, 150, 422, color, 1);
  line(canvas, width, height, 150, 422, 154, 423, color, 1);
  line(canvas, width, height, 154, 423, 158, 419, color, 1);
}

void draw_new(std::span<std::uint16_t> canvas, int width, int height, std::uint16_t color) {
  line(canvas, width, height, 309, 393, 335, 393, color, 1);
  line(canvas, width, height, 335, 393, 335, 427, color, 1);
  line(canvas, width, height, 335, 427, 309, 427, color, 1);
  line(canvas, width, height, 309, 427, 309, 393, color, 1);
  line(canvas, width, height, 315, 410, 329, 410, color, 1);
  line(canvas, width, height, 322, 403, 322, 417, color, 1);
}

std::array<std::uint8_t, 7> glyph_rows(char character) {
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

void draw_text(std::span<std::uint16_t> canvas, int width, int height, int x, int y,
               std::string_view text, std::uint16_t color, int scale = 2) {
  for (const char character : text) {
    const auto rows = glyph_rows(character);
    for (int row = 0; row < 7; ++row) {
      for (int column = 0; column < 5; ++column) {
        if ((rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) != 0U) {
          fill_rect(canvas, width, height, x + column * scale, y + row * scale,
                    x + (column + 1) * scale, y + (row + 1) * scale, color);
        }
      }
    }
    x += 6 * scale;
  }
}

void draw_battery(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state) {
  if (state.battery_percentage < 0) {
    return;
  }

  const int percentage = std::clamp(state.battery_percentage, 0, 100);
  rounded_rect(canvas, width, height, kBatteryLeft + 2, kBatteryTop + 3, kBatteryRight + 2,
               kBatteryBottom + 3, 9, kShadow);
  rounded_rect(canvas, width, height, kBatteryLeft - 1, kBatteryTop - 1, kBatteryRight + 1,
               kBatteryBottom + 1, 9, kBorder);
  rounded_rect(canvas, width, height, kBatteryLeft, kBatteryTop, kBatteryRight, kBatteryBottom, 8,
               kWhite);

  constexpr int icon_left = 228;
  constexpr int icon_top = 33;
  constexpr int icon_right = 254;
  constexpr int icon_bottom = 53;
  const std::uint16_t outline = state.battery_charging ? kSelected : kInk;
  fill_rect(canvas, width, height, icon_left, icon_top, icon_right, icon_top + 2, outline);
  fill_rect(canvas, width, height, icon_left, icon_bottom - 2, icon_right, icon_bottom, outline);
  fill_rect(canvas, width, height, icon_left, icon_top, icon_left + 2, icon_bottom, outline);
  fill_rect(canvas, width, height, icon_right - 2, icon_top, icon_right, icon_bottom, outline);
  fill_rect(canvas, width, height, icon_right, icon_top + 6, icon_right + 4, icon_bottom - 6,
            outline);
  const int level_width = percentage * (icon_right - icon_left - 8) / 100;
  fill_rect(canvas, width, height, icon_left + 4, icon_top + 4, icon_left + 4 + level_width,
            icon_bottom - 4, outline);
  if (state.battery_charging) {
    line(canvas, width, height, icon_left + 19, icon_top + 2, icon_left + 11, icon_top + 10,
         kCharging, 1);
    line(canvas, width, height, icon_left + 11, icon_top + 10, icon_left + 18, icon_top + 10,
         kCharging, 1);
    line(canvas, width, height, icon_left + 18, icon_top + 10, icon_left + 10, icon_bottom - 2,
         kCharging, 1);
  }

  std::array<char, 4> label{};
  std::size_t length = 0;
  if (percentage == 100) {
    label[length++] = '1';
  }
  if (percentage >= 10) {
    label[length++] = static_cast<char>('0' + (percentage / 10) % 10);
  }
  label[length++] = static_cast<char>('0' + percentage % 10);
  label[length++] = '%';
  const int label_x = 334 - static_cast<int>(length * 12U);
  const std::string_view label_view(label.data(), length);
  draw_text(canvas, width, height, label_x, 34, label_view, kInk, 2);
  draw_text(canvas, width, height, label_x + 1, 34, label_view, kInk, 2);
}

void draw_new_dialog(std::span<std::uint16_t> canvas, int width, int height) {
  rounded_rect(canvas, width, height, kDialogLeft + 3, kDialogTop + 4, kDialogRight + 3,
               kDialogBottom + 4, 17, kShadow);
  rounded_rect(canvas, width, height, kDialogLeft - 1, kDialogTop - 1, kDialogRight + 1,
               kDialogBottom + 1, 17, kBorder);
  rounded_rect(canvas, width, height, kDialogLeft, kDialogTop, kDialogRight, kDialogBottom, 16,
               kWhite);
  draw_text(canvas, width, height, 113, 158, "NEW DRAWING?", kInk);

  rounded_rect(canvas, width, height, 44, 204, 178, 266, 10, kBorder);
  rounded_rect(canvas, width, height, 46, 206, 176, 264, 9, kWhite);
  draw_text(canvas, width, height, 100, 228, "NO", kInk);

  rounded_rect(canvas, width, height, 190, 204, 324, 266, 10, kSelected);
  rounded_rect(canvas, width, height, 192, 206, 322, 264, 9, kSelected);
  draw_text(canvas, width, height, 240, 228, "YES", kWhite);
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
  if (state.confirm_new) {
    return inside(point, 0.0F, 0.0F, static_cast<float>(kCanvasWidth),
                  static_cast<float>(kCanvasHeight));
  }
  const bool main_row = inside(point, 0.0F, static_cast<float>(kMainHitTop),
                               static_cast<float>(kCanvasWidth), static_cast<float>(kCanvasHeight));
  const bool color_palette =
      state.colors_open && inside(point, 0.0F, static_cast<float>(kColorPaletteHitTop),
                                  static_cast<float>(kCanvasWidth), static_cast<float>(kMainTop));
  const bool compact_palette =
      (state.tools_open || state.sizes_open) &&
      inside(point, 0.0F, static_cast<float>(kSizePaletteHitTop), static_cast<float>(kCanvasWidth),
             static_cast<float>(kMainTop));
  return main_row || color_palette || compact_palette;
}

std::optional<Rect> battery_overlay_rect(const ToolbarState& state) {
  if (state.battery_percentage < 0) {
    return std::nullopt;
  }
  return Rect{kBatteryLeft - 2, kBatteryTop - 2, kBatteryRight + 4, kBatteryBottom + 6};
}

bool toolbar_overlay_contains(Point point, const ToolbarState& state) {
  const bool battery =
      battery_overlay_rect(state).has_value() &&
      inside(point, static_cast<float>(kBatteryLeft - 1), static_cast<float>(kBatteryTop - 1),
             static_cast<float>(kBatteryRight + 4), static_cast<float>(kBatteryBottom + 6));
  const bool main_dock =
      inside(point, 0.0F, static_cast<float>(kMainTop - 1), static_cast<float>(kCanvasWidth),
             static_cast<float>(kMainBottom + 4));
  const int palette_top = state.colors_open ? kColorPaletteTop : kSizePaletteTop;
  const bool palette =
      (state.tools_open || state.colors_open || state.sizes_open) &&
      inside(point, 0.0F, static_cast<float>(palette_top - 1), static_cast<float>(kCanvasWidth),
             static_cast<float>(kPaletteBottom + 4));
  const bool dialog =
      state.confirm_new &&
      inside(point, static_cast<float>(kDialogLeft - 1), static_cast<float>(kDialogTop - 1),
             static_cast<float>(kDialogRight + 4), static_cast<float>(kDialogBottom + 5));
  return battery || main_dock || palette || dialog;
}

int toolbar_overlay_top(const ToolbarState& state) {
  if (state.confirm_new) {
    return kDialogTop - 1;
  }
  if (state.colors_open) {
    return kColorPaletteTop - 1;
  }
  if (state.tools_open || state.sizes_open) {
    return kSizePaletteTop - 1;
  }
  return kMainTop - 1;
}

ToolbarAction toolbar_action_at(Point point, const ToolbarState& state) {
  if (state.confirm_new) {
    if (inside(point, 36.0F, 194.0F, 184.0F, 276.0F)) {
      return ToolbarAction::kCancelNewDrawing;
    }
    if (inside(point, 184.0F, 194.0F, 332.0F, 276.0F)) {
      return ToolbarAction::kConfirmNewDrawing;
    }
    return ToolbarAction::kNone;
  }
  if (toolbar_color_at(point, state).has_value()) {
    return ToolbarAction::kSelectColor;
  }
  if (state.tools_open && inside(point, 0.0F, static_cast<float>(kSizePaletteHitTop),
                                 static_cast<float>(kCanvasWidth), static_cast<float>(kMainTop))) {
    return point.x < static_cast<float>(kCanvasWidth) * 0.5F ? ToolbarAction::kSelectPen
                                                             : ToolbarAction::kSelectPan;
  }
  if (state.sizes_open && inside(point, 0.0F, static_cast<float>(kSizePaletteHitTop),
                                 static_cast<float>(kCanvasWidth), static_cast<float>(kMainTop))) {
    const auto index =
        std::min(static_cast<std::size_t>(point.x * 4.0F / kCanvasWidth), std::size_t{3});
    constexpr std::array actions{ToolbarAction::kSelectSmall, ToolbarAction::kSelectMedium,
                                 ToolbarAction::kSelectLarge, ToolbarAction::kSelectExtraLarge};
    return actions[index];
  }
  if (!inside(point, 0.0F, static_cast<float>(kMainHitTop), static_cast<float>(kCanvasWidth),
              static_cast<float>(kCanvasHeight))) {
    return ToolbarAction::kNone;
  }
  const auto main_index =
      std::min(static_cast<std::size_t>(point.x * 6.0F / kCanvasWidth), std::size_t{5});
  if (state.tools_open || state.colors_open || state.sizes_open) {
    if (state.tools_open && main_index == 1U) {
      return ToolbarAction::kToggleTools;
    }
    if (state.colors_open && main_index == 3U) {
      return ToolbarAction::kToggleColors;
    }
    if (state.sizes_open && main_index == 4U) {
      return ToolbarAction::kToggleSizes;
    }
    return ToolbarAction::kNone;
  }
  constexpr std::array actions{
      ToolbarAction::kUndo,         ToolbarAction::kToggleTools, ToolbarAction::kSelectEraser,
      ToolbarAction::kToggleColors, ToolbarAction::kToggleSizes, ToolbarAction::kNewDrawing,
  };
  return actions[main_index];
}

std::optional<InkColor> toolbar_color_at(Point point, const ToolbarState& state) {
  if (!state.colors_open ||
      !inside(point, 0.0F, static_cast<float>(kColorPaletteHitTop),
              static_cast<float>(kCanvasWidth), static_cast<float>(kMainTop))) {
    return std::nullopt;
  }
  const auto column =
      std::min(static_cast<std::size_t>(point.x * 4.0F / kCanvasWidth), std::size_t{3});
  const auto row =
      std::min(static_cast<std::size_t>((point.y - static_cast<float>(kColorPaletteHitTop)) * 3.0F /
                                        static_cast<float>(kMainTop - kColorPaletteHitTop)),
               std::size_t{2});
  return kColors[row * kPaletteCentersX.size() + column];
}

std::uint16_t rgb565(InkColor color) {
  switch (color) {
    case InkColor::kBlack:
      return 0x18E3U;
    case InkColor::kGrey:
      return 0x9D56U;
    case InkColor::kLightViolet:
      return 0xE43EU;
    case InkColor::kViolet:
      return 0xA9F9U;
    case InkColor::kBlue:
      return 0x433DU;
    case InkColor::kLightBlue:
      return 0x4D1EU;
    case InkColor::kYellow:
      return 0xF569U;
    case InkColor::kOrange:
      return 0xE343U;
    case InkColor::kGreen:
      return 0x0C8DU;
    case InkColor::kLightGreen:
      return 0x4D8BU;
    case InkColor::kLightRed:
      return 0xFBAEU;
    case InkColor::kRed:
      return 0xE186U;
  }
  return 0x18E3U;
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
  fill_rect(canvas, width, height, 0, kMainTop - 1, width, kMainTop + 1, kBorder);
  fill_rect(canvas, width, height, 0, kMainBottom, width, kMainBottom + 1, kBorder);

  if (state.tool == DrawingTool::kEraser) {
    rounded_rect(canvas, width, height, 128, 380, 184, 440, 9, kSelected);
  } else {
    rounded_rect(canvas, width, height, 73, 380, 128, 440, 9, kSelected);
  }

  draw_undo(canvas, width, height, state.can_undo ? kInk : kMuted);
  if (state.tool == DrawingTool::kPan) {
    draw_hand(canvas, width, height, 102, 411, kWhite);
  } else {
    draw_pen(canvas, width, height, 102, 411, state.tool == DrawingTool::kPen ? kWhite : kInk);
  }
  draw_eraser(canvas, width, height, state.tool == DrawingTool::kEraser ? kWhite : kInk);

  fill_circle(canvas, width, height, 212, 410, 18, kSelected);
  fill_circle(canvas, width, height, 212, 410, 15, kWhite);
  fill_circle(canvas, width, height, 212, 410, 13, rgb565(state.color));

  const int selected_size_radius = size_radius(state.size);
  fill_circle(canvas, width, height, 267, 410, selected_size_radius + 4, kSelected);
  fill_circle(canvas, width, height, 267, 410, selected_size_radius + 2, kWhite);
  fill_circle(canvas, width, height, 267, 410, selected_size_radius, kInk);
  draw_new(canvas, width, height, kInk);
  if (state.recording) {
    fill_circle(canvas, width, height, 184, 382, 5, kRecording);
  }
  draw_battery(canvas, width, height, state);

  if (state.confirm_new) {
    draw_new_dialog(canvas, width, height);
    return;
  }
  if (!state.tools_open && !state.colors_open && !state.sizes_open) {
    return;
  }
  const int palette_top = state.colors_open ? kColorPaletteTop : kSizePaletteTop;
  draw_dock(canvas, width, height, 4, palette_top, 364, kPaletteBottom);
  fill_rect(canvas, width, height, 0, palette_top - 1, width, palette_top + 1, kBorder);
  fill_rect(canvas, width, height, 0, kPaletteBottom, width, kPaletteBottom + 1, kBorder);
  if (state.colors_open) {
    for (std::size_t row = 0; row < kColorCentersY.size(); ++row) {
      for (std::size_t column = 0; column < kPaletteCentersX.size(); ++column) {
        const auto color = kColors[row * kPaletteCentersX.size() + column];
        const int center_x = kPaletteCentersX[column];
        const int center_y = kColorCentersY[row];
        if (state.color == color) {
          fill_circle(canvas, width, height, center_x, center_y, 21, kSelected);
          fill_circle(canvas, width, height, center_x, center_y, 18, kWhite);
        }
        fill_circle(canvas, width, height, center_x, center_y, 15, rgb565(color));
      }
    }
    return;
  }
  if (state.tools_open) {
    if (state.tool == DrawingTool::kPen) {
      fill_circle(canvas, width, height, 96, 331, 26, kSelected);
    } else if (state.tool == DrawingTool::kPan) {
      fill_circle(canvas, width, height, 272, 331, 26, kSelected);
    }
    draw_pen(canvas, width, height, 96, 331, kInk);
    draw_hand(canvas, width, height, 272, 331, kInk);
    return;
  }

  constexpr std::array palette_sizes{PenSize::kSmall, PenSize::kMedium, PenSize::kLarge,
                                     PenSize::kExtraLarge};
  for (std::size_t index = 0; index < palette_sizes.size(); ++index) {
    const int radius = size_radius(palette_sizes[index]);
    if (state.size == palette_sizes[index]) {
      fill_circle(canvas, width, height, kPaletteCentersX[index], 331, radius + 5, kSelected);
      fill_circle(canvas, width, height, kPaletteCentersX[index], 331, radius + 2, kWhite);
    }
    fill_circle(canvas, width, height, kPaletteCentersX[index], 331, radius, kInk);
  }
}

}  // namespace tinydraw
