#include <algorithm>
#include <cmath>
#include <cstddef>

#include "esp_timer.h"
#include "tinydraw/vector_v2/panel_staging.h"
#include "vector_v2_presenter.h"
#include "vector_v2_presenter_internal.h"

namespace tinydraw::esp32 {
namespace {

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

bool valid_panel_strip(vector_v2::PixelRect bounds, std::span<const std::uint16_t> pixels,
                       int stride) {
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const bool valid_bounds = width > 0 && height > 0 && bounds.x0 >= 0 && bounds.y0 >= 0 &&
                            bounds.x1 <= vector_v2::kOverviewWidth &&
                            bounds.y1 <= vector_v2::kOverviewHeight &&
                            ((bounds.x0 | bounds.y0 | width | height) & 1) == 0 && stride >= width;
  const std::size_t required = height > 0 && stride > 0 ? static_cast<std::size_t>(height - 1) *
                                                                  static_cast<std::size_t>(stride) +
                                                              static_cast<std::size_t>(width)
                                                        : 0U;
  return valid_bounds && pixels.size() >= required;
}

bool valid_ring_strip(vector_v2::PixelRect bounds, std::span<const std::uint16_t> pixels,
                      int stride, int shift_x, int shift_y, int area_width, int area_height) {
  const std::size_t required =
      area_height > 0 && stride > 0
          ? static_cast<std::size_t>(area_height - 1) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(area_width)
          : 0U;
  return valid_panel_strip(bounds, pixels, stride) && area_width >= bounds.x1 &&
         area_height >= bounds.y1 && shift_x >= 0 && shift_x < area_width && shift_y >= 0 &&
         shift_y < area_height && stride >= area_width && pixels.size() >= required;
}

}  // namespace

LivePresentationTiming VectorV2Presenter::present_ring_region(vector_v2::PixelRect bounds,
                                                              const vector_v2::ChromeState& chrome,
                                                              std::uint32_t event_us,
                                                              std::int64_t compose_us,
                                                              bool wait_for_completion) {
  bounds = presenter_internal::align_bounds(bounds);
  LivePresentationTiming timing{.compose_us = compose_us};
  if (bounds.x0 >= bounds.x1 || bounds.y0 >= bounds.y1) {
    timing.passed = true;
    return timing;
  }
  if (frame_ring_bottom_ <= 0) {
    return present(bounds, chrome, event_us, compose_us, wait_for_completion);
  }

  const vector_v2::PixelRect canvas_bounds{bounds.x0, bounds.y0, bounds.x1,
                                           std::min(bounds.y1, frame_ring_bottom_)};
  const vector_v2::PixelRect chrome_bounds{bounds.x0, std::max(bounds.y0, frame_ring_bottom_),
                                           bounds.x1, bounds.y1};
  const std::uint32_t first_sequence = display_.submit_count() + 1U;
  const std::int64_t present_started = esp_timer_get_time();
  bool submitted = false;
  const auto fail_after_drain = [&]() {
    static_cast<void>(display_.wait_for_all(2'000'000));
    timing.complete_us = esp_timer_get_time() - present_started;
    timing.passed = false;
    return timing;
  };

  if (canvas_bounds.y0 < canvas_bounds.y1) {
    const std::span<const vector_v2::PixelRect> no_exposed{};
    const auto part = present_ring(canvas_bounds, chrome, event_us, no_exposed);
    timing.chrome_prepare_us += part.chrome_prepare_us;
    timing.chrome_stage_us += part.chrome_stage_us;
    timing.chrome_us += part.chrome_us;
    timing.submitted_pixels += part.submitted_pixels;
    timing.pushes += part.pushes;
    timing.first_submit_us = part.first_submit_us;
    if (!part.passed) {
      return fail_after_drain();
    }
    submitted = part.pushes != 0U;
  }

  if (chrome_bounds.y0 < chrome_bounds.y1) {
    const auto part = present(chrome_bounds, chrome, event_us, 0, false);
    timing.chrome_prepare_us += part.chrome_prepare_us;
    timing.chrome_stage_us += part.chrome_stage_us;
    timing.chrome_us += part.chrome_us;
    if (!submitted) {
      timing.first_submit_us = part.first_submit_us;
    }
    timing.submitted_pixels += part.submitted_pixels;
    timing.pushes += part.pushes;
    if (!part.passed) {
      return fail_after_drain();
    }
    submitted = submitted || part.pushes != 0U;
  }

  const bool completed = !wait_for_completion || display_.wait_for_all(2'000'000);
  const std::int64_t finished = esp_timer_get_time();
  timing.complete_us = finished - present_started;
  if (event_us != 0U && submitted && wait_for_completion) {
    const std::int64_t dma_complete = display_.complete_time_us(first_sequence);
    if (dma_complete >= 0) {
      timing.first_complete_us =
          static_cast<std::uint32_t>(static_cast<std::uint32_t>(dma_complete) - event_us);
    }
  }
  timing.passed = completed;
  if (timing.passed) {
    // Chrome is staged over a canvas-only ring, so a successful local chrome
    // update advances the reuse identity without materializing the frame.
    frame_chrome_ = chrome;
  }
  return timing;
}

LivePresentationTiming VectorV2Presenter::refresh_pan(int old_x, int old_y,
                                                      const vector_v2::ChromeState& chrome,
                                                      std::uint32_t event_us) {
  const int delta_x = level_x() - old_x;
  const int delta_y = level_y() - old_y;
  // Composition-epoch drift is deliberately not part of this identity:
  // same-revision tile content is deterministic, so canvas changes between
  // frames (production, eviction) only affect quality, never correctness.
  // Revision changes invalidate through the frame writers.
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
  const std::int64_t exposed_started = esp_timer_get_time();
  for (const auto& exposed_rect : exposed) {
    if (!compose_into_ring(exposed_rect)) {
      return timing;
    }
  }
  const std::int64_t exposed_completed = esp_timer_get_time();
  const auto navigation = chrome_navigation();
  if (!chrome_cache_.prepare(chrome, navigation, canvas_.current_revision().value)) {
    return timing;
  }
  const std::int64_t chrome_prepared = esp_timer_get_time();
  const std::int64_t prestaged_exposed_us = exposed_completed - exposed_started;
  const std::int64_t prestaged_chrome_us = chrome_prepared - exposed_completed;
  const std::span<const vector_v2::PixelRect> no_exposed{};
  const std::int64_t tear_started = chrome_prepared;
  std::int64_t tear_completed = tear_started;
  const std::uint32_t first_sequence = display_.submit_count() + 1U;

  // Require a newly observed rising TE edge, then submit one monotonically
  // increasing row-zero sweep.
  const auto wait = display_.wait_for_tear_edge(selected_tear_edge(), kTearWaitTimeoutUs);
  tear_completed = esp_timer_get_time();
  timing.tear_wait_us = tear_completed - tear_started;
  record_tear_wait(timing, wait);
  if (!wait.observed) {
    return timing;
  }
  const auto sweep =
      present_ring({0, 0, vector_v2::kOverviewWidth, canvas_bottom}, chrome, event_us, no_exposed);
  timing.pushes = sweep.pushes;
  timing.submitted_pixels = sweep.submitted_pixels;
  timing.first_submit_us = sweep.first_submit_us;
  timing.exposed_compose_us = sweep.exposed_compose_us;
  timing.chrome_us = sweep.chrome_us;
  timing.compose_us += sweep.exposed_compose_us;
  timing.passed = sweep.passed;

  timing.exposed_compose_us += prestaged_exposed_us;
  timing.chrome_us += prestaged_chrome_us;
  timing.chrome_prepare_us += prestaged_chrome_us;
  timing.compose_us += prestaged_exposed_us;

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
    overlay_pending(strip_level, pixels, width);
    copy_pixels_to_ring({panel_bounds.x0, y, panel_bounds.x1, y + rows}, pixels, width);
  }
  return true;
}

