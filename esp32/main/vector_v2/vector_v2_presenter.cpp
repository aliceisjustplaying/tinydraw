#include "vector_v2_presenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

#if !defined(TINYDRAW_VECTOR_V2_PRESENTATION_BOUNDARY_TOP_SWEEP) && \
    !defined(TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL)
#define TINYDRAW_VECTOR_V2_PRESENTATION_BOUNDARY_TOP_SWEEP 1
#endif
#if defined(TINYDRAW_VECTOR_V2_PRESENTATION_BOUNDARY_TOP_SWEEP) && \
    defined(TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL)
#error "Select exactly one Vector V2 presentation experiment"
#endif
#if !defined(TINYDRAW_VECTOR_V2_TE_EDGE_RISING) && !defined(TINYDRAW_VECTOR_V2_TE_EDGE_FALLING)
#define TINYDRAW_VECTOR_V2_TE_EDGE_RISING 1
#endif
#if defined(TINYDRAW_VECTOR_V2_TE_EDGE_RISING) && defined(TINYDRAW_VECTOR_V2_TE_EDGE_FALLING)
#error "Select exactly one Vector V2 TE edge"
#endif
#ifndef TINYDRAW_CO5300_CLOCK_MHZ
#define TINYDRAW_CO5300_CLOCK_MHZ 50
#endif

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::int64_t kTearWaitTimeoutUs = 40'000;
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
constexpr int kOpticalPatternX = 176;
constexpr int kOpticalPatternWidth = 16;

std::uint16_t optical_pattern_pixel(std::uint8_t generation, int row, int column) {
  constexpr std::array<std::uint8_t, 2> kFrameIds{0x35U, 0xCAU};
  constexpr std::array<std::uint16_t, 2> kFrameBitColors{0xF800U, 0x07E0U};
  constexpr std::array<std::uint16_t, 2> kRowBitColors{0xFFFFU, 0x001FU};
  if (column < 8) {
    return kFrameBitColors[(kFrameIds[generation & 1U] >> column) & 1U];
  }
  return kRowBitColors[(row >> (column - 8)) & 1];
}
#endif

void record_tear_wait(LivePresentationTiming& timing, const TearEdgeWaitResult& wait) {
  timing.tear_edge_observed = wait.observed;
  timing.tear_edge_timed_out = timing.tear_edge_timed_out || wait.timed_out;
  timing.tear_heal_attempted = timing.tear_heal_attempted || wait.heal_attempted;
  timing.tear_heal_command_sent = timing.tear_heal_command_sent || wait.heal_command_sent;
  timing.tear_edge_wait_resumed = timing.tear_edge_wait_resumed || wait.observed;
  if (wait.observed) {
    timing.tear_edge_isr_to_resume_us = wait.task_resume_timestamp_us - wait.isr_timestamp_us;
  }
}

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

const char* presentation_experiment_name() {
#ifdef TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL
  return "beam-race-control";
#else
  return "boundary-top-sweep";
#endif
}

const char* selected_tear_edge_name() {
#ifdef TINYDRAW_VECTOR_V2_TE_EDGE_FALLING
  return "falling";
#else
  return "rising";
#endif
}

TearSignalEdge selected_tear_edge() {
#ifdef TINYDRAW_VECTOR_V2_TE_EDGE_FALLING
  return TearSignalEdge::kFalling;
#else
  return TearSignalEdge::kRising;
#endif
}

int panel_clock_mhz() { return TINYDRAW_CO5300_CLOCK_MHZ; }

VectorV2Presenter::VectorV2Presenter(vector_v2::MaterializedCanvas& canvas,
                                     vector_v2::NavigationState& navigation,
                                     vector_v2::DisplayScheduler& scheduler,
                                     Co5300PanelTransport& display,
                                     std::span<std::uint16_t> frame_pixels,
                                     std::span<std::uint16_t> region_pixels,
                                     std::span<std::uint16_t> chrome_cache_pixels)
    : canvas_(canvas),
      navigation_(navigation),
      scheduler_(scheduler),
      display_(display),
      frame_(frame_pixels),
      region_(region_pixels),
      chrome_cache_(chrome_cache_pixels),
      renderer_(std::make_unique<RibbonRenderer>()) {
  std::printf(
      "TINYDRAW_VECTOR_V2_PRESENTATION experiment=%s te_edge=%s clock_mhz=%d "
      "optical_acceptance=external_manual\n",
      presentation_experiment_name(), selected_tear_edge_name(), panel_clock_mhz());
}

