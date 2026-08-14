#include "tinydraw/vector_v2/chrome.h"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "tinydraw/ui/pixel_painter.h"

namespace tinydraw::vector_v2 {
namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kInk = 0x2104U;
constexpr std::uint16_t kMuted = 0x8410U;
constexpr std::uint16_t kBorder = 0xDEDBU;
constexpr std::uint16_t kShadow = 0xBDF7U;
constexpr std::uint16_t kSelected = 0x349FU;
constexpr int kWidth = 368;
constexpr int kHeight = 448;
constexpr int kMainTop = 374;
constexpr int kMainBottom = 444;
constexpr int kPopupTop = 296;
constexpr int kPopupBottom = 366;
constexpr int kDialogLeft = 28;
constexpr int kDialogTop = 126;
constexpr int kDialogRight = 340;
constexpr int kDialogBottom = 286;
constexpr int kToastLeft = 104;
constexpr int kToastTop = 70;
constexpr int kToastRight = 264;
constexpr int kToastBottom = 132;
constexpr int kHitSlop = 8;
constexpr int kMainHitTop = kMainTop - kHitSlop;
constexpr std::array kMainCenters{34, 94, 154, 214, 274, 334};
constexpr std::array kPopupCenters{62, 184, 306};
constexpr std::array kSizeCenters{52, 140, 228, 316};
constexpr std::array kPaletteCentersX{46, 138, 230, 322};
constexpr std::array kPaletteCentersY{108, 198, 288, 378};
constexpr int kPaletteControlsBottom = 64;
constexpr int kPaletteRowHeight = 90;

using Painter = PixelPainter;

bool inside(ChromePoint point, float x0, float y0, float x1, float y1) {
  return point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1;
}

void draw_dock(Painter& painter, int top, int bottom) {
  painter.rounded({6, top + 3, kWidth - 2, bottom + 3}, 11, kShadow);
  painter.rounded({3, top - 1, kWidth - 3, bottom + 1}, 11, kBorder);
  painter.rounded({4, top, kWidth - 4, bottom}, 10, kWhite);
}

void draw_arrow(Painter& painter, int cx, int cy, bool redo, std::uint16_t color) {
  const int direction = redo ? 1 : -1;
  painter.line({cx - 12 * direction, cy, cx + 10 * direction, cy}, color, 2);
  painter.line({cx + 10 * direction, cy, cx + 2 * direction, cy - 8}, color, 2);
  painter.line({cx + 10 * direction, cy, cx + 2 * direction, cy + 8}, color, 2);
}

// These are the proven Raster V1 tool glyphs, translated around a caller-
// supplied center so the main button and popup use the same silhouettes.
void draw_pen(Painter& painter, int cx, int cy, std::uint16_t color) {
  painter.line({cx - 10, cy + 8, cx + 7, cy - 13}, color);
  painter.line({cx - 4, cy + 14, cx + 13, cy - 7}, color);
  painter.line({cx + 7, cy - 13, cx + 13, cy - 7}, color);
  painter.line({cx - 10, cy + 8, cx - 14, cy + 18}, color);
  painter.line({cx - 4, cy + 14, cx - 14, cy + 18}, color);
  painter.line({cx - 10, cy + 8, cx - 4, cy + 14}, color);
}

void draw_hand(Painter& painter, int cx, int cy, std::uint16_t color) {
  painter.line({cx - 10, cy - 2, cx - 10, cy - 13}, color);
  painter.line({cx - 4, cy - 4, cx - 4, cy - 17}, color);
  painter.line({cx + 2, cy - 4, cx + 2, cy - 18}, color);
  painter.line({cx + 8, cy - 2, cx + 8, cy - 14}, color);
  painter.line({cx - 10, cy - 2, cx - 16, cy - 7}, color);
  painter.line({cx - 16, cy - 7, cx - 19, cy - 3}, color);
  painter.line({cx - 19, cy - 3, cx - 8, cy + 16}, color);
  painter.line({cx - 8, cy + 16, cx + 8, cy + 16}, color);
  painter.line({cx + 8, cy + 16, cx + 12, cy + 5}, color);
  painter.line({cx + 12, cy + 5, cx + 8, cy - 2}, color);
}

void draw_eraser(Painter& painter, int cx, int cy, std::uint16_t color) {
  painter.line({cx - 8, cy - 1, cx + 3, cy - 12}, color);
  painter.line({cx + 3, cy - 12, cx + 13, cy - 2}, color);
  painter.line({cx + 13, cy - 2, cx + 2, cy + 9}, color);
  painter.line({cx - 8, cy - 1, cx + 2, cy + 9}, color);
  painter.line({cx - 8, cy - 1, cx - 12, cy + 3}, color);
  painter.line({cx - 12, cy + 3, cx - 12, cy + 6}, color);
  painter.line({cx - 12, cy + 6, cx - 6, cy + 12}, color);
  painter.line({cx - 6, cy + 12, cx - 2, cy + 13}, color);
  painter.line({cx - 2, cy + 13, cx + 2, cy + 9}, color);
}

void draw_document(Painter& painter, int cx, int cy, std::uint16_t color) {
  painter.line({cx - 12, cy - 16, cx + 10, cy - 16}, color, 2);
  painter.line({cx + 10, cy - 16, cx + 10, cy + 16}, color, 2);
  painter.line({cx + 10, cy + 16, cx - 12, cy + 16}, color, 2);
  painter.line({cx - 12, cy + 16, cx - 12, cy - 16}, color, 2);
  painter.line({cx - 6, cy - 5, cx + 4, cy - 5}, color);
  painter.line({cx - 6, cy + 3, cx + 4, cy + 3}, color);
}

int size_radius(ChromeSize size) {
  switch (size) {
    case ChromeSize::kSmall:
      return 6;
    case ChromeSize::kMedium:
      return 10;
    case ChromeSize::kLarge:
      return 14;
    case ChromeSize::kExtraLarge:
      return 18;
  }
  return 10;
}

void draw_palette(Painter& painter, const ChromeState& state) {
  painter.rect({0, 0, kWidth, kHeight}, kWhite);
  for (std::size_t index = 0; index < kPaletteColorCount; ++index) {
    const std::size_t column = index % kPaletteCentersX.size();
    const std::size_t row = index / kPaletteCentersX.size();
    const int cx = kPaletteCentersX[column];
    const int cy = kPaletteCentersY[row];
    if (index == state.color_index) {
      painter.circle(cx, cy, 38, kSelected);
      painter.circle(cx, cy, 34, kWhite);
    } else {
      painter.circle(cx, cy, 34, kBorder);
    }
    painter.circle(cx, cy, 30, kPico8Palettes[state.palette_page][index]);
  }
  painter.rounded({8, 6, 88, 58}, 10, state.palette_page == 0 ? kMuted : kSelected);
  painter.rounded({kWidth - 88, 6, kWidth - 8, 58}, 10,
                  state.palette_page == 1 ? kMuted : kSelected);
  draw_arrow(painter, 48, 32, false, kWhite);
  draw_arrow(painter, kWidth - 48, 32, true, kWhite);
}

void draw_bottom(Painter& painter, const ChromeState& state) {
  draw_dock(painter, kMainTop, kMainBottom);
  draw_arrow(painter, kMainCenters[0], 410, false, state.can_undo ? kInk : kMuted);
  draw_arrow(painter, kMainCenters[1], 410, true, state.can_redo ? kInk : kMuted);
  switch (state.tool) {
    case ChromeTool::kDraw:
      draw_pen(painter, kMainCenters[2], 410, kInk);
      break;
    case ChromeTool::kErase:
      draw_eraser(painter, kMainCenters[2], 410, kInk);
      break;
    case ChromeTool::kPan:
      draw_hand(painter, kMainCenters[2], 410, kInk);
      break;
  }
  painter.circle(kMainCenters[3], 410, 18, kSelected);
  painter.circle(kMainCenters[3], 410, 14, selected_color(state));
  const int radius = size_radius(state.size);
  painter.circle(kMainCenters[4], 410, radius + 4, kSelected);
  painter.circle(kMainCenters[4], 410, radius, kInk);
  draw_document(painter, kMainCenters[5], 410, kInk);
}

void draw_tools_popup(Painter& painter, const ChromeState& state) {
  const std::array tools{ChromeTool::kDraw, ChromeTool::kErase, ChromeTool::kPan};
  for (std::size_t index = 0; index < tools.size(); ++index) {
    if (state.tool == tools[index]) {
      painter.circle(kPopupCenters[index], 331, 27, kSelected);
    }
  }
  draw_pen(painter, kPopupCenters[0], 331, state.tool == ChromeTool::kDraw ? kWhite : kInk);
  draw_eraser(painter, kPopupCenters[1], 331, state.tool == ChromeTool::kErase ? kWhite : kInk);
  draw_hand(painter, kPopupCenters[2], 331, state.tool == ChromeTool::kPan ? kWhite : kInk);
}

void draw_sizes_popup(Painter& painter, const ChromeState& state) {
  constexpr std::array sizes{ChromeSize::kSmall, ChromeSize::kMedium, ChromeSize::kLarge,
                             ChromeSize::kExtraLarge};
  for (std::size_t index = 0; index < sizes.size(); ++index) {
    const int radius = size_radius(sizes[index]);
    if (state.size == sizes[index]) {
      painter.circle(kSizeCenters[index], 331, radius + 5, kSelected);
    }
    painter.circle(kSizeCenters[index], 331, radius, kInk);
  }
}

void draw_document_popup(Painter& painter, const ChromeState& state) {
  draw_document(painter, 92, 331, kInk);
  painter.line({76, 331, 108, 331}, kInk, 2);
  painter.line({92, 315, 92, 347}, kInk, 2);
  draw_arrow(painter, 276, 331, true, state.can_export ? kInk : kMuted);
}

void draw_new_dialog(Painter& painter) {
  painter.rounded({kDialogLeft + 3, kDialogTop + 4, kDialogRight + 3, kDialogBottom + 4}, 17,
                  kShadow);
  painter.rounded({kDialogLeft - 1, kDialogTop - 1, kDialogRight + 1, kDialogBottom + 1}, 17,
                  kBorder);
  painter.rounded({kDialogLeft, kDialogTop, kDialogRight, kDialogBottom}, 16, kWhite);
  painter.text(113, 158, "NEW DRAWING?", kInk);

  painter.rounded({44, 204, 178, 266}, 10, kBorder);
  painter.rounded({46, 206, 176, 264}, 9, kWhite);
  painter.text(100, 228, "NO", kInk);

  painter.rounded({190, 204, 324, 266}, 10, kSelected);
  painter.rounded({192, 206, 322, 264}, 9, kSelected);
  painter.text(240, 228, "YES", kWhite);
}

void draw_export_toast(Painter& painter, const ChromeState& state) {
  if (state.export_status == ChromeExportStatus::kIdle) {
    return;
  }
  painter.rounded({kToastLeft + 2, kToastTop + 3, kToastRight + 2, kToastBottom + 3}, 12, kShadow);
  painter.rounded({kToastLeft - 1, kToastTop - 1, kToastRight + 1, kToastBottom + 1}, 12, kBorder);
  painter.rounded({kToastLeft, kToastTop, kToastRight, kToastBottom}, 11, kWhite);
  if (state.export_status == ChromeExportStatus::kSaving) {
    painter.text(130, 82, "SAVING", kInk, 3);
    painter.rounded({120, 108, 248, 124}, 5, kBorder);
    painter.rounded({123, 111, 245, 121}, 3, kWhite);
    const int progress = std::clamp<int>(state.export_progress, 0, 100);
    painter.rect({123, 111, 123 + progress * 122 / 100, 121}, kSelected);
    return;
  }
  const bool saved = state.export_status == ChromeExportStatus::kSaved;
  painter.text(saved ? 139 : 130, 90, saved ? "SAVED" : "ERROR", saved ? kInk : 0xE186U, 3);
}

}  // namespace

