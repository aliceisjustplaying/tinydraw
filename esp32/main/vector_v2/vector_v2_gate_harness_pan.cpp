#include "vector_v2_gate_harness_internal.h"

namespace tinydraw::esp32::gate_harness {

bool verify_pan_adapter(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                        const vector_v2::ChromeState& chrome, ZoomLevel zoom) {
  constexpr int kPanDelta = 88;
  const vector_v2::ViewRequest destination{
      .zoom = zoom,
      .level_pixels = {kPanDelta, kPanDelta, kPanDelta + vector_v2::kOverviewWidth,
                       kPanDelta + vector_v2::kOverviewHeight},
  };
  while (true) {
    const auto remaining = producer.visible_tiles_remaining(destination);
    if (!remaining.has_value()) {
      return false;
    }
    if (*remaining == 0U) {
      break;
    }
    if (!producer.produce_next(destination).has_value()) {
      return false;
    }
  }
  const auto setup = presenter.set_view(zoom, 0, 0, chrome, now_us());
  const int before_x = presenter.level_x();
  const int before_y = presenter.level_y();
  const auto pan = presenter.pan_from(before_x, before_y, {240.0F, 240.0F},
                                      {240.0F - kPanDelta, 240.0F - kPanDelta}, chrome, now_us());
  const bool moved = presenter.level_x() > before_x && presenter.level_y() > before_y;
  std::printf(
      "TINYDRAW_GATE1_PAN zoom=%s from_x=%d from_y=%d to_x=%d to_y=%d compose_us=%lld "
      "scroll_us=%lld exposed_compose_us=%lld chrome_us=%lld event_submit_us=%lld "
      "event_complete_us=%lld transfer_us=%lld setup=%u present=%u moved=%u "
      "frame_reused=%u pass=%u\n",
      zoom_name(zoom), before_x, before_y, presenter.level_x(), presenter.level_y(),
      static_cast<long long>(pan.compose_us), static_cast<long long>(pan.scroll_us),
      static_cast<long long>(pan.exposed_compose_us), static_cast<long long>(pan.chrome_us),
      static_cast<long long>(pan.first_submit_us), static_cast<long long>(pan.first_complete_us),
      static_cast<long long>(pan.complete_us), setup.passed, pan.passed, moved, pan.frame_reused,
      setup.passed && pan.passed && moved && pan.frame_reused && pan.compose_us < 30'000 &&
          pan.first_complete_us < 60'000);
  return setup.passed && pan.passed && moved && pan.frame_reused && pan.compose_us < 30'000 &&
         pan.first_complete_us < 60'000;
}

bool run_ring_locality_gate(VectorV2Presenter& presenter, OperationLog& log,
                            MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome,
                            const InPlaceAppendWorkspace& workspace) {
  constexpr int kOrigin = 256;
  constexpr Point kPanTouch{240.0F, 240.0F};
  if (!presenter.set_view(ZoomLevel::k400Percent, kOrigin, kOrigin, chrome, now_us()).passed) {
    return false;
  }
  const auto first_pan =
      presenter.pan_from(kOrigin, kOrigin, kPanTouch, {216.0F, 216.0F}, chrome, now_us());
  const int ring_x = presenter.level_x();
  const int ring_y = presenter.level_y();
  const auto local = presenter.refresh_region(
      {ring_x + 40, ring_y + 40, ring_x + 104, ring_y + 104}, chrome, now_us());

  vector_v2::ChromeState changed_chrome = chrome;
  changed_chrome.battery_percentage =
      static_cast<std::uint8_t>(changed_chrome.battery_percentage == 50U ? 51U : 50U);
  const vector_v2::ChromeRect battery = vector_v2::chrome_battery_region();
  const auto local_chrome = presenter.present_frame_region(
      {battery.x0, battery.y0, battery.x1, battery.y1}, changed_chrome, now_us());

  CurvedRibbonStream ribbon;
  constexpr std::array<Point, 4> kPoints{
      {{96.0F, 120.0F}, {120.0F, 132.0F}, {144.0F, 124.0F}, {168.0F, 142.0F}}};
  std::array<CompactOperationSample, kPoints.size()> operation_samples{};
  float running_length = 0.0F;
  Point previous = kPoints.front();
  std::uint32_t timestamp_us = now_us();
  bool ink_pass = true;
  std::uint32_t ink_max_pushes = 0;
  std::size_t ink_max_submitted_pixels = 0;
  InkPoint last{};
  for (std::size_t index = 0; index < kPoints.size(); ++index) {
    const float distance =
        index == 0U ? 0.0F
                    : std::hypot(kPoints[index].x - previous.x, kPoints[index].y - previous.y);
    running_length += distance;
    timestamp_us += 8'333U;
    last = {.position = kPoints[index],
            .pressure = 1.0F,
            .radius = 8.0F,
            .distance = distance,
            .running_length = running_length,
            .timestamp_us = timestamp_us};
    const vector_v2::OperationPoint operation_point = presenter.operation_point(last);
    operation_samples[index] = {
        .x_quarter = static_cast<std::uint16_t>(std::lround(operation_point.world_x * 16.0F)),
        .y_quarter = static_cast<std::uint16_t>(std::lround(operation_point.world_y * 16.0F)),
        .radius_256 = static_cast<std::uint16_t>(std::lround(operation_point.radius * 256.0F)),
        .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
    };
    LivePresentationTiming ink{};
    if (index == 0U) {
      static_cast<void>(ribbon.append(last, true));
      ink = presenter.show_start(last, 0x001FU, changed_chrome, now_us());
    } else {
      ink = presenter.show_update(ribbon.append(last, true), 0x001FU, changed_chrome, now_us());
    }
    ink_pass = ink_pass && ink.passed && ink.pushes != 0U;
    ink_max_pushes = std::max(ink_max_pushes, ink.pushes);
    ink_max_submitted_pixels = std::max(ink_max_submitted_pixels, ink.submitted_pixels);
    previous = kPoints[index];
  }
  const auto ink_finish =
      presenter.show_update(ribbon.finish(last), 0x001FU, changed_chrome, now_us());
  ink_pass = ink_pass && ink_finish.passed && ink_finish.pushes != 0U;
  ink_max_pushes = std::max(ink_max_pushes, ink_finish.pushes);
  ink_max_submitted_pixels = std::max(ink_max_submitted_pixels, ink_finish.submitted_pixels);

  const auto committed = append_and_absorb(
      log, canvas,
      vector_v2::OperationAppend{.tool = OperationTool::kPen,
                                 .color = 0x001FU,
                                 .gesture_id = 7'001U,
                                 .samples = operation_samples},
      workspace,
      vector_v2::ViewRequest{
          .zoom = presenter.zoom(),
          .level_pixels = {presenter.level_x(), presenter.level_y(),
                           presenter.level_x() + vector_v2::kOverviewWidth,
                           presenter.level_y() + vector_v2::chrome_canvas_bottom(changed_chrome)},
      });
  const vector_v2::PixelRect committed_level{ring_x + 86, ring_y + 110, ring_x + 178, ring_y + 152};
  const auto committed_ink =
      committed.has_value() ? presenter.refresh_region(committed_level, changed_chrome, now_us())
                            : LivePresentationTiming{};

  const auto second_pan =
      presenter.pan_from(ring_x, ring_y, kPanTouch, {224.0F, 224.0F}, changed_chrome, now_us());
  const std::uint32_t local_max_pushes =
      std::max({local.pushes, local_chrome.pushes, ink_max_pushes, committed_ink.pushes});
  const std::size_t local_max_submitted_pixels =
      std::max({local.submitted_pixels, local_chrome.submitted_pixels, ink_max_submitted_pixels,
                committed_ink.submitted_pixels});
  const std::size_t full_canvas_pixels = static_cast<std::size_t>(vector_v2::kOverviewWidth) *
                                         vector_v2::chrome_canvas_bottom(changed_chrome);
  const bool local_submissions =
      local.submitted_pixels != 0U && local_chrome.submitted_pixels != 0U &&
      committed_ink.submitted_pixels != 0U && ink_max_submitted_pixels != 0U &&
      local_max_submitted_pixels < full_canvas_pixels;
  const bool full_sweeps = first_pan.submitted_pixels == full_canvas_pixels &&
                           second_pan.submitted_pixels == full_canvas_pixels;
  const bool passed = first_pan.passed && first_pan.frame_reused && local.passed &&
                      local_chrome.passed && ink_pass && committed.has_value() &&
                      committed_ink.passed && local_submissions && second_pan.passed &&
                      second_pan.frame_reused && full_sweeps;
  std::printf(
      "TINYDRAW_GATE1_RING_LOCAL first_pan=%u local_refresh=%u local_chrome=%u live_ink=%u "
      "committed_ink=%u full_pushes=%lu local_max_pushes=%lu local_max_submitted_pixels=%lu "
      "full_canvas_pixels=%lu first_pan_submitted_pixels=%lu next_pan_submitted_pixels=%lu "
      "next_pan=%u next_reused=%u full_sweeps=%u pass=%u\n",
      first_pan.passed && first_pan.frame_reused, local.passed, local_chrome.passed, ink_pass,
      committed.has_value() && committed_ink.passed, static_cast<unsigned long>(first_pan.pushes),
      static_cast<unsigned long>(local_max_pushes),
      static_cast<unsigned long>(local_max_submitted_pixels),
      static_cast<unsigned long>(full_canvas_pixels),
      static_cast<unsigned long>(first_pan.submitted_pixels),
      static_cast<unsigned long>(second_pan.submitted_pixels), second_pan.passed,
      second_pan.frame_reused, full_sweeps, passed);
  std::fflush(stdout);
  return passed;
}

// Scripted warm-pan drag attribution. Every microsecond of a cached pan frame
// is accounted: PSRAM scroll, exposed-strip compose, TE wait, byte-swap
// staging (prepare), staging-slot waits, and DMA completion. The pass bound is
// transport discipline (reuse + edge observation + presentation success); timing bounds live in
// the single-frame pan gate until the optimized distribution is measured.
struct PanSequenceFrame {
  std::int64_t scroll_us = 0;
  std::int64_t exposed_us = 0;
  std::int64_t chrome_us = 0;
  std::int64_t tear_wait_us = 0;
  std::int64_t present_us = 0;
  std::int64_t prepare_us = 0;
  std::int64_t acquire_wait_us = 0;
  std::int64_t ring_copy_us = 0;
  std::int64_t patch_us = 0;
  std::int64_t byte_swap_us = 0;
  std::int64_t staging_us = 0;
  std::int64_t first_submit_us = 0;
  std::int64_t first_complete_us = 0;
  std::int64_t frame_us = 0;
  int delta_x = 0;
  int delta_y = 0;
  bool reused = false;
  bool tear_edge_observed = false;
  bool passed = false;
};

constexpr std::size_t kPanSequenceTypicalFrames = 16U;
constexpr std::size_t kPanSequenceFastFrames = 8U;
constexpr std::size_t kPanSequenceFrames = kPanSequenceTypicalFrames + kPanSequenceFastFrames;

std::int64_t pan_sequence_percentile(std::span<const std::int64_t> sorted, int percent) {
  if (sorted.empty()) {
    return 0;
  }
  const std::size_t rank = (sorted.size() * static_cast<std::size_t>(percent) + 99U) / 100U;
  return sorted[std::min(sorted.size() - 1U, rank == 0U ? 0U : rank - 1U)];
}

bool run_pan_sequence_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                           const vector_v2::ChromeState& chrome, ZoomLevel zoom) {
  // 16 typical forward steps, then 4 fast forward and 4 fast backward steps.
  // The out-and-back fast leg keeps the swept footprint at roughly 17x15
  // tile identities so the 320-slot pool never evicts the sequence's own
  // tiles (a one-way fast leg at 400% needs ~21x19 mostly-raw identities and
  // loses reuse on the earliest origins), and it exercises exposed-strip
  // composition on both leading edges.
  const auto step_delta = [](std::size_t step) {
    if (step < kPanSequenceTypicalFrames) {
      return vector_v2::NavigationPoint{24, 18};
    }
    return step < kPanSequenceTypicalFrames + kPanSequenceFastFrames / 2U
               ? vector_v2::NavigationPoint{72, 54}
               : vector_v2::NavigationPoint{-72, -54};
  };
  const auto prewarm = [&](int x, int y) {
    const vector_v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {x, y, x + vector_v2::kOverviewWidth, y + vector_v2::kOverviewHeight},
    };
    while (true) {
      const auto remaining = producer.visible_tiles_remaining(view);
      if (!remaining.has_value()) {
        return false;
      }
      if (*remaining == 0U) {
        return true;
      }
      if (!producer.produce_next(view).has_value()) {
        return false;
      }
    }
  };
  int warm_x = 0;
  int warm_y = 0;
  if (!prewarm(warm_x, warm_y)) {
    return false;
  }
  for (std::size_t step = 0; step < kPanSequenceFrames; ++step) {
    warm_x += step_delta(step).x;
    warm_y += step_delta(step).y;
    if (!prewarm(warm_x, warm_y)) {
      return false;
    }
  }
  auto frames = allocate_external<PanSequenceFrame>(kPanSequenceFrames);
  if (frames == nullptr) {
    return false;
  }
  const auto setup = presenter.set_view(zoom, 0, 0, chrome, now_us());
  if (!setup.passed) {
    return false;
  }
  const vector_v2::ChromeStagingCacheStats chrome_before = presenter.chrome_cache_stats();
  presenter.display().reset_timing();
  bool all_passed = true;
  bool all_reused = true;
  std::size_t tear_edge_failures = 0;
  for (std::size_t step = 0; step < kPanSequenceFrames; ++step) {
    const vector_v2::NavigationPoint delta = step_delta(step);
    const int from_x = presenter.level_x();
    const int from_y = presenter.level_y();
    const std::int64_t prepare_before = presenter.display().prepare_us();
    const std::int64_t acquire_wait_before = presenter.display().acquire_wait_us();
    const std::int64_t ring_copy_before = presenter.display().ring_copy_us();
    const std::int64_t patch_before = presenter.display().patch_us();
    const std::int64_t byte_swap_before = presenter.display().byte_swap_us();
    const std::int64_t staging_before = presenter.display().transfer_us();
    const std::int64_t frame_started = esp_timer_get_time();
    const auto timing = presenter.pan_from(
        from_x, from_y, {300.0F, 300.0F},
        {300.0F - static_cast<float>(delta.x), 300.0F - static_cast<float>(delta.y)}, chrome,
        now_us());
    const std::int64_t frame_us = esp_timer_get_time() - frame_started;
    frames.get()[step] = {
        .scroll_us = timing.scroll_us,
        .exposed_us = timing.exposed_compose_us,
        .chrome_us = timing.chrome_us,
        .tear_wait_us = timing.tear_wait_us,
        .present_us = timing.complete_us,
        .prepare_us = presenter.display().prepare_us() - prepare_before,
        .acquire_wait_us = presenter.display().acquire_wait_us() - acquire_wait_before,
        .ring_copy_us = presenter.display().ring_copy_us() - ring_copy_before,
        .patch_us = presenter.display().patch_us() - patch_before,
        .byte_swap_us = presenter.display().byte_swap_us() - byte_swap_before,
        .staging_us = presenter.display().transfer_us() - staging_before,
        .first_submit_us = timing.first_submit_us,
        .first_complete_us = timing.first_complete_us,
        .frame_us = frame_us,
        .delta_x = delta.x,
        .delta_y = delta.y,
        .reused = timing.frame_reused,
        .tear_edge_observed = timing.tear_edge_observed,
        .passed = timing.passed,
    };
    all_passed = all_passed && timing.passed;
    all_reused = all_reused && timing.frame_reused;
    tear_edge_failures += !timing.tear_edge_observed;
  }
  std::array<std::int64_t, kPanSequenceFrames> sorted_frame{};
  std::array<std::int64_t, kPanSequenceFrames> sorted_complete{};
  PanSequenceFrame totals{};
  for (std::size_t step = 0; step < kPanSequenceFrames; ++step) {
    const PanSequenceFrame& frame = frames.get()[step];
    std::printf(
        "TINYDRAW_PANSEQ_FRAME zoom=%s step=%u dx=%d dy=%d scroll_us=%lld "
        "exposed_compose_us=%lld chrome_us=%lld tear_wait_us=%lld present_us=%lld "
        "prepare_us=%lld "
        "acquire_wait_us=%lld ring_copy_us=%lld patch_us=%lld byte_swap_us=%lld "
        "staging_us=%lld event_submit_us=%lld event_complete_us=%lld frame_us=%lld "
        "tear_edge_observed=%u frame_reused=%u pass=%u\n",
        zoom_name(zoom), static_cast<unsigned>(step), frame.delta_x, frame.delta_y,
        static_cast<long long>(frame.scroll_us), static_cast<long long>(frame.exposed_us),
        static_cast<long long>(frame.chrome_us), static_cast<long long>(frame.tear_wait_us),
        static_cast<long long>(frame.present_us), static_cast<long long>(frame.prepare_us),
        static_cast<long long>(frame.acquire_wait_us), static_cast<long long>(frame.ring_copy_us),
        static_cast<long long>(frame.patch_us), static_cast<long long>(frame.byte_swap_us),
        static_cast<long long>(frame.staging_us), static_cast<long long>(frame.first_submit_us),
        static_cast<long long>(frame.first_complete_us), static_cast<long long>(frame.frame_us),
        frame.tear_edge_observed, frame.reused, frame.passed);
    sorted_frame[step] = frame.frame_us;
    sorted_complete[step] = frame.first_complete_us;
    totals.scroll_us += frame.scroll_us;
    totals.exposed_us += frame.exposed_us;
    totals.chrome_us += frame.chrome_us;
    totals.tear_wait_us += frame.tear_wait_us;
    totals.present_us += frame.present_us;
    totals.prepare_us += frame.prepare_us;
    totals.acquire_wait_us += frame.acquire_wait_us;
    totals.ring_copy_us += frame.ring_copy_us;
    totals.patch_us += frame.patch_us;
    totals.byte_swap_us += frame.byte_swap_us;
    totals.staging_us += frame.staging_us;
    totals.frame_us += frame.frame_us;
  }
  std::sort(sorted_frame.begin(), sorted_frame.end());
  std::sort(sorted_complete.begin(), sorted_complete.end());
  constexpr auto kFrames = static_cast<std::int64_t>(kPanSequenceFrames);
  const PanelStagingTiming& staging = presenter.display().staging_timing();
  for (std::size_t index = 0; index < staging.strip_count; ++index) {
    const PanelStripStagingTiming& strip = staging.strips[index];
    const std::int64_t mean_us =
        strip.samples == 0U ? 0 : strip.total_us / static_cast<std::int64_t>(strip.samples);
    const std::int64_t over_budget_us =
        std::max<std::int64_t>(0, strip.maximum_us - strip.wire_budget_us);
    std::printf(
        "TINYDRAW_PANSEQ_STRIP zoom=%s strip=%u panel_y=%d rows=%d samples=%lu "
        "staging_mean_us=%lld staging_max_us=%lld wire_budget_us=%lld over_budget_us=%lld "
        "pass=%u\n",
        zoom_name(zoom), static_cast<unsigned>(index), strip.panel_y, strip.rows,
        static_cast<unsigned long>(strip.samples), static_cast<long long>(mean_us),
        static_cast<long long>(strip.maximum_us), static_cast<long long>(strip.wire_budget_us),
        static_cast<long long>(over_budget_us), strip.maximum_us < strip.wire_budget_us);
  }
  const PanelStripStagingTiming& worst = staging.strips[staging.worst_strip_index];
  const std::int64_t staging_mean_us =
      staging.samples == 0U ? 0 : staging.total_us / static_cast<std::int64_t>(staging.samples);
  const std::int64_t worst_headroom_us = worst.wire_budget_us - worst.maximum_us;
  const std::int64_t frame_p95_us = pan_sequence_percentile(sorted_frame, 95);
  const vector_v2::ChromeStagingCacheStats chrome_after = presenter.chrome_cache_stats();
  const bool pacing_pass = frame_p95_us <= contract::kPanFrameP95RequiredUs;
  // Transport discipline requires a factual configured-edge observation and
  // every strip producer staying strictly faster than its measured wire time.
  // Pacing is a separate ship-contract gate; neither is a glass claim.
  const bool pass = all_passed && all_reused && tear_edge_failures == 0U && staging.samples != 0U &&
                    staging.all_under_wire && pacing_pass;
  std::printf(
      "TINYDRAW_GATE1_PANSEQ zoom=%s frames=%u scroll_avg_us=%lld exposed_avg_us=%lld "
      "chrome_avg_us=%lld tear_wait_avg_us=%lld present_avg_us=%lld prepare_avg_us=%lld "
      "acquire_wait_avg_us=%lld ring_copy_avg_us=%lld patch_avg_us=%lld "
      "byte_swap_avg_us=%lld staging_avg_us=%lld frame_avg_us=%lld frame_p50_us=%lld "
      "frame_p95_us=%lld frame_max_us=%lld "
      "complete_p50_us=%lld complete_p95_us=%lld complete_max_us=%lld "
      "tear_edge_failures=%lu presentation_experiment=%s te_edge=%s "
      "clock_mhz=%d "
      "chrome_bottom_redraws=%lu chrome_battery_redraws=%lu chrome_zoom_redraws=%lu "
      "chrome_minimap_base_redraws=%lu "
      "strip_samples=%lu staging_mean_us=%lld staging_max_us=%lld worst_strip=%u "
      "worst_strip_y=%d worst_wire_budget_us=%lld worst_headroom_us=%lld "
      "staging_invariant=%u pacing_pass=%u all_reused=%u pass=%u\n",
      zoom_name(zoom), static_cast<unsigned>(kPanSequenceFrames),
      static_cast<long long>(totals.scroll_us / kFrames),
      static_cast<long long>(totals.exposed_us / kFrames),
      static_cast<long long>(totals.chrome_us / kFrames),
      static_cast<long long>(totals.tear_wait_us / kFrames),
      static_cast<long long>(totals.present_us / kFrames),
      static_cast<long long>(totals.prepare_us / kFrames),
      static_cast<long long>(totals.acquire_wait_us / kFrames),
      static_cast<long long>(totals.ring_copy_us / kFrames),
      static_cast<long long>(totals.patch_us / kFrames),
      static_cast<long long>(totals.byte_swap_us / kFrames),
      static_cast<long long>(totals.staging_us / kFrames),
      static_cast<long long>(totals.frame_us / kFrames),
      static_cast<long long>(pan_sequence_percentile(sorted_frame, 50)),
      static_cast<long long>(frame_p95_us), static_cast<long long>(sorted_frame.back()),
      static_cast<long long>(pan_sequence_percentile(sorted_complete, 50)),
      static_cast<long long>(pan_sequence_percentile(sorted_complete, 95)),
      static_cast<long long>(sorted_complete.back()),
      static_cast<unsigned long>(tear_edge_failures), presentation_experiment_name(),
      selected_tear_edge_name(), kCo5300ClockMHz,
      static_cast<unsigned long>(chrome_after.bottom_redraws - chrome_before.bottom_redraws),
      static_cast<unsigned long>(chrome_after.battery_redraws - chrome_before.battery_redraws),
      static_cast<unsigned long>(chrome_after.zoom_redraws - chrome_before.zoom_redraws),
      static_cast<unsigned long>(chrome_after.minimap_base_redraws -
                                 chrome_before.minimap_base_redraws),
      static_cast<unsigned long>(staging.samples), static_cast<long long>(staging_mean_us),
      static_cast<long long>(staging.maximum_us), static_cast<unsigned>(staging.worst_strip_index),
      worst.panel_y, static_cast<long long>(worst.wire_budget_us),
      static_cast<long long>(worst_headroom_us), staging.all_under_wire, pacing_pass, all_reused,
      pass);
  std::fflush(stdout);
  return pass;
}

