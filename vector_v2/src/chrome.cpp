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
constexpr int kPopupTop = 296;
constexpr int kPopupBottom = 366;
constexpr int kColorPopupTop = 4;
constexpr int kColorPopupBottom = 366;
constexpr int kDialogLeft = 28;
constexpr int kDialogTop = 126;
constexpr int kDialogRight = 340;
constexpr int kDialogBottom = 286;
constexpr int kToastLeft = 104;
constexpr int kToastTop = 70;
constexpr int kToastRight = 264;
constexpr int kToastBottom = 132;
constexpr int kBatteryLeft = 222;
constexpr int kBatteryTop = 18;
constexpr int kBatteryRight = 340;
constexpr int kBatteryBottom = 54;
constexpr ChromeRect kZoomRailRect{304, 72, 360, 226};
constexpr ChromeRect kZoomRailOverlayRect{kZoomRailRect.x0 - 1, kZoomRailRect.y0 - 1,
                                          kZoomRailRect.x1 + 2, kZoomRailRect.y1 + 3};
constexpr ChromeRect kMinimapRect{266, 252, 358, 366};
constexpr ChromeRect kMinimapOverlayRect{kMinimapRect.x0 - 1, kMinimapRect.y0 - 1,
                                         kMinimapRect.x1 + 2, kMinimapRect.y1 + 3};
constexpr ChromeRect kBatteryOverlayRect{kBatteryLeft - 2, kBatteryTop - 2, kBatteryRight + 4,
                                         kBatteryBottom + 6};
constexpr int kMinimapLeft = 272;
constexpr int kMinimapTop = 258;
constexpr int kMinimapWidth = 80;
constexpr int kMinimapHeight = 98;
constexpr int kHitSlop = 8;
constexpr int kMainHitTop = kMainTop - kHitSlop;
constexpr std::array kMainCenters{34, 94, 154, 214, 274, 334};
constexpr std::array kPopupCenters{62, 184, 306};
constexpr std::array kSizeCenters{52, 140, 228, 316};
constexpr std::array kPaletteCentersX{46, 138, 230, 322};
constexpr std::array kPaletteCentersY{103, 178, 253, 328};
constexpr int kPaletteControlsBottom = 64;
constexpr int kPaletteRowHeight = 75;

using Painter = PixelPainter;

bool inside(ChromePoint point, float x0, float y0, float x1, float y1) {
  return point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1;
}

bool canvas_overlays_visible(const ChromeState& state) {
  return state.popup == ChromePopup::kNone && !state.confirm_new &&
         state.export_status == ChromeExportStatus::kIdle;
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
  draw_dock(painter, kColorPopupTop, kColorPopupBottom);
  for (std::size_t index = 0; index < kPaletteColorCount; ++index) {
    const std::size_t column = index % kPaletteCentersX.size();
    const std::size_t row = index / kPaletteCentersX.size();
    const int cx = kPaletteCentersX[column];
    const int cy = kPaletteCentersY[row];
    if (index == state.color_index) {
      painter.circle(cx, cy, 35, kSelected);
      painter.circle(cx, cy, 32, kWhite);
    } else {
      painter.circle(cx, cy, 32, kBorder);
    }
    painter.circle(cx, cy, 28, kPico8Palettes[state.palette_page][index]);
  }
  painter.rounded({8, 6, 88, 58}, 10, state.palette_page == 0 ? kMuted : kSelected);
  painter.rounded({kWidth - 88, 6, kWidth - 8, 58}, 10,
                  state.palette_page == 1 ? kMuted : kSelected);
  draw_arrow(painter, 48, 32, false, kWhite);
  draw_arrow(painter, kWidth - 48, 32, true, kWhite);
}