std::uint16_t selected_color(const ChromeState& state) {
  const std::size_t page = std::min<std::size_t>(state.palette_page, 1U);
  const std::size_t color = std::min<std::size_t>(state.color_index, kPaletteColorCount - 1U);
  return kPico8Palettes[page][color];
}

float brush_size(ChromeSize size) {
  switch (size) {
    case ChromeSize::kSmall:
      return 5.0F;
    case ChromeSize::kMedium:
      return 8.0F;
    case ChromeSize::kLarge:
      return 13.0F;
    case ChromeSize::kExtraLarge:
      return 20.0F;
  }
  return 8.0F;
}

int chrome_canvas_bottom(const ChromeState& state) {
  if (state.popup == ChromePopup::kNone) {
    return kChromeCanvasBottom;
  }
  return state.popup == ChromePopup::kColors ? 0 : kChromePopupCanvasBottom;
}

int chrome_input_bottom(const ChromeState& state) {
  if (state.confirm_new || state.export_status == ChromeExportStatus::kSaving) {
    return 0;
  }
  if (state.popup == ChromePopup::kNone) {
    return kChromeCanvasBottom;
  }
  return state.popup == ChromePopup::kColors ? 0 : kChromePopupInputBottom;
}

std::optional<ChromePoint> clip_canvas_segment(ChromePoint previous, ChromePoint current,
                                               const ChromeState& state) {
  const int input_bottom = chrome_input_bottom(state);
  if (input_bottom == 0) {
    return std::nullopt;
  }
  const float bottom = static_cast<float>(input_bottom - 1);
  if (current.y <= bottom) {
    return current;
  }
  if (previous.y > bottom) {
    return std::nullopt;
  }
  const float vertical_distance = current.y - previous.y;
  if (vertical_distance <= 0.0F) {
    return ChromePoint{current.x, bottom};
  }
  const float progress = (bottom - previous.y) / vertical_distance;
  return ChromePoint{previous.x + (current.x - previous.x) * progress, bottom};
}