bool VectorV2Presenter::ready() const {
  return canvas_.ready() && scheduler_.ready() && display_.ready() &&
         frame_.size() == vector_v2::kOverviewPixels &&
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
  timing.resident_tiles = stats->immediate_tiles + stats->settled_tiles + stats->exact_tiles;
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
      bounds = align_bounds(bounds);
    }
  }
  auto timing = present(bounds, chrome, event_us, compose_us);
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
  return present(bounds, chrome, event_us, compose_us, wait_for_completion);
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
  // The ring stays pure. Exposed canvas and fixed chrome are painted into
  // each host-order internal staging strip during the ordered sweep.
  LivePresentationTiming timing{};
  timing.compose_us = scroll_completed - started;
  const std::int64_t tear_started = esp_timer_get_time();
  std::int64_t tear_completed = tear_started;
  const std::uint32_t first_sequence = display_.submit_count() + 1U;

#ifdef TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL
  // Experimental control only: retain the two-band age model without claiming
  // that estimated rows correspond to visible scanout.
  constexpr std::int64_t kBeamMarginUs =
      static_cast<std::int64_t>(kBeamStartMarginRows) * kTePeriodUs / kPanelSweepRows;
  const auto te = display_.tear_signal_timing();
  const std::uint32_t selected_count =
      selected_tear_edge() == TearSignalEdge::kRising ? te.rising_edges : te.falling_edges;
  const std::int64_t now_us_health = esp_timer_get_time();
  if (selected_count != te_last_count_) {
    te_last_count_ = selected_count;
    te_last_change_us_ = now_us_health;
  }
  timing.tear_edge_observed = selected_count != 0U;
  if (selected_count == 0U || now_us_health - te_last_change_us_ > 100'000) {
    return timing;
  }

  int start_row = 0;
  bool model_ready = false;
  const std::int64_t discipline_deadline = tear_started + 2 * kTePeriodUs;
  while (!model_ready && esp_timer_get_time() < discipline_deadline) {
    const std::int64_t age = display_.tear_age_us(selected_tear_edge());
    if (age < 0) {
      break;
    }
    if (age < kBeamMarginUs) {
      esp_rom_delay_us(100);
      continue;
    }
    const std::int64_t estimated_row = age * kPanelSweepRows / kTePeriodUs;
    if (estimated_row < canvas_bottom) {
      start_row = std::max(0, static_cast<int>(estimated_row) - kBeamStartMarginRows) & ~1;
      model_ready = true;
    } else {
      const auto wait = display_.wait_for_tear_edge(selected_tear_edge(), kTearWaitTimeoutUs);
      record_tear_wait(timing, wait);
      if (!wait.observed) {
        break;
      }
    }
  }
  if (!model_ready) {
    return timing;
  }
  tear_completed = esp_timer_get_time();
  const LivePresentationTiming edge_timing = timing;
  timing = present_ring({0, start_row, vector_v2::kOverviewWidth, canvas_bottom}, chrome, event_us,
                        exposed);
  timing.compose_us = scroll_completed - started + timing.exposed_compose_us;
  timing.tear_edge_observed = true;
  timing.tear_edge_timed_out = edge_timing.tear_edge_timed_out;
  timing.tear_heal_attempted = edge_timing.tear_heal_attempted;
  timing.tear_heal_command_sent = edge_timing.tear_heal_command_sent;
  timing.tear_edge_isr_to_resume_us = edge_timing.tear_edge_isr_to_resume_us;
  timing.tear_edge_wait_resumed = edge_timing.tear_edge_wait_resumed;
  std::int64_t band_wait_us = 0;
  if (timing.passed && start_row > 0) {
    const std::int64_t band_started = esp_timer_get_time();
    bool band_ready = false;
    const std::int64_t band_deadline = band_started + 2 * kTePeriodUs;
    while (!band_ready && esp_timer_get_time() < band_deadline) {
      const std::int64_t age = display_.tear_age_us(selected_tear_edge());
      if (age < 0) {
        break;
      }
      const bool wrapped_during_band = age < esp_timer_get_time() - tear_completed;
      if (!wrapped_during_band) {
        const auto wait = display_.wait_for_tear_edge(selected_tear_edge(), kTearWaitTimeoutUs);
        record_tear_wait(timing, wait);
        if (!wait.observed) {
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
    if (band_ready) {
      const auto wrapped =
          present_ring({0, 0, vector_v2::kOverviewWidth, start_row}, chrome, event_us, exposed);
      timing.pushes += wrapped.pushes;
      timing.exposed_compose_us += wrapped.exposed_compose_us;
      timing.chrome_us += wrapped.chrome_us;
      timing.compose_us += wrapped.exposed_compose_us;
      timing.passed = wrapped.passed;
    } else {
      timing.passed = false;
    }
  }
  timing.tear_wait_us = (tear_completed - tear_started) + band_wait_us;
#else
  // Normal development path: require a newly observed configured TE edge,
  // then submit one monotonically increasing row-zero sweep.
  const auto wait = display_.wait_for_tear_edge(selected_tear_edge(), kTearWaitTimeoutUs);
  tear_completed = esp_timer_get_time();
  timing.tear_wait_us = tear_completed - tear_started;
  record_tear_wait(timing, wait);
  if (!wait.observed) {
    return timing;
  }
  const auto sweep =
      present_ring({0, 0, vector_v2::kOverviewWidth, canvas_bottom}, chrome, event_us, exposed);
  timing.pushes = sweep.pushes;
  timing.first_submit_us = sweep.first_submit_us;
  timing.exposed_compose_us = sweep.exposed_compose_us;
  timing.chrome_us = sweep.chrome_us;
  timing.compose_us += sweep.exposed_compose_us;
  timing.passed = sweep.passed;
#endif

  // Every strip belongs to this ordered presentation. Drain once after the
  // final submission; failed staging or completion remains non-reusable.
  const bool frame_completed = display_.wait_for_all(2'000'000);
  const std::int64_t frame_drained = esp_timer_get_time();
  timing.passed = timing.passed && frame_completed;
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  if (optical_row_pattern_enabled_ && timing.pushes > 0U) {
    optical_generation_ ^= 1U;
  }
#endif
  // Overlays (zoom rail, minimap viewport, battery) rode the same sweep strips.
  if (timing.passed && vector_v2::chrome_minimap_region(chrome).has_value()) {
    presented_minimap_revision_ = canvas_.current_revision();
    minimap_presented_ = true;
  }
  timing.complete_us = frame_drained - tear_completed;
  if (event_us != 0U) {
    const std::int64_t dma_complete = display_.complete_time_us(first_sequence);
    if (dma_complete >= 0) {
      timing.first_complete_us =
          static_cast<std::uint32_t>(static_cast<std::uint32_t>(dma_complete) - event_us);
    }
  }
  timing.scroll_us = scroll_completed - started;
  timing.frame_reused = timing.passed && timing.tear_edge_observed;
  static_cast<void>(canvas_.remember_view(navigation_.view()));
  frame_level_x_ = level_x();
  frame_level_y_ = level_y();
  frame_chrome_ = chrome;
  frame_reusable_ = timing.passed && timing.tear_edge_observed;
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

void VectorV2Presenter::copy_ring_to_stage(vector_v2::PixelRect panel_bounds,
                                           const PanelStageSurface& surface) {
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  for (int y = panel_bounds.y0; y < panel_bounds.y1; ++y) {
    const int ring_y = vector_v2::ring_row(frame_ring_, ring_area, y);
    const int ring_x = vector_v2::ring_column(frame_ring_, ring_area, panel_bounds.x0);
    const auto source_row =
        frame_.subspan(static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth);
    auto destination = surface.pixels.subspan(
        static_cast<std::size_t>(y - surface.panel_y) * static_cast<std::size_t>(surface.stride) +
            static_cast<std::size_t>(panel_bounds.x0 - surface.panel_x),
        static_cast<std::size_t>(panel_bounds.x1 - panel_bounds.x0));
    const int width = panel_bounds.x1 - panel_bounds.x0;
    const int first = std::min(width, vector_v2::kOverviewWidth - ring_x);
    std::copy(source_row.begin() + ring_x, source_row.begin() + ring_x + first,
              destination.begin());
    if (first < width) {
      std::copy(source_row.begin(), source_row.begin() + (width - first),
                destination.begin() + first);
    }
  }
}

#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
void VectorV2Presenter::enable_optical_row_pattern() {
  optical_row_pattern_enabled_ = true;
  optical_generation_ = 0;
}

void VectorV2Presenter::paint_optical_row_pattern(const PanelStageSurface& surface) {
  const int first_x = std::max(surface.panel_x, kOpticalPatternX);
  const int last_x =
      std::min(surface.panel_x + surface.width, kOpticalPatternX + kOpticalPatternWidth);
  if (first_x >= last_x) {
    return;
  }
  for (int row = 0; row < surface.height; ++row) {
    const int panel_y = surface.panel_y + row;
    for (int panel_x = first_x; panel_x < last_x; ++panel_x) {
      surface.pixels[static_cast<std::size_t>(row) * surface.stride +
                     static_cast<std::size_t>(panel_x - surface.panel_x)] =
          optical_pattern_pixel(optical_generation_, panel_y, panel_x - kOpticalPatternX);
    }
  }
}
#endif

#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
bool VectorV2Presenter::verify_staging_preserves_canvas(const vector_v2::ChromeState& chrome) {
  if (frame_ring_bottom_ != 0) {
    return false;
  }
  const auto checksum = [this]() {
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    for (const std::uint16_t pixel : frame_) {
      hash ^= pixel;
      hash *= 1'099'511'628'211ULL;
    }
    return hash;
  };
  const std::uint64_t before = checksum();
  const auto timing = present_with_overlays(
      {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome, 0, 0, false);
  return timing.passed && checksum() == before;
}
#endif

bool VectorV2Presenter::paint_stage_thunk(void* raw, const PanelStageSurface& surface) {
  auto& context = *static_cast<StageContext*>(raw);
  return context.presenter != nullptr && context.presenter->paint_stage_surface(context, surface);
}

bool VectorV2Presenter::paint_stage_surface(StageContext& context,
                                            const PanelStageSurface& surface) {
  const std::int64_t exposed_started = esp_timer_get_time();
  const vector_v2::PixelRect staged{surface.panel_x, surface.panel_y,
                                    surface.panel_x + surface.width,
                                    surface.panel_y + surface.height};
  for (const auto& exposed : context.exposed) {
    const vector_v2::PixelRect part{
        std::max(exposed.x0, staged.x0), std::max(exposed.y0, staged.y0),
        std::min(exposed.x1, staged.x1), std::min(exposed.y1, staged.y1)};
    if (part.x0 >= part.x1 || part.y0 >= part.y1) {
      continue;
    }
    if (!compose_into_ring(part)) {
      return false;
    }
    copy_ring_to_stage(part, surface);
  }
  context.exposed_us += esp_timer_get_time() - exposed_started;

  const std::int64_t chrome_started = esp_timer_get_time();
  if (!chrome_cache_.paint(
          {surface.pixels, surface.width, surface.height, surface.panel_x, surface.panel_y},
          *context.chrome, context.navigation, canvas_.current_revision().value)) {
    return false;
  }
  context.chrome_us += esp_timer_get_time() - chrome_started;
#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
  if (optical_row_pattern_enabled_) {
    paint_optical_row_pattern(surface);
  }
#endif
  return true;
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
  const auto sequence = scheduler_.schedule({.revision = canvas_.current_revision(),
                                             .panel_bounds = band,
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

  int rows_per_strip = std::max(2, 16'384 / band_width);
  rows_per_strip &= ~1;
  StageContext context{
      .presenter = this, .chrome = &chrome, .navigation = chrome_navigation(), .exposed = exposed};
  const std::uint32_t pushes_before = display_.push_count();
  const std::int64_t first_submitted = esp_timer_get_time();
  const bool streamed = display_.stream_rect_ring(
      band.x0, band.y0, band_width, band_height, scheduled->strip.pixels.data(),
      scheduled->strip.stride, scheduled->strip.source_shift_x, scheduled->strip.source_shift_y,
      scheduled->strip.source_area_width, scheduled->strip.source_area_height, rows_per_strip,
      {.context = &context, .paint = &paint_stage_thunk});
  if (!streamed) {
    static_cast<void>(scheduler_.abort(*sequence));
    return timing;
  }
  if (!scheduler_.complete(*sequence)) {
    return timing;
  }
  timing.pushes = display_.push_count() - pushes_before;
  timing.first_submit_us =
      event_us == 0U
          ? 0
          : static_cast<std::uint32_t>(static_cast<std::uint32_t>(first_submitted) - event_us);
  timing.exposed_compose_us = context.exposed_us;
  timing.chrome_us = context.chrome_us;
  timing.passed = true;
  return timing;
}

LivePresentationTiming VectorV2Presenter::present(vector_v2::PixelRect bounds,
                                                  const vector_v2::ChromeState& chrome,
                                                  std::uint32_t event_us, std::int64_t compose_us,
                                                  bool wait_for_completion) {
  bounds = align_bounds(bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return {.compose_us = compose_us, .passed = true};
  }
  const auto pixels =
      frame_.subspan(static_cast<std::size_t>(bounds.y0 * vector_v2::kOverviewWidth + bounds.x0));
  return present_pixels(bounds, pixels, vector_v2::kOverviewWidth, chrome, event_us, compose_us,
                        wait_for_completion);
}

LivePresentationTiming VectorV2Presenter::present_pixels(
    vector_v2::PixelRect bounds, std::span<const std::uint16_t> pixels, int stride,
    const vector_v2::ChromeState& chrome, std::uint32_t event_us, std::int64_t compose_us,
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
    const auto wait = display_.wait_for_tear_edge(selected_tear_edge(), kTearWaitTimeoutUs);
    timing.tear_wait_us = esp_timer_get_time() - tear_wait_started;
    record_tear_wait(timing, wait);
    if (!wait.observed) {
      return timing;
    }
  }

  const auto sequence = scheduler_.schedule({.revision = canvas_.current_revision(),
                                             .panel_bounds = bounds,
                                             .pixels = pixels,
                                             .stride = stride});
  const auto scheduled = scheduler_.front();
  if (!sequence.has_value() || !scheduled.has_value() || scheduled->sequence != *sequence) {
    return timing;
  }
  int rows_per_strip = std::max(2, 16'384 / width);
  rows_per_strip &= ~1;
  StageContext context{.presenter = this, .chrome = &chrome, .navigation = chrome_navigation()};
  const std::uint32_t submits_before = display_.submit_count();
  const std::uint32_t pushes_before = display_.push_count();
  const std::int64_t first_submitted = esp_timer_get_time();
  const bool streamed = display_.stream_rect(
      bounds.x0, bounds.y0, width, height, scheduled->strip.pixels.data(), scheduled->strip.stride,
      rows_per_strip, {.context = &context, .paint = &paint_stage_thunk});
  if (!streamed) {
    static_cast<void>(scheduler_.abort(*sequence));
    return timing;
  }
  if (!scheduler_.complete(*sequence)) {
    return timing;
  }

  const bool completed = !wait_for_completion || display_.wait_for_all(2'000'000);
  const std::int64_t finished = esp_timer_get_time();
  const std::int64_t dma_complete = display_.complete_time_us(submits_before + 1U);
  const auto first_submitted_us = static_cast<std::uint32_t>(first_submitted);
  const auto dma_complete_us =
      static_cast<std::uint32_t>(dma_complete >= 0 ? dma_complete : finished);
  timing.first_submit_us =
      event_us == 0U ? 0 : static_cast<std::uint32_t>(first_submitted_us - event_us);
  timing.first_complete_us =
      event_us == 0U ? 0 : static_cast<std::uint32_t>(dma_complete_us - event_us);
  timing.complete_us = finished - first_submitted;
  timing.chrome_us = context.chrome_us;
  timing.pushes = display_.push_count() - pushes_before;
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
