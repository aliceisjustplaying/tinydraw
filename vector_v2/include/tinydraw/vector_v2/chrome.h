#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {

struct ChromePoint {
  float x = 0.0F;
  float y = 0.0F;
};

struct ChromeRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  bool operator==(const ChromeRect&) const = default;
};

inline constexpr int kChromeCanvasBottom = 372;
inline constexpr int kChromePopupInputBottom = 288;
inline constexpr int kChromePopupCanvasBottom = 294;
inline constexpr std::size_t kPaletteColorCount = 16;

enum class ChromeTool { kDraw, kErase, kPan };
enum class ChromeSize { kSmall, kMedium, kLarge, kExtraLarge };
enum class ChromePopup { kNone, kTools, kColors, kSizes, kDocument };
enum class ChromeExportStatus { kIdle, kSaving, kSaved, kError };
enum class ChromeAction {
  kNone,
  kUndo,
  kRedo,
  kToggleTools,
  kToggleColors,
  kToggleSizes,
  kToggleDocument,
  kSelectDraw,
  kSelectErase,
  kSelectPan,
  kSelectSmall,
  kSelectMedium,
  kSelectLarge,
  kSelectExtraLarge,
  kPreviousPalette,
  kNextPalette,
  kSelectColor,
  kNewDrawing,
  kCancelNewDrawing,
  kConfirmNewDrawing,
  kExport,
  kZoomIn,
  kZoomOut,
};

struct ChromeState {
  ChromeTool tool = ChromeTool::kDraw;
  ChromeSize size = ChromeSize::kLarge;
  ChromePopup popup = ChromePopup::kNone;
  std::uint8_t palette_page = 0;
  std::uint8_t color_index = 12;
  bool can_undo = false;
  bool can_redo = false;
  bool can_export = false;
  bool confirm_new = false;
  ChromeExportStatus export_status = ChromeExportStatus::kIdle;
  std::uint8_t export_progress = 0;
  int battery_percentage = -1;
  bool battery_charging = false;
  bool operator==(const ChromeState&) const = default;
};

struct ChromeNavigation {
  int zoom_percent = 25;
  int level_x = 0;
  int level_y = 0;
  int level_width = 368;
  int level_height = 448;
  bool can_pan_top = false;
  bool can_pan_left = false;
  bool can_pan_right = false;
  bool can_pan_bottom = false;
  std::span<const std::uint16_t> overview_pixels{};
};

struct ChromeOverlayRegions {
  std::array<ChromeRect, 3> regions{};
  std::size_t count = 0;
};

struct ChromePresentationRegions {
  std::array<ChromeRect, 16> regions{};
  std::size_t count = 0;
};

[[nodiscard]] constexpr std::uint16_t rgb565(std::uint32_t rgb888) {
  return static_cast<std::uint16_t>(((rgb888 >> 19U) & 0x1FU) << 11U |
                                    ((rgb888 >> 10U) & 0x3FU) << 5U | ((rgb888 >> 3U) & 0x1FU));
}

inline constexpr std::array<std::array<std::uint16_t, kPaletteColorCount>, 2> kPico8Palettes{{
    {rgb565(0x000000U), rgb565(0x1D2B53U), rgb565(0x7E2553U), rgb565(0x008751U), rgb565(0xAB5236U),
     rgb565(0x5F574FU), rgb565(0xC2C3C7U), rgb565(0xFFF1E8U), rgb565(0xFF004DU), rgb565(0xFFA300U),
     rgb565(0xFFEC27U), rgb565(0x00E436U), rgb565(0x29ADFFU), rgb565(0x83769CU), rgb565(0xFF77A8U),
     rgb565(0xFFCCAAU)},
    {rgb565(0x291814U), rgb565(0x111D35U), rgb565(0x422136U), rgb565(0x125359U), rgb565(0x742F29U),
     rgb565(0x49333BU), rgb565(0xA28879U), rgb565(0xF3EF7DU), rgb565(0xBE1250U), rgb565(0xFF6C24U),
     rgb565(0xA8E72EU), rgb565(0x00B543U), rgb565(0x065AB5U), rgb565(0x754665U), rgb565(0xFF6E59U),
     rgb565(0xFF9D81U)},
}};

[[nodiscard]] std::uint16_t selected_color(const ChromeState& state);
[[nodiscard]] float brush_size(ChromeSize size);
[[nodiscard]] int chrome_canvas_bottom(const ChromeState& state);
[[nodiscard]] int chrome_input_bottom(const ChromeState& state);
[[nodiscard]] std::optional<ChromePoint> clip_canvas_segment(ChromePoint previous,
                                                             ChromePoint current,
                                                             const ChromeState& state);
[[nodiscard]] bool chrome_contains(ChromePoint point, const ChromeState& state);
[[nodiscard]] ChromeAction chrome_action_at(ChromePoint point, const ChromeState& state);
[[nodiscard]] std::optional<std::uint8_t> chrome_color_at(ChromePoint point,
                                                          const ChromeState& state);
[[nodiscard]] ChromeOverlayRegions chrome_overlay_regions(const ChromeState& state);
// Splits panel bounds around fixed canvas overlays so latency-critical live
// ink can be presented without redrawing or damaging those overlays.
[[nodiscard]] ChromePresentationRegions chrome_unobscured_regions(ChromeRect bounds,
                                                                  const ChromeState& state);
[[nodiscard]] std::optional<ChromeRect> chrome_minimap_region(const ChromeState& state);
[[nodiscard]] bool chrome_minimap_refresh_required(const ChromeState& state, bool overview_changed,
                                                   bool allow_minimap_refresh);
void draw_chrome(std::span<std::uint16_t> pixels, int width, int height, const ChromeState& state);
// Draws only the minimap overlay (frame plus overview resample plus viewport
// rectangle). Cheap enough to run on every pan frame; returns false when the
// canvas overlays are hidden or the surface does not match the panel.
[[nodiscard]] bool draw_chrome_minimap_overlay(std::span<std::uint16_t> pixels, int width,
                                               int height, const ChromeState& state,
                                               const ChromeNavigation& navigation);
void draw_chrome_canvas_overlays(std::span<std::uint16_t> pixels, int width, int height,
                                 const ChromeState& state, const ChromeNavigation& navigation);

}  // namespace tinydraw::vector_v2