bool run_pan_boundary_gate(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome,
                           ZoomLevel zoom) {
  constexpr int kStart = 512;
  constexpr Point kTouchStart{300.0F, 300.0F};
  const auto reset = [&] {
    return presenter.set_view(zoom, kStart, kStart, chrome, now_us()).passed;
  };
  if (!reset()) {
    return false;
  }

  bool slow_pass = true;
  for (int drag = 1; drag <= 4; ++drag) {
    const auto timing = presenter.pan_from(
        kStart, kStart, kTouchStart, {kTouchStart.x - static_cast<float>(drag), kTouchStart.y},
        chrome, now_us());
    const int expected_x = kStart + drag - drag % 2;
    slow_pass = slow_pass && timing.passed && timing.frame_reused &&
                presenter.level_x() == expected_x && presenter.level_y() == kStart;
  }

  const auto probe = [&](int delta, bool expect_reuse) {
    if (!reset()) {
      return false;
    }
    const auto timing = presenter.pan_from(
        kStart, kStart, kTouchStart, {kTouchStart.x - static_cast<float>(delta), kTouchStart.y},
        chrome, now_us());
    return timing.passed && timing.frame_reused == expect_reuse &&
           presenter.level_x() == kStart + delta && presenter.level_y() == kStart;
  };
  const bool below = probe(94, true);
  const bool at = probe(kMaximumCachedPanDelta, true);
  const bool above = probe(98, false);
  const bool pass = slow_pass && below && at && above;
  std::printf(
      "TINYDRAW_GATE1_PAN_BOUNDARY zoom=%s slow_1px_trace=%u below_delta=94 below_reused=%u "
      "at_delta=%d at_reused=%u above_delta=98 above_fallback=%u pass=%u\n",
      zoom_name(zoom), slow_pass, below, kMaximumCachedPanDelta, at, above, pass);
  return pass;
}

