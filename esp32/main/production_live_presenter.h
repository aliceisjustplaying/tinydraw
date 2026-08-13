#ifndef TINYDRAW_ESP32_PRODUCTION_LIVE_PRESENTER_H
#define TINYDRAW_ESP32_PRODUCTION_LIVE_PRESENTER_H

#include <cstdint>
#include <memory>
#include <span>

#include "co5300_panel_transport.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/ribbon_renderer.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/production/display_scheduler.h"
#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation_builder.h"
#include "tinydraw/production/tile_producer.h"
#include "tinydraw/ui/toolbar.h"

namespace tinydraw::esp32 {

// Even-window panel alignment can expand an unaligned 128x128 supertask by
// one pixel on each side.
inline constexpr int kMaximumProgressiveRegionWidth = production::kTileProducerWidth + 2;
inline constexpr int kMaximumProgressiveRegionHeight = production::kTileProducerHeight + 2;
inline constexpr std::size_t kMaximumProgressiveRegionPixels =
    static_cast<std::size_t>(kMaximumProgressiveRegionWidth) * kMaximumProgressiveRegionHeight;

struct LivePresentationTiming {
  std::int64_t compose_us = 0;
  std::int64_t first_submit_us = 0;
  std::int64_t first_complete_us = 0;
  std::int64_t complete_us = 0;
  std::size_t tile_pixels = 0;
  std::size_t overview_pixels = 0;
  std::size_t fallback_pixels = 0;
  std::size_t resident_tiles = 0;
  std::size_t fallback_tiles = 0;
  std::uint32_t pushes = 0;
  bool passed = false;
};

class ProductionLivePresenter {
 public:
  ProductionLivePresenter(production::MaterializedCanvas& canvas,
                          production::DisplayScheduler& scheduler, Co5300PanelTransport& display,
                          std::span<std::uint16_t> frame_pixels,
                          std::span<std::uint16_t> region_pixels);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] production::ZoomLevel zoom() const;
  [[nodiscard]] int level_x() const;
  [[nodiscard]] int level_y() const;
  [[nodiscard]] float scale() const;
  [[nodiscard]] production::OperationPoint operation_point(InkPoint point) const;

  [[nodiscard]] LivePresentationTiming refresh(const ToolbarState& toolbar,
                                               std::uint32_t event_us = 0);
  // Re-composes and presents only the intersection between changed level-space
  // pixels and the visible canvas above the toolbar.
  [[nodiscard]] LivePresentationTiming refresh_region(production::PixelRect level_bounds,
                                                      std::uint32_t event_us = 0);
  [[nodiscard]] LivePresentationTiming show_start(InkPoint point, std::uint16_t color,
                                                  std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming show_update(const RibbonUpdate& update, std::uint16_t color,
                                                   std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming set_zoom(production::ZoomLevel zoom,
                                                const ToolbarState& toolbar,
                                                std::uint32_t event_us);
  // Test/adapter seam for deterministic hardware views. Coordinates are
  // clamped to the selected level before the full fallback view is presented.
  [[nodiscard]] LivePresentationTiming set_view(production::ZoomLevel zoom, int level_x,
                                                int level_y, const ToolbarState& toolbar,
                                                std::uint32_t event_us);
  [[nodiscard]] LivePresentationTiming pan_from(int start_x, int start_y, Point start_touch,
                                                Point current_touch, const ToolbarState& toolbar,
                                                std::uint32_t event_us);

 private:
  [[nodiscard]] LivePresentationTiming present(production::PixelRect bounds, std::uint32_t event_us,
                                               std::int64_t compose_us = 0);
  [[nodiscard]] LivePresentationTiming present_pixels(production::PixelRect bounds,
                                                      std::span<const std::uint16_t> pixels,
                                                      int stride, std::uint32_t event_us,
                                                      std::int64_t compose_us);
  [[nodiscard]] production::PixelRect primitive_bounds(
      std::span<const RibbonPrimitive> primitives) const;
  [[nodiscard]] production::PixelRect clamp_view_origin(int x, int y) const;
  [[nodiscard]] LivePresentationTiming compose_and_present(production::PixelRect level_bounds,
                                                           production::PixelRect panel_bounds,
                                                           std::uint32_t event_us);

  production::MaterializedCanvas& canvas_;
  production::DisplayScheduler& scheduler_;
  Co5300PanelTransport& display_;
  std::span<std::uint16_t> frame_;
  std::span<std::uint16_t> region_;
  std::unique_ptr<RibbonRenderer> renderer_;
  production::ZoomLevel zoom_ = production::ZoomLevel::k25Percent;
  int level_x_ = 0;
  int level_y_ = 0;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_PRODUCTION_LIVE_PRESENTER_H