bool chrome_contains(ChromePoint point, const ChromeState& state) {
  if (state.confirm_new || state.export_status == ChromeExportStatus::kSaving ||
      state.popup == ChromePopup::kColors) {
    return inside(point, 0.0F, 0.0F, static_cast<float>(kWidth), static_cast<float>(kHeight));
  }
  const bool main = inside(point, 0.0F, static_cast<float>(kMainHitTop), static_cast<float>(kWidth),
                           static_cast<float>(kHeight));
  const bool popup = state.popup != ChromePopup::kNone &&
                     inside(point, 0.0F, static_cast<float>(kPopupTop - kHitSlop),
                            static_cast<float>(kWidth), static_cast<float>(kMainTop));
  return main || popup;
}

std::optional<std::uint8_t> chrome_color_at(ChromePoint point, const ChromeState& state) {
  if (state.popup != ChromePopup::kColors || point.y < kPaletteControlsBottom ||
      point.y >= kPaletteControlsBottom + kPaletteRowHeight * 4) {
    return std::nullopt;
  }
  const int column = std::clamp(static_cast<int>(point.x) * 4 / kWidth, 0, 3);
  const int row =
      std::clamp((static_cast<int>(point.y) - kPaletteControlsBottom) / kPaletteRowHeight, 0, 3);
  return static_cast<std::uint8_t>(row * 4 + column);
}

