#ifndef TINYDRAW_VECTOR_V2_CHROME_LAYOUT_H
#define TINYDRAW_VECTOR_V2_CHROME_LAYOUT_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "tinydraw/vector_v2/chrome.h"

namespace tinydraw::vector_v2 {
namespace chrome_layout {

inline constexpr std::uint16_t kWhite = 0xFFFFU;
inline constexpr std::uint16_t kInk = 0x2104U;
inline constexpr std::uint16_t kMuted = 0x8410U;
inline constexpr std::uint16_t kBorder = 0xDEDBU;
inline constexpr std::uint16_t kShadow = 0xBDF7U;
inline constexpr std::uint16_t kSelected = 0x349FU;
inline constexpr std::uint16_t kRecording = 0xE186U;
inline constexpr int kWidth = 368;
inline constexpr int kHeight = 448;
inline constexpr int kMainTop = 374;
inline constexpr int kPopupTop = 296;
inline constexpr int kPopupBottom = 366;
inline constexpr int kColorPopupTop = 4;
inline constexpr int kColorPopupBottom = 366;
inline constexpr int kDialogLeft = 28;
inline constexpr int kDialogTop = 126;
inline constexpr int kDialogRight = 340;
inline constexpr int kDialogBottom = 286;
inline constexpr int kToastLeft = 80;
inline constexpr int kToastTop = 70;
inline constexpr int kToastRight = 288;
inline constexpr int kToastBottom = 132;
inline constexpr int kExportDialogLeft = 24;
inline constexpr int kExportDialogTop = 92;
inline constexpr int kExportDialogRight = 344;
inline constexpr int kExportDialogBottom = 326;
inline constexpr ChromeRect kExportExitRect{52, 238, 316, 298};
inline constexpr int kTimeToastLeft = 80;
inline constexpr int kTimeToastRight = 288;
inline constexpr int kHistoryBusyLeft = 160;
inline constexpr int kHistoryBusyTop = 74;
inline constexpr int kHistoryBusyRight = 208;
inline constexpr int kHistoryBusyBottom = 128;
inline constexpr int kBatteryLeft = 222;
inline constexpr int kBatteryTop = 18;
inline constexpr int kBatteryRight = 340;
inline constexpr int kBatteryBottom = 54;
inline constexpr ChromeRect kZoomRailRect{304, 72, 360, 226};
inline constexpr ChromeRect kZoomRailOverlayRect{kZoomRailRect.x0 - 1, kZoomRailRect.y0 - 1,
                                                 kZoomRailRect.x1 + 2, kZoomRailRect.y1 + 3};
inline constexpr ChromeRect kMinimapRect{266, 252, 358, 366};
inline constexpr ChromeRect kMinimapHitRect{250, 236, 368, kChromeCanvasBottom};
inline constexpr ChromeRect kMinimapDockDragRect{kMinimapHitRect.x0, kChromeCanvasBottom,
                                                 kMinimapHitRect.x1, kHeight};
inline constexpr ChromeRect kMinimapOverlayRect{kMinimapRect.x0 - 1, kMinimapRect.y0 - 1,
                                                kMinimapRect.x1 + 2, kMinimapRect.y1 + 3};
inline constexpr ChromeRect kBatteryOverlayRect{kBatteryLeft - 2, kBatteryTop - 2,
                                                kBatteryRight + 4, kBatteryBottom + 6};
inline constexpr ChromeRect kBottomCacheRect{0, kChromeCanvasBottom, kWidth, kHeight};
inline constexpr ChromeRect kRecordingRect{178, 376, 191, 389};
inline constexpr std::uint16_t kCacheTransparent = 0x0001U;
inline constexpr int kMinimapLeft = 272;
inline constexpr int kMinimapTop = 258;
inline constexpr int kMinimapWidth = 80;
inline constexpr int kMinimapHeight = 98;
inline constexpr int kHitSlop = 8;
inline constexpr float kMinimapDockDragPromotionPixels = 8.0F;
inline constexpr float kMinimapDockUpPromotionPixels = 2.0F;
inline constexpr float kPanDragPromotionPixels = 8.0F;
inline constexpr int kMainHitTop = kMainTop - kHitSlop;
inline constexpr std::array kMainCenters{34, 94, 154, 214, 274, 334};
inline constexpr std::array kPopupCenters{62, 184, 306};
inline constexpr std::array kSizeCenters{52, 140, 228, 316};
inline constexpr std::array kPaletteCentersX{46, 138, 230, 322};
inline constexpr std::array kPaletteCentersY{103, 178, 253, 328};
inline constexpr int kPaletteControlsBottom = 64;
inline constexpr int kPaletteRowHeight = 75;

[[nodiscard]] constexpr std::size_t rect_pixels(ChromeRect rect) {
  return static_cast<std::size_t>(rect.x1 - rect.x0) * static_cast<std::size_t>(rect.y1 - rect.y0);
}

[[nodiscard]] inline bool inside(ChromePoint point, float x0, float y0, float x1, float y1) {
  return point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1;
}

[[nodiscard]] inline bool time_sync_active(const ChromeState& state) {
  return state.time_sync_status == ChromeTimeSyncStatus::kConnecting ||
         state.time_sync_status == ChromeTimeSyncStatus::kSynchronizing;
}

[[nodiscard]] inline bool export_mode_active(const ChromeState& state) {
  return state.export_status == ChromeExportStatus::kPresented ||
         state.export_status == ChromeExportStatus::kHostEjected ||
         state.export_status == ChromeExportStatus::kExitError;
}

[[nodiscard]] inline bool canvas_overlays_visible(const ChromeState& state) {
  return state.visible && state.popup == ChromePopup::kNone && !state.confirm_new &&
         state.export_status == ChromeExportStatus::kIdle &&
         state.time_sync_status == ChromeTimeSyncStatus::kIdle;
}

}  // namespace chrome_layout
}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_CHROME_LAYOUT_H
