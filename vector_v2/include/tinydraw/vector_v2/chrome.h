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

struct ChromeLevelPoint {
  int x = 0;
  int y = 0;
  bool operator==(const ChromeLevelPoint&) const = default;
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
enum class ChromeExportStatus {
  kIdle,
  kSaving,
  kSaved,
  kPresented,
  kHostEjected,
  kExitError,
  kError
};
enum class ChromeTimeSyncStatus { kIdle, kConnecting, kSynchronizing, kSaved, kError };
inline constexpr std::uint32_t kChromeTimeSyncToastDurationUs = 3'000'000U;
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
  kExitExport,
  kSyncTime,
  kZoomIn,
  kZoomOut,
};

struct ChromeState {
  // The physical side button hides canvas HUD overlays. The bottom toolbar,
  // its popups, and critical dialogs remain visible and interactive.
  bool hud_visible = true;
  ChromeTool tool = ChromeTool::kDraw;
  ChromeSize size = ChromeSize::kLarge;
  ChromePopup popup = ChromePopup::kNone;
  std::uint8_t palette_page = 0;
  std::uint8_t color_index = 12;
  bool can_undo = false;
  bool can_redo = false;
  bool can_export = false;
  bool can_sync_time = false;
  bool confirm_new = false;
  ChromeExportStatus export_status = ChromeExportStatus::kIdle;
  std::uint8_t export_progress = 0;
  ChromeTimeSyncStatus time_sync_status = ChromeTimeSyncStatus::kIdle;
  int battery_percentage = -1;
  bool battery_charging = false;
  bool recording = false;
  // A history refill is still rebuilding its damaged region; presents show a
  // small hourglass over the retained pixels until the exact swap.
  bool history_busy = false;
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
// Presents the internal 25%-to-400% rendering scale as the user-facing
// 1x-to-16x magnification shown in the zoom rail.
[[nodiscard]] int chrome_zoom_display_multiplier(int zoom_percent);
[[nodiscard]] ChromeTimeSyncStatus chrome_time_sync_status_after(ChromeTimeSyncStatus status,
                                                                 std::uint32_t elapsed_us);
[[nodiscard]] float brush_size(ChromeSize size);
[[nodiscard]] int chrome_canvas_bottom(const ChromeState& state);
[[nodiscard]] int chrome_input_bottom(const ChromeState& state);
[[nodiscard]] bool chrome_can_toggle_hud(const ChromeState& state);
[[nodiscard]] bool toggle_hud_visibility(ChromeState& state);
// The bottom bound for committed ink. Deeper than the render/input bottoms:
// with no popup open, strokes continue under the dock into the hidden world
// rows; modal states still block ink entirely.
[[nodiscard]] int chrome_ink_bottom(const ChromeState& state);
// Clips a stroke segment against chrome_ink_bottom. Live preview rendering
// is separately clipped by the presenter at chrome_input_bottom, so a
// segment accepted here never paints over chrome.
[[nodiscard]] std::optional<ChromePoint> clip_canvas_segment(ChromePoint previous,
                                                             ChromePoint current,
                                                             const ChromeState& state);
// A stationary contact in the top sensor fringe is an edge exit, not a tap.
// Once the gesture produces a drawable segment it remains valid from any edge.
[[nodiscard]] bool chrome_accepts_stroke_finish(ChromePoint start, bool has_drawn_segment);
[[nodiscard]] bool chrome_contains(ChromePoint point, const ChromeState& state);
// Minimap gestures own its visible frame regardless of the selected tool.
[[nodiscard]] bool chrome_minimap_contains(ChromePoint point, const ChromeState& state);
// Projects panel coordinates into the active level, clamping beyond the
// minimap interior so a captured drag continues smoothly outside its frame.
[[nodiscard]] ChromeLevelPoint chrome_minimap_level_point(ChromePoint point,
                                                          const ChromeNavigation& navigation);
// Resolves the current minimap pointer to a centered level origin. The whole
// minimap directly acquires the viewport.
[[nodiscard]] ChromeLevelPoint chrome_minimap_drag_origin(ChromePoint current,
                                                          ChromeLevelPoint focus,
                                                          const ChromeNavigation& navigation);
// The minimap and the size/document dock buttons share a physical finger
// footprint near the map's bottom edge. A Down in this zone remains a normal
// dock tap unless movement crosses chrome_promotes_minimap_dock_drag().
[[nodiscard]] bool chrome_minimap_dock_drag_candidate(ChromePoint point, const ChromeState& state);
[[nodiscard]] bool chrome_promotes_minimap_dock_drag(ChromePoint start, ChromePoint current,
                                                     const ChromeState& state);
// Zoom controls own stationary taps, but a deliberate drag that starts on the
// rail becomes a canvas pan when the pan tool is active.
[[nodiscard]] bool chrome_promotes_pan_drag(ChromePoint start, ChromePoint current,
                                            const ChromeState& state);
[[nodiscard]] ChromeAction chrome_action_at(ChromePoint point, const ChromeState& state);
[[nodiscard]] std::optional<std::uint8_t> chrome_color_at(ChromePoint point,
                                                          const ChromeState& state);
[[nodiscard]] std::optional<ChromeRect> chrome_minimap_region(const ChromeState& state);
// The battery indicator's panel-space overlay bounds. A battery-only state
// change needs to re-present just this region, not the full frame.
[[nodiscard]] ChromeRect chrome_battery_region();
[[nodiscard]] ChromeRect chrome_recording_region();

// Outer bounds (including shadow) of the history busy hourglass toast; the
// presenter uses this region to show and erase the cue.
[[nodiscard]] ChromeRect chrome_history_busy_region();
// The only dock pixels changed by can_undo/can_redo synchronization.
[[nodiscard]] ChromeRect chrome_history_controls_region();
[[nodiscard]] bool chrome_minimap_refresh_required(const ChromeState& state, bool overview_changed,
                                                   bool allow_minimap_refresh);
// True only when every chrome writer used by paint_prepared understands a
// transfer surface that is already in panel byte order.
[[nodiscard]] bool chrome_accepts_byte_swapped_staging(const ChromeState& state);
// A caller-owned pixel surface whose (0, 0) sits at panel coordinate
// (origin_x, origin_y).
struct MinimapSurface {
  std::span<std::uint16_t> pixels;
  int width = 0;
  int height = 0;
  int origin_x = 0;
  int origin_y = 0;
  bool byte_swapped = false;
};

// Caller-funded PSRAM for prerendered fixed chrome sprites. The cache keeps
// rasterization out of per-strip DMA staging while the canvas ring remains
// presentation-pure. Popup/modal states use the uncached general renderer.
inline constexpr std::size_t kChromeStagingCachePixels = 53'956U;

#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
struct ChromeStagingCacheStats {
  std::uint32_t bottom_redraws = 0;
  std::uint32_t battery_redraws = 0;
  std::uint32_t zoom_redraws = 0;
  std::uint32_t minimap_base_redraws = 0;
};
#endif

class ChromeStagingCache {
 public:
  explicit ChromeStagingCache(std::span<std::uint16_t> pixels) : pixels_(pixels) {}