ChromeAction chrome_action_at(ChromePoint point, const ChromeState& state) {
  if (state.confirm_new) {
    if (inside(point, 36.0F, 194.0F, 184.0F, 276.0F)) {
      return ChromeAction::kCancelNewDrawing;
    }
    if (inside(point, 184.0F, 194.0F, 332.0F, 276.0F)) {
      return ChromeAction::kConfirmNewDrawing;
    }
    return ChromeAction::kNone;
  }
  if (state.export_status == ChromeExportStatus::kSaving) {
    return ChromeAction::kNone;
  }
  if (state.popup == ChromePopup::kColors) {
    if (inside(point, 0.0F, 0.0F, 96.0F, static_cast<float>(kPaletteControlsBottom))) {
      return ChromeAction::kPreviousPalette;
    }
    if (inside(point, static_cast<float>(kWidth - 96), 0.0F, static_cast<float>(kWidth),
               static_cast<float>(kPaletteControlsBottom))) {
      return ChromeAction::kNextPalette;
    }
    return chrome_color_at(point, state).has_value() ? ChromeAction::kSelectColor
                                                     : ChromeAction::kNone;
  }
  if (inside(point, 0.0F, static_cast<float>(kPopupTop - kHitSlop), static_cast<float>(kWidth),
             static_cast<float>(kMainTop)) &&
      state.popup != ChromePopup::kNone) {
    const std::size_t third =
        std::min(static_cast<std::size_t>(point.x * 3.0F / kWidth), std::size_t{2});
    if (state.popup == ChromePopup::kTools) {
      constexpr std::array actions{ChromeAction::kSelectDraw, ChromeAction::kSelectErase,
                                   ChromeAction::kSelectPan};
      return actions[third];
    }
    if (state.popup == ChromePopup::kSizes) {
      const std::size_t quarter =
          std::min(static_cast<std::size_t>(point.x * 4.0F / kWidth), std::size_t{3});
      constexpr std::array actions{ChromeAction::kSelectSmall, ChromeAction::kSelectMedium,
                                   ChromeAction::kSelectLarge, ChromeAction::kSelectExtraLarge};
      return actions[quarter];
    }
    if (state.popup == ChromePopup::kDocument) {
      return point.x < kWidth / 2.0F ? ChromeAction::kNewDrawing : ChromeAction::kExport;
    }
  }
  if (!inside(point, 0.0F, static_cast<float>(kMainHitTop), static_cast<float>(kWidth),
              static_cast<float>(kHeight))) {
    return ChromeAction::kNone;
  }
  constexpr std::array actions{ChromeAction::kUndo,        ChromeAction::kRedo,
                               ChromeAction::kToggleTools, ChromeAction::kToggleColors,
                               ChromeAction::kToggleSizes, ChromeAction::kToggleDocument};
  const std::size_t index =
      std::min(static_cast<std::size_t>(point.x * 6.0F / kWidth), std::size_t{5});
  return actions[index];
}