void VectorV2Presenter::copy_pixels_to_ring(vector_v2::PixelRect panel_bounds,
                                            std::span<const std::uint16_t> pixels, int stride) {
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  int ring_y = vector_v2::ring_row(frame_ring_, ring_area, panel_bounds.y0);
  if (vector_v2::copy_pixel_rows_to_ring(pixels.data(), stride, width, height, frame_.data(),
                                         vector_v2::kOverviewWidth, frame_ring_bottom_, ring_y,
                                         frame_ring_.shift_x, panel_bounds.x0)) {
    return;
  }
  for (int row = 0; row < height; ++row) {
    auto destination_row = frame_.subspan(
        static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth, vector_v2::kOverviewWidth);
    vector_v2::copy_to_ring_row(pixels.data() + static_cast<std::ptrdiff_t>(row) * stride, width,
                                destination_row.data(), vector_v2::kOverviewWidth,
                                frame_ring_.shift_x, panel_bounds.x0);
    if (++ring_y == ring_area.y1) {
      ring_y = ring_area.y0;
    }
  }
}

bool VectorV2Presenter::render_into_ring(std::span<const RibbonPrimitive> primitives,
                                         std::uint16_t color, vector_v2::PixelRect panel_bounds) {
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  if (primitives.empty() || width <= 0 || height <= 0) {
    return true;
  }
  if (frame_ring_bottom_ <= 0 || panel_bounds.x0 < 0 || panel_bounds.y0 < 0 ||
      panel_bounds.x1 > vector_v2::kOverviewWidth || panel_bounds.y1 > frame_ring_bottom_) {
    return false;
  }
  const int rows_per_strip = static_cast<int>(region_.size() / static_cast<std::size_t>(width));
  if (rows_per_strip <= 0) {
    return false;
  }
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  for (int y = panel_bounds.y0; y < panel_bounds.y1; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, panel_bounds.y1 - y);
    auto pixels = region_.first(static_cast<std::size_t>(width) * rows);
    int ring_y = vector_v2::ring_row(frame_ring_, ring_area, y);
    for (int row = 0; row < rows; ++row) {
      const auto source_row = frame_.subspan(
          static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth, vector_v2::kOverviewWidth);
      vector_v2::copy_ring_row(source_row.data(), vector_v2::kOverviewWidth, frame_ring_.shift_x,
                               panel_bounds.x0, width,
                               pixels.data() + static_cast<std::ptrdiff_t>(row) * width);
      if (++ring_y == ring_area.y1) {
        ring_y = ring_area.y0;
      }
    }
    static_cast<void>(renderer_->render_surface(primitives, pixels, width, rows, width,
                                                panel_bounds.x0, y, color));
    copy_pixels_to_ring({panel_bounds.x0, y, panel_bounds.x1, y + rows}, pixels, width);
  }
  return true;
}