  [[nodiscard]] bool ready() const { return pixels_.size() >= kChromeStagingCachePixels; }
  // Regenerates each caller-funded sprite at its own lifetime. Camera motion
  // changes only the transient minimap viewport drawn by paint_prepared().
  [[nodiscard]] bool prepare(const ChromeState& state, const ChromeNavigation& navigation,
                             std::uint32_t overview_revision);
  // Prepares only sprites intersecting one upcoming panel submission. This is
  // safe to call before transport starts, keeping cache rasterization out of
  // DMA staging and unrelated cache lifetimes out of small ink updates.
  [[nodiscard]] bool prepare_for(ChromeRect panel_bounds, const ChromeState& state,
                                 const ChromeNavigation& navigation,
                                 std::uint32_t overview_revision);
  // Paints without regenerating cache state. Returns false if an intersecting
  // sprite was not prepared for the requested identity.
  [[nodiscard]] bool paint_prepared(const MinimapSurface& surface, const ChromeState& state,
                                    const ChromeNavigation& navigation,
                                    std::uint32_t overview_revision);
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  [[nodiscard]] ChromeStagingCacheStats stats() const { return stats_; }
#endif

 private:
  std::span<std::uint16_t> pixels_{};
  ChromeState bottom_state_{};
  int battery_percentage_ = -1;
  bool battery_charging_ = false;
  int zoom_percent_ = 0;
  std::uint32_t overview_revision_ = 0;
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  ChromeStagingCacheStats stats_{};
#endif
  bool bottom_valid_ = false;
  bool battery_valid_ = false;
  bool zoom_valid_ = false;
  bool minimap_base_valid_ = false;
};
}  // namespace tinydraw::vector_v2