bool run_cache_retention_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome) {
  constexpr std::array zooms{
      ZoomLevel::k50Percent,
      ZoomLevel::k100Percent,
      ZoomLevel::k200Percent,
      ZoomLevel::k400Percent,
  };
  constexpr int kUnalignedOrigin = vector_v2::kTileWidth - 1;
  constexpr int kDisjointOrigin = 9 * vector_v2::kTileWidth - 1;
  const auto fill = [&](ZoomLevel zoom, int x, int y) {
    const auto fallback = presenter.set_view(zoom, x, y, chrome, now_us());
    if (!fallback.passed) {
      return false;
    }
    const vector_v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {presenter.level_x(), presenter.level_y(),
                         presenter.level_x() + vector_v2::kOverviewWidth,
                         presenter.level_y() + vector_v2::kOverviewHeight},
    };
    const std::int64_t started = esp_timer_get_time();
    std::size_t published = 0;
    while (true) {
      const auto step = producer.produce_next(view);
      if (!step.has_value()) {
        return false;
      }
      if (step->tiles_published != 0U) {
        published += step->tiles_published;
        if (!presenter.refresh_region(step->level_bounds, chrome).passed) {
          return false;
        }
      }
      if (step->complete) {
        break;
      }
    }
    const std::int64_t elapsed_us = esp_timer_get_time() - started;
    std::printf(
        "TINYDRAW_GATE1_CACHE_FILL zoom=%s x=%d y=%d published=%lu total_us=%lld "
        "within_cold_gate=%u complete=1\n",
        zoom_name(zoom), presenter.level_x(), presenter.level_y(),
        static_cast<unsigned long>(published), static_cast<long long>(elapsed_us),
        elapsed_us < 500'000);
    return true;
  };

  bool passed = canvas.discard_tiles();
  for (const ZoomLevel zoom : zooms) {
    passed = fill(zoom, kUnalignedOrigin, kUnalignedOrigin) && passed;
  }
  for (const ZoomLevel zoom : zooms) {
    const vector_v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {kUnalignedOrigin, kUnalignedOrigin,
                         kUnalignedOrigin + vector_v2::kOverviewWidth,
                         kUnalignedOrigin + vector_v2::kOverviewHeight},
    };
    const auto remaining = producer.visible_tiles_remaining(view);
    const auto revisit =
        presenter.set_view(zoom, kUnalignedOrigin, kUnalignedOrigin, chrome, now_us());
    const bool hit = remaining == 0U && revisit.passed && revisit.fallback_pixels == 0U;
    std::printf(
        "TINYDRAW_GATE1_CACHE_REVISIT zoom=%s remaining=%lu tile_pixels=%lu fallback_pixels=%lu "
        "compose_us=%lld complete_us=%lld hit=%u\n",
        zoom_name(zoom), static_cast<unsigned long>(remaining.value_or(999U)),
        static_cast<unsigned long>(revisit.tile_pixels),
        static_cast<unsigned long>(revisit.fallback_pixels),
        static_cast<long long>(revisit.compose_us), static_cast<long long>(revisit.complete_us),
        hit);
    passed = hit && passed;
  }

  passed = fill(ZoomLevel::k400Percent, kDisjointOrigin, kDisjointOrigin) && passed;
  bool every_round_trip_hit = true;
  for (const ZoomLevel zoom : zooms) {
    const vector_v2::ViewRequest origin{
        .zoom = zoom,
        .level_pixels = {kUnalignedOrigin, kUnalignedOrigin,
                         kUnalignedOrigin + vector_v2::kOverviewWidth,
                         kUnalignedOrigin + vector_v2::kOverviewHeight},
    };
    const auto origin_remaining = producer.visible_tiles_remaining(origin);
    const auto round_trip =
        presenter.set_view(zoom, kUnalignedOrigin, kUnalignedOrigin, chrome, now_us());
    const bool hit =
        origin_remaining == 0U && round_trip.passed && round_trip.fallback_pixels == 0U;
    std::printf(
        "TINYDRAW_GATE1_CACHE_ROUND_TRIP zoom=%s from_x=%d from_y=%d via_x=%d via_y=%d "
        "remaining=%lu tile_pixels=%lu fallback_pixels=%lu compose_us=%lld complete_us=%lld "
        "hit=%u\n",
        zoom_name(zoom), kUnalignedOrigin, kUnalignedOrigin, kDisjointOrigin, kDisjointOrigin,
        static_cast<unsigned long>(origin_remaining.value_or(999U)),
        static_cast<unsigned long>(round_trip.tile_pixels),
        static_cast<unsigned long>(round_trip.fallback_pixels),
        static_cast<long long>(round_trip.compose_us),
        static_cast<long long>(round_trip.complete_us), hit);
    every_round_trip_hit = hit && every_round_trip_hit;
  }
  passed = every_round_trip_hit && passed;
  std::printf("TINYDRAW_GATE1_CACHE_RETENTION pass=%u slots=%lu revision=%lu\n", passed,
              static_cast<unsigned long>(canvas.slot_capacity()),
              static_cast<unsigned long>(canvas.current_revision().value));
  return passed;
}

