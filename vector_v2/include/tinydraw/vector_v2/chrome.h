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
  // Set when a piece could not be stored; a caller must not treat the
  // regions as full coverage of the requested bounds.
  bool overflowed = false;
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

// Caller-funded PSRAM for prerendered fixed chrome sprites and one transparent
// full-panel modal overlay. Rasterization always finishes before DMA staging.
inline constexpr std::size_t kChromeSpriteCachePixels = 53'956U;
inline constexpr std::size_t kChromeModalCachePixels = 368U * 448U;
inline constexpr std::size_t kChromeStagingCachePixels =
    kChromeSpriteCachePixels + kChromeModalCachePixels;

struct ChromeStagingCacheStats {
  std::uint32_t bottom_redraws = 0;
  std::uint32_t battery_redraws = 0;
  std::uint32_t zoom_redraws = 0;
  std::uint32_t minimap_base_redraws = 0;
  std::uint32_t modal_redraws = 0;
};

class ChromeStagingCache {
 public:
  explicit ChromeStagingCache(std::span<std::uint16_t> pixels) : pixels_(pixels) {}

  [[nodiscard]] bool ready() const { return pixels_.size() >= kChromeStagingCachePixels; }
  // Regenerates each caller-funded sprite at its own lifetime. Camera motion
  // changes only the transient minimap viewport drawn by paint().
  [[nodiscard]] bool prepare(const ChromeState& state, const ChromeNavigation& navigation,
                             std::uint32_t overview_revision);
  // Prepares only sprites intersecting one upcoming panel submission. This is
  // safe to call before transport starts, keeping cache rasterization out of
  // DMA staging and unrelated cache lifetimes out of small ink updates.
  [[nodiscard]] bool prepare_for(ChromeRect panel_bounds, const ChromeState& state,
                                 const ChromeNavigation& navigation,
                                 std::uint32_t overview_revision);
  [[nodiscard]] bool paint(const MinimapSurface& surface, const ChromeState& state,
                           const ChromeNavigation& navigation, std::uint32_t overview_revision);
  // Paints without regenerating cache state. Returns false if an intersecting
  // sprite was not prepared for the requested identity.
  [[nodiscard]] bool paint_prepared(const MinimapSurface& surface, const ChromeState& state,
                                    const ChromeNavigation& navigation,
                                    std::uint32_t overview_revision);
  [[nodiscard]] ChromeStagingCacheStats stats() const { return stats_; }

 private:
  std::span<std::uint16_t> pixels_{};
  ChromeState bottom_state_{};
  ChromeState modal_state_{};
  int modal_zoom_percent_ = 0;
  int modal_level_x_ = 0;
  int modal_level_y_ = 0;
  int modal_level_width_ = 0;
  int modal_level_height_ = 0;
  std::uint32_t modal_overview_revision_ = 0;
  int battery_percentage_ = -1;
  bool battery_charging_ = false;
  int zoom_percent_ = 0;
  std::uint32_t overview_revision_ = 0;
  ChromeStagingCacheStats stats_{};
  bool bottom_valid_ = false;
  bool battery_valid_ = false;
  bool zoom_valid_ = false;
  bool minimap_base_valid_ = false;
  bool modal_valid_ = false;
};
// Draws the minimap into the surface; the pan path composes the overlay into
// a small scratch surface instead of writing into the (possibly
// ring-rotated) frame.
[[nodiscard]] bool draw_chrome_minimap_surface(const MinimapSurface& surface,
                                               const ChromeState& state,
                                               const ChromeNavigation& navigation);
// Draws every canvas overlay's share (zoom rail, minimap, battery) that
// intersects the surface.
[[nodiscard]] bool draw_chrome_strip_overlays(const MinimapSurface& surface,
                                              const ChromeState& state,
                                              const ChromeNavigation& navigation);
// Paints all fixed chrome intersecting one host-order staged panel surface:
// dock/popups/dialog/status plus the canvas overlays above. The caller owns
// clipping through the surface origin and byte-swaps only after this returns.
[[nodiscard]] bool draw_chrome_staging_surface(const MinimapSurface& surface,
                                               const ChromeState& state,
                                               const ChromeNavigation& navigation);
void draw_chrome_canvas_overlays(std::span<std::uint16_t> pixels, int width, int height,
                                 const ChromeState& state, const ChromeNavigation& navigation);

}  // namespace tinydraw::vector_v2
