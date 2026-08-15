#ifndef TINYDRAW_ESP32_VECTOR_V2_PRESENTER_H
#define TINYDRAW_ESP32_VECTOR_V2_PRESENTER_H

#include <cstdint>
#include <memory>
#include <span>

#include "co5300_panel_transport.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/ribbon_renderer.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/display_scheduler.h"
#include "tinydraw/vector_v2/frame_scroller.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace tinydraw::esp32 {

// Even-window panel alignment can expand an unaligned 128x128 supertask by
// one pixel on each side.
inline constexpr int kMaximumProgressiveRegionWidth = vector_v2::kTileProducerWidth + 2;
inline constexpr int kMaximumProgressiveRegionHeight = vector_v2::kTileProducerHeight + 2;
inline constexpr std::size_t kMaximumProgressiveRegionPixels =
    static_cast<std::size_t>(kMaximumProgressiveRegionWidth) * kMaximumProgressiveRegionHeight;
inline constexpr int kMaximumCachedPanDelta = 96;
// Legacy timing-model inputs retained only for the explicitly selected
// beam-race control. They are not panel-phase or glass-correctness claims.
inline constexpr std::int64_t kTePeriodUs = 16'800;
inline constexpr int kPanelSweepRows = 448;
inline constexpr int kBeamStartMarginRows = 48;
// Cached pan composition is strip-looped through the same bounded scratch as
// progressive tile presentation, so wider reuse costs no additional PSRAM.
inline constexpr std::size_t kLiveRegionScratchPixels = kMaximumProgressiveRegionPixels;
// Interactive gestures commit in bounded chunks so intermediate authority
// publication stays inside one input-poll slice. Visible tiles are exempt
// from the commit budget, so the chunk size bounds the visible band a
// commit must paint; the mixed-draw and long-gesture gates re-prove the
// 15 ms slice bound with this exact constant.
inline constexpr std::size_t kInteractiveChunkSampleLimit = 32;
// Wall-clock bound for one in-place chunk commit (overview replay plus
// visible-tile painting). Tiles that do not fit are dropped to correct
// overview fallback and re-produced lazily. The deadline is checked between
// tiles, so the worst commit is budget + one uniform-conversion paint
// (~2 ms) + the commit tail (~1.5 ms); 10 ms keeps that under the 15 ms
// alarm. The uninterruptible full-width 25% overview band replay (~13.7 ms
// worst) is the other measured ceiling and the next optimization target.
inline constexpr std::int64_t kInPlaceCommitBudgetUs = 10'000;
// Cold-fill producer slices run to this deadline before yielding to input.
// The worst observed single resumable produce_next step is ~11.2 ms (a seed-7
// publication step; bounded by the producer's internal budgets), so the
// deadline must leave that much headroom under the 15 ms input-poll alarm:
// 2.5 ms measured a 13.2 ms worst tick while still batching several steps per
// slice (6 ms measured 15.2 ms and tripped the alarm).
inline constexpr std::int64_t kColdFillSliceDeadlineUs = 2'500;
// Idle repair's level sweep stops when occupied raw slots come within this
// headroom of capacity: one more repaired view would only evict warmer
// tiles. Sized to one viewport's worth of tiles.
inline constexpr std::size_t kRepairSaturationHeadroomTiles = 48;

struct LivePresentationTiming {
  std::int64_t compose_us = 0;
  std::int64_t scroll_us = 0;
  std::int64_t exposed_compose_us = 0;
  std::int64_t chrome_us = 0;
  std::int64_t first_submit_us = 0;
  std::int64_t first_complete_us = 0;
  std::int64_t complete_us = 0;
  std::size_t tile_pixels = 0;
  std::size_t uniform_pixels = 0;
  std::size_t overview_pixels = 0;
  std::size_t fallback_pixels = 0;
  std::size_t resident_tiles = 0;
  std::size_t fallback_tiles = 0;
  std::uint32_t pushes = 0;
  std::int64_t tear_wait_us = 0;
  std::uint32_t tear_edge_isr_to_resume_us = 0;
  bool tear_edge_observed = false;
  bool tear_edge_wait_resumed = false;
  bool tear_edge_timed_out = false;
  bool tear_heal_attempted = false;
  bool tear_heal_command_sent = false;
  bool frame_reused = false;
  bool passed = false;
};

