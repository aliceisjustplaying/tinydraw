#include "vector_v2_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

VectorV2Presenter::VectorV2Presenter(
    vector_v2::MaterializedCanvas& canvas, vector_v2::NavigationState& navigation,
    vector_v2::DisplayScheduler& scheduler, Co5300PanelTransport& display,
    std::span<std::uint16_t> frame_pixels, std::span<std::uint16_t> region_pixels,
    std::span<std::uint16_t> strip_scratch_pixels, std::span<std::uint16_t> overlay_backup_pixels)
    : canvas_(canvas),
      navigation_(navigation),
      scheduler_(scheduler),
      display_(display),
      frame_(frame_pixels),
      region_(region_pixels),
      strip_scratch_(strip_scratch_pixels),
      overlay_backup_(overlay_backup_pixels),
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
  // entire compose-and-present transaction succeeds below. A full compose
  // also rewrites every canvas row linearly, which materializes any active
  // pan ring for free.
  frame_reusable_ = false;
  frame_ring_ = {};
  frame_ring_bottom_ = 0;
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
  frame_chrome_ = chrome;
  // Fallback pixels are quality-only staleness: the frame is correct for the
  // current revision and stays pan-reusable; refinement sharpens the regions
  // later through refresh_region, which preserves reusability.
  frame_reusable_ = timing.passed;
  if (zoom() != vector_v2::ZoomLevel::k25Percent) {
    static_cast<void>(canvas_.remember_view(navigation_.view()));
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::refresh_region(vector_v2::PixelRect level_bounds,
                                                         const vector_v2::ChromeState& chrome,
                                                         std::uint32_t event_us) {
  if (frame_ring_bottom_ != 0) {
    // The frame is ring-rotated from an active pan; a full refresh both
    // materializes it and covers the requested region.
    return refresh(chrome, event_us);
  }
  // A successful region update writes exact current-revision pixels into the
  // linear frame, so it preserves whatever pan-reusability the frame had.
  const bool was_reusable = frame_reusable_;
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
    const auto result = compose_and_present(aligned_level, panel, chrome, event_us);
    frame_reusable_ = was_reusable && result.passed;
    return result;
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
  frame_reusable_ = was_reusable && total.passed;
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
  if (frame_ring_bottom_ != 0 && !refresh(chrome, event_us).passed) {
    return {};
  }
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
  if (frame_ring_bottom_ != 0 && !refresh(chrome, event_us).passed) {
    return {};
  }
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
  // Gesture deltas quantize to even pixels (even ring shift keeps staging on
  // the aligned word path). Truncate toward zero: `& ~1` rounds toward
  // negative infinity, which biased 1 px drags toward the negative axes.
  const auto quantize_even = [](float delta) {
    const int value = static_cast<int>(std::lround(delta));
    return value - value % 2;
  };
  const int requested_x = start_x + quantize_even(start_touch.x - current_touch.x);
  const int requested_y = start_y + quantize_even(start_touch.y - current_touch.y);
  const vector_v2::NavigationPoint panel_focus{
      .x = std::clamp(static_cast<int>(std::lround(current_touch.x)), 0,
                      vector_v2::kOverviewWidth - 1),
      .y = std::clamp(static_cast<int>(std::lround(current_touch.y)), 0,
                      vector_v2::kOverviewHeight - 1),
  };
  if (!navigation_.set_origin(requested_x, requested_y, panel_focus)) {
    return {};
  }
  if (level_x() == old_x && level_y() == old_y) {
    // Quantization or the level clamp absorbed the drag: a successful no-op,
    // not a presentation failure (it was inflating the pan failure counter
    // during edge scrubbing).
    return {.frame_reused = true, .passed = true};
  }
  return refresh_pan(old_x, old_y, chrome, event_us);
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
                                                             std::int64_t compose_us,
                                                             bool wait_for_completion) {
  auto visible =
      vector_v2::chrome_unobscured_regions({bounds.x0, bounds.y0, bounds.x1, bounds.y1}, chrome);
  if (visible.overflowed) {
    // A dropped region would stay at stale panel contents; fail loudly so
    // the caller repaints instead.
    return {.compose_us = compose_us};
  }
  // Push top-down so a tear-synchronized writer stays behind the beam even
  // when overlay subtraction split the bounds out of row order.
  std::sort(visible.regions.begin(), visible.regions.begin() + visible.count,
            [](const auto& left, const auto& right) { return left.y0 < right.y0; });
  LivePresentationTiming total{.compose_us = compose_us, .passed = true};
  for (std::size_t index = 0; index < visible.count; ++index) {
    const auto region = visible.regions[index];
    const auto part =
        present({region.x0, region.y0, region.x1, region.y1}, event_us, 0, wait_for_completion);
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
  // Composition-epoch drift is deliberately not part of this identity:
  // same-revision tile content is deterministic, so canvas changes between
  // frames (production, eviction) only affect quality, never correctness.
  // Revision changes invalidate through the scheduler's require_revision and
  // the frame writers.
  const bool reusable = frame_reusable_ && frame_zoom_ == zoom() && frame_level_x_ == old_x &&
                        frame_level_y_ == old_y && frame_chrome_ == chrome &&
                        std::abs(delta_x) <= kMaximumCachedPanDelta &&
                        std::abs(delta_y) <= kMaximumCachedPanDelta;
  if (!reusable) {
    return refresh(chrome, event_us);
  }
  // The ring advances before exposed composition can fail. Invalidate first
  // so every recovery path either establishes a fresh identity or stays
  // safely non-reusable.
  frame_reusable_ = false;
  const int canvas_bottom = vector_v2::chrome_canvas_bottom(chrome);
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, canvas_bottom};
  const std::int64_t started = esp_timer_get_time();
  // Pan is pointer math: the ring origin advances and only the exposed
  // strips compose; nobody moves the overlap.
  const auto scroll = vector_v2::ring_scroll(frame_ring_, ring_area, delta_x, delta_y);
  const std::int64_t scroll_completed = esp_timer_get_time();
  if (!scroll.has_value()) {
    return refresh(chrome, event_us);
  }
  frame_ring_bottom_ = canvas_bottom;
  const std::span<const vector_v2::PixelRect> exposed{scroll->exposed.data(),
                                                      scroll->exposed_count};
  // Overlays draw into the ring itself so their pixels ride the same
  // wire-efficient full-width strip pushes as the canvas: no x-splits (each
  // split transaction costs ~1 ms of synchronous window commands and the
  // split slivers broke row-major order), and no moment where the panel
  // shows canvas beneath an opaque overlay. The saved canvas restores after
  // staging so the ring stays pure for reuse. The exposed share under each
  // overlay settles first so overlays draw over current backdrop.
  struct PreparedOverlay {
    vector_v2::PixelRect rect;
    std::size_t offset;
    std::size_t count;
  };
  std::array<PreparedOverlay, 3> prepared{};
  std::size_t prepared_count = 0;
  std::size_t backup_used = 0;
  const auto overlay_regions = vector_v2::chrome_overlay_regions(chrome);
  for (std::size_t index = 0; index < overlay_regions.count; ++index) {
    auto rect =
        align_bounds({overlay_regions.regions[index].x0, overlay_regions.regions[index].y0,
                      overlay_regions.regions[index].x1, overlay_regions.regions[index].y1});
    rect.y1 = std::min(rect.y1, canvas_bottom);
    const int rect_width = rect.x1 - rect.x0;
    const int rect_height = rect.y1 - rect.y0;
    if (rect_width <= 0 || rect_height <= 0) {
      continue;
    }
    const std::size_t count = static_cast<std::size_t>(rect_width) * rect_height;
    if (backup_used + count > overlay_backup_.size() || count > strip_scratch_.size()) {
      return refresh(chrome, event_us);
    }
    for (const auto& exposed_rect : exposed) {
      const vector_v2::PixelRect part{
          std::max(exposed_rect.x0, rect.x0), std::max(exposed_rect.y0, rect.y0),
          std::min(exposed_rect.x1, rect.x1), std::min(exposed_rect.y1, rect.y1)};
      if (part.x0 < part.x1 && part.y0 < part.y1 && !compose_into_ring(part)) {
        return refresh(chrome, event_us);
      }
    }
    const auto backup = overlay_backup_.subspan(backup_used, count);
    copy_ring_region(rect, backup);
    std::copy(backup.begin(), backup.end(), strip_scratch_.begin());
    // Per-overlay surface: only this overlay's content survives the draw
    // clipping (the minimap interior fits only its own rect), so each rect
    // costs one cheap draw instead of three full redraws.
    if (!vector_v2::draw_chrome_strip_overlays(
            {strip_scratch_.first(count), rect_width, rect_height, rect.x0, rect.y0}, chrome,
            chrome_navigation())) {
      return refresh(chrome, event_us);
    }
    write_ring_region(rect, strip_scratch_.first(count));
    prepared[prepared_count++] = {rect, backup_used, count};
    backup_used += count;
  }
  const std::int64_t exposed_completed = esp_timer_get_time();
  // Tear discipline, fail-closed: the writer may start a band only with at
  // least kBeamStartMarginRows of estimated beam lead (TE-to-scan-start
  // uncertainty is ~15 rows, so a start inside the margin can sit ahead of
  // the beam and tear at the start row). A dead or stale tear signal, or a
  // failed frame-start wait, falls back to a full refresh: never race
  // blind, never reuse an unsynchronized frame.
  constexpr std::int64_t kBeamMarginUs =
      static_cast<std::int64_t>(kBeamStartMarginRows) * kTePeriodUs / kPanelSweepRows;
  const auto te = display_.tear_signal_timing();
  const std::int64_t now_us_health = esp_timer_get_time();
  if (te.rising_edges != te_last_count_) {
    te_last_count_ = te.rising_edges;
    te_last_change_us_ = now_us_health;
  }
  if (now_us_health - te_last_change_us_ > 100'000) {
    // No tear edge for several frame periods: the signal is dead or the
    // 32-bit age could alias as fresh after a timer wrap. Full refresh.
    return refresh(chrome, event_us);
  }
  const std::int64_t tear_started = esp_timer_get_time();
  int start_row = 0;
  bool tear_ready = false;
  // Deadline-based discipline: a dock-band start may need one frame-start
  // wait plus the margin lead, so the loop is bounded by wall clock (two
  // frame periods), not by attempt count.
  const std::int64_t discipline_deadline = tear_started + 2 * kTePeriodUs;
  while (!tear_ready && esp_timer_get_time() < discipline_deadline) {
    const std::int64_t age = display_.tear_age_us();
    if (age < 0) {
      break;
    }
    if (age < kBeamMarginUs) {
      // Just past a frame start: give the beam its margin lead (bounded
      // busy wait, at most kBeamMarginUs).
      esp_rom_delay_us(100);
      continue;
    }
    const std::int64_t beam_row = age * kPanelSweepRows / kTePeriodUs;
    if (beam_row < canvas_bottom) {
      start_row = std::max(0, (static_cast<int>(beam_row) - kBeamStartMarginRows)) & ~1;
      tear_ready = true;
    } else if (!display_.wait_for_safe_frame_start(40'000)) {
      break;
    }
  }
  if (!tear_ready) {
    return refresh(chrome, event_us);
  }
  const std::int64_t tear_completed = esp_timer_get_time();
  // Every strip push defers its completion wait; the frame drains the panel
  // exactly once at the end.
  const std::uint32_t first_sequence = display_.submit_count() + 1U;
  auto timing = present_ring({0, start_row, vector_v2::kOverviewWidth, canvas_bottom}, chrome,
                             event_us, exposed);
  timing.compose_us = exposed_completed - started;
  std::int64_t band_wait_us = 0;
  if (timing.passed && start_row > 0) {
    // The wrapped top band is a fresh scan start: the beam must have
    // wrapped past row 0 AND advanced its margin lead, or the writer starts
    // at row 0 with no margin and tears exactly like an unmargined first
    // band (observed on glass at da99311).
    const std::int64_t band_started = esp_timer_get_time();
    bool band_ready = false;
    const std::int64_t band_deadline = band_started + 2 * kTePeriodUs;
    while (!band_ready && esp_timer_get_time() < band_deadline) {
      const std::int64_t age = display_.tear_age_us();
      if (age < 0) {
        break;
      }
      const bool wrapped_during_band = age < esp_timer_get_time() - tear_completed;
      if (!wrapped_during_band) {
        if (!display_.wait_for_safe_frame_start(40'000)) {
          break;
        }
        continue;
      }
      if (age < kBeamMarginUs) {
        esp_rom_delay_us(100);
        continue;
      }
      band_ready = true;
    }
    band_wait_us = esp_timer_get_time() - band_started;
    if (!band_ready) {
      return refresh(chrome, event_us);
    }
    const auto wrapped =
        present_ring({0, 0, vector_v2::kOverviewWidth, start_row}, chrome, event_us, exposed);
    timing.pushes += wrapped.pushes;
    timing.passed = wrapped.passed;
  }
  if (!timing.passed) {
    // Pushes already started when a compose or push failed, so parts of the
    // panel may hold mixed content. Repaint everything.
    return refresh(chrome, event_us);
  }
  // Every strip staged synchronously, so the ring is free to mutate while
  // the panel drains: restore the saved canvas beneath the overlays and the
  // frame stays pure for the next cached pan.
  for (std::size_t index = 0; index < prepared_count; ++index) {
    write_ring_region(prepared[index].rect,
                      overlay_backup_.subspan(prepared[index].offset, prepared[index].count));
  }
  timing.tear_wait_us = (tear_completed - tear_started) + band_wait_us;
  // Cached pan frames are synchronized by construction: every degraded path
  // above fell back to a full refresh instead of presenting.
  timing.tear_synchronized = true;
  // Overlays (zoom rail, minimap viewport, battery) rode the sweep strips
  // inside the same pushes as their backdrops, so the minimap is current.
  if (vector_v2::chrome_minimap_region(chrome).has_value()) {
    presented_minimap_revision_ = canvas_.current_revision();
    minimap_presented_ = true;
  }
  const bool frame_completed = display_.wait_for_all(2'000'000);
  const std::int64_t frame_drained = esp_timer_get_time();
  timing.passed = timing.passed && frame_completed;
  timing.complete_us = frame_drained - tear_completed;
  if (event_us != 0U) {
    const std::int64_t physical = display_.complete_time_us(first_sequence);
    if (physical >= 0) {
      timing.first_complete_us =
          static_cast<std::uint32_t>(static_cast<std::uint32_t>(physical) - event_us);
    }
  }
  timing.scroll_us = scroll_completed - started;
  timing.exposed_compose_us = exposed_completed - scroll_completed;
  timing.frame_reused = true;
  static_cast<void>(canvas_.remember_view(navigation_.view()));
  frame_level_x_ = level_x();
  frame_level_y_ = level_y();
  frame_chrome_ = chrome;
  frame_reusable_ = timing.passed;
  return timing;
}

bool VectorV2Presenter::compose_into_ring(vector_v2::PixelRect panel_bounds) {
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  if (width <= 0 || height <= 0) {
    return true;
  }
  // Ring row/column math divides by the ring height; an inactive ring or
  // out-of-area bounds are contract violations, not composable requests.
  if (frame_ring_bottom_ <= 0 || panel_bounds.y1 > frame_ring_bottom_) {
    return false;
  }
  const int rows_per_strip = static_cast<int>(region_.size() / static_cast<std::size_t>(width));
  if (rows_per_strip <= 0) {
    return false;
  }
  for (int y = panel_bounds.y0; y < panel_bounds.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, panel_bounds.y1 - y);
    const vector_v2::PixelRect strip_level{
        level_x() + panel_bounds.x0,
        level_y() + y,
        level_x() + panel_bounds.x1,
        level_y() + y + rows,
    };
    const std::size_t count = static_cast<std::size_t>(width) * rows;
    const auto pixels = region_.first(count);
    // Fallback pixels in exposed strips are fine: motion shows the best
    // available quality and refinement sharpens on idle. Only composition
    // failure aborts to a full refresh.
    const auto stats = canvas_.compose_view({.zoom = zoom(), .level_pixels = strip_level}, pixels);
    if (!stats.has_value()) {
      return false;
    }
    for (int row = 0; row < rows; ++row) {
      const auto source = pixels.subspan(static_cast<std::size_t>(row) * width, width);
      const int ring_y = vector_v2::ring_row(frame_ring_, ring_area, y + row);
      const int ring_x = vector_v2::ring_column(frame_ring_, ring_area, panel_bounds.x0);
      auto destination_row =
          frame_.subspan(static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth);
      const int first = std::min(width, vector_v2::kOverviewWidth - ring_x);
      std::copy(source.begin(), source.begin() + first, destination_row.begin() + ring_x);
      if (first < width) {
        std::copy(source.begin() + first, source.end(), destination_row.begin());
      }
    }
  }
  return true;
}

void VectorV2Presenter::write_ring_region(vector_v2::PixelRect panel_bounds,
                                          std::span<const std::uint16_t> source) {
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  if (frame_ring_bottom_ <= 0 || panel_bounds.y1 > frame_ring_bottom_) {
    return;
  }
  const int width = panel_bounds.x1 - panel_bounds.x0;
  for (int y = panel_bounds.y0; y < panel_bounds.y1; ++y) {
    const int ring_y = vector_v2::ring_row(frame_ring_, ring_area, y);
    const int ring_x = vector_v2::ring_column(frame_ring_, ring_area, panel_bounds.x0);
    auto destination_row =
        frame_.subspan(static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth);
    const auto source_row = source.subspan(
        static_cast<std::size_t>(y - panel_bounds.y0) * static_cast<std::size_t>(width),
        static_cast<std::size_t>(width));
    const int first = std::min(width, vector_v2::kOverviewWidth - ring_x);
    std::copy(source_row.begin(), source_row.begin() + first, destination_row.begin() + ring_x);
    if (first < width) {
      std::copy(source_row.begin() + first, source_row.end(), destination_row.begin());
    }
  }
}

void VectorV2Presenter::copy_ring_region(vector_v2::PixelRect panel_bounds,
                                         std::span<std::uint16_t> destination) {
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  if (frame_ring_bottom_ <= 0 || panel_bounds.y1 > frame_ring_bottom_) {
    return;
  }
  const int width = panel_bounds.x1 - panel_bounds.x0;
  for (int y = panel_bounds.y0; y < panel_bounds.y1; ++y) {
    const int ring_y = vector_v2::ring_row(frame_ring_, ring_area, y);
    const int ring_x = vector_v2::ring_column(frame_ring_, ring_area, panel_bounds.x0);
    const auto source_row =
        frame_.subspan(static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth);
    auto target = destination.subspan(
        static_cast<std::size_t>(y - panel_bounds.y0) * static_cast<std::size_t>(width),
        static_cast<std::size_t>(width));
    const int first = std::min(width, vector_v2::kOverviewWidth - ring_x);
    std::copy(source_row.begin() + ring_x, source_row.begin() + ring_x + first, target.begin());
    if (first < width) {
      std::copy(source_row.begin(), source_row.begin() + (width - first), target.begin() + first);
    }
  }
}

LivePresentationTiming VectorV2Presenter::present_ring(
    vector_v2::PixelRect band, const vector_v2::ChromeState& chrome, std::uint32_t event_us,
    std::span<const vector_v2::PixelRect> exposed) {
  band = align_bounds(band);
  LivePresentationTiming timing{};
  const int band_width = band.x1 - band.x0;
  const int band_height = band.y1 - band.y0;
  if (band_width <= 0 || band_height <= 0) {
    timing.passed = true;
    return timing;
  }
  if (frame_ring_bottom_ <= 0 || band.y1 > frame_ring_bottom_) {
    return timing;
  }
  const std::size_t area_pixels =
      static_cast<std::size_t>(frame_ring_bottom_) * vector_v2::kOverviewWidth;
  scheduler_.require_revision(canvas_.current_revision());
  int rows_per_strip = std::max(2, 16'384 / band_width);
  rows_per_strip &= ~1;
  std::int64_t first_submitted = 0;
  // Strict row-major full-width sweep: the minimum number of transactions,
  // no row is ever revisited, and overlay pixels already sit in the ring, so
  // a raced beam can never meet a seam or a bare backdrop.
  for (int y = band.y0; y < band.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, band.y1 - y);
    const vector_v2::PixelRect strip_bounds{band.x0, y, band.x1, y + rows};
    // Just-in-time exposed composition fills the transfer-semaphore idle of
    // the DMA-bound sweep. Overlay rects are subtracted: their exposed share
    // settled before the overlays were drawn into the ring, and composing
    // here would overwrite the overlay pixels mid-push.
    for (const auto& rect : exposed) {
      const vector_v2::PixelRect part{std::max(rect.x0, band.x0), std::max(rect.y0, y),
                                      std::min(rect.x1, band.x1), std::min(rect.y1, y + rows)};
      if (part.x0 >= part.x1 || part.y0 >= part.y1) {
        continue;
      }
      const auto pieces =
          vector_v2::chrome_unobscured_regions({part.x0, part.y0, part.x1, part.y1}, chrome);
      if (pieces.overflowed) {
        return timing;
      }
      for (std::size_t piece = 0; piece < pieces.count; ++piece) {
        const auto& region = pieces.regions[piece];
        if (!compose_into_ring({region.x0, region.y0, region.x1, region.y1})) {
          return timing;
        }
      }
    }
    const auto sequence = scheduler_.schedule({.revision = canvas_.current_revision(),
                                               .panel_bounds = strip_bounds,
                                               .pixels = frame_.first(area_pixels),
                                               .stride = vector_v2::kOverviewWidth,
                                               .source_shift_x = frame_ring_.shift_x,
                                               .source_shift_y = frame_ring_.shift_y,
                                               .source_area_width = vector_v2::kOverviewWidth,
                                               .source_area_height = frame_ring_bottom_});
    const auto scheduled = scheduler_.front();
    if (!sequence.has_value() || !scheduled.has_value() || scheduled->sequence != *sequence) {
      return timing;
    }
    const std::uint32_t pushes_before = display_.push_count();
    display_.push_rect_ring(
        strip_bounds.x0, strip_bounds.y0, band_width, rows, scheduled->strip.pixels.data(),
        scheduled->strip.stride, scheduled->strip.source_shift_x, scheduled->strip.source_shift_y,
        scheduled->strip.source_area_width, scheduled->strip.source_area_height);
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
  timing.first_submit_us =
      event_us == 0U || first_submitted == 0
          ? 0
          : static_cast<std::uint32_t>(static_cast<std::uint32_t>(first_submitted) - event_us);
  timing.passed = true;
  return timing;
}

LivePresentationTiming VectorV2Presenter::present(vector_v2::PixelRect bounds,
                                                  std::uint32_t event_us, std::int64_t compose_us,
                                                  bool wait_for_completion) {
  bounds = align_bounds(bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    // Nothing to push is success, not failure: a caller presenting several
    // regions must not abort the frame over a clip-collapsed rectangle.
    return {.compose_us = compose_us, .passed = true};
  }
  const auto pixels =
      frame_.subspan(static_cast<std::size_t>(bounds.y0 * vector_v2::kOverviewWidth + bounds.x0));
  return present_pixels(bounds, pixels, vector_v2::kOverviewWidth, event_us, compose_us,
                        wait_for_completion);
}

LivePresentationTiming VectorV2Presenter::present_pixels(vector_v2::PixelRect bounds,
                                                         std::span<const std::uint16_t> pixels,
                                                         int stride, std::uint32_t event_us,
                                                         std::int64_t compose_us,
                                                         bool wait_for_completion) {
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
  int rows_per_strip = std::max(2, 16'384 / width);
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
  // A caller presenting several regions in one frame defers the completion
  // wait to the end of the frame instead of sleeping once per region.
  const bool completed = !wait_for_completion || display_.wait_for_all(2'000'000);
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