bool run_full_world_cache_gate(vector_v2::TileProducer& producer, MaterializedCanvas& canvas) {
  if (!canvas.discard_tiles()) {
    return false;
  }
  constexpr ZoomLevel kZoom = ZoomLevel::k100Percent;
  constexpr int kGroupPixels = vector_v2::kTileProducerWidth;
  const std::int64_t started = esp_timer_get_time();
  for (int y = 0; y < vector_v2::kWorldHeight; y += kGroupPixels) {
    for (int x = 0; x < vector_v2::kWorldWidth; x += kGroupPixels) {
      const vector_v2::ViewRequest view{
          .zoom = kZoom,
          .level_pixels = {x, y, std::min(x + kGroupPixels, vector_v2::kWorldWidth),
                           std::min(y + kGroupPixels, vector_v2::kWorldHeight)},
      };
      while (true) {
        const auto step = producer.produce_next(view);
        if (!step.has_value()) {
          return false;
        }
        if (step->complete) {
          break;
        }
      }
    }
  }

  const vector_v2::TileGrid grid = vector_v2::tile_grid(kZoom);
  std::size_t raw = 0;
  std::size_t uniform = 0;
  std::size_t fallback = 0;
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {kZoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      if (!source.has_value() || source->kind == vector_v2::SourceKind::kOverview) {
        ++fallback;
      } else if (source->kind == vector_v2::SourceKind::kUniform) {
        ++uniform;
      } else {
        ++raw;
      }
    }
  }
  const std::size_t identities =
      static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows);
  const bool passed =
      fallback == 0U && raw <= canvas.slot_capacity() && raw + uniform == identities;
  std::printf(
      "TINYDRAW_PAPER_SWEEP zoom=100 identities=%lu raw=%lu uniform=%lu fallback=%lu "
      "slots=%lu total_us=%lld pass=%u\n",
      static_cast<unsigned long>(identities), static_cast<unsigned long>(raw),
      static_cast<unsigned long>(uniform), static_cast<unsigned long>(fallback),
      static_cast<unsigned long>(canvas.slot_capacity()),
      static_cast<long long>(esp_timer_get_time() - started), passed);
  return passed;
}

}  // namespace tinydraw::esp32::gate_harness
