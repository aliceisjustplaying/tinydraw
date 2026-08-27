#include "vector_v2_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "esp_timer.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "vector_v2_presenter_internal.h"

namespace tinydraw::esp32 {
namespace {

constexpr vector_v2::NavigationPoint kDefaultNavigationFocus{vector_v2::kOverviewWidth / 2,
                                                             vector_v2::kChromeCanvasBottom / 2};

}  // namespace

const char* presentation_experiment_name() { return "boundary-top-sweep"; }

const char* selected_tear_edge_name() { return "rising"; }

TearSignalEdge selected_tear_edge() { return TearSignalEdge::kRising; }

VectorV2Presenter::VectorV2Presenter(vector_v2::MaterializedCanvas& canvas,
                                     vector_v2::NavigationState& navigation,
                                     Co5300PanelTransport& display,
                                     std::span<std::uint16_t> frame_pixels,
                                     std::span<std::uint16_t> region_pixels,
                                     std::span<std::uint16_t> chrome_cache_pixels)
    : canvas_(canvas),
      navigation_(navigation),
      display_(display),
      frame_(frame_pixels),
      region_(region_pixels),
      chrome_cache_(chrome_cache_pixels),
      renderer_(std::make_unique<RibbonRenderer>()) {
  std::printf(
      "TINYDRAW_VECTOR_V2_PRESENTATION experiment=%s te_edge=%s "
      "clock_mhz=%d "
      "optical_acceptance=external_manual\n",
      presentation_experiment_name(), selected_tear_edge_name(), kCo5300ClockMHz);
}

bool VectorV2Presenter::ready() const {
  return canvas_.ready() && display_.ready() && frame_.size() == vector_v2::kOverviewPixels &&
         region_.size() >= kLiveRegionScratchPixels && chrome_cache_.ready() &&
         renderer_ != nullptr;
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

void VectorV2Presenter::overlay_pending(vector_v2::PixelRect level_bounds,
                                        std::span<std::uint16_t> pixels, int stride) {
  // Lockstep is the overwhelmingly common case; the revision check keeps the
  // patch free when nothing is pending. A failed overlay is deliberately
  // swallowed: composed canvas pixels are a correct (trailing) fallback, and
  // the drain loop converges the canvas regardless.
  if (authority_ == nullptr || authority_->current_revision() == canvas_.current_revision()) {
    return;
  }
  static_cast<void>(vector_v2::overlay_pending_operations(
      *authority_, canvas_,
      {.zoom = zoom(), .level_bounds = level_bounds, .pixels = pixels, .stride = stride}));
}

bool VectorV2Presenter::stage_settled_pixels(vector_v2::PixelRect panel_bounds,
                                             std::span<const std::uint16_t> pixels, int stride) {
  interrupt_refresh();
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  if (width <= 0 || height <= 0 || stride < width || panel_bounds.x0 < 0 || panel_bounds.y0 < 0 ||
      panel_bounds.x1 > vector_v2::kOverviewWidth || panel_bounds.y1 > vector_v2::kOverviewHeight ||
      pixels.size() < static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(stride) +
                          static_cast<std::size_t>(width)) {
    return false;
  }
  if (frame_ring_bottom_ != 0) {
    if (panel_bounds.y1 > frame_ring_bottom_) {
      return false;
    }
    copy_pixels_to_ring(panel_bounds, pixels, stride);
    return true;
  }
  for (int row = 0; row < height; ++row) {
    const auto source =
        pixels.subspan(static_cast<std::size_t>(row) * static_cast<std::size_t>(stride),
                       static_cast<std::size_t>(width));
    auto target =
        frame_.subspan(static_cast<std::size_t>(panel_bounds.y0 + row) * vector_v2::kOverviewWidth +
                           static_cast<std::size_t>(panel_bounds.x0),
                       static_cast<std::size_t>(width));
    std::copy(source.begin(), source.end(), target.begin());
  }
  return true;
}

LivePresentationTiming VectorV2Presenter::present_frame_region(vector_v2::PixelRect panel_bounds,
                                                               const vector_v2::ChromeState& chrome,
                                                               std::uint32_t event_us) {
  interrupt_refresh();
  return present_with_overlays(panel_bounds, chrome, event_us, 0, false);
}

void VectorV2Presenter::cancel_refresh() {
  refresh_cursor_.cancel();
  refresh_pending_ = false;
  refresh_deferred_ = false;
  refresh_compose_us_ = 0;
  refresh_compose_slice_max_us_ = 0;
  refresh_compose_slices_ = 0;
}

void VectorV2Presenter::interrupt_refresh() {
  if (!refresh_pending_ && !refresh_deferred_) {
    return;
  }
  refresh_cursor_.cancel();
  refresh_pending_ = false;
  refresh_deferred_ = true;
  refresh_compose_us_ = 0;
  refresh_compose_slice_max_us_ = 0;
  refresh_compose_slices_ = 0;
}

LivePresentationTiming VectorV2Presenter::refresh(const vector_v2::ChromeState& chrome,
                                                  std::uint32_t event_us) {
  cancel_refresh();
  clear_live_overlay();
  // Composition mutates the cached frame. It cannot remain reusable unless the
  // entire compose-and-present transaction succeeds below. A full compose
  // also rewrites every canvas row linearly, which materializes any active
  // pan ring for free.
  frame_reusable_ = false;
  frame_ring_ = {};
  frame_ring_bottom_ = 0;
  const std::int64_t compose_started = esp_timer_get_time();
  const auto view = navigation_.view();
  const auto stats = canvas_.compose_view(view, frame_);
  if (!stats.has_value()) {
    return {};
  }
  ++frame_compose_epoch_;
  overlay_pending(view.level_pixels, frame_, vector_v2::kOverviewWidth);
  auto timing =
      present_with_overlays({0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome,
                            event_us, esp_timer_get_time() - compose_started, true);
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  if (optical_row_pattern_enabled_ && timing.pushes > 0U) {
    optical_generation_ ^= 1U;
  }
#endif
  timing.tile_pixels = stats->tile_pixels;
  timing.uniform_pixels = stats->uniform_pixels;
  timing.overview_pixels = stats->overview_pixels;
  timing.fallback_pixels = stats->fallback_pixels;
  timing.resident_tiles = stats->immediate_tiles + stats->settled_tiles;
  timing.fallback_tiles = stats->fallback_tiles;
  frame_zoom_ = zoom();
  frame_level_x_ = level_x();
  frame_level_y_ = level_y();
  frame_chrome_ = chrome;
  // Fallback pixels are quality-only staleness. Reuse additionally requires
  // that software observed the configured TE edge for this completed frame.
  frame_reusable_ = timing.passed && timing.tear_edge_observed;
  if (zoom() != vector_v2::ZoomLevel::k25Percent) {
    static_cast<void>(canvas_.remember_view(navigation_.view()));
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::refresh_slice(const vector_v2::ChromeState& chrome,
                                                        std::uint32_t event_us,
                                                        bool interaction_active) {
  const bool authority_pending =
      authority_ != nullptr && authority_->current_revision() != canvas_.current_revision();
  if (interaction_active || authority_pending) {
    if (!refresh_pending_ && !refresh_deferred_) {
      refresh_deferred_ = true;
    } else {
      interrupt_refresh();
    }
    return {.compose_pending = true};
  }
  const auto view = navigation_.view();
  if (refresh_pending_ && (!(refresh_view_ == view) || !(refresh_chrome_ == chrome))) {
    cancel_refresh();
  }
  if (!refresh_pending_) {
    refresh_deferred_ = false;
    clear_live_overlay();
    frame_reusable_ = false;
    frame_ring_ = {};
    frame_ring_bottom_ = 0;
    refresh_view_ = view;
    refresh_chrome_ = chrome;
    refresh_event_us_ = event_us;
    refresh_pending_ = true;
  }

  const std::int64_t slice_started = esp_timer_get_time();
  const auto result = canvas_.compose_view_slice(refresh_view_, frame_, refresh_cursor_);
  const std::int64_t slice_us = esp_timer_get_time() - slice_started;
  refresh_compose_us_ += slice_us;
  refresh_compose_slice_max_us_ = std::max(refresh_compose_slice_max_us_, slice_us);
  ++refresh_compose_slices_;
  LivePresentationTiming timing{
      .compose_us = refresh_compose_us_,
      .compose_slice_max_us = refresh_compose_slice_max_us_,
      .compose_slices = refresh_compose_slices_,
      .compose_pending = result.status == vector_v2::ViewCompositionStatus::kInProgress,
  };
  if (result.status == vector_v2::ViewCompositionStatus::kInProgress) {
    return timing;
  }
  if (result.status == vector_v2::ViewCompositionStatus::kError) {
    const auto retry_view = navigation_.view();
    const std::uint32_t retry_event_us = refresh_event_us_;
    cancel_refresh();
    refresh_view_ = retry_view;
    refresh_chrome_ = chrome;
    refresh_event_us_ = retry_event_us;
    refresh_pending_ = true;
    timing.compose_pending = true;
    return timing;
  }

  ++frame_compose_epoch_;
  overlay_pending(refresh_view_.level_pixels, frame_, vector_v2::kOverviewWidth);
  const auto stats = result.stats;
  const auto completed_view = refresh_view_;
  const auto completed_chrome = refresh_chrome_;
  const std::uint32_t completed_event_us = refresh_event_us_;
  const std::int64_t compose_us = refresh_compose_us_;
  const std::int64_t max_slice_us = refresh_compose_slice_max_us_;
  const std::uint32_t slices = refresh_compose_slices_;
  cancel_refresh();
  timing = present_with_overlays({0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
                                 completed_chrome, completed_event_us, compose_us, true);
  timing.compose_slice_max_us = max_slice_us;
  timing.compose_slices = slices;
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  if (optical_row_pattern_enabled_ && timing.pushes > 0U) {
    optical_generation_ ^= 1U;
  }
#endif
  timing.tile_pixels = stats.tile_pixels;
  timing.uniform_pixels = stats.uniform_pixels;
  timing.overview_pixels = stats.overview_pixels;
  timing.fallback_pixels = stats.fallback_pixels;
  timing.resident_tiles = stats.immediate_tiles + stats.settled_tiles;
  timing.fallback_tiles = stats.fallback_tiles;
  frame_zoom_ = completed_view.zoom;
  frame_level_x_ = completed_view.level_pixels.x0;
  frame_level_y_ = completed_view.level_pixels.y0;
  frame_chrome_ = completed_chrome;
  frame_reusable_ = timing.passed && timing.tear_edge_observed;
  if (completed_view.zoom != vector_v2::ZoomLevel::k25Percent) {
    static_cast<void>(canvas_.remember_view(completed_view));
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::refresh_region(vector_v2::PixelRect level_bounds,
                                                         const vector_v2::ChromeState& chrome,
                                                         std::uint32_t event_us) {
  interrupt_refresh();
  // A successful region update writes exact current-revision pixels into the
  // linear or ring frame, preserving its current addressing and reusability.
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
    // fixed canvas overlay and still needs one revision-driven presentation.
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
  panel = presenter_internal::align_bounds(panel);
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
    total.chrome_us += part.chrome_us;
    total.chrome_prepare_us += part.chrome_prepare_us;
    total.chrome_stage_us += part.chrome_stage_us;
    total.tile_pixels += part.tile_pixels;
    total.uniform_pixels += part.uniform_pixels;
    total.overview_pixels += part.overview_pixels;
    total.fallback_pixels += part.fallback_pixels;
    total.resident_tiles += part.resident_tiles;
    total.fallback_tiles += part.fallback_tiles;
    total.submitted_pixels += part.submitted_pixels;
    total.pushes += part.pushes;
  }
  frame_reusable_ = was_reusable && total.passed;
  return total;
}

LivePresentationTiming VectorV2Presenter::compose_and_present(vector_v2::PixelRect level_bounds,
                                                              vector_v2::PixelRect panel_bounds,
                                                              const vector_v2::ChromeState& chrome,
                                                              std::uint32_t event_us) {
  interrupt_refresh();
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
  overlay_pending(level_bounds, destination, width);
  if (frame_ring_bottom_ != 0) {
    if (panel_bounds.y1 > frame_ring_bottom_) {
      return {};
    }
    copy_pixels_to_ring(panel_bounds, destination, width);
  } else {
    for (int row = 0; row < height; ++row) {
      const auto source = destination.subspan(static_cast<std::size_t>(row) * width, width);
      auto target = frame_.subspan(
          static_cast<std::size_t>(panel_bounds.y0 + row) * vector_v2::kOverviewWidth +
              static_cast<std::size_t>(panel_bounds.x0),
          static_cast<std::size_t>(width));
      std::copy(source.begin(), source.end(), target.begin());
    }
  }
  auto timing = present_with_overlays(panel_bounds, chrome, event_us,
                                      esp_timer_get_time() - compose_started, true);
  timing.tile_pixels = stats->tile_pixels;
  timing.uniform_pixels = stats->uniform_pixels;
  timing.overview_pixels = stats->overview_pixels;
  timing.fallback_pixels = stats->fallback_pixels;
  timing.resident_tiles = stats->immediate_tiles + stats->settled_tiles;
  timing.fallback_tiles = stats->fallback_tiles;
  return timing;
}

LivePresentationTiming VectorV2Presenter::show_start(InkPoint point, std::uint16_t color,
                                                     const vector_v2::ChromeState& chrome,
                                                     std::uint32_t event_us) {
  interrupt_refresh();
  if (frame_ring_bottom_ == 0) {
    frame_reusable_ = false;
  }
  const int canvas_bottom = vector_v2::chrome_input_bottom(chrome);
  if (canvas_bottom == 0) {
    return {.passed = true};
  }
  const RibbonPrimitive cap{
      .kind = RibbonPrimitiveKind::kCircle, .center = point.position, .radius = point.radius};
  const std::array primitives{cap};
  live_provisional_[0] = cap;
  live_provisional_count_ = 1U;
  live_provisional_bounds_ = primitive_bounds(primitives, canvas_bottom);
  live_provisional_color_ = color;
  return present_unobscured(live_provisional_bounds_, chrome, event_us);
}

LivePresentationTiming VectorV2Presenter::show_update(const RibbonUpdate& update,
                                                      std::uint16_t color,
                                                      const vector_v2::ChromeState& chrome,
                                                      std::uint32_t event_us) {
  interrupt_refresh();
  if (frame_ring_bottom_ == 0) {
    frame_reusable_ = false;
  }
  const int canvas_bottom = vector_v2::chrome_input_bottom(chrome);
  if (canvas_bottom == 0) {
    clear_live_overlay();
    return {.passed = true};
  }
  const vector_v2::PixelRect old_provisional_bounds = live_provisional_bounds_;
  const auto committed = std::span(update.committed.begin(), update.committed.size());
  if (!committed.empty()) {
    if (frame_ring_bottom_ != 0) {
      if (!render_into_ring(committed, color, primitive_bounds(committed, canvas_bottom))) {
        return {};
      }
    } else {
      static_cast<void>(
          renderer_->render(committed, frame_, vector_v2::kOverviewWidth, canvas_bottom, color));
    }
  }
  const vector_v2::PixelRect committed_bounds = primitive_bounds(committed, canvas_bottom);

  live_provisional_count_ = update.provisional.size();
  std::copy(update.provisional.begin(), update.provisional.end(), live_provisional_.begin());
  const auto provisional =
      std::span(live_provisional_.data(), static_cast<std::size_t>(live_provisional_count_));
  live_provisional_bounds_ = primitive_bounds(provisional, canvas_bottom);
  live_provisional_color_ = color;

  vector_v2::PixelRect damage = old_provisional_bounds;
  const auto include = [&damage](vector_v2::PixelRect bounds) {
    if (bounds.x0 >= bounds.x1 || bounds.y0 >= bounds.y1) {
      return;
    }
    if (damage.x0 >= damage.x1 || damage.y0 >= damage.y1) {
      damage = bounds;
      return;
    }
    damage.x0 = std::min(damage.x0, bounds.x0);
    damage.y0 = std::min(damage.y0, bounds.y0);
    damage.x1 = std::max(damage.x1, bounds.x1);
    damage.y1 = std::max(damage.y1, bounds.y1);
  };
  include(committed_bounds);
  include(live_provisional_bounds_);
  return present_unobscured(damage, chrome, event_us);
}

LivePresentationTiming VectorV2Presenter::set_zoom(vector_v2::ZoomLevel target_zoom,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us) {
  if (!navigation_.set_zoom(target_zoom, kDefaultNavigationFocus)) {
    return {};
  }
  return refresh(chrome, event_us);
}

#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
LivePresentationTiming VectorV2Presenter::set_view(vector_v2::ZoomLevel target_zoom, int level_x,
                                                   int level_y,
                                                   const vector_v2::ChromeState& chrome,
                                                   std::uint32_t event_us) {
  if (!navigation_.set_zoom(target_zoom, kDefaultNavigationFocus) ||
      !navigation_.set_origin(level_x, level_y, kDefaultNavigationFocus)) {
    return {};
  }
  return refresh(chrome, event_us);
}
#endif

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

LivePresentationTiming VectorV2Presenter::pan_minimap_from(int start_x, int start_y,
                                                           Point current_touch,
                                                           const vector_v2::ChromeState& chrome,
                                                           std::uint32_t event_us) {
  const int old_x = level_x();
  const int old_y = level_y();
  auto navigation = chrome_navigation();
  // Projection is relative to the gesture's original view even after earlier
  // Move events changed the live origin.
  navigation.level_x = start_x;
  navigation.level_y = start_y;
  const auto requested = vector_v2::chrome_minimap_drag_origin(
      {current_touch.x, current_touch.y},
      {.x = kDefaultNavigationFocus.x, .y = kDefaultNavigationFocus.y}, navigation);
  if (!navigation_.set_origin(requested.x, requested.y, kDefaultNavigationFocus)) {
    return {};
  }
  if (level_x() == old_x && level_y() == old_y) {
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

LivePresentationTiming VectorV2Presenter::present_with_overlays(
    vector_v2::PixelRect bounds, const vector_v2::ChromeState& chrome, std::uint32_t event_us,
    std::int64_t compose_us, bool allow_minimap_refresh) {
  const bool overview_changed =
      !minimap_presented_ || presented_minimap_revision_ != canvas_.current_revision();
  const bool refresh_minimap =
      vector_v2::chrome_minimap_refresh_required(chrome, overview_changed, allow_minimap_refresh);
  if (refresh_minimap) {
    if (const auto minimap = vector_v2::chrome_minimap_region(chrome); minimap.has_value()) {
      bounds.x0 = std::min(bounds.x0, minimap->x0);
      bounds.y0 = std::min(bounds.y0, minimap->y0);
      bounds.x1 = std::max(bounds.x1, minimap->x1);
      bounds.y1 = std::max(bounds.y1, minimap->y1);
      bounds = presenter_internal::align_bounds(bounds);
    }
  }
  auto timing = frame_ring_bottom_ == 0 ? present(bounds, chrome, event_us, compose_us)
                                        : present_ring_region(bounds, chrome, event_us, compose_us);
  if (refresh_minimap && timing.passed) {
    presented_minimap_revision_ = canvas_.current_revision();
    minimap_presented_ = true;
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::present_unobscured(vector_v2::PixelRect bounds,
                                                             const vector_v2::ChromeState& chrome,
                                                             std::uint32_t event_us,
                                                             std::int64_t compose_us,
                                                             bool wait_for_completion) {
  // Fixed chrome is restored inside the same staged window, so intersecting
  // ink no longer splits into multiple panel submissions or completion drains.
  return frame_ring_bottom_ == 0
             ? present(bounds, chrome, event_us, compose_us, wait_for_completion)
             : present_ring_region(bounds, chrome, event_us, compose_us, wait_for_completion);
}

}  // namespace tinydraw::esp32
