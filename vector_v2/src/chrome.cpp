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
constexpr int kHitSlop = 8;
constexpr int kMainHitTop = kMainTop - kHitSlop;
constexpr std::array kMainCenters{34, 94, 154, 214, 274, 334};
constexpr std::array kPopupCenters{62, 184, 306};
constexpr std::array kSizeCenters{52, 140, 228, 316};

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

void draw_tools(Painter& painter, int cx, int cy, std::uint16_t color) {
  painter.line({cx - 11, cy + 10, cx + 8, cy - 12}, color, 2);
  painter.line({cx - 3, cy + 14, cx + 13, cy - 5}, color, 2);
}

void draw_hand(Painter& painter, int cx, int cy, std::uint16_t color) {
  for (int x = -9; x <= 9; x += 6) {
    painter.line({cx + x, cy - 1, cx + x, cy - 14}, color);
  }
  painter.line({cx - 9, cy, cx - 14, cy - 5}, color, 2);
  painter.line({cx - 14, cy - 5, cx - 5, cy + 15}, color, 2);
  painter.line({cx - 5, cy + 15, cx + 11, cy + 15}, color, 2);
}

void draw_eraser(Painter& painter, int cx, int cy, std::uint16_t color) {
  painter.line({cx - 13, cy + 5, cx + 3, cy - 11}, color, 2);
  painter.line({cx + 3, cy - 11, cx + 14, cy}, color, 2);
  painter.line({cx + 14, cy, cx - 2, cy + 16}, color, 2);
  painter.line({cx - 2, cy + 16, cx - 13, cy + 5}, color, 2);
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
  painter.rect({0, 0, kWidth, kHeight}, 0x1082U);
  constexpr int gap = 6;
  constexpr int cell_width = (kWidth - gap * 5) / 4;
  constexpr int cell_height = 88;
  for (std::size_t index = 0; index < kPaletteColorCount; ++index) {
    const int column = static_cast<int>(index % 4U);
    const int row = static_cast<int>(index / 4U);
    const int x0 = gap + column * (cell_width + gap);
    const int y0 = 42 + row * (cell_height + gap);
    const bool selected = index == state.color_index;
    const int outline = selected ? 3 : 0;
    painter.rounded(
        {x0 - outline, y0 - outline, x0 + cell_width + outline, y0 + cell_height + outline}, 9,
        selected ? kSelected : kBorder);
    painter.rounded({x0, y0, x0 + cell_width, y0 + cell_height}, 7,
                    kPico8Palettes[state.palette_page][index]);
  }
  painter.rounded({8, 6, 62, 36}, 8, state.palette_page == 0 ? kMuted : kSelected);
  painter.rounded({kWidth - 62, 6, kWidth - 8, 36}, 8,
                  state.palette_page == 1 ? kMuted : kSelected);
  draw_arrow(painter, 34, 21, false, kWhite);
  draw_arrow(painter, kWidth - 34, 21, true, kWhite);
}

void draw_bottom(Painter& painter, const ChromeState& state) {
  draw_dock(painter, kMainTop, kMainBottom);
  draw_arrow(painter, kMainCenters[0], 410, false, state.can_undo ? kInk : kMuted);
  draw_arrow(painter, kMainCenters[1], 410, true, state.can_redo ? kInk : kMuted);
  draw_tools(painter, kMainCenters[2], 410, kInk);
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
  draw_tools(painter, kPopupCenters[0], 331, state.tool == ChromeTool::kDraw ? kWhite : kInk);
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
  if (state.popup == ChromePopup::kColors) {
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
  if (state.popup != ChromePopup::kColors || point.y < 42.0F || point.y >= 418.0F) {
    return std::nullopt;
  }
  constexpr int gap = 6;
  constexpr int cell_width = (kWidth - gap * 5) / 4;
  constexpr int cell_height = 88;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int x0 = gap + column * (cell_width + gap);
      const int y0 = 42 + row * (cell_height + gap);
      if (inside(point, static_cast<float>(x0), static_cast<float>(y0),
                 static_cast<float>(x0 + cell_width), static_cast<float>(y0 + cell_height))) {
        return static_cast<std::uint8_t>(row * 4 + column);
      }
    }
  }
  return std::nullopt;
}

ChromeAction chrome_action_at(ChromePoint point, const ChromeState& state) {
  if (state.popup == ChromePopup::kColors) {
    if (inside(point, 0.0F, 0.0F, 80.0F, 42.0F)) {
      return ChromeAction::kPreviousPalette;
    }
    if (inside(point, static_cast<float>(kWidth - 80), 0.0F, static_cast<float>(kWidth), 42.0F)) {
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
  if (state.popup == ChromePopup::kColors) {
    draw_palette(painter, state);
    return;
  }
  // Every row below the canvas boundary belongs to chrome, including the
  // narrow gutters above rounded docks. Repainting ownership here prevents
  // stationary seam pixels when cached canvas rows are scrolled in place.
  painter.rect({0, chrome_canvas_bottom(state), kWidth, kMainTop}, kWhite);
  draw_bottom(painter, state);
  if (state.popup == ChromePopup::kNone) {
    return;
  }
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

}  // namespace tinydraw::vector_v2