void VectorV2Presenter::copy_ring_to_stage(vector_v2::PixelRect panel_bounds,
                                           const PanelStageSurface& surface) {
  const vector_v2::PixelRect ring_area{0, 0, vector_v2::kOverviewWidth, frame_ring_bottom_};
  int ring_y = vector_v2::ring_row(frame_ring_, ring_area, panel_bounds.y0);
  const int width = panel_bounds.x1 - panel_bounds.x0;
  const int height = panel_bounds.y1 - panel_bounds.y0;
  auto destination = surface.pixels.subspan(
      static_cast<std::size_t>(panel_bounds.y0 - surface.panel_y) *
              static_cast<std::size_t>(surface.stride) +
          static_cast<std::size_t>(panel_bounds.x0 - surface.panel_x),
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(surface.stride) +
          static_cast<std::size_t>(width));
  if (surface.byte_swapped && panel_bounds.x0 == 0 &&
      panel_bounds.x1 == vector_v2::kOverviewWidth && surface.panel_x == 0 &&
      surface.width == vector_v2::kOverviewWidth &&
      vector_v2::stage_full_ring_rows_swapped(frame_.data(), vector_v2::kOverviewWidth, ring_y,
                                              height, frame_ring_bottom_, frame_ring_.shift_x,
                                              destination.data(), surface.stride)) {
    return;
  }
  for (int y = panel_bounds.y0; y < panel_bounds.y1; ++y) {
    const auto source_row =
        frame_.subspan(static_cast<std::size_t>(ring_y) * vector_v2::kOverviewWidth);
    auto destination_row = surface.pixels.subspan(
        static_cast<std::size_t>(y - surface.panel_y) * static_cast<std::size_t>(surface.stride) +
            static_cast<std::size_t>(panel_bounds.x0 - surface.panel_x),
        static_cast<std::size_t>(width));
    if (surface.byte_swapped) {
      vector_v2::stage_ring_row(source_row.data(), vector_v2::kOverviewWidth, frame_ring_.shift_x,
                                panel_bounds.x0, width, destination_row.data());
    } else {
      vector_v2::copy_ring_row(source_row.data(), vector_v2::kOverviewWidth, frame_ring_.shift_x,
                               panel_bounds.x0, width, destination_row.data());
    }
    if (++ring_y == ring_area.y1) {
      ring_y = ring_area.y0;
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
      const std::uint16_t pixel =
          optical_pattern_pixel(optical_generation_, panel_y, panel_x - kOpticalPatternX);
      surface.pixels[static_cast<std::size_t>(row) * surface.stride +
                     static_cast<std::size_t>(panel_x - surface.panel_x)] =
          surface.byte_swapped ? static_cast<std::uint16_t>((pixel >> 8U) | (pixel << 8U)) : pixel;
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

  if (live_provisional_count_ != 0U) {
    if (surface.byte_swapped) {
      return false;
    }
    static_cast<void>(renderer_->render_surface(
        std::span(live_provisional_.data(), live_provisional_count_), surface.pixels, surface.width,
        surface.height, surface.stride, surface.panel_x, surface.panel_y, live_provisional_color_));
  }

  const std::int64_t chrome_started = esp_timer_get_time();
  if (!chrome_cache_.paint_prepared({surface.pixels, surface.width, surface.height, surface.panel_x,
                                     surface.panel_y, surface.byte_swapped},
                                    *context.chrome, context.navigation,
                                    canvas_.current_revision().value)) {
    return false;
  }
  if (demo_pointer_.has_value() &&
      !vector_v2::paint_demo_pointer({surface.pixels, surface.width, surface.height,
                                      surface.panel_x, surface.panel_y, surface.byte_swapped},
                                     {demo_pointer_->x, demo_pointer_->y}, demo_pointer_opacity_)) {
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
  band = presenter_internal::align_bounds(band);
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
  const auto navigation = chrome_navigation();
  const std::int64_t chrome_prepare_started = esp_timer_get_time();
  if (!chrome_cache_.prepare_for({band.x0, band.y0, band.x1, band.y1}, chrome, navigation,
                                 canvas_.current_revision().value)) {
    return timing;
  }
  timing.chrome_prepare_us = esp_timer_get_time() - chrome_prepare_started;
  const std::size_t area_pixels =
      static_cast<std::size_t>(frame_ring_bottom_) * vector_v2::kOverviewWidth;
  const auto ring_pixels = frame_.first(area_pixels);
  if (!valid_ring_strip(band, ring_pixels, vector_v2::kOverviewWidth, frame_ring_.shift_x,
                        frame_ring_.shift_y, vector_v2::kOverviewWidth, frame_ring_bottom_)) {
    return timing;
  }

  int rows_per_strip = std::max(2, 16'384 / band_width);
  rows_per_strip &= ~1;
  StageContext context{
      .presenter = this, .chrome = &chrome, .navigation = navigation, .exposed = exposed};
  const std::uint32_t pushes_before = display_.push_count();
  const std::int64_t first_submitted = esp_timer_get_time();
  const bool streamed = display_.stream_rect_ring(
      band.x0, band.y0, band_width, band_height, ring_pixels.data(), vector_v2::kOverviewWidth,
      frame_ring_.shift_x, frame_ring_.shift_y, vector_v2::kOverviewWidth, frame_ring_bottom_,
      rows_per_strip,
      {.context = &context,
       .paint = &paint_stage_thunk,
       .accepts_byte_swapped = live_provisional_count_ == 0U && !demo_pointer_.has_value() &&
                               vector_v2::chrome_accepts_byte_swapped_staging(chrome)});
  if (!streamed) {
    return timing;
  }
  timing.pushes = display_.push_count() - pushes_before;
  timing.submitted_pixels = static_cast<std::size_t>(band_width) * band_height;
  timing.first_submit_us =
      event_us == 0U
          ? 0
          : static_cast<std::uint32_t>(static_cast<std::uint32_t>(first_submitted) - event_us);
  timing.exposed_compose_us = context.exposed_us;
  timing.chrome_stage_us = context.chrome_us;
  timing.chrome_us = timing.chrome_prepare_us + timing.chrome_stage_us;
  timing.passed = true;
  return timing;
}

LivePresentationTiming VectorV2Presenter::present(vector_v2::PixelRect bounds,
                                                  const vector_v2::ChromeState& chrome,
                                                  std::uint32_t event_us, std::int64_t compose_us,
                                                  bool wait_for_completion) {
  bounds = presenter_internal::align_bounds(bounds);
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

  const auto navigation = chrome_navigation();
  const std::int64_t chrome_prepare_started = esp_timer_get_time();
  if (!chrome_cache_.prepare_for({bounds.x0, bounds.y0, bounds.x1, bounds.y1}, chrome, navigation,
                                 canvas_.current_revision().value)) {
    return timing;
  }
  timing.chrome_prepare_us = esp_timer_get_time() - chrome_prepare_started;

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

  if (!valid_panel_strip(bounds, pixels, stride)) {
    return timing;
  }
  int rows_per_strip = std::max(2, 16'384 / width);
  rows_per_strip &= ~1;
  StageContext context{.presenter = this, .chrome = &chrome, .navigation = navigation};
  const std::uint32_t submits_before = display_.submit_count();
  const std::uint32_t pushes_before = display_.push_count();
  const std::int64_t first_submitted = esp_timer_get_time();
  const bool streamed =
      display_.stream_rect(bounds.x0, bounds.y0, width, height, pixels.data(), stride,
                           rows_per_strip, {.context = &context, .paint = &paint_stage_thunk});
  if (!streamed) {
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
  timing.chrome_stage_us = context.chrome_us;
  timing.chrome_us = timing.chrome_prepare_us + timing.chrome_stage_us;
  timing.pushes = display_.push_count() - pushes_before;
  timing.submitted_pixels = static_cast<std::size_t>(width) * height;
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
  auto bounds = presenter_internal::align_bounds(
      {static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)),
       static_cast<int>(std::ceil(x1)), static_cast<int>(std::ceil(y1))});
  bounds.y1 = std::min(bounds.y1, canvas_bottom);
  return bounds;
}

void VectorV2Presenter::clear_live_overlay() {
  live_provisional_count_ = 0U;
  live_provisional_bounds_ = {};
  live_provisional_color_ = 0;
}

}  // namespace tinydraw::esp32
