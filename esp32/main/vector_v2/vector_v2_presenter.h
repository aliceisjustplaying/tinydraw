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
#include "tinydraw/ui/toolbar.h"
#include "tinydraw/vector_v2/display_scheduler.h"
#include "tinydraw/vector_v2/frame_scroller.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace tinydraw::esp32 {

// Even-window panel alignment can expand an unaligned 128x128 supertask by
// one pixel on each side.
inline constexpr int kMaximumProgressiveRegionWidth = vector_v2::kTileProducerWidth + 2;
inline constexpr int kMaximumProgressiveRegionHeight = vector_v2::kTileProducerHeight + 2;
inline constexpr std::size_t kMaximumProgressiveRegionPixels =
    static_cast<std::size_t>(kMaximumProgressiveRegionWidth) * kMaximumProgressiveRegionHeight;
inline constexpr int kMaximumCachedPanDelta = 32;
inline constexpr std::size_t kMaximumCachedPanRegionPixels =
    static_cast<std::size_t>(vector_v2::kOverviewHeight) * kMaximumCachedPanDelta;
inline constexpr std::size_t kLiveRegionScratchPixels =
    kMaximumProgressiveRegionPixels > kMaximumCachedPanRegionPixels
        ? kMaximumProgressiveRegionPixels
        : kMaximumCachedPanRegionPixels;

struct LivePresentationTiming {
  std::int64_t compose_us = 0;
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
  bool frame_reused = false;
  bool passed = false;
};

class VectorV2Presenter {
 public:
  VectorV2Presenter(vector_v2::MaterializedCanvas& canvas, vector_v2::DisplayScheduler& scheduler,
                    Co5300PanelTransport& display, std::span<std::uint16_t> frame_pixels,
                    std::span<std::uint16_t> region_pixels);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] vector_v2::ZoomLevel zoom() const;
  [[nodiscard]] int level_x() const;
  [[nodiscard]] int level_y() const;
  [[nodiscard]] float scale() const;
  [[nodiscard]] vector_v2::OperationPoint operation_point(InkPoint point) const;

  [[nodiscard]] LivePresentationTiming refresh(const ToolbarState& toolbar,
                                               std::uint32_t event_us = 0);
  // Re-composes and presents only the intersection between changed level-space
  // pixels and the visible canvas above the toolbar.
  [[nodiscard]] LivePresentationTiming refresh_region(vector_v2::PixelRect level_bounds,
                                                      std::uint32_t event_us = 0);
  [[nodiscard]] LivePresentationTiming show_start(InkPoint point, std::uint16_t color,
                                                  std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming show_update(const RibbonUpdate& update, std::uint16_t color,
                                                   std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming set_zoom(vector_v2::ZoomLevel zoom,
                                                const ToolbarState& toolbar,
                                                std::uint32_t event_us);
  // Test/adapter seam for deterministic hardware views. Coordinates are
  // clamped to the selected level before the full fallback view is presented.
  [[nodiscard]] LivePresentationTiming set_view(vector_v2::ZoomLevel zoom, int level_x, int level_y,
                                                const ToolbarState& toolbar,
                                                std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming pan_from(int start_x, int start_y, Point start_touch,
                                                Point current_touch, const ToolbarState& toolbar,
                                                std::uint32_t event_us);

 private:
  [[nodiscard]] LivePresentationTiming present(vector_v2::PixelRect bounds, std::uint32_t event_us,
                                               std::int64_t compose_us = 0);
  [[nodiscard]] LivePresentationTiming present_pixels(vector_v2::PixelRect bounds,
                                                      std::span<const std::uint16_t> pixels,
                                                      int stride, std::uint32_t event_us,
                                                      std::int64_t compose_us);
  [[nodiscard]] vector_v2::PixelRect primitive_bounds(
      std::span<const RibbonPrimitive> primitives) const;
  [[nodiscard]] vector_v2::PixelRect clamp_view_origin(int x, int y) const;
  [[nodiscard]] LivePresentationTiming compose_and_present(vector_v2::PixelRect level_bounds,
                                                           vector_v2::PixelRect panel_bounds,
                                                           std::uint32_t event_us);
  [[nodiscard]] bool compose_into_frame(vector_v2::PixelRect panel_bounds);
  [[nodiscard]] LivePresentationTiming refresh_pan(int old_x, int old_y,
                                                   const ToolbarState& toolbar,
                                                   std::uint32_t event_us);

  vector_v2::MaterializedCanvas& canvas_;
  vector_v2::DisplayScheduler& scheduler_;
  Co5300PanelTransport& display_;
  std::span<std::uint16_t> frame_;
  std::span<std::uint16_t> region_;
  std::unique_ptr<RibbonRenderer> renderer_;
  vector_v2::ZoomLevel zoom_ = vector_v2::ZoomLevel::k25Percent;
  int level_x_ = 0;
  int level_y_ = 0;
  vector_v2::ZoomLevel frame_zoom_ = vector_v2::ZoomLevel::k25Percent;
  int frame_level_x_ = 0;
  int frame_level_y_ = 0;
  std::uint64_t frame_epoch_ = 0;
  bool frame_reusable_ = false;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_PRESENTER_H
