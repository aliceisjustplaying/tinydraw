#include "vector_v2_gate_harness_internal.h"

namespace tinydraw::esp32::gate_harness {

#ifdef TINYDRAW_VECTOR_V2_TEARING_PROBE
// Explicit software A/B diagnostic. The center stripe encodes alternating
// 0x35/0xCA frame IDs plus the low eight row bits; camera/glass classification
// is deliberately external and is never inferred from software counters.
bool run_tearing_probe(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome) {
  presenter.enable_optical_row_pattern();
  const auto initial = presenter.set_view(ZoomLevel::k100Percent, 200, 300, chrome, now_us());
  constexpr std::array deltas{
      vector_v2::NavigationPoint{24, 0},  vector_v2::NavigationPoint{-24, 0},
      vector_v2::NavigationPoint{0, 18},  vector_v2::NavigationPoint{0, -18},
      vector_v2::NavigationPoint{24, 18}, vector_v2::NavigationPoint{-24, -18},
      vector_v2::NavigationPoint{0, 54},  vector_v2::NavigationPoint{0, -54},
  };
  constexpr std::size_t kCycles = 5;
  constexpr std::size_t kFrames = kCycles * deltas.size();
  std::array<std::int64_t, kFrames> intervals{};
  std::array<std::int64_t, kFrames> resume_latencies{};
  std::size_t frames = 0;
  std::size_t edge_failures = 0;
  std::size_t resume_samples = 0;
  std::size_t required_deadline_misses = 0;
  std::size_t guard_deadline_misses = 0;
  bool software_pass = initial.passed && initial.tear_edge_observed;
  std::int64_t previous_complete = esp_timer_get_time();
  for (std::size_t cycle = 0; cycle < kCycles; ++cycle) {
    for (const auto delta : deltas) {
      const int from_x = presenter.level_x();
      const int from_y = presenter.level_y();
      const auto timing = presenter.pan_from(
          from_x, from_y, {300.0F, 300.0F},
          {300.0F - static_cast<float>(delta.x), 300.0F - static_cast<float>(delta.y)}, chrome,
          now_us());
      const std::int64_t completed = esp_timer_get_time();
      const std::int64_t interval = completed - previous_complete;
      previous_complete = completed;
      intervals[frames] = interval;
      if (timing.tear_edge_wait_resumed) {
        resume_latencies[resume_samples++] = timing.tear_edge_isr_to_resume_us;
      }
      required_deadline_misses += interval > contract::kPanFrameP95RequiredUs;
      guard_deadline_misses += interval > contract::kPanFrameP95GuardUs;
      edge_failures += !timing.tear_edge_observed;
      software_pass =
          software_pass && timing.passed && timing.frame_reused && timing.tear_edge_observed;
      ++frames;
    }
  }
  std::sort(intervals.begin(), intervals.end());
  std::sort(resume_latencies.begin(), resume_latencies.begin() + resume_samples);
  const auto percentile = [](const auto& sorted, std::size_t count, int percent) {
    if (count == 0U) {
      return std::int64_t{0};
    }
    const std::size_t rank = (count * static_cast<std::size_t>(percent) + 99U) / 100U;
    return sorted[std::min(count - 1U, rank - 1U)];
  };
  std::printf(
      "TINYDRAW_TEARING_AB policy=%s edge=%s clock_mhz=%d frames=%lu "
      "initial_edge_observed=%u edge_failures=%lu edge_wait_resume_samples=%lu "
      "edge_wait_isr_to_resume_p50_us=%lld "
      "edge_wait_isr_to_resume_p95_us=%lld edge_wait_isr_to_resume_max_us=%lld "
      "frame_interval_p50_us=%lld frame_interval_p95_us=%lld frame_interval_max_us=%lld "
      "required_deadline_misses=%lu guard_deadline_misses=%lu "
      "optical_pattern=alternating_frame_id_row_barcode_v1 "
      "optical_acceptance=external_manual software_pass=%u\n",
      presentation_experiment_name(), selected_tear_edge_name(), kCo5300ClockMHz,
      static_cast<unsigned long>(frames), initial.tear_edge_observed,
      static_cast<unsigned long>(edge_failures), static_cast<unsigned long>(resume_samples),
      static_cast<long long>(percentile(resume_latencies, resume_samples, 50)),
      static_cast<long long>(percentile(resume_latencies, resume_samples, 95)),
      static_cast<long long>(percentile(resume_latencies, resume_samples, 100)),
      static_cast<long long>(percentile(intervals, intervals.size(), 50)),
      static_cast<long long>(percentile(intervals, intervals.size(), 95)),
      static_cast<long long>(intervals.back()),
      static_cast<unsigned long>(required_deadline_misses),
      static_cast<unsigned long>(guard_deadline_misses), software_pass);
  std::printf(
      "TINYDRAW_TEARING_PROBE_DONE frames=%lu software_pass=%u "
      "optical_acceptance=external_manual\n",
      static_cast<unsigned long>(frames), software_pass);
  std::fflush(stdout);
  return software_pass;
}
#endif

bool run_cooperative_compose_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                  OperationLog& log, MaterializedCanvas& canvas,
                                  const vector_v2::InPlaceAppendWorkspace& workspace,
                                  const vector_v2::ChromeState& chrome) {
  presenter.display().reset_timing();
  const std::uint32_t pushes_before = presenter.display().push_count();
  LivePresentationTiming timing{};
  bool incomplete_without_push = true;
  std::uint32_t calls = 0;
  do {
    timing = presenter.refresh_slice(chrome, now_us());
    ++calls;
    if (timing.compose_pending) {
      incomplete_without_push =
          incomplete_without_push && presenter.display().push_count() == pushes_before;
    }
  } while (timing.compose_pending && calls <= 64U);
  const LivePresentationTiming baseline_timing = timing;
  const bool baseline_pass = incomplete_without_push && timing.passed && calls == 56U &&
                             timing.compose_slices == calls && timing.compose_slice_max_us > 0 &&
                             timing.submitted_pixels == vector_v2::kOverviewPixels &&
                             presenter.display().push_count() > pushes_before;

  const std::uint32_t pushes_after_baseline = presenter.display().push_count();
  const auto pressed = presenter.refresh_slice(chrome, now_us(), true);
  const bool pressed_blocked = pressed.compose_pending && presenter.refresh_pending() &&
                               !presenter.refresh_composing() &&
                               presenter.display().push_count() == pushes_after_baseline;

  const auto interrupted_first = presenter.refresh_slice(chrome, now_us());
  const std::uint32_t pushes_before_ink = presenter.display().push_count();
  const InkPoint interruption_point{
      .position = {96.0F, 96.0F},
      .pressure = 1.0F,
      .radius = 5.0F,
      .distance = 0.0F,
      .running_length = 0.0F,
      .timestamp_us = now_us(),
  };
  const auto interruption_ink = presenter.show_start(interruption_point, 0x001FU, chrome, now_us());
  const bool interruption_deferred =
      interrupted_first.compose_pending && interruption_ink.passed && presenter.refresh_pending() &&
      !presenter.refresh_composing() && presenter.display().push_count() > pushes_before_ink;
  const std::uint32_t pushes_after_ink = presenter.display().push_count();
  bool restart_incomplete_without_push = true;
  std::uint32_t restart_calls = 0;
  do {
    timing = presenter.refresh_slice(chrome, now_us());
    ++restart_calls;
    if (timing.compose_pending) {
      restart_incomplete_without_push =
          restart_incomplete_without_push && presenter.display().push_count() == pushes_after_ink;
    }
  } while (timing.compose_pending && restart_calls <= 64U);
  const bool restart_pass = interruption_deferred && restart_incomplete_without_push &&
                            timing.passed && restart_calls == 56U &&
                            timing.compose_slices == restart_calls;

  constexpr std::array<CompactOperationSample, 2> pending_samples{{
      {.x_quarter = 1'024U, .y_quarter = 1'024U, .radius_256 = 1'280U},
      {.x_quarter = 1'152U, .y_quarter = 1'152U, .radius_256 = 1'280U, .elapsed_ms = 8U},
  }};
  const auto pending_append = vector_v2::append_authority_only(log, {.tool = OperationTool::kPen,
                                                                     .color = 0x001FU,
                                                                     .gesture_id = 39'999U,
                                                                     .samples = pending_samples});
  const std::uint32_t pushes_before_pending = presenter.display().push_count();
  const auto pending_block = presenter.refresh_slice(chrome, now_us());
  const bool authority_blocked = pending_append.has_value() && pending_block.compose_pending &&
                                 presenter.refresh_pending() && !presenter.refresh_composing() &&
                                 presenter.display().push_count() == pushes_before_pending;
  producer.cancel_pending_work();
  const auto absorbed = vector_v2::absorb_pending_operation(log, canvas, workspace);
  bool pending_restart_without_push = true;
  const std::uint32_t pushes_after_pending_block = presenter.display().push_count();
  std::uint32_t pending_restart_calls = 0;
  do {
    timing = presenter.refresh_slice(chrome, now_us());
    ++pending_restart_calls;
    if (timing.compose_pending) {
      pending_restart_without_push = pending_restart_without_push &&
                                     presenter.display().push_count() == pushes_after_pending_block;
    }
  } while (absorbed.has_value() && timing.compose_pending && pending_restart_calls <= 64U);
  const bool pending_restart = absorbed.has_value() && pending_restart_without_push &&
                               timing.passed && pending_restart_calls == 56U &&
                               timing.compose_slices == pending_restart_calls;

  const bool pass =
      baseline_pass && pressed_blocked && restart_pass && authority_blocked && pending_restart;
  std::printf(
      "TINYDRAW_GATE1_COOPERATIVE_COMPOSE calls=%lu slices=%lu max_slice_us=%lld compose_us=%lld "
      "incomplete_no_push=%u submitted=%lu pressed_blocked=%u interrupted=%u restart_calls=%lu "
      "restart_no_push=%u authority_blocked=%u pending_restart_calls=%lu "
      "pending_restart_no_push=%u pass=%u\n",
      static_cast<unsigned long>(calls), static_cast<unsigned long>(baseline_timing.compose_slices),
      static_cast<long long>(baseline_timing.compose_slice_max_us),
      static_cast<long long>(baseline_timing.compose_us), incomplete_without_push,
      static_cast<unsigned long>(baseline_timing.submitted_pixels), pressed_blocked,
      interruption_deferred, static_cast<unsigned long>(restart_calls),
      restart_incomplete_without_push, authority_blocked,
      static_cast<unsigned long>(pending_restart_calls), pending_restart_without_push, pass);
  std::fflush(stdout);
  return pass;
}

bool run_tile_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                   OperationLog& log, MaterializedCanvas& canvas,
                   const vector_v2::ChromeState& chrome, ZoomLevel zoom) {
  // The seed-7 corpus fills lines from the upper left. Fixing the origin makes
  // both zooms measure real ink rather than a potentially blank center crop.
  const auto fallback = presenter.set_view(zoom, 0, 0, chrome, now_us());
  print_gate_presentation("gate_fallback", presenter, fallback);
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = zoom,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  const std::int64_t started = esp_timer_get_time();
  std::int64_t maximum_supertask_us = 0;
  std::int64_t presentation_us = 0;
  std::size_t steps = 0;
  std::size_t operations_scanned = 0;
  std::size_t operations_rendered = 0;
  std::size_t tiles_published = 0;
  while (true) {
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    const std::int64_t step_us = esp_timer_get_time() - step_started;
    maximum_supertask_us = std::max(maximum_supertask_us, step_us);
    if (!step.has_value()) {
      return false;
    }
    if (step->tiles_published != 0U) {
      const auto present_started = esp_timer_get_time();
      const auto timing = presenter.refresh_region(step->level_bounds, chrome);
      presentation_us += esp_timer_get_time() - present_started;
      if (!timing.passed) {
        return false;
      }
    }
    ++steps;
    operations_scanned += step->operations_scanned;
    operations_rendered += step->operations_rendered;
    tiles_published += step->tiles_published;
    if (step->complete) {
      break;
    }
  }
  const std::int64_t total_us = esp_timer_get_time() - started;
  const bool passed =
      total_us <= contract::kColdViewportRequiredUs && maximum_supertask_us < 30'000;
  std::printf(
      "TINYDRAW_GATE1_HARD zoom=%s cold=1 operations=%lu samples=%lu steps=%lu tiles=%lu "
      "scanned=%lu rendered=%lu max_supertask_us=%lld presentation_us=%lld total_us=%lld "
      "maximum_wall_us=%lld pass=%u\n",
      zoom_name(zoom), static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<unsigned long>(steps),
      static_cast<unsigned long>(tiles_published), static_cast<unsigned long>(operations_scanned),
      static_cast<unsigned long>(operations_rendered), static_cast<long long>(maximum_supertask_us),
      static_cast<long long>(presentation_us), static_cast<long long>(total_us),
      static_cast<long long>(contract::kColdViewportRequiredUs), passed);
  return passed;
}

bool run_paced_cold_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                         MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                         const vector_v2::ChromeState& chrome, ZoomLevel zoom, int level_x,
                         int level_y, const char* corpus, std::int64_t maximum_wall_us) {
  if (!canvas.discard_tiles()) {
    return false;
  }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  vector_v2::g_raster_census.reset();
#endif
  const auto fallback = presenter.set_view(zoom, level_x, level_y, chrome, now_us());
  if (!fallback.passed) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = zoom,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };

  bool complete = false;
  bool presentation_pending = false;
  vector_v2::PixelRect pending_bounds{};
  std::size_t steps = 0;
  std::size_t tiles = 0;
  std::int64_t compute_us = 0;
  std::int64_t present_us = 0;
  std::int64_t touch_us = 0;
  std::int64_t maximum_tick_us = 0;
  std::uint8_t background_ticks = 0;
  const std::int64_t started = esp_timer_get_time();
  while (!complete || presentation_pending) {
    const std::int64_t tick_started = esp_timer_get_time();
    const std::int64_t touch_started = esp_timer_get_time();
    static_cast<void>(touch.read_next());
    touch_us += esp_timer_get_time() - touch_started;

    if (presentation_pending) {
      const std::int64_t present_started = esp_timer_get_time();
      if (!presenter.refresh_region(pending_bounds, chrome).passed) {
        return false;
      }
      present_us += esp_timer_get_time() - present_started;
      presentation_pending = false;
    } else {
      // Mirror the product loop: fill the slice to the shared deadline
      // instead of taking one producer step per tick.
      const std::int64_t slice_started = esp_timer_get_time();
      do {
        const std::int64_t compute_started = esp_timer_get_time();
        const auto step = producer.produce_next(view);
        compute_us += esp_timer_get_time() - compute_started;
        if (!step.has_value()) {
          return false;
        }
        ++steps;
        tiles += step->tiles_published;
        complete = step->complete;
        if (step->tiles_published != 0U) {
          pending_bounds = step->level_bounds;
          presentation_pending = true;
        }
      } while (!complete && !presentation_pending &&
               esp_timer_get_time() - slice_started < kColdFillSliceDeadlineUs);
    }
    maximum_tick_us = std::max(maximum_tick_us, esp_timer_get_time() - tick_started);
    // Mirror the interactive loop: producer work already yields at bounded
    // input-poll boundaries; one real delay per eight ticks feeds idle tasks.
    if (++background_ticks == 8U) {
      background_ticks = 0U;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  constexpr std::int64_t kMaximumTickUs = 15'000;
  constexpr std::uint32_t kMaximumTouchIntervalUs = 15'000;
  const std::int64_t wall_us = esp_timer_get_time() - started;
  const std::int64_t pacing_us = wall_us - compute_us - present_us - touch_us;
  const TouchSamplerMetrics sampler = touch.take_metrics();
  const bool passed = wall_us <= maximum_wall_us && maximum_tick_us < kMaximumTickUs &&
                      sampler.maximum_interval_us < kMaximumTouchIntervalUs &&
                      sampler.errors == 0U && sampler.queue_overflows == 0U &&
                      sampler.queue_resyncs == 0U;
  std::printf(
      "TINYDRAW_GATE1_PACED_COLD corpus=%s zoom=%s x=%d y=%d steps=%lu tiles=%lu "
      "compute_us=%lld present_us=%lld touch_us=%lld pacing_us=%lld wall_us=%lld "
      "maximum_wall_us=%lld "
      "max_tick_us=%lld touch_samples=%lu touch_interval_max_us=%lu touch_read_max_us=%lu "
      "touch_events=%lu touch_down=%lu touch_up=%lu touch_events_ge_8ms=%lu "
      "touch_event_age_max_us=%lu touch_errors=%lu touch_overflows=%lu touch_resyncs=%lu "
      "pass=%u\n",
      corpus, zoom_name(zoom), presenter.level_x(), presenter.level_y(),
      static_cast<unsigned long>(steps), static_cast<unsigned long>(tiles),
      static_cast<long long>(compute_us), static_cast<long long>(present_us),
      static_cast<long long>(touch_us), static_cast<long long>(pacing_us),
      static_cast<long long>(wall_us), static_cast<long long>(maximum_wall_us),
      static_cast<long long>(maximum_tick_us), static_cast<unsigned long>(sampler.samples),
      static_cast<unsigned long>(sampler.maximum_interval_us),
      static_cast<unsigned long>(sampler.maximum_read_us),
      static_cast<unsigned long>(sampler.events_consumed),
      static_cast<unsigned long>(sampler.down_events),
      static_cast<unsigned long>(sampler.up_events),
      static_cast<unsigned long>(sampler.events_at_least_8ms_old),
      static_cast<unsigned long>(sampler.maximum_event_age_us),
      static_cast<unsigned long>(sampler.errors),
      static_cast<unsigned long>(sampler.queue_overflows),
      static_cast<unsigned long>(sampler.queue_resyncs), passed);
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  {
    const auto& census = vector_v2::g_raster_census;
    std::printf(
        "TINYDRAW_RASTER_CENSUS corpus=%s zoom=%s gate_ms=%.1f setup_ms=%.1f paint_ms=%.1f "
        "publish_ms=%.1f segs_painted=%llu segs_rejected=%llu rows_prefinal=%llu "
        "const_rows=%llu const_search=%llu const_probed_empty=%llu span_px=%llu "
        "const_span_px=%llu\n",
        corpus, zoom_name(zoom), static_cast<double>(census.gate_ticks) / 240e3,
        static_cast<double>(census.setup_ticks) / 240e3,
        static_cast<double>(census.paint_ticks) / 240e3,
        static_cast<double>(census.publish_ticks) / 240e3,
        static_cast<unsigned long long>(census.segments_painted),
        static_cast<unsigned long long>(census.segments_bbox_rejected),
        static_cast<unsigned long long>(census.rows_prefinalized),
        static_cast<unsigned long long>(census.const_rows_scanned),
        static_cast<unsigned long long>(census.const_search_calls),
        static_cast<unsigned long long>(census.const_rows_probed_empty),
        static_cast<unsigned long long>(census.span_pixels),
        static_cast<unsigned long long>(census.const_span_pixels));
  }
#endif
  return passed;
}

bool append_overlapping_scribble(OperationLog& log, MaterializedCanvas& canvas,
                                 const InPlaceAppendWorkspace& workspace) {
  constexpr std::size_t kStrokeCount = 8;
  constexpr std::size_t kSamplesPerStroke = 150;
  constexpr std::uint16_t kRadius256 = 80U * 256U;
  std::array<CompactOperationSample, kSamplesPerStroke> samples{};
  for (std::size_t stroke = 0; stroke < kStrokeCount; ++stroke) {
    for (std::size_t index = 0; index < kSamplesPerStroke; ++index) {
      const std::size_t phase = (index + stroke * 7U) % 64U;
      const std::size_t triangle = phase <= 32U ? phase : 64U - phase;
      const std::size_t x = 64U + index * static_cast<std::size_t>(vector_v2::kWorldWidth - 128) /
                                      (kSamplesPerStroke - 1U);
      const std::size_t y = 320U + triangle * 32U + stroke * 3U;
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>(x * 16U),
          .y_quarter = static_cast<std::uint16_t>(y * 16U),
          .radius_256 = kRadius256,
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    if (!append_and_absorb(log, canvas,
                           vector_v2::OperationAppend{
                               .tool = OperationTool::kPen,
                               .color = static_cast<std::uint16_t>(0x001FU + stroke * 0x111U),
                               .samples = samples},
                           workspace)
             .has_value()) {
      return false;
    }
  }
  std::printf("TINYDRAW_OVERLAP_WORKLOAD operations=%lu samples=%lu radius_world=80\n",
              static_cast<unsigned long>(log.operation_count()),
              static_cast<unsigned long>(log.sample_count()));
  return true;
}

bool append_adversarial_tapered_document(OperationLog& log, MaterializedCanvas& canvas,
                                         const InPlaceAppendWorkspace& workspace) {
  vector_v2::test_support::AdversarialTaperedCorpusStats stats{};
  const std::int64_t started = esp_timer_get_time();
  const bool appended = vector_v2::test_support::emit_adversarial_tapered_corpus(
      [&](const vector_v2::OperationAppend& operation) {
        return append_and_absorb(log, canvas, operation, workspace).has_value();
      },
      &stats);
  std::printf(
      "TINYDRAW_ADVERSARIAL_TAPERED_WORKLOAD operations=%lu samples=%lu erasers=%lu "
      "load_us=%lld appended=%u\n",
      static_cast<unsigned long>(stats.operations), static_cast<unsigned long>(stats.samples),
      static_cast<unsigned long>(stats.erasers),
      static_cast<long long>(esp_timer_get_time() - started), appended);
  return appended;
}

bool run_overlap_cold_gates(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                            MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                            const vector_v2::ChromeState& chrome) {
  constexpr std::array zooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                             ZoomLevel::k400Percent};
  bool passed = true;
  for (const ZoomLevel zoom : zooms) {
    const int percent = vector_v2::zoom_percent(zoom);
    const int level_width = vector_v2::kWorldWidth * percent / 100;
    const int level_height = vector_v2::kWorldHeight * percent / 100;
    const int x = std::clamp(level_width / 2 - vector_v2::kOverviewWidth / 2 + 31, 0,
                             level_width - vector_v2::kOverviewWidth);
    const int y = std::clamp(level_height / 2 - vector_v2::kOverviewHeight / 2 + 31, 0,
                             level_height - vector_v2::kOverviewHeight);
    passed = run_paced_cold_gate(presenter, producer, canvas, touch, chrome, zoom, x, y, "overlap",
                                 contract::kColdViewportRequiredUs) &&
             passed;
  }
  return passed;
}

bool run_general_cold_gates(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                            MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                            const vector_v2::ChromeState& chrome) {
  constexpr std::array gates{ZoomLevel::k50Percent, ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                             ZoomLevel::k400Percent};
  bool passed = true;
  for (const ZoomLevel zoom : gates) {
    // 400% runs against the owner-accepted hold-the-line ceiling (ship
    // contract owner decision #2, 2026-08-16); every other zoom keeps the
    // <=500 ms product line.
    const std::int64_t budget_us = zoom == ZoomLevel::k400Percent
                                       ? contract::kColdViewport400HoldTheLineUs
                                       : contract::kColdViewportRequiredUs;
    passed = run_paced_cold_gate(presenter, producer, canvas, touch, chrome, zoom, 0, 0,
                                 "adversarial_tapered_4x+evil_hairlines", budget_us) &&
             passed;
  }
  return passed;
}

}  // namespace tinydraw::esp32::gate_harness