[[nodiscard]] const char* presentation_experiment_name();
[[nodiscard]] const char* selected_tear_edge_name();
[[nodiscard]] TearSignalEdge selected_tear_edge();
[[nodiscard]] int panel_clock_mhz();

class VectorV2Presenter {
 public:
  VectorV2Presenter(vector_v2::MaterializedCanvas& canvas, vector_v2::NavigationState& navigation,
                    vector_v2::DisplayScheduler& scheduler, Co5300PanelTransport& display,
                    std::span<std::uint16_t> frame_pixels, std::span<std::uint16_t> region_pixels,
                    std::span<std::uint16_t> chrome_cache_pixels);

  [[nodiscard]] bool ready() const;
  // Read-only transport telemetry (prepare/staging counters) for gates that
  // attribute presentation cost without owning the panel reference.
  [[nodiscard]] Co5300PanelTransport& display() { return display_; }
  [[nodiscard]] const Co5300PanelTransport& display() const { return display_; }
  [[nodiscard]] vector_v2::ZoomLevel zoom() const;
  [[nodiscard]] int level_x() const;
  [[nodiscard]] int level_y() const;
  [[nodiscard]] float scale() const;
  [[nodiscard]] vector_v2::OperationPoint operation_point(InkPoint point) const;

  [[nodiscard]] LivePresentationTiming refresh(const vector_v2::ChromeState& chrome,
                                               std::uint32_t event_us = 0);
  // Re-composes and presents only the intersection between changed level-space
  // pixels and the visible canvas above the chrome.
  [[nodiscard]] LivePresentationTiming refresh_region(vector_v2::PixelRect level_bounds,
                                                      const vector_v2::ChromeState& chrome,
                                                      std::uint32_t event_us = 0);
  [[nodiscard]] LivePresentationTiming show_start(InkPoint point, std::uint16_t color,
                                                  const vector_v2::ChromeState& chrome,
                                                  std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming show_update(const RibbonUpdate& update, std::uint16_t color,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming set_zoom(vector_v2::ZoomLevel zoom,
                                                const vector_v2::ChromeState& chrome,
                                                std::uint32_t event_us);
  // Test/adapter seam for deterministic hardware views. Coordinates are
  // clamped to the selected level before the full fallback view is presented.
  [[nodiscard]] LivePresentationTiming set_view(vector_v2::ZoomLevel zoom, int level_x, int level_y,
                                                const vector_v2::ChromeState& chrome,
                                                std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming pan_from(int start_x, int start_y, Point start_touch,
                                                Point current_touch,
                                                const vector_v2::ChromeState& chrome,
                                                std::uint32_t event_us);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // Deterministic acceptance seam: re-presents the complete staged frame and
  // proves transport/chrome composition did not mutate the pure canvas ring.
  [[nodiscard]] bool verify_staging_preserves_canvas(const vector_v2::ChromeState& chrome);
#endif
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  // Gate-only visual diagnostic. The presenter-owned frame/ring remains derived
  // state; this never writes the operation log or MaterializedCanvas authority.
  void enable_optical_row_pattern();
#endif

 private:
  struct StageContext {
    VectorV2Presenter* presenter = nullptr;
    const vector_v2::ChromeState* chrome = nullptr;
    vector_v2::ChromeNavigation navigation{};
    std::span<const vector_v2::PixelRect> exposed{};
    std::int64_t exposed_us = 0;
    std::int64_t chrome_us = 0;
  };

  [[nodiscard]] static bool paint_stage_thunk(void* context, const PanelStageSurface& surface);
  [[nodiscard]] bool paint_stage_surface(StageContext& context, const PanelStageSurface& surface);
  [[nodiscard]] LivePresentationTiming present(vector_v2::PixelRect bounds,
                                               const vector_v2::ChromeState& chrome,
                                               std::uint32_t event_us, std::int64_t compose_us = 0,
                                               bool wait_for_completion = true);
  [[nodiscard]] LivePresentationTiming present_pixels(
      vector_v2::PixelRect bounds, std::span<const std::uint16_t> pixels, int stride,
      const vector_v2::ChromeState& chrome, std::uint32_t event_us, std::int64_t compose_us,
      bool wait_for_completion = true);
  [[nodiscard]] vector_v2::PixelRect primitive_bounds(std::span<const RibbonPrimitive> primitives,
                                                      int canvas_bottom) const;
  [[nodiscard]] LivePresentationTiming compose_and_present(vector_v2::PixelRect level_bounds,
                                                           vector_v2::PixelRect panel_bounds,
                                                           const vector_v2::ChromeState& chrome,
                                                           std::uint32_t event_us);
  [[nodiscard]] vector_v2::ChromeNavigation chrome_navigation() const;
  [[nodiscard]] LivePresentationTiming present_with_overlays(vector_v2::PixelRect bounds,
                                                             const vector_v2::ChromeState& chrome,
                                                             std::uint32_t event_us,
                                                             std::int64_t compose_us = 0,
                                                             bool allow_minimap_refresh = false);
  [[nodiscard]] LivePresentationTiming present_unobscured(vector_v2::PixelRect bounds,
                                                          const vector_v2::ChromeState& chrome,
                                                          std::uint32_t event_us,
                                                          std::int64_t compose_us = 0,
                                                          bool wait_for_completion = true);
  // One-window row-major ring sweep. Exposed canvas and chrome are patched
  // into each internal staging strip; neither is scattered into PSRAM.
  [[nodiscard]] LivePresentationTiming present_ring(vector_v2::PixelRect band,
                                                    const vector_v2::ChromeState& chrome,
                                                    std::uint32_t event_us,
                                                    std::span<const vector_v2::PixelRect> exposed);
  [[nodiscard]] bool compose_into_ring(vector_v2::PixelRect panel_bounds);
  void copy_ring_to_stage(vector_v2::PixelRect panel_bounds, const PanelStageSurface& surface);
  [[nodiscard]] LivePresentationTiming refresh_pan(int old_x, int old_y,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us);
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  void paint_optical_row_pattern(const PanelStageSurface& surface);
#endif

  vector_v2::MaterializedCanvas& canvas_;
  vector_v2::NavigationState& navigation_;
  vector_v2::DisplayScheduler& scheduler_;
  Co5300PanelTransport& display_;
  std::span<std::uint16_t> frame_;
  std::span<std::uint16_t> region_;
  vector_v2::ChromeStagingCache chrome_cache_;
  std::uint32_t te_last_count_ = 0;
  std::int64_t te_last_change_us_ = 0;
  std::unique_ptr<RibbonRenderer> renderer_;
  vector_v2::ZoomLevel frame_zoom_ = vector_v2::ZoomLevel::k25Percent;
  int frame_level_x_ = 0;
  int frame_level_y_ = 0;
  vector_v2::ChromeState frame_chrome_{};
  vector_v2::DocumentRevision presented_minimap_revision_{};
  // Toroidal pan addressing: while frame_ring_bottom_ is nonzero the canvas
  // rows [0, frame_ring_bottom_) of frame_ are ring-rotated by frame_ring_
  // and only the ring-aware pan present may read them; every other frame
  // writer materializes first through a full refresh.
  vector_v2::RingFrame frame_ring_{};
  int frame_ring_bottom_ = 0;
  bool minimap_presented_ = false;
  bool frame_reusable_ = false;
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  bool optical_row_pattern_enabled_ = false;
  std::uint8_t optical_generation_ = 0;
#endif
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_PRESENTER_H