void draw_bottom(Painter& painter, const ChromeState& state) {
  // The physical panel already rounds the lower corners. A full-width white
  // slab prevents canvas pixels from peeking around an independently rounded
  // dock; two owned rows fake a subtle elevation shadow at its top edge.
  painter.rect({0, kChromeCanvasBottom, kWidth, kMainTop}, kShadow);
  painter.rect({0, kChromeCanvasBottom + 1, kWidth, kMainTop}, kBorder);
  painter.rect({0, kMainTop, kWidth, kHeight}, kWhite);
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

void draw_battery(Painter& painter, const ChromeState& state) {
  if (state.battery_percentage < 0) {
    return;
  }
  const int percentage = std::clamp(state.battery_percentage, 0, 100);
  painter.rounded({kBatteryLeft + 2, kBatteryTop + 3, kBatteryRight + 2, kBatteryBottom + 3}, 9,
                  kShadow);
  painter.rounded({kBatteryLeft - 1, kBatteryTop - 1, kBatteryRight + 1, kBatteryBottom + 1}, 9,
                  kBorder);
  painter.rounded({kBatteryLeft, kBatteryTop, kBatteryRight, kBatteryBottom}, 8, kWhite);

  constexpr int icon_left = kBatteryLeft + 8;
  constexpr int icon_top = 28;
  constexpr int icon_right = icon_left + 30;
  constexpr int icon_bottom = 45;
  const std::uint16_t outline = state.battery_charging ? kSelected : kInk;
  painter.rect({icon_left, icon_top, icon_right, icon_top + 2}, outline);
  painter.rect({icon_left, icon_bottom - 2, icon_right, icon_bottom}, outline);
  painter.rect({icon_left, icon_top, icon_left + 2, icon_bottom}, outline);
  painter.rect({icon_right - 2, icon_top, icon_right, icon_bottom}, outline);
  painter.rect({icon_right, icon_top + 6, icon_right + 4, icon_bottom - 6}, outline);
  const int level_width = percentage * (icon_right - icon_left - 8) / 100;
  painter.rect({icon_left + 4, icon_top + 4, icon_left + 4 + level_width, icon_bottom - 4},
               outline);
  if (state.battery_charging) {
    painter.line({icon_left + 19, icon_top + 1, icon_left + 11, icon_top + 9}, 0xF569U);
    painter.line({icon_left + 11, icon_top + 9, icon_left + 18, icon_top + 9}, 0xF569U);
    painter.line({icon_left + 18, icon_top + 9, icon_left + 10, icon_bottom - 3}, 0xF569U);
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
  constexpr int text_advance = 6 * 5 / 2;
  const int text_x = kBatteryRight - 8 - static_cast<int>(length) * text_advance;
  painter.text(text_x, 28, std::string_view(label.data(), length), kInk, 5, 2);
}

void draw_zoom_rail(Painter& painter, const ChromeNavigation& navigation) {
  painter.rounded(
      {kZoomRailRect.x0 + 2, kZoomRailRect.y0 + 3, kZoomRailRect.x1 + 2, kZoomRailRect.y1 + 3}, 11,
      kShadow);
  painter.rounded(
      {kZoomRailRect.x0 - 1, kZoomRailRect.y0 - 1, kZoomRailRect.x1 + 1, kZoomRailRect.y1 + 1}, 11,
      kBorder);
  painter.rounded({kZoomRailRect.x0, kZoomRailRect.y0, kZoomRailRect.x1, kZoomRailRect.y1}, 10,
                  kWhite);
  painter.rect({kZoomRailRect.x0, 123, kZoomRailRect.x1, 125}, kBorder);
  painter.rect({kZoomRailRect.x0, 173, kZoomRailRect.x1, 175}, kBorder);
  const std::uint16_t plus = navigation.zoom_percent < 400 ? kInk : kMuted;
  const std::uint16_t minus = navigation.zoom_percent > 25 ? kInk : kMuted;
  painter.line({320, 98, 344, 98}, plus, 2);
  painter.line({332, 86, 332, 110}, plus, 2);
  painter.line({320, 200, 344, 200}, minus, 2);

  std::array<char, 4> label{};
  std::size_t length = 0;
  int value = std::clamp(navigation.zoom_percent, 0, 999);
  if (value >= 100) {
    label[length++] = static_cast<char>('0' + value / 100);
  }
  if (value >= 10) {
    label[length++] = static_cast<char>('0' + (value / 10) % 10);
  }
  label[length++] = static_cast<char>('0' + value % 10);
  label[length++] = '%';
  const int text_x = 332 - static_cast<int>(length) * 6;
  painter.text(text_x, 142, std::string_view(label.data(), length), kInk);
}

// Fills the minimap interior with the resampled overview. The interior is
// fully inside the surface, so rows write directly without per-pixel
// clipping; this runs on every pan frame and must stay cheap.
void draw_minimap_content(const MinimapSurface& surface, const ChromeNavigation& navigation) {
  const int map_left = kMinimapLeft - surface.origin_x;
  const int map_top = kMinimapTop - surface.origin_y;
  // Row-wise resample through a source-column table.
  std::array<std::uint16_t, kMinimapWidth> source_columns{};
  for (int x = 0; x < kMinimapWidth; ++x) {
    source_columns[static_cast<std::size_t>(x)] =
        static_cast<std::uint16_t>(x * kWidth / kMinimapWidth);
  }
  // Rows outside the surface clip away, so a strip surface covering only
  // part of the minimap draws exactly its share.
  const int first_row = std::max(0, -map_top);
  const int last_row = std::min(kMinimapHeight, surface.height - map_top);
  for (int y = first_row; y < last_row; ++y) {
    const int source_y = y * kHeight / kMinimapHeight;
    const auto source =
        navigation.overview_pixels.subspan(static_cast<std::size_t>(source_y) * kWidth, kWidth);
    const auto destination = surface.pixels.subspan(
        static_cast<std::size_t>(map_top + y) * static_cast<std::size_t>(surface.width) +
            static_cast<std::size_t>(map_left),
        kMinimapWidth);
    for (int x = 0; x < kMinimapWidth; ++x) {
      destination[static_cast<std::size_t>(x)] =
          source[source_columns[static_cast<std::size_t>(x)]];
    }
  }
}

// Draws the viewport rectangle over the minimap interior in absolute panel
// coordinates; the painter translates and clips.
void draw_minimap_viewport(Painter& painter, const ChromeNavigation& navigation) {
  const int map_left = kMinimapLeft;
  const int map_top = kMinimapTop;
  const int level_width = std::max(navigation.level_width, 1);
  const int level_height = std::max(navigation.level_height, 1);
  const int x0 = map_left + navigation.level_x * kMinimapWidth / level_width;
  const int y0 = map_top + navigation.level_y * kMinimapHeight / level_height;
  const int x1 = map_left + (navigation.level_x + kWidth) * kMinimapWidth / level_width;
  const int y1 =
      map_top + (navigation.level_y + kChromeCanvasBottom) * kMinimapHeight / level_height;
  const int right = std::clamp(x1, x0 + 2, map_left + kMinimapWidth);
  const int bottom = std::clamp(y1, y0 + 2, map_top + kMinimapHeight);
  painter.line({x0, y0, right, y0}, kSelected, 1);
  painter.line({right, y0, right, bottom}, kSelected, 1);
  painter.line({right, bottom, x0, bottom}, kSelected, 1);
  painter.line({x0, bottom, x0, y0}, kSelected, 1);
}

// Draws the minimap into a surface. The full-frame path passes origin (0, 0)
// with the panel-sized surface; the pan path passes a small scratch surface
// covering just the minimap overlay region.
void draw_minimap(const MinimapSurface& surface, const ChromeNavigation& navigation) {
  Painter painter(surface.pixels, surface.width, surface.height, surface.origin_x,
                  surface.origin_y);
  painter.rounded(
      {kMinimapRect.x0 + 2, kMinimapRect.y0 + 3, kMinimapRect.x1 + 2, kMinimapRect.y1 + 3}, 9,
      kShadow);
  painter.rounded(
      {kMinimapRect.x0 - 1, kMinimapRect.y0 - 1, kMinimapRect.x1 + 1, kMinimapRect.y1 + 1}, 9,
      kBorder);
  painter.rounded({kMinimapRect.x0, kMinimapRect.y0, kMinimapRect.x1, kMinimapRect.y1}, 8, kWhite);
  const int map_left = kMinimapLeft - surface.origin_x;
  // Both the interior content and its white fallback draw only when the
  // interior fits horizontally: a surface that clips the minimap in x (for
  // example one covering just the zoom rail) must not receive a partial
  // white block.
  const bool interior_fits = map_left >= 0 && map_left + kMinimapWidth <= surface.width;
  if (interior_fits) {
    if (navigation.overview_pixels.size() >=
        static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight)) {
      draw_minimap_content(surface, navigation);
    } else {
      painter.rect(
          {kMinimapLeft, kMinimapTop, kMinimapLeft + kMinimapWidth, kMinimapTop + kMinimapHeight},
          kWhite);
    }
  }
  draw_minimap_viewport(painter, navigation);
}

// Draws every canvas overlay's share that intersects the surface, which is
// one full-width sweep strip during a pan frame. Absolute coordinates, the
// painter and the minimap content path translate and clip.
bool draw_strip_overlays(const MinimapSurface& surface, const ChromeState& state,
                         const ChromeNavigation& navigation) {
  if (!canvas_overlays_visible(state)) {
    return false;
  }
  // Each overlay draws only when its rect can intersect the surface: the
  // painters clip correctly but still walk their full geometry, which cost
  // ~5 ms per pan frame across the per-overlay prep surfaces.
  const auto intersects = [&surface](const ChromeRect& rect) {
    return rect.x0 - 2 < surface.origin_x + surface.width && rect.x1 + 2 > surface.origin_x &&
           rect.y0 - 3 < surface.origin_y + surface.height && rect.y1 + 3 > surface.origin_y;
  };
  Painter painter(surface.pixels, surface.width, surface.height, surface.origin_x,
                  surface.origin_y);
  if (intersects(kZoomRailOverlayRect)) {
    draw_zoom_rail(painter, navigation);
  }
  if (intersects(kMinimapOverlayRect)) {
    draw_minimap(surface, navigation);
  }
  if (state.battery_percentage >= 0 && intersects(kBatteryOverlayRect)) {
    draw_battery(painter, state);
  }
  return true;
}

void draw_fixed_chrome(const MinimapSurface& surface, const ChromeState& state) {
  const int surface_top = surface.origin_y;
  const int surface_bottom = surface.origin_y + surface.height;
  const auto intersects_rows = [surface_top, surface_bottom](int top, int bottom) {
    return top < surface_bottom && bottom > surface_top;
  };
  Painter painter(surface.pixels, surface.width, surface.height, surface.origin_x,
                  surface.origin_y);
  if (intersects_rows(kChromeCanvasBottom, kHeight)) {
    draw_bottom(painter, state);
  }
  if (state.popup == ChromePopup::kColors && !state.confirm_new &&
      state.export_status == ChromeExportStatus::kIdle) {
    if (intersects_rows(kColorPopupTop, kColorPopupBottom)) {
      draw_palette(painter, state);
    }
    return;
  }
  if (state.popup != ChromePopup::kNone &&
      intersects_rows(chrome_canvas_bottom(state), kPopupBottom + 4)) {
    painter.rect({0, chrome_canvas_bottom(state), kWidth, kPopupTop}, kWhite);
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
  if (state.confirm_new && intersects_rows(kDialogTop - 1, kDialogBottom + 5)) {
    draw_new_dialog(painter);
  }
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
  painter.text(saved ? 139 : 130, saved ? 91 : 90, saved ? "SAVED" : "ERROR",
               saved ? kInk : 0xE186U, 3);
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

int chrome_ink_bottom(const ChromeState& state) {
  if (state.confirm_new || state.export_status == ChromeExportStatus::kSaving) {
    return 0;
  }
  if (state.popup == ChromePopup::kNone) {
    // Committed ink continues under the dock: the rows behind it are real
    // world content, chrome renders on top, and the stroke reappears when
    // the view pans. This matches drawing under the minimap and zoom rail.
    return kHeight;
  }
  return state.popup == ChromePopup::kColors ? 0 : kChromePopupInputBottom;
}

std::optional<ChromePoint> clip_canvas_segment(ChromePoint previous, ChromePoint current,
                                               const ChromeState& state) {
  const int input_bottom = chrome_ink_bottom(state);
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
  // The dock face starts at the canvas bottom; rows above it are visible
  // canvas where strokes may start even though committed ink continues
  // beneath the dock.
  const bool main = inside(point, 0.0F, static_cast<float>(kChromeCanvasBottom),
                           static_cast<float>(kWidth), static_cast<float>(kHeight));
  const bool popup = state.popup != ChromePopup::kNone &&
                     inside(point, 0.0F, static_cast<float>(kPopupTop - kHitSlop),
                            static_cast<float>(kWidth), static_cast<float>(kMainTop));
  const bool zoom_rail =
      canvas_overlays_visible(state) &&
      inside(point, static_cast<float>(kZoomRailRect.x0 - kHitSlop),
             static_cast<float>(kZoomRailRect.y0 - kHitSlop), static_cast<float>(kWidth),
             static_cast<float>(kZoomRailRect.y1 + kHitSlop));
  return main || popup || zoom_rail;
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

namespace {

ChromeAction confirmation_action_at(ChromePoint point) {
  if (inside(point, 36.0F, 194.0F, 184.0F, 276.0F)) {
    return ChromeAction::kCancelNewDrawing;
  }
  if (inside(point, 184.0F, 194.0F, 332.0F, 276.0F)) {
    return ChromeAction::kConfirmNewDrawing;
  }
  return ChromeAction::kNone;
}

ChromeAction palette_action_at(ChromePoint point, const ChromeState& state) {
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

ChromeAction popup_action_at(ChromePoint point, ChromePopup popup) {
  const std::size_t third =
      std::min(static_cast<std::size_t>(point.x * 3.0F / kWidth), std::size_t{2});
  switch (popup) {
    case ChromePopup::kTools: {
      constexpr std::array actions{ChromeAction::kSelectDraw, ChromeAction::kSelectErase,
                                   ChromeAction::kSelectPan};
      return actions[third];
    }
    case ChromePopup::kSizes: {
      const std::size_t quarter =
          std::min(static_cast<std::size_t>(point.x * 4.0F / kWidth), std::size_t{3});
      constexpr std::array actions{ChromeAction::kSelectSmall, ChromeAction::kSelectMedium,
                                   ChromeAction::kSelectLarge, ChromeAction::kSelectExtraLarge};
      return actions[quarter];
    }
    case ChromePopup::kDocument:
      return point.x < kWidth / 2.0F ? ChromeAction::kNewDrawing : ChromeAction::kExport;
    case ChromePopup::kNone:
    case ChromePopup::kColors:
      return ChromeAction::kNone;
  }
  return ChromeAction::kNone;
}

ChromeAction zoom_action_at(ChromePoint point) {
  if (point.y < 125.0F) {
    return ChromeAction::kZoomIn;
  }
  return point.y >= 175.0F ? ChromeAction::kZoomOut : ChromeAction::kNone;
}

}  // namespace

ChromeAction chrome_action_at(ChromePoint point, const ChromeState& state) {
  if (state.confirm_new) {
    return confirmation_action_at(point);
  }
  if (state.export_status == ChromeExportStatus::kSaving) {
    return ChromeAction::kNone;
  }
  if (state.popup == ChromePopup::kColors) {
    if (inside(point, 0.0F, static_cast<float>(kMainHitTop), static_cast<float>(kWidth),
               static_cast<float>(kHeight))) {
      const std::size_t index =
          std::min(static_cast<std::size_t>(point.x * 6.0F / kWidth), std::size_t{5});
      return index == 3U ? ChromeAction::kToggleColors : ChromeAction::kNone;
    }
    return palette_action_at(point, state);
  }
  const bool in_popup = state.popup != ChromePopup::kNone &&
                        inside(point, 0.0F, static_cast<float>(kPopupTop - kHitSlop),
                               static_cast<float>(kWidth), static_cast<float>(kMainTop));
  if (in_popup) {
    return popup_action_at(point, state.popup);
  }
  const bool in_zoom =
      canvas_overlays_visible(state) &&
      inside(point, static_cast<float>(kZoomRailRect.x0 - kHitSlop),
             static_cast<float>(kZoomRailRect.y0 - kHitSlop), static_cast<float>(kWidth),
             static_cast<float>(kZoomRailRect.y1 + kHitSlop));
  if (in_zoom) {
    return zoom_action_at(point);
  }
  if (!inside(point, 0.0F, static_cast<float>(kChromeCanvasBottom), static_cast<float>(kWidth),
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

std::optional<ChromeRect> chrome_minimap_region(const ChromeState& state) {
  return canvas_overlays_visible(state) ? std::optional{kMinimapOverlayRect} : std::nullopt;
}

bool chrome_minimap_refresh_required(const ChromeState& state, bool overview_changed,
                                     bool allow_minimap_refresh) {
  return canvas_overlays_visible(state) && overview_changed && allow_minimap_refresh;
}

ChromeOverlayRegions chrome_overlay_regions(const ChromeState& state) {
  ChromeOverlayRegions result;
  if (!canvas_overlays_visible(state)) {
    return result;
  }
  result.regions[result.count++] = kZoomRailOverlayRect;
  result.regions[result.count++] = kMinimapOverlayRect;
  if (state.battery_percentage >= 0) {
    result.regions[result.count++] = kBatteryOverlayRect;
  }
  return result;
}

ChromePresentationRegions chrome_unobscured_regions(ChromeRect bounds, const ChromeState& state) {
  const auto align_to_panel = [](ChromeRect region) {
    region.x0 = std::clamp(region.x0 & ~1, 0, kWidth);
    region.y0 = std::clamp(region.y0 & ~1, 0, kHeight);
    region.x1 = std::clamp((region.x1 + 1) & ~1, region.x0, kWidth);
    region.y1 = std::clamp((region.y1 + 1) & ~1, region.y0, kHeight);
    return region;
  };
  const auto append = [](ChromePresentationRegions& regions, ChromeRect region) {
    if (region.x1 <= region.x0 || region.y1 <= region.y0) {
      return;
    }
    if (regions.count >= regions.regions.size()) {
      regions.overflowed = true;
      return;
    }
    regions.regions[regions.count++] = region;
  };

  ChromePresentationRegions visible;
  append(visible, align_to_panel(bounds));
  const auto overlays = chrome_overlay_regions(state);
  for (std::size_t overlay_index = 0; overlay_index < overlays.count; ++overlay_index) {
    const ChromeRect overlay = align_to_panel(overlays.regions[overlay_index]);
    ChromePresentationRegions next;
    next.overflowed = visible.overflowed;
    for (std::size_t region_index = 0; region_index < visible.count; ++region_index) {
      const ChromeRect region = visible.regions[region_index];
      const ChromeRect intersection{
          std::max(region.x0, overlay.x0), std::max(region.y0, overlay.y0),
          std::min(region.x1, overlay.x1), std::min(region.y1, overlay.y1)};
      if (intersection.x1 <= intersection.x0 || intersection.y1 <= intersection.y0) {
        append(next, region);
        continue;
      }
      append(next, {region.x0, region.y0, region.x1, intersection.y0});
      append(next, {region.x0, intersection.y1, region.x1, region.y1});
      append(next, {region.x0, intersection.y0, intersection.x0, intersection.y1});
      append(next, {intersection.x1, intersection.y0, region.x1, intersection.y1});
    }
    visible = next;
  }
  return visible;
}

void draw_chrome(std::span<std::uint16_t> pixels, int width, int height, const ChromeState& state) {
  if (width != kWidth || height != kHeight ||
      pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return;
  }
  const MinimapSurface surface{pixels, width, height, 0, 0};
  draw_fixed_chrome(surface, state);
  Painter painter(pixels, width, height);
  draw_export_toast(painter, state);
}

void draw_chrome_canvas_overlays(std::span<std::uint16_t> pixels, int width, int height,
                                 const ChromeState& state, const ChromeNavigation& navigation) {
  if (width != kWidth || height != kHeight ||
      pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
      !canvas_overlays_visible(state)) {
    return;
  }
  Painter painter(pixels, width, height);
  draw_zoom_rail(painter, navigation);
  draw_minimap({pixels, width, height, 0, 0}, navigation);
  draw_battery(painter, state);
}

bool draw_chrome_minimap_overlay(std::span<std::uint16_t> pixels, int width, int height,
                                 const ChromeState& state, const ChromeNavigation& navigation) {
  if (width != kWidth || height != kHeight ||
      pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
      !canvas_overlays_visible(state)) {
    return false;
  }
  draw_minimap({pixels, width, height, 0, 0}, navigation);
  return true;
}

bool draw_chrome_minimap_surface(const MinimapSurface& surface, const ChromeState& state,
                                 const ChromeNavigation& navigation) {
  if (surface.width <= 0 || surface.height <= 0 ||
      surface.pixels.size() <
          static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height) ||
      !canvas_overlays_visible(state)) {
    return false;
  }
  draw_minimap(surface, navigation);
  return true;
}

bool draw_chrome_strip_overlays(const MinimapSurface& surface, const ChromeState& state,
                                const ChromeNavigation& navigation) {
  if (surface.width <= 0 || surface.height <= 0 ||
      surface.pixels.size() <
          static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height)) {
    return false;
  }
  return draw_strip_overlays(surface, state, navigation);
}

bool draw_chrome_staging_surface(const MinimapSurface& surface, const ChromeState& state,
                                 const ChromeNavigation& navigation) {
  if (surface.width <= 0 || surface.height <= 0 ||
      surface.pixels.size() <
          static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height)) {
    return false;
  }
  draw_fixed_chrome(surface, state);
  Painter painter(surface.pixels, surface.width, surface.height, surface.origin_x,
                  surface.origin_y);
  draw_export_toast(painter, state);
  static_cast<void>(draw_strip_overlays(surface, state, navigation));
  return true;
}

}  // namespace tinydraw::vector_v2
