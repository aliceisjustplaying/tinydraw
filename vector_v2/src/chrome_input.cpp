#include <algorithm>
#include <array>
#include <cmath>

#include "chrome_layout.h"
#include "tinydraw/vector_v2/chrome.h"

namespace tinydraw::vector_v2 {
using namespace chrome_layout;

std::uint16_t selected_color(const ChromeState& state) {
  const std::size_t page = std::min<std::size_t>(state.palette_page, 1U);
  const std::size_t color = std::min<std::size_t>(state.color_index, kPaletteColorCount - 1U);
  return kPico8Palettes[page][color];
}

ChromeTimeSyncStatus chrome_time_sync_status_after(ChromeTimeSyncStatus status,
                                                   std::uint32_t elapsed_us) {
  const bool terminal =
      status == ChromeTimeSyncStatus::kSaved || status == ChromeTimeSyncStatus::kError;
  return terminal && elapsed_us >= kChromeTimeSyncToastDurationUs ? ChromeTimeSyncStatus::kIdle
                                                                  : status;
}

float brush_size(ChromeSize size) {
  switch (size) {
    case ChromeSize::kSmall:
      return 3.0F;
    case ChromeSize::kMedium:
      return 6.0F;
    case ChromeSize::kLarge:
      return 12.0F;
    case ChromeSize::kExtraLarge:
      return 24.0F;
    case ChromeSize::kDoubleExtraLarge:
      return 36.0F;
    case ChromeSize::kTripleExtraLarge:
      return 64.0F;
  }
  return 6.0F;
}

int chrome_canvas_bottom(const ChromeState& state) {
  if (export_mode_active(state)) {
    return 0;
  }
  if (!state.hud_visible) {
    return kHeight;
  }
  if (state.popup == ChromePopup::kNone) {
    return kChromeCanvasBottom;
  }
  if (state.popup == ChromePopup::kColors) {
    return 0;
  }
  return state.popup == ChromePopup::kSizes ? kChromeSizePopupCanvasBottom
                                            : kChromePopupCanvasBottom;
}

int chrome_input_bottom(const ChromeState& state) {
  if (state.confirm_new || state.export_status == ChromeExportStatus::kSaving ||
      export_mode_active(state) || time_sync_active(state)) {
    return 0;
  }
  if (!state.hud_visible) {
    return kHeight;
  }
  if (state.popup == ChromePopup::kNone) {
    return kChromeCanvasBottom;
  }
  if (state.popup == ChromePopup::kColors) {
    return 0;
  }
  return state.popup == ChromePopup::kSizes ? kChromeSizePopupInputBottom : kChromePopupInputBottom;
}

int chrome_ink_bottom(const ChromeState& state) {
  if (state.confirm_new || state.export_status == ChromeExportStatus::kSaving ||
      export_mode_active(state) || time_sync_active(state)) {
    return 0;
  }
  if (state.popup == ChromePopup::kNone) {
    // Committed ink continues under the dock: the rows behind it are real
    // world content, chrome renders on top, and the stroke reappears when
    // the view pans. This matches drawing under the minimap and zoom rail.
    return kHeight;
  }
  if (state.popup == ChromePopup::kColors) {
    return 0;
  }
  return state.popup == ChromePopup::kSizes ? kChromeSizePopupInputBottom : kChromePopupInputBottom;
}

bool chrome_can_toggle_hud(const ChromeState& state) {
  return !state.confirm_new && state.export_status == ChromeExportStatus::kIdle &&
         state.time_sync_status == ChromeTimeSyncStatus::kIdle;
}

bool toggle_hud_visibility(ChromeState& state) {
  if (!chrome_can_toggle_hud(state)) {
    return false;
  }
  state.hud_visible = !state.hud_visible;
  if (!state.hud_visible) {
    state.popup = ChromePopup::kNone;
  }
  return true;
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

ChromePoint attract_canvas_edges(ChromePoint point, ChromePoint stroke_origin,
                                 const ChromeState& state) {
  constexpr float kEdgeAttractionBandPixels = 36.0F;
  constexpr float kDirectionDeadZonePixels = 2.0F;
  constexpr float kMaximumNormalPerTangent = 0.125F;
  constexpr float kRightEdge = static_cast<float>(kWidth - 1);
  constexpr float kBottomEdge = static_cast<float>(kHeight - 1);
  const auto attract_low = [=](float coordinate) {
    return coordinate < kEdgeAttractionBandPixels
               ? std::max(0.0F, coordinate * 2.0F - kEdgeAttractionBandPixels)
               : coordinate;
  };
  const auto attract_high = [=](float coordinate, float edge) {
    const float distance = edge - coordinate;
    return distance < kEdgeAttractionBandPixels
               ? edge - std::max(0.0F, distance * 2.0F - kEdgeAttractionBandPixels)
               : coordinate;
  };
  const float horizontal_travel = std::abs(point.x - stroke_origin.x);
  const float vertical_travel = std::abs(point.y - stroke_origin.y);
  const bool follows_vertical_edge =
      horizontal_travel <=
      std::max(kDirectionDeadZonePixels, vertical_travel * kMaximumNormalPerTangent);
  const bool follows_horizontal_edge =
      vertical_travel <=
      std::max(kDirectionDeadZonePixels, horizontal_travel * kMaximumNormalPerTangent);
  const bool started_near_left = stroke_origin.x < kEdgeAttractionBandPixels;
  const bool started_near_right = kRightEdge - stroke_origin.x < kEdgeAttractionBandPixels;
  const bool started_near_top = stroke_origin.y < kEdgeAttractionBandPixels;
  const bool started_near_bottom = kBottomEdge - stroke_origin.y < kEdgeAttractionBandPixels;
  if (!started_near_left || follows_vertical_edge) {
    point.x = attract_low(point.x);
  }
  if (!started_near_right || follows_vertical_edge) {
    point.x = attract_high(point.x, kRightEdge);
  }
  if (!started_near_top || follows_horizontal_edge) {
    point.y = attract_low(point.y);
  }
  if (!state.hud_visible && (!started_near_bottom || follows_horizontal_edge)) {
    point.y = attract_high(point.y, kBottomEdge);
  }
  return point;
}

bool chrome_accepts_stroke_finish(ChromePoint start, bool has_drawn_segment) {
  constexpr float kTopSensorFringePixels = 4.0F;
  return has_drawn_segment || start.y >= kTopSensorFringePixels;
}

static bool zoom_rail_contains(ChromePoint point, const ChromeState& state) {
  return canvas_overlays_visible(state) &&
         inside(point, static_cast<float>(kZoomRailRect.x0 - kZoomRailHitSlop),
                static_cast<float>(kZoomRailRect.y0 - kZoomRailHitSlop), static_cast<float>(kWidth),
                static_cast<float>(kZoomRailRect.y1 + kZoomRailHitSlop));
}

static bool navigation_guard_contains(ChromePoint point, const ChromeState& state) {
  return canvas_overlays_visible(state) &&
         inside(point, static_cast<float>(kNavigationGuardRect.x0),
                static_cast<float>(kNavigationGuardRect.y0),
                static_cast<float>(kNavigationGuardRect.x1),
                static_cast<float>(kNavigationGuardRect.y1));
}

bool chrome_minimap_contains(ChromePoint point, const ChromeState& state) {
  return canvas_overlays_visible(state) &&
         inside(point, static_cast<float>(kMinimapHitRect.x0),
                static_cast<float>(kMinimapHitRect.y0), static_cast<float>(kMinimapHitRect.x1),
                static_cast<float>(kMinimapHitRect.y1));
}

ChromeLevelPoint chrome_minimap_level_point(ChromePoint point, const ChromeNavigation& navigation) {
  const auto project = [](float coordinate, int origin, int span, int extent) {
    const float scaled = (coordinate - static_cast<float>(origin)) *
                         static_cast<float>(std::max(extent, 0)) / static_cast<float>(span);
    return std::clamp(static_cast<int>(std::lround(scaled)), 0, std::max(extent, 0));
  };
  return {
      .x = project(point.x, kMinimapLeft, kMinimapWidth, navigation.level_width),
      .y = project(point.y, kMinimapTop, kMinimapHeight, navigation.level_height),
  };
}

ChromeLevelPoint chrome_minimap_drag_origin(ChromePoint current, ChromeLevelPoint focus,
                                            const ChromeNavigation& navigation) {
  const ChromeLevelPoint current_level = chrome_minimap_level_point(current, navigation);
  // The minimap is an absolute pointer, not a miniature relative trackpad.
  // Every position centers the viewport beneath the finger, so available map
  // travel always spans the complete world even when the viewport indicator
  // is only a few pixels wide at 400%.
  const int requested_x = current_level.x - focus.x;
  const int requested_y = current_level.y - focus.y;
  // Preserve the even-origin invariant used by the fast ring-shift path.
  const auto quantize_even = [](int delta) { return delta - delta % 2; };
  const int quantized_x = navigation.level_x + quantize_even(requested_x - navigation.level_x);
  const int quantized_y = navigation.level_y + quantize_even(requested_y - navigation.level_y);
  return {
      .x = std::clamp(quantized_x, 0, std::max(navigation.level_width - kWidth, 0)),
      .y = std::clamp(quantized_y, 0, std::max(navigation.level_height - kChromeCanvasBottom, 0)),
  };
}

bool chrome_minimap_dock_drag_candidate(ChromePoint point, const ChromeState& state) {
  return canvas_overlays_visible(state) &&
         inside(point, static_cast<float>(kMinimapDockDragRect.x0),
                static_cast<float>(kMinimapDockDragRect.y0),
                static_cast<float>(kMinimapDockDragRect.x1),
                static_cast<float>(kMinimapDockDragRect.y1));
}

bool chrome_promotes_minimap_dock_drag(ChromePoint start, ChromePoint current,
                                       const ChromeState& state) {
  if (!chrome_minimap_dock_drag_candidate(start, state)) {
    return false;
  }
  const float delta_x = current.x - start.x;
  const float delta_y = current.y - start.y;
  // Returning upward toward the visible map is unambiguous intent and should
  // not feel stuck behind the full dock-tap jitter band. Horizontal/downward
  // motion retains 8 px because it crosses the actual size/document buttons.
  return delta_y <= -kMinimapDockUpPromotionPixels ||
         delta_x * delta_x + delta_y * delta_y >=
             kMinimapDockDragPromotionPixels * kMinimapDockDragPromotionPixels;
}

bool chrome_suppresses_minimap_recontact(ChromePoint released, ChromePoint current,
                                         std::uint32_t elapsed_us, const ChromeState& state) {
  constexpr std::uint32_t kRecontactWindowUs = 120'000U;
  constexpr float kRecontactRadiusPixels = 32.0F;
  if (elapsed_us > kRecontactWindowUs || !chrome_contains(current, state)) {
    return false;
  }
  const float delta_x = current.x - released.x;
  const float delta_y = current.y - released.y;
  return delta_x * delta_x + delta_y * delta_y <= kRecontactRadiusPixels * kRecontactRadiusPixels;
}

bool chrome_contains(ChromePoint point, const ChromeState& state) {
  if (state.confirm_new || state.export_status == ChromeExportStatus::kSaving ||
      export_mode_active(state) || time_sync_active(state)) {
    return inside(point, 0.0F, 0.0F, static_cast<float>(kWidth), static_cast<float>(kHeight));
  }
  if (!state.hud_visible) {
    return false;
  }
  if (state.popup == ChromePopup::kColors) {
    return inside(point, 0.0F, 0.0F, static_cast<float>(kWidth), static_cast<float>(kHeight));
  }
  // The dock face starts at the canvas bottom; rows above it are visible
  // canvas where strokes may start even though committed ink continues
  // beneath the dock.
  const bool main = inside(point, 0.0F, static_cast<float>(kChromeCanvasBottom),
                           static_cast<float>(kWidth), static_cast<float>(kHeight));
  const int popup_top = state.popup == ChromePopup::kSizes ? kSizePopupTop : kPopupTop;
  const bool popup = state.popup != ChromePopup::kNone &&
                     inside(point, 0.0F, static_cast<float>(popup_top - kHitSlop),
                            static_cast<float>(kWidth), static_cast<float>(kMainTop));
  return main || popup || navigation_guard_contains(point, state) ||
         zoom_rail_contains(point, state) || chrome_minimap_contains(point, state);
}

bool chrome_promotes_pan_drag(ChromePoint start, ChromePoint current, const ChromeState& state) {
  if (state.tool != ChromeTool::kPan || !zoom_rail_contains(start, state)) {
    return false;
  }
  const float delta_x = current.x - start.x;
  const float delta_y = current.y - start.y;
  return delta_x * delta_x + delta_y * delta_y >= kPanDragPromotionPixels * kPanDragPromotionPixels;
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
      const std::size_t column = std::min(
          static_cast<std::size_t>(std::max(0.0F, point.x) * 3.0F / kWidth), std::size_t{2});
      const int popup_height = kPopupBottom - kSizePopupTop;
      const int row =
          std::clamp((static_cast<int>(point.y) - kSizePopupTop) * 2 / popup_height, 0, 1);
      constexpr std::array actions{
          ChromeAction::kSelectSmall,
          ChromeAction::kSelectMedium,
          ChromeAction::kSelectLarge,
          ChromeAction::kSelectExtraLarge,
          ChromeAction::kSelectDoubleExtraLarge,
          ChromeAction::kSelectTripleExtraLarge,
      };
      return actions[static_cast<std::size_t>(row) * 3U + column];
    }
    case ChromePopup::kDocument: {
      constexpr std::array actions{ChromeAction::kNewDrawing, ChromeAction::kExport,
                                   ChromeAction::kSyncTime};
      return actions[third];
    }
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
  if (export_mode_active(state)) {
    return inside(point, static_cast<float>(kExportExitRect.x0),
                  static_cast<float>(kExportExitRect.y0), static_cast<float>(kExportExitRect.x1),
                  static_cast<float>(kExportExitRect.y1))
               ? ChromeAction::kExitExport
               : ChromeAction::kNone;
  }
  if (state.export_status == ChromeExportStatus::kSaving || time_sync_active(state)) {
    return ChromeAction::kNone;
  }
  if (!state.hud_visible) {
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
  const int popup_top = state.popup == ChromePopup::kSizes ? kSizePopupTop : kPopupTop;
  const bool in_popup = state.popup != ChromePopup::kNone &&
                        inside(point, 0.0F, static_cast<float>(popup_top - kHitSlop),
                               static_cast<float>(kWidth), static_cast<float>(kMainTop));
  if (in_popup) {
    return popup_action_at(point, state.popup);
  }
  if (zoom_rail_contains(point, state)) {
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
  const ChromeAction action = actions[index];
  if (state.popup == ChromePopup::kNone) {
    return action;
  }
  const ChromeAction active_popup_toggle =
      state.popup == ChromePopup::kTools   ? ChromeAction::kToggleTools
      : state.popup == ChromePopup::kSizes ? ChromeAction::kToggleSizes
                                           : ChromeAction::kToggleDocument;
  return action == active_popup_toggle ? action : ChromeAction::kNone;
}

ChromeRect chrome_battery_region() { return kBatteryOverlayRect; }

ChromeRect chrome_recording_region() { return kRecordingRect; }

ChromeRect chrome_history_busy_region() {
  return {kHistoryBusyLeft - 1, kHistoryBusyTop - 1, kHistoryBusyRight + 2, kHistoryBusyBottom + 3};
}

ChromeRect chrome_history_controls_region() {
  // Undo and Redo are the first two 60 px dock cells. Include the seam's
  // antialias fringe while keeping unrelated tool cells off the SPI wire.
  return {0, kChromeCanvasBottom, 124, kHeight};
}

std::optional<ChromeRect> chrome_minimap_region(const ChromeState& state) {
  return canvas_overlays_visible(state) ? std::optional{kMinimapOverlayRect} : std::nullopt;
}

bool chrome_minimap_refresh_required(const ChromeState& state, bool overview_changed,
                                     bool allow_minimap_refresh) {
  return canvas_overlays_visible(state) && overview_changed && allow_minimap_refresh;
}

}  // namespace tinydraw::vector_v2
