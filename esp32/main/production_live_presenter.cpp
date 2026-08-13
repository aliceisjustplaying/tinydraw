#include "production_live_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "esp_timer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;

production::PixelRect align_bounds(production::PixelRect bounds) {
  bounds.x0 &= ~1;
  bounds.y0 &= ~1;
  bounds.x1 = (bounds.x1 + 1) & ~1;
  bounds.y1 = (bounds.y1 + 1) & ~1;
  bounds.x0 = std::clamp(bounds.x0, 0, production::kOverviewWidth);
  bounds.y0 = std::clamp(bounds.y0, 0, production::kOverviewHeight);
  bounds.x1 = std::clamp(bounds.x1, bounds.x0, production::kOverviewWidth);
  bounds.y1 = std::clamp(bounds.y1, bounds.y0, production::kOverviewHeight);
  return bounds;
}

}  // namespace

ProductionLivePresenter::ProductionLivePresenter(production::MaterializedCanvas& canvas,
                                                 production::DisplayScheduler& scheduler,
                                                 Co5300PanelTransport& display,
                                                 std::span<std::uint16_t> frame_pixels)
    : canvas_(canvas),
      scheduler_(scheduler),
      display_(display),
      frame_(frame_pixels),
      renderer_(std::make_unique<RibbonRenderer>()) {}

bool ProductionLivePresenter::ready() const {
  return canvas_.ready() && scheduler_.ready() && display_.ready() &&
         frame_.size() == production::kOverviewPixels && renderer_ != nullptr;
}

production::ZoomLevel ProductionLivePresenter::zoom() const { return zoom_; }

int ProductionLivePresenter::level_x() const { return level_x_; }

int ProductionLivePresenter::level_y() const { return level_y_; }

float ProductionLivePresenter::scale() const {
  return static_cast<float>(production::zoom_percent(zoom_)) / 100.0F;
}

production::OperationPoint ProductionLivePresenter::operation_point(InkPoint point) const {
  const float inverse_scale = 1.0F / scale();
  return {
      .world_x = std::clamp((static_cast<float>(level_x_) + point.position.x) * inverse_scale, 0.0F,
                            static_cast<float>(production::kWorldWidth)),
      .world_y = std::clamp((static_cast<float>(level_y_) + point.position.y) * inverse_scale, 0.0F,
                            static_cast<float>(production::kWorldHeight)),
      .radius = point.radius * inverse_scale,
      .timestamp_us = point.timestamp_us,
  };
}

LivePresentationTiming ProductionLivePresenter::refresh(const ToolbarState& toolbar,
                                                        std::uint32_t event_us) {
  const std::int64_t compose_started = esp_timer_get_time();
  const auto stats = canvas_.compose_view(
      {.zoom = zoom_,
       .level_pixels = {level_x_, level_y_, level_x_ + production::kOverviewWidth,
                        level_y_ + production::kOverviewHeight}},
      frame_);
  if (!stats.has_value()) {
    return {};
  }
  draw_toolbar(frame_, production::kOverviewWidth, production::kOverviewHeight, toolbar);
  return present({0, 0, production::kOverviewWidth, production::kOverviewHeight}, event_us,
                 esp_timer_get_time() - compose_started);
}

LivePresentationTiming ProductionLivePresenter::show_start(InkPoint point, std::uint16_t color,
                                                           std::uint32_t event_us) {
  const RibbonPrimitive cap{
      .kind = RibbonPrimitiveKind::kCircle, .center = point.position, .radius = point.radius};
  const std::array primitives{cap};
  static_cast<void>(renderer_->render(primitives, frame_, production::kOverviewWidth,
                                      production::kOverviewHeight, color));
  return present(primitive_bounds(primitives), event_us);
}

LivePresentationTiming ProductionLivePresenter::show_update(const RibbonUpdate& update,
                                                            std::uint16_t color,
                                                            std::uint32_t event_us) {
  if (update.committed.empty()) {
    return {.passed = true};
  }
  static_cast<void>(renderer_->render(std::span(update.committed.begin(), update.committed.size()),
                                      frame_, production::kOverviewWidth,
                                      production::kOverviewHeight, color));
  return present(primitive_bounds(std::span(update.committed.begin(), update.committed.size())),
                 event_us);
}

LivePresentationTiming ProductionLivePresenter::set_zoom(production::ZoomLevel zoom,
                                                         const ToolbarState& toolbar,
                                                         std::uint32_t event_us) {
  if (zoom == zoom_) {
    return refresh(toolbar, event_us);
  }
  const float old_scale = scale();
  const float focus_world_x =
      (static_cast<float>(level_x_) + production::kOverviewWidth * 0.5F) / old_scale;
  const float focus_world_y =
      (static_cast<float>(level_y_) + production::kOverviewHeight * 0.5F) / old_scale;
  zoom_ = zoom;
  const auto origin = clamp_view_origin(
      static_cast<int>(std::lround(focus_world_x * scale() - production::kOverviewWidth * 0.5F)),
      static_cast<int>(std::lround(focus_world_y * scale() - production::kOverviewHeight * 0.5F)));
  level_x_ = origin.x0;
  level_y_ = origin.y0;
  return refresh(toolbar, event_us);
}