void draw_chrome(std::span<std::uint16_t> pixels, int width, int height, const ChromeState& state) {
  if (width != kWidth || height != kHeight ||
      pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return;
  }
  Painter painter(pixels, width, height);
  if (state.popup == ChromePopup::kColors && !state.confirm_new &&
      state.export_status == ChromeExportStatus::kIdle) {
    draw_palette(painter, state);
    return;
  }
  // Every row below the canvas boundary belongs to chrome, including the
  // narrow gutters above rounded docks. Repainting ownership here prevents
  // stationary seam pixels when cached canvas rows are scrolled in place.
  painter.rect({0, chrome_canvas_bottom(state), kWidth, kMainTop}, kWhite);
  draw_bottom(painter, state);
  if (state.popup != ChromePopup::kNone) {
    draw_dock(painter, kPopupTop, kPopupBottom);
    switch (state.popup) {
      case ChromePopup::kTools:
        draw_tools_popup(painter, state);
        break;
      case ChromePopup::kSizes:
        draw_sizes_popup(painter, state);
        break;
      case ChromePopup::kDocument:
        draw_document_popup(painter, state);
        break;
      case ChromePopup::kNone:
      case ChromePopup::kColors:
        break;
    }
  }
  if (state.confirm_new) {
    draw_new_dialog(painter);
  }
  draw_export_toast(painter, state);
}

}  // namespace tinydraw::vector_v2
