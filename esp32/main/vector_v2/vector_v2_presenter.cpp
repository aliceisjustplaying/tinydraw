#include "vector_v2_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "esp_timer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;

vector_v2::PixelRect align_bounds(vector_v2::PixelRect bounds) {
  bounds.x0 &= ~1;
  bounds.y0 &= ~1;
  bounds.x1 = (bounds.x1 + 1) & ~1;
  bounds.y1 = (bounds.y1 + 1) & ~1;
  bounds.x0 = std::clamp(bounds.x0, 0, vector_v2::kOverviewWidth);
  bounds.y0 = std::clamp(bounds.y0, 0, vector_v2::kOverviewHeight);
  bounds.x1 = std::clamp(bounds.x1, bounds.x0, vector_v2::kOverviewWidth);
  bounds.y1 = std::clamp(bounds.y1, bounds.y0, vector_v2::kOverviewHeight);
  return bounds;
}

}  // namespace

VectorV2Presenter::VectorV2Presenter(vector_v2::MaterializedCanvas& canvas,
                                     vector_v2::NavigationState& navigation,
                                     vector_v2::DisplayScheduler& scheduler,
                                     Co5300PanelTransport& display,
                                     std::span<std::uint16_t> frame_pixels,
                                     std::span<std::uint16_t> region_pixels)
    : canvas_(canvas),
      navigation_(navigation),
      scheduler_(scheduler),
      display_(display),
      frame_(frame_pixels),
      region_(region_pixels),
      renderer_(std::make_unique<RibbonRenderer>()) {}

bool VectorV2Presenter::ready() const {
  return canvas_.ready() && scheduler_.ready() && display_.ready() &&
         frame_.size() == vector_v2::kOverviewPixels &&
         region_.size() >= kLiveRegionScratchPixels && renderer_ != nullptr;
}

vector_v2::ZoomLevel VectorV2Presenter::zoom() const { return navigation_.zoom(); }

int VectorV2Presenter::level_x() const { return navigation_.origin().x; }

int VectorV2Presenter::level_y() const { return navigation_.origin().y; }

float VectorV2Presenter::scale() const {
  return static_cast<float>(vector_v2::zoom_percent(zoom())) / 100.0F;
}

vector_v2::OperationPoint VectorV2Presenter::operation_point(InkPoint point) const {
  const float inverse_scale = 1.0F / scale();
  return {
      .world_x = std::clamp((static_cast<float>(level_x()) + point.position.x) * inverse_scale,
                            0.0F, static_cast<float>(vector_v2::kWorldWidth)),
      .world_y = std::clamp((static_cast<float>(level_y()) + point.position.y) * inverse_scale,
                            0.0F, static_cast<float>(vector_v2::kWorldHeight)),
      .radius = point.radius * inverse_scale,
      .timestamp_us = point.timestamp_us,
  };
}