LivePresentationTiming ProductionLivePresenter::pan_from(int start_x, int start_y,
                                                         Point start_touch, Point current_touch,
                                                         const ToolbarState& toolbar,
                                                         std::uint32_t event_us) {
  const auto origin =
      clamp_view_origin(start_x + static_cast<int>(std::lround(start_touch.x - current_touch.x)),
                        start_y + static_cast<int>(std::lround(start_touch.y - current_touch.y)));
  if (origin.x0 == level_x_ && origin.y0 == level_y_) {
    return {};
  }
  level_x_ = origin.x0;
  level_y_ = origin.y0;
  return refresh(toolbar, event_us);
}

LivePresentationTiming ProductionLivePresenter::present(production::PixelRect bounds,
                                                        std::uint32_t event_us,
                                                        std::int64_t compose_us) {
  LivePresentationTiming timing{.compose_us = compose_us};
  bounds = align_bounds(bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return timing;
  }
  scheduler_.require_revision(canvas_.current_revision());
  const int width = bounds.x1 - bounds.x0;
  int rows_per_strip = std::max(2, 8'192 / width);
  rows_per_strip &= ~1;
  const std::uint32_t submits_before = display_.submit_count();
  std::int64_t first_submitted = 0;
  for (int y = bounds.y0; y < bounds.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, bounds.y1 - y);
    const production::PixelRect strip_bounds{bounds.x0, y, bounds.x1, y + rows};
    const auto pixels =
        frame_.subspan(static_cast<std::size_t>(y * production::kOverviewWidth + bounds.x0));
    const auto sequence = scheduler_.schedule({.revision = canvas_.current_revision(),
                                               .panel_bounds = strip_bounds,
                                               .pixels = pixels,
                                               .stride = production::kOverviewWidth});
    const auto scheduled = scheduler_.front();
    if (!sequence.has_value() || !scheduled.has_value() || scheduled->sequence != *sequence) {
      return timing;
    }
    const std::uint32_t pushes_before = display_.push_count();
    display_.push_rect(bounds.x0, y, width, rows, scheduled->strip.pixels.data(),
                       scheduled->strip.stride);
    if (display_.push_count() != pushes_before + 1U) {
      static_cast<void>(scheduler_.abort(*sequence));
      return timing;
    }
    if (first_submitted == 0) {
      first_submitted = esp_timer_get_time();
      timing.first_submit_us = event_us == 0U ? 0 : first_submitted - event_us;
    }
    if (!scheduler_.complete(*sequence)) {
      return timing;
    }
    ++timing.pushes;
  }
  const bool completed = display_.wait_for_all(2'000'000);
  const std::int64_t finished = esp_timer_get_time();
  const std::int64_t physical_complete = display_.complete_time_us(submits_before + 1U);
  timing.first_complete_us =
      event_us == 0U ? 0 : (physical_complete >= 0 ? physical_complete : finished) - event_us;
  timing.complete_us = first_submitted == 0 ? 0 : finished - first_submitted;
  timing.passed = completed;
  return timing;
}

production::PixelRect ProductionLivePresenter::primitive_bounds(
    std::span<const RibbonPrimitive> primitives) const {
  float x0 = static_cast<float>(production::kOverviewWidth);
  float y0 = static_cast<float>(production::kOverviewHeight);
  float x1 = 0.0F;
  float y1 = 0.0F;
  for (const RibbonPrimitive& primitive : primitives) {
    if (primitive.kind == RibbonPrimitiveKind::kCircle) {
      x0 = std::min(x0, primitive.center.x - primitive.radius - 2.0F);
      y0 = std::min(y0, primitive.center.y - primitive.radius - 2.0F);
      x1 = std::max(x1, primitive.center.x + primitive.radius + 2.0F);
      y1 = std::max(y1, primitive.center.y + primitive.radius + 2.0F);
      continue;
    }
    for (std::size_t index = 0; index < primitive.point_count; ++index) {
      x0 = std::min(x0, primitive.points[index].x - 2.0F);
      y0 = std::min(y0, primitive.points[index].y - 2.0F);
      x1 = std::max(x1, primitive.points[index].x + 2.0F);
      y1 = std::max(y1, primitive.points[index].y + 2.0F);
    }
  }
  return align_bounds({static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)),
                       static_cast<int>(std::ceil(x1)), static_cast<int>(std::ceil(y1))});
}

production::PixelRect ProductionLivePresenter::clamp_view_origin(int x, int y) const {
  const int level_width = production::kWorldWidth * production::zoom_percent(zoom_) / 100;
  const int level_height = production::kWorldHeight * production::zoom_percent(zoom_) / 100;
  return {
      .x0 = std::clamp(x, 0, std::max(0, level_width - production::kOverviewWidth)),
      .y0 = std::clamp(y, 0, std::max(0, level_height - production::kOverviewHeight)),
  };
}

}  // namespace tinydraw::esp32