LivePresentationTiming VectorV2Presenter::refresh(const vector_v2::ChromeState& chrome,
                                                  std::uint32_t event_us) {
  // Composition mutates the cached frame. It cannot remain reusable unless the
  // entire compose-and-present transaction succeeds below.
  frame_reusable_ = false;
  const std::int64_t compose_started = esp_timer_get_time();
  const auto stats = canvas_.compose_view(navigation_.view(), frame_);
  if (!stats.has_value()) {
    return {};
  }
  vector_v2::draw_chrome(frame_, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight, chrome);
  auto timing =
      present_with_overlays({0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome,
                            event_us, esp_timer_get_time() - compose_started, true);
  timing.tile_pixels = stats->tile_pixels;
  timing.uniform_pixels = stats->uniform_pixels;
  timing.overview_pixels = stats->overview_pixels;
  timing.fallback_pixels = stats->fallback_pixels;
  timing.resident_tiles = stats->immediate_tiles + stats->settled_tiles + stats->exact_tiles;
  timing.fallback_tiles = stats->fallback_tiles;
  frame_zoom_ = zoom();
  frame_level_x_ = level_x();
  frame_level_y_ = level_y();
  frame_epoch_ = canvas_.composition_epoch();
  frame_chrome_ = chrome;
  frame_reusable_ = timing.passed && stats->fallback_pixels == 0U;
  if (zoom() != vector_v2::ZoomLevel::k25Percent) {
    static_cast<void>(canvas_.remember_view(navigation_.view()));
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::refresh_region(vector_v2::PixelRect level_bounds,
                                                         const vector_v2::ChromeState& chrome,
                                                         std::uint32_t event_us) {
  const int canvas_bottom = vector_v2::chrome_canvas_bottom(chrome);
  const vector_v2::PixelRect view{level_x(), level_y(), level_x() + vector_v2::kOverviewWidth,
                                  level_y() + canvas_bottom};
  const vector_v2::PixelRect intersection{
      .x0 = std::max(view.x0, level_bounds.x0),
      .y0 = std::max(view.y0, level_bounds.y0),
      .x1 = std::min(view.x1, level_bounds.x1),
      .y1 = std::min(view.y1, level_bounds.y1),
  };
  if (intersection.x1 <= intersection.x0 || intersection.y1 <= intersection.y0) {
    // At 25% the live commit writes overview authority directly, so there may
    // be no canvas region left to refresh on lift. The minimap is a separate
    // physical overlay and still needs one revision-driven presentation.
    const bool overview_changed =
        !minimap_presented_ || presented_minimap_revision_ != canvas_.current_revision();
    if (vector_v2::chrome_minimap_refresh_required(chrome, overview_changed, true)) {
      if (const auto minimap = vector_v2::chrome_minimap_region(chrome); minimap.has_value()) {
        return present_with_overlays({minimap->x0, minimap->y0, minimap->x1, minimap->y1}, chrome,
                                     event_us, 0, true);
      }
    }
    return {.passed = true};
  }
  frame_reusable_ = false;
  vector_v2::PixelRect panel{
      .x0 = intersection.x0 - level_x(),
      .y0 = intersection.y0 - level_y(),
      .x1 = intersection.x1 - level_x(),
      .y1 = intersection.y1 - level_y(),
  };
  panel = align_bounds(panel);
  panel.y1 = std::min(panel.y1, canvas_bottom);
  const int width = panel.x1 - panel.x0;
  const int height = panel.y1 - panel.y0;
  const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
  const vector_v2::PixelRect aligned_level{
      .x0 = level_x() + panel.x0,
      .y0 = level_y() + panel.y0,
      .x1 = level_x() + panel.x1,
      .y1 = level_y() + panel.y1,
  };
  if (pixel_count <= region_.size()) {
    return compose_and_present(aligned_level, panel, chrome, event_us);
  }

  int rows_per_strip = static_cast<int>(region_.size() / static_cast<std::size_t>(width));
  rows_per_strip &= ~1;
  if (rows_per_strip <= 0) {
    return {};
  }
  LivePresentationTiming total{.passed = true};
  for (int y = panel.y0; y < panel.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, panel.y1 - y);
    const vector_v2::PixelRect strip_panel{panel.x0, y, panel.x1, y + rows};
    const vector_v2::PixelRect strip_level{level_x() + strip_panel.x0, level_y() + strip_panel.y0,
                                           level_x() + strip_panel.x1, level_y() + strip_panel.y1};
    const auto part = compose_and_present(strip_level, strip_panel, chrome, event_us);
    if (!part.passed) {
      total.passed = false;
      return total;
    }
    if (total.pushes == 0U) {
      total.first_submit_us = part.first_submit_us;
      total.first_complete_us = part.first_complete_us;
    }
    total.compose_us += part.compose_us;
    total.complete_us += part.complete_us;
    total.tile_pixels += part.tile_pixels;
    total.uniform_pixels += part.uniform_pixels;
    total.overview_pixels += part.overview_pixels;
    total.fallback_pixels += part.fallback_pixels;
    total.resident_tiles += part.resident_tiles;
    total.fallback_tiles += part.fallback_tiles;
    total.pushes += part.pushes;
  }
  return total;
}

LivePresentationTiming VectorV2Presenter::compose_and_present(vector_v2::PixelRect level_bounds,
                                                              vector_v2::PixelRect panel_bounds,
                                                              const vector_v2::ChromeState& chrome,
                                                              std::uint32_t event_us) {
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  if (width <= 0 || height <= 0) {
    return {.passed = true};
  }
  const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
  if (region_.size() < pixel_count) {
    return {};
  }
  const std::int64_t compose_started = esp_timer_get_time();
  const auto destination = region_.first(pixel_count);
  const auto stats =
      canvas_.compose_view({.zoom = zoom(), .level_pixels = level_bounds}, destination);
  if (!stats.has_value()) {
    return {};
  }
  for (int row = 0; row < height; ++row) {
    const auto source = destination.subspan(static_cast<std::size_t>(row) * width, width);
    auto target =
        frame_.subspan(static_cast<std::size_t>(panel_bounds.y0 + row) * vector_v2::kOverviewWidth +
                           static_cast<std::size_t>(panel_bounds.x0),
                       static_cast<std::size_t>(width));
    std::copy(source.begin(), source.end(), target.begin());
  }
  auto timing = present_with_overlays(panel_bounds, chrome, event_us,
                                      esp_timer_get_time() - compose_started, true);
  timing.tile_pixels = stats->tile_pixels;
  timing.uniform_pixels = stats->uniform_pixels;
  timing.overview_pixels = stats->overview_pixels;
  timing.fallback_pixels = stats->fallback_pixels;
  timing.resident_tiles = stats->immediate_tiles + stats->settled_tiles + stats->exact_tiles;
  timing.fallback_tiles = stats->fallback_tiles;
  return timing;
}

LivePresentationTiming VectorV2Presenter::show_start(InkPoint point, std::uint16_t color,
                                                     const vector_v2::ChromeState& chrome,
                                                     std::uint32_t event_us) {
  frame_reusable_ = false;
  const int canvas_bottom = vector_v2::chrome_input_bottom(chrome);
  if (canvas_bottom == 0) {
    return {.passed = true};
  }
  const RibbonPrimitive cap{
      .kind = RibbonPrimitiveKind::kCircle, .center = point.position, .radius = point.radius};
  const std::array primitives{cap};
  static_cast<void>(
      renderer_->render(primitives, frame_, vector_v2::kOverviewWidth, canvas_bottom, color));
  return present_unobscured(primitive_bounds(primitives, canvas_bottom), chrome, event_us);
}

LivePresentationTiming VectorV2Presenter::show_update(const RibbonUpdate& update,
                                                      std::uint16_t color,
                                                      const vector_v2::ChromeState& chrome,
                                                      std::uint32_t event_us) {
  frame_reusable_ = false;
  const int canvas_bottom = vector_v2::chrome_input_bottom(chrome);
  if (update.committed.empty() || canvas_bottom == 0) {
    return {.passed = true};
  }
  static_cast<void>(renderer_->render(std::span(update.committed.begin(), update.committed.size()),
                                      frame_, vector_v2::kOverviewWidth, canvas_bottom, color));
  return present_unobscured(
      primitive_bounds(std::span(update.committed.begin(), update.committed.size()), canvas_bottom),
      chrome, event_us);
}

LivePresentationTiming VectorV2Presenter::set_zoom(vector_v2::ZoomLevel target_zoom,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us) {
  constexpr vector_v2::NavigationPoint kDefaultFocus{vector_v2::kOverviewWidth / 2,
                                                     vector_v2::kChromeCanvasBottom / 2};
  if (!navigation_.set_zoom(target_zoom, kDefaultFocus)) {
    return {};
  }
  return refresh(chrome, event_us);
}

LivePresentationTiming VectorV2Presenter::set_view(vector_v2::ZoomLevel target_zoom, int level_x,
                                                   int level_y,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us) {
  constexpr vector_v2::NavigationPoint kDefaultFocus{vector_v2::kOverviewWidth / 2,
                                                     vector_v2::kChromeCanvasBottom / 2};
  if (!navigation_.set_zoom(target_zoom, kDefaultFocus) ||
      !navigation_.set_origin(level_x, level_y, kDefaultFocus)) {
    return {};
  }
  return refresh(chrome, event_us);
}

LivePresentationTiming VectorV2Presenter::pan_from(int start_x, int start_y, Point start_touch,
                                                   Point current_touch,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us) {
  const int old_x = level_x();
  const int old_y = level_y();
  const int requested_x = start_x + static_cast<int>(std::lround(start_touch.x - current_touch.x));
  const int requested_y = start_y + static_cast<int>(std::lround(start_touch.y - current_touch.y));
  const vector_v2::NavigationPoint panel_focus{
      .x = std::clamp(static_cast<int>(std::lround(current_touch.x)), 0,
                      vector_v2::kOverviewWidth - 1),
      .y = std::clamp(static_cast<int>(std::lround(current_touch.y)), 0,
                      vector_v2::kOverviewHeight - 1),
  };
  if (!navigation_.set_origin(requested_x, requested_y, panel_focus) ||
      (level_x() == old_x && level_y() == old_y)) {
    return {};
  }
  return refresh_pan(old_x, old_y, chrome, event_us);
}

bool VectorV2Presenter::compose_into_frame(vector_v2::PixelRect panel_bounds) {
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  if (width <= 0 || height <= 0) {
    return true;
  }
  const int rows_per_strip = static_cast<int>(region_.size() / static_cast<std::size_t>(width));
  if (rows_per_strip <= 0) {
    return false;
  }
  for (int y = panel_bounds.y0; y < panel_bounds.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, panel_bounds.y1 - y);
    const vector_v2::PixelRect strip_panel{panel_bounds.x0, y, panel_bounds.x1, y + rows};
    const vector_v2::PixelRect strip_level{
        level_x() + strip_panel.x0,
        level_y() + strip_panel.y0,
        level_x() + strip_panel.x1,
        level_y() + strip_panel.y1,
    };
    const std::size_t count = static_cast<std::size_t>(width) * rows;
    const auto pixels = region_.first(count);
    const auto stats = canvas_.compose_view({.zoom = zoom(), .level_pixels = strip_level}, pixels);
    if (!stats.has_value() || stats->fallback_pixels != 0U) {
      return false;
    }
    for (int row = 0; row < rows; ++row) {
      const auto source = pixels.subspan(static_cast<std::size_t>(row) * width, width);
      auto destination = frame_.subspan(
          static_cast<std::size_t>(y + row) * vector_v2::kOverviewWidth + panel_bounds.x0,
          static_cast<std::size_t>(width));
      std::copy(source.begin(), source.end(), destination.begin());
    }
  }
  return true;
}

vector_v2::ChromeNavigation VectorV2Presenter::chrome_navigation() const {
  const int percent = vector_v2::zoom_percent(zoom());
  const auto extent = navigation_.extent();
  return {
      .zoom_percent = percent,
      .level_x = level_x(),
      .level_y = level_y(),
      .level_width = vector_v2::kWorldWidth * percent / 100,
      .level_height = vector_v2::kWorldHeight * percent / 100,
      .can_pan_top = extent.top,
      .can_pan_left = extent.left,
      .can_pan_right = extent.right,
      .can_pan_bottom = extent.bottom,
      .overview_pixels = canvas_.overview_pixels(),
  };
}

bool VectorV2Presenter::restore_canvas_region(vector_v2::PixelRect bounds) {
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t count = static_cast<std::size_t>(width) * height;
  if (width <= 0 || height <= 0 || count > region_.size()) {
    return false;
  }
  const vector_v2::PixelRect level_bounds{
      level_x() + bounds.x0,
      level_y() + bounds.y0,
      level_x() + bounds.x1,
      level_y() + bounds.y1,
  };
  const auto pixels = region_.first(count);
  if (!canvas_.compose_view({.zoom = zoom(), .level_pixels = level_bounds}, pixels).has_value()) {
    return false;
  }
  for (int row = 0; row < height; ++row) {
    const auto source = pixels.subspan(static_cast<std::size_t>(row) * width, width);
    auto destination = frame_.subspan(
        static_cast<std::size_t>(bounds.y0 + row) * vector_v2::kOverviewWidth + bounds.x0,
        static_cast<std::size_t>(width));
    std::copy(source.begin(), source.end(), destination.begin());
  }
  return true;
}

bool VectorV2Presenter::restore_canvas_overlays(const vector_v2::ChromeState& chrome) {
  const auto overlays = vector_v2::chrome_overlay_regions(chrome);
  for (std::size_t index = 0; index < overlays.count; ++index) {
    const auto bounds = overlays.regions[index];
    if (!restore_canvas_region({bounds.x0, bounds.y0, bounds.x1, bounds.y1})) {
      return false;
    }
  }
  return true;
}

LivePresentationTiming VectorV2Presenter::present_with_overlays(
    vector_v2::PixelRect bounds, const vector_v2::ChromeState& chrome, std::uint32_t event_us,
    std::int64_t compose_us, bool allow_minimap_refresh) {
  const auto overlays = vector_v2::chrome_overlay_regions(chrome);
  bool intersects = false;
  for (std::size_t index = 0; index < overlays.count; ++index) {
    const auto overlay = overlays.regions[index];
    intersects = intersects || (bounds.x0 < overlay.x1 && bounds.x1 > overlay.x0 &&
                                bounds.y0 < overlay.y1 && bounds.y1 > overlay.y0);
  }
  const bool overview_changed =
      !minimap_presented_ || presented_minimap_revision_ != canvas_.current_revision();
  const bool refresh_minimap =
      vector_v2::chrome_minimap_refresh_required(chrome, overview_changed, allow_minimap_refresh);
  if (!intersects && !refresh_minimap) {
    return present(bounds, event_us, compose_us);
  }
  const bool full_frame = bounds.x0 == 0 && bounds.y0 == 0 &&
                          bounds.x1 == vector_v2::kOverviewWidth &&
                          bounds.y1 == vector_v2::kOverviewHeight;
  if (intersects && !refresh_minimap && !full_frame) {
    return present_unobscured(bounds, chrome, event_us, compose_us);
  }

  const std::int64_t chrome_started = esp_timer_get_time();
  vector_v2::draw_chrome_canvas_overlays(frame_, vector_v2::kOverviewWidth,
                                         vector_v2::kOverviewHeight, chrome, chrome_navigation());
  const std::int64_t chrome_completed = esp_timer_get_time();
  auto timing = present(bounds, event_us, compose_us + chrome_completed - chrome_started);
  timing.chrome_us = chrome_completed - chrome_started;

  bool minimap_refreshed = false;
  const auto minimap = vector_v2::chrome_minimap_region(chrome);
  if (refresh_minimap && minimap.has_value() && timing.passed) {
    const bool requested_contains_minimap = bounds.x0 <= minimap->x0 && bounds.y0 <= minimap->y0 &&
                                            bounds.x1 >= minimap->x1 && bounds.y1 >= minimap->y1;
    if (requested_contains_minimap) {
      minimap_refreshed = true;
    } else {
      const vector_v2::PixelRect minimap_bounds{minimap->x0, minimap->y0, minimap->x1, minimap->y1};
      const auto minimap_timing = present(minimap_bounds, 0);
      timing.complete_us += minimap_timing.complete_us;
      timing.pushes += minimap_timing.pushes;
      timing.passed = minimap_timing.passed;
      minimap_refreshed = minimap_timing.passed;
    }
  }
  if (minimap_refreshed) {
    presented_minimap_revision_ = canvas_.current_revision();
    minimap_presented_ = true;
  }
  if (!restore_canvas_overlays(chrome)) {
    timing.passed = false;
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::present_unobscured(vector_v2::PixelRect bounds,
                                                             const vector_v2::ChromeState& chrome,
                                                             std::uint32_t event_us,
                                                             std::int64_t compose_us) {
  auto visible =
      vector_v2::chrome_unobscured_regions({bounds.x0, bounds.y0, bounds.x1, bounds.y1}, chrome);
  // Push top-down so a tear-synchronized writer stays behind the beam even
  // when overlay subtraction split the bounds out of row order.
  std::sort(visible.regions.begin(), visible.regions.begin() + visible.count,
            [](const auto& left, const auto& right) { return left.y0 < right.y0; });
  LivePresentationTiming total{.compose_us = compose_us, .passed = true};
  for (std::size_t index = 0; index < visible.count; ++index) {
    const auto region = visible.regions[index];
    const auto part = present({region.x0, region.y0, region.x1, region.y1}, event_us);
    if (!part.passed) {
      total.passed = false;
      return total;
    }
    if (total.pushes == 0U && part.pushes > 0U) {
      total.first_submit_us = part.first_submit_us;
      total.first_complete_us = part.first_complete_us;
    }
    total.complete_us += part.complete_us;
    total.pushes += part.pushes;
  }
  return total;
}

LivePresentationTiming VectorV2Presenter::refresh_pan(int old_x, int old_y,
                                                      const vector_v2::ChromeState& chrome,
                                                      std::uint32_t event_us) {
  const int delta_x = level_x() - old_x;
  const int delta_y = level_y() - old_y;
  const bool reusable = frame_reusable_ && frame_zoom_ == zoom() && frame_level_x_ == old_x &&
                        frame_level_y_ == old_y && frame_epoch_ == canvas_.composition_epoch() &&
                        frame_chrome_ == chrome && std::abs(delta_x) <= kMaximumCachedPanDelta &&
                        std::abs(delta_y) <= kMaximumCachedPanDelta;
  if (!reusable) {
    return refresh(chrome, event_us);
  }
  // scroll_frame mutates overlap before exposed composition can fail. Invalidate
  // first so every recovery path either establishes a fresh identity or stays
  // safely non-reusable.
  frame_reusable_ = false;
  const std::int64_t started = esp_timer_get_time();
  const auto scroll = vector_v2::scroll_frame(
      frame_, vector_v2::kOverviewWidth,
      {0, 0, vector_v2::kOverviewWidth, vector_v2::chrome_canvas_bottom(chrome)}, delta_x, delta_y);
  const std::int64_t scroll_completed = esp_timer_get_time();
  if (!scroll.has_value()) {
    return refresh(chrome, event_us);
  }
  for (std::size_t index = 0; index < scroll->exposed_count; ++index) {
    if (!compose_into_frame(scroll->exposed[index])) {
      // scroll_frame already moved overlap in place. A full refresh is the
      // only safe recovery and establishes a new reusable frame.
      return refresh(chrome, event_us);
    }
  }
  const std::int64_t exposed_completed = esp_timer_get_time();
  // Pan frames present canvas only: fixed overlays are opaque and unchanged,
  // dock rows do not move, and the frame holds pure canvas throughout the
  // gesture, so there is no per-frame chrome redraw or overlay restore. The
  // changing minimap viewport refreshes on a bounded cadence below.
  const std::int64_t tear_started = esp_timer_get_time();
  bool tear_synchronized = true;
  const std::int64_t tear_age = display_.tear_age_us();
  if (tear_age < 0 || tear_age > kPanTearElisionWindowUs) {
    tear_synchronized = display_.wait_for_safe_frame_start(40'000);
  }
  const std::int64_t tear_completed = esp_timer_get_time();
  auto timing =
      present_unobscured({0, 0, vector_v2::kOverviewWidth, vector_v2::chrome_canvas_bottom(chrome)},
                         chrome, event_us, exposed_completed - started);
  timing.tear_wait_us = tear_completed - tear_started;
  timing.tear_synchronized = tear_synchronized;
  if (timing.passed) {
    // The minimap viewport rectangle tracks every pan frame. The minimap-only
    // draw with the row-wise resample costs ~2 ms per frame including the
    // region present and canvas restore.
    if (const auto minimap = vector_v2::chrome_minimap_region(chrome); minimap.has_value()) {
      const std::int64_t chrome_started = esp_timer_get_time();
      const bool drawn = vector_v2::draw_chrome_minimap_overlay(frame_, vector_v2::kOverviewWidth,
                                                                vector_v2::kOverviewHeight, chrome,
                                                                chrome_navigation());
      const auto minimap_timing = present({minimap->x0, minimap->y0, minimap->x1, minimap->y1}, 0);
      const bool restored =
          restore_canvas_region({minimap->x0, minimap->y0, minimap->x1, minimap->y1});
      timing.chrome_us = esp_timer_get_time() - chrome_started;
      timing.complete_us += minimap_timing.complete_us;
      timing.pushes += minimap_timing.pushes;
      timing.passed = drawn && minimap_timing.passed && restored;
      if (minimap_timing.passed) {
        presented_minimap_revision_ = canvas_.current_revision();
        minimap_presented_ = true;
      }
    }
  }
  timing.scroll_us = scroll_completed - started;
  timing.exposed_compose_us = exposed_completed - scroll_completed;
  timing.frame_reused = true;
  static_cast<void>(canvas_.remember_view(navigation_.view()));
  frame_level_x_ = level_x();
  frame_level_y_ = level_y();
  frame_epoch_ = canvas_.composition_epoch();
  frame_chrome_ = chrome;
  frame_reusable_ = timing.passed;
  return timing;
}

LivePresentationTiming VectorV2Presenter::present(vector_v2::PixelRect bounds,
                                                  std::uint32_t event_us, std::int64_t compose_us) {
  bounds = align_bounds(bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return {.compose_us = compose_us};
  }
  const auto pixels =
      frame_.subspan(static_cast<std::size_t>(bounds.y0 * vector_v2::kOverviewWidth + bounds.x0));
  return present_pixels(bounds, pixels, vector_v2::kOverviewWidth, event_us, compose_us);
}

LivePresentationTiming VectorV2Presenter::present_pixels(vector_v2::PixelRect bounds,
                                                         std::span<const std::uint16_t> pixels,
                                                         int stride, std::uint32_t event_us,
                                                         std::int64_t compose_us) {
  LivePresentationTiming timing{.compose_us = compose_us};
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  if (width <= 0 || height <= 0 || stride < width) {
    return timing;
  }
  const std::size_t required =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(stride) +
      static_cast<std::size_t>(width);
  if (pixels.size() < required) {
    return timing;
  }
  scheduler_.require_revision(canvas_.current_revision());
  const bool full_frame = bounds.x0 == 0 && bounds.y0 == 0 &&
                          bounds.x1 == vector_v2::kOverviewWidth &&
                          bounds.y1 == vector_v2::kOverviewHeight;
  if (full_frame) {
    const std::int64_t tear_wait_started = esp_timer_get_time();
    timing.tear_synchronized = display_.wait_for_safe_frame_start(40'000);
    timing.tear_wait_us = esp_timer_get_time() - tear_wait_started;
  }
  int rows_per_strip = std::max(2, 8'192 / width);
  rows_per_strip &= ~1;
  const std::uint32_t submits_before = display_.submit_count();
  std::int64_t first_submitted = 0;
  for (int y = bounds.y0; y < bounds.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, bounds.y1 - y);
    const vector_v2::PixelRect strip_bounds{bounds.x0, y, bounds.x1, y + rows};
    const auto strip_pixels = pixels.subspan(static_cast<std::size_t>(y - bounds.y0) * stride);
    const auto sequence = scheduler_.schedule({.revision = canvas_.current_revision(),
                                               .panel_bounds = strip_bounds,
                                               .pixels = strip_pixels,
                                               .stride = stride});
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
    }
    if (!scheduler_.complete(*sequence)) {
      return timing;
    }
    ++timing.pushes;
  }
  const bool completed = display_.wait_for_all(2'000'000);
  const std::int64_t finished = esp_timer_get_time();
  const std::int64_t physical_complete = display_.complete_time_us(submits_before + 1U);
  const auto first_submitted_us = static_cast<std::uint32_t>(first_submitted);
  const auto physical_complete_us =
      static_cast<std::uint32_t>(physical_complete >= 0 ? physical_complete : finished);
  timing.first_submit_us =
      event_us == 0U ? 0 : static_cast<std::uint32_t>(first_submitted_us - event_us);
  timing.first_complete_us =
      event_us == 0U ? 0 : static_cast<std::uint32_t>(physical_complete_us - event_us);
  timing.complete_us = first_submitted == 0 ? 0 : finished - first_submitted;
  timing.passed = completed;
  return timing;
}

vector_v2::PixelRect VectorV2Presenter::primitive_bounds(
    std::span<const RibbonPrimitive> primitives, int canvas_bottom) const {
  float x0 = static_cast<float>(vector_v2::kOverviewWidth);
  float y0 = static_cast<float>(vector_v2::kOverviewHeight);
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
  auto bounds = align_bounds({static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)),
                              static_cast<int>(std::ceil(x1)), static_cast<int>(std::ceil(y1))});
  bounds.y1 = std::min(bounds.y1, canvas_bottom);
  return bounds;
}

}  // namespace tinydraw::esp32
