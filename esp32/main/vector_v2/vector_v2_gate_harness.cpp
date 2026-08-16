#include "vector_v2_gate_harness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
#include "vector_v2_tile_census.h"
#endif
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/vector_v2/adversarial_tapered_corpus.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/raster_census.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "vector_v2_ship_contract.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::CompactOperationSample;
using vector_v2::DocumentRevision;
using vector_v2::IncrementalDocumentWorkspace;
using vector_v2::MaterializedCanvas;
using vector_v2::OperationLog;
using vector_v2::OperationTool;
using vector_v2::ZoomLevel;
namespace contract = vector_v2_ship_contract;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr std::uint32_t kStressOperations = 1'000;
constexpr std::uint32_t kStressSamplesPerOperation = 20;
constexpr std::size_t kRealisticStrokeCapacity = 1'000;
constexpr std::size_t kRealisticSampleCapacity = 24'576;
constexpr std::size_t kWorkspaceTileCapacity = vector_v2::kMaximumVisibleTiles;

template <typename Type>
[[nodiscard]] std::unique_ptr<Type, decltype(&heap_caps_free)> allocate_external(
    std::size_t count) {
  return {static_cast<Type*>(heap_caps_malloc(count * sizeof(Type), kExternalCaps)),
          &heap_caps_free};
}

std::uint32_t now_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

const char* zoom_name(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return "25";
    case ZoomLevel::k50Percent:
      return "50";
    case ZoomLevel::k100Percent:
      return "100";
    case ZoomLevel::k200Percent:
      return "200";
    case ZoomLevel::k400Percent:
      return "400";
  }
  return "unknown";
}

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld scroll_us=%lld "
      "exposed_compose_us=%lld chrome_us=%lld chrome_prepare_us=%lld chrome_stage_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld "
      "transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu pushes=%lu tear_wait_us=%lld tear_edge_isr_to_resume_us=%lu "
      "tear_edge_observed=%u tear_edge_wait_resumed=%u tear_edge_timeout=%u "
      "tear_heal_attempted=%u "
      "tear_heal_command_sent=%u presentation_experiment=%s te_edge=%s "
      "requested_clock_mhz=%d effective_clock_mhz=%d "
      "frame_reused=%u pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.scroll_us),
      static_cast<long long>(timing.exposed_compose_us), static_cast<long long>(timing.chrome_us),
      static_cast<long long>(timing.chrome_prepare_us),
      static_cast<long long>(timing.chrome_stage_us),
      static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.tile_pixels),
      static_cast<unsigned long>(timing.uniform_pixels),
      static_cast<unsigned long>(timing.overview_pixels),
      static_cast<unsigned long>(timing.fallback_pixels),
      static_cast<unsigned long>(timing.resident_tiles),
      static_cast<unsigned long>(timing.fallback_tiles), static_cast<unsigned long>(timing.pushes),
      static_cast<long long>(timing.tear_wait_us),
      static_cast<unsigned long>(timing.tear_edge_isr_to_resume_us), timing.tear_edge_observed,
      timing.tear_edge_wait_resumed, timing.tear_edge_timed_out, timing.tear_heal_attempted,
      timing.tear_heal_command_sent, presentation_experiment_name(), selected_tear_edge_name(),
      requested_panel_clock_mhz(), effective_panel_clock_mhz(), timing.frame_reused, timing.passed);
}

bool load_realistic_document(OperationLog& log, MaterializedCanvas& canvas,
                             const IncrementalDocumentWorkspace& workspace,
                             std::span<VectorStroke> stroke_storage,
                             std::span<StrokeSample> sample_storage,
                             std::span<CompactOperationSample> conversion_storage) {
  VectorDocument source(stroke_storage, sample_storage);
  RealisticWorkloadStats stats{};
  const RectF area{.x0 = 0.0F,
                   .y0 = 0.0F,
                   .x1 = static_cast<float>(vector_v2::kWorldWidth),
                   .y1 = static_cast<float>(vector_v2::kWorldHeight)};
  if (!populate_realistic_handwriting(source, 7U, kRealisticStrokeCapacity, area, &stats)) {
    return false;
  }
  const std::int64_t started = esp_timer_get_time();
  for (const VectorStroke& stroke : source.strokes()) {
    const auto input = source.samples(stroke);
    if (input.empty() || input.size() > conversion_storage.size()) {
      return false;
    }
    for (std::size_t index = 0; index < input.size(); ++index) {
      conversion_storage[index] = {
          .x_quarter = static_cast<std::uint16_t>(std::lround(input[index].x * 4.0F)),
          .y_quarter = static_cast<std::uint16_t>(std::lround(input[index].y * 4.0F)),
          .radius_256 = static_cast<std::uint16_t>(std::lround(input[index].radius * 256.0F)),
          .elapsed_ms = static_cast<std::uint16_t>(index * 15U),
      };
    }
    const auto result = vector_v2::append_incrementally(
        log, canvas,
        {.tool = stroke.tool == VectorTool::kEraser ? OperationTool::kEraser : OperationTool::kPen,
         .color = stroke.color,
         .samples = conversion_storage.first(input.size())},
        workspace);
    if (!result.has_value()) {
      return false;
    }
  }
  std::printf(
      "TINYDRAW_GATE1_WORKLOAD kind=realistic seed=7 operations=%lu samples=%lu "
      "maximum_stroke=%lu load_us=%lld raw_source=1 lod_copies=0\n",
      static_cast<unsigned long>(stats.strokes), static_cast<unsigned long>(stats.samples),
      static_cast<unsigned long>(stats.maximum_stroke_samples),
      static_cast<long long>(esp_timer_get_time() - started));
  return true;
}

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
      "TINYDRAW_TEARING_AB policy=%s edge=%s requested_clock_mhz=%d "
      "effective_clock_mhz=%d frames=%lu "
      "initial_edge_observed=%u edge_failures=%lu edge_wait_resume_samples=%lu "
      "edge_wait_isr_to_resume_p50_us=%lld "
      "edge_wait_isr_to_resume_p95_us=%lld edge_wait_isr_to_resume_max_us=%lld "
      "frame_interval_p50_us=%lld frame_interval_p95_us=%lld frame_interval_max_us=%lld "
      "required_deadline_misses=%lu guard_deadline_misses=%lu "
      "optical_pattern=alternating_frame_id_row_barcode_v1 "
      "optical_acceptance=external_manual software_pass=%u\n",
      presentation_experiment_name(), selected_tear_edge_name(), requested_panel_clock_mhz(),
      effective_panel_clock_mhz(), static_cast<unsigned long>(frames), initial.tear_edge_observed,
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

bool run_tile_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                   OperationLog& log, MaterializedCanvas& canvas,
                   const vector_v2::ChromeState& chrome, ZoomLevel zoom) {
  // The seed-7 corpus fills lines from the upper left. Fixing the origin makes
  // both zooms measure real ink rather than a potentially blank center crop.
  const auto fallback = presenter.set_view(zoom, 0, 0, chrome, now_us());
  print_presentation("gate_fallback", presenter, fallback);
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
                      sampler.errors == 0U && sampler.queue_overflows == 0U;
  std::printf(
      "TINYDRAW_GATE1_PACED_COLD corpus=%s zoom=%s x=%d y=%d steps=%lu tiles=%lu "
      "compute_us=%lld present_us=%lld touch_us=%lld pacing_us=%lld wall_us=%lld "
      "maximum_wall_us=%lld "
      "max_tick_us=%lld touch_samples=%lu touch_interval_max_us=%lu touch_read_max_us=%lu "
      "touch_events=%lu touch_down=%lu touch_up=%lu touch_events_ge_8ms=%lu "
      "touch_event_age_max_us=%lu touch_errors=%lu touch_overflows=%lu pass=%u\n",
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
      static_cast<unsigned long>(sampler.queue_overflows), passed);
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
                                 const IncrementalDocumentWorkspace& workspace) {
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
          .x_quarter = static_cast<std::uint16_t>(x * 4U),
          .y_quarter = static_cast<std::uint16_t>(y * 4U),
          .radius_256 = kRadius256,
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    if (!vector_v2::append_incrementally(
             log, canvas,
             {.tool = OperationTool::kPen,
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
                                         const IncrementalDocumentWorkspace& workspace) {
  vector_v2::test_support::AdversarialTaperedCorpusStats stats{};
  const std::int64_t started = esp_timer_get_time();
  const bool appended = vector_v2::test_support::emit_adversarial_tapered_corpus(
      [&](const vector_v2::OperationAppend& operation) {
        return vector_v2::append_incrementally(log, canvas, operation, workspace).has_value();
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
    passed = run_paced_cold_gate(presenter, producer, canvas, touch, chrome, zoom, 0, 0,
                                 "adversarial_tapered_4x+evil_hairlines",
                                 contract::kColdViewportRequiredUs) &&
             passed;
  }
  return passed;
}

struct LiveInkPathMeasurement {
  std::int64_t maximum_wall_us = 0;
  std::int64_t maximum_submit_us = 0;
  std::int64_t maximum_complete_us = 0;
  std::int64_t maximum_chrome_us = 0;
  std::size_t presented_updates = 0;
  std::size_t failures = 0;
};

LiveInkPathMeasurement measure_live_ink_circle(VectorV2Presenter& presenter,
                                               const vector_v2::ChromeState& chrome, Point center,
                                               float path_radius) {
  constexpr std::size_t kPointCount = 48;
  constexpr float kTau = 6.28318530718F;
  CurvedRibbonStream ribbon;
  LiveInkPathMeasurement measurement;
  float running_length = 0.0F;
  Point previous{};
  std::uint32_t timestamp_us = now_us();
  for (std::size_t index = 0; index < kPointCount; ++index) {
    const float angle = kTau * static_cast<float>(index) / static_cast<float>(kPointCount - 1U);
    const Point position{center.x + std::cos(angle) * path_radius,
                         center.y + std::sin(angle) * path_radius};
    const float distance =
        index == 0U ? 0.0F : std::hypot(position.x - previous.x, position.y - previous.y);
    running_length += distance;
    timestamp_us += 8'333U;
    const InkPoint point{.position = position,
                         .pressure = 1.0F,
                         .radius = 8.0F,
                         .distance = distance,
                         .running_length = running_length,
                         .timestamp_us = timestamp_us};
    const std::uint32_t event_us = now_us();
    const std::int64_t started_us = esp_timer_get_time();
    LivePresentationTiming timing;
    if (index == 0U) {
      static_cast<void>(ribbon.append(point, true));
      timing = presenter.show_start(point, 0x001FU, chrome, event_us);
    } else {
      timing = presenter.show_update(ribbon.append(point, true), 0x001FU, chrome, event_us);
    }
    const std::int64_t wall_us = esp_timer_get_time() - started_us;
    if (!timing.passed) {
      ++measurement.failures;
    }
    if (timing.pushes != 0U) {
      ++measurement.presented_updates;
      measurement.maximum_wall_us = std::max(measurement.maximum_wall_us, wall_us);
      measurement.maximum_submit_us =
          std::max(measurement.maximum_submit_us, timing.first_submit_us);
      measurement.maximum_complete_us =
          std::max(measurement.maximum_complete_us, timing.first_complete_us);
      measurement.maximum_chrome_us = std::max(measurement.maximum_chrome_us, timing.chrome_us);
    }
    previous = position;
  }
  return measurement;
}

bool fill_view_to_completion(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                             std::size_t& tiles, std::int64_t& wall_us);

// Commits single-cap ink at the four panel-edge corners and verifies the
// composed pixels at the true edge columns/rows: a presentation or compose
// path that drops column 0/367 or row 0 fails here instead of on glass.
bool run_edge_ink_case(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                       OperationLog& log, MaterializedCanvas& canvas,
                       const vector_v2::ChromeState& chrome,
                       const IncrementalDocumentWorkspace& workspace,
                       std::span<std::uint16_t> compose_scratch) {
  if (!presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  constexpr std::uint16_t kColor = 0x001FU;
  constexpr float kRadius = 6.0F;
  constexpr std::array<std::array<int, 2>, 4> kCorners{{{1, 1}, {366, 1}, {1, 369}, {366, 369}}};
  std::uint32_t timestamp_us = now_us();
  for (const auto& corner : kCorners) {
    const auto x = static_cast<float>(corner[0]);
    const auto y = static_cast<float>(corner[1]);
    const InkPoint point{.position = {x, y},
                         .pressure = 1.0F,
                         .radius = kRadius,
                         .distance = 0.0F,
                         .running_length = 0.0F,
                         .timestamp_us = timestamp_us};
    timestamp_us += 8'333U;
    if (!presenter.show_start(point, kColor, chrome, now_us()).passed) {
      return false;
    }
    // At 100% with origin (0, 0) panel coordinates equal world coordinates.
    const std::array<CompactOperationSample, 1> samples{{{
        .x_quarter = static_cast<std::uint16_t>(corner[0] * 4),
        .y_quarter = static_cast<std::uint16_t>(corner[1] * 4),
        .radius_256 = static_cast<std::uint16_t>(kRadius * 256.0F),
        .elapsed_ms = 0,
    }}};
    if (!vector_v2::append_incrementally(log, canvas, {.color = kColor, .samples = samples},
                                         workspace)
             .has_value()) {
      return false;
    }
  }
  // Exact raw tiles for the view, then compose and sample the edges.
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k100Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  std::size_t fill_tiles = 0;
  std::int64_t fill_us = 0;
  if (!fill_view_to_completion(producer, view, fill_tiles, fill_us)) {
    return false;
  }
  std::size_t edge_failures = 0;
  for (const auto& corner : kCorners) {
    constexpr int kBandRows = 8;
    const int band_top = std::clamp(corner[1] - 2, 0, vector_v2::kOverviewHeight - kBandRows);
    const vector_v2::ViewRequest band{
        .zoom = ZoomLevel::k100Percent,
        .level_pixels = {0, band_top, vector_v2::kOverviewWidth, band_top + kBandRows},
    };
    const std::size_t band_pixels = static_cast<std::size_t>(vector_v2::kOverviewWidth) * kBandRows;
    if (band_pixels > compose_scratch.size()) {
      return false;
    }
    const auto stats = canvas.compose_view(band, compose_scratch.first(band_pixels));
    if (!stats.has_value() || stats->fallback_pixels != 0U) {
      return false;
    }
    const auto pixel_at = [&](int x, int y) {
      return compose_scratch[static_cast<std::size_t>(y - band_top) * vector_v2::kOverviewWidth +
                             static_cast<std::size_t>(x)];
    };
    // The cap covers its center and the adjacent true edge column/row.
    const int edge_x = corner[0] == 1 ? 0 : vector_v2::kOverviewWidth - 1;
    edge_failures += pixel_at(corner[0], corner[1]) != kColor;
    edge_failures += pixel_at(edge_x, corner[1]) != kColor;
  }
  std::printf("TINYDRAW_EDGE_INK corners=%lu edge_failures=%lu pass=%u\n",
              static_cast<unsigned long>(kCorners.size()),
              static_cast<unsigned long>(edge_failures), edge_failures == 0U);
  return edge_failures == 0U;
}

bool run_overlay_canvas_purity_gate(VectorV2Presenter& presenter, OperationLog& log,
                                    MaterializedCanvas& canvas,
                                    const vector_v2::ChromeState& chrome,
                                    const IncrementalDocumentWorkspace& workspace) {
  constexpr std::array<std::array<int, 2>, 4> kOverlayCenters{{
      {280, 36},   // battery
      {332, 150},  // zoom rail
      {310, 310},  // minimap
      {184, 410},  // toolbar
  }};
  const DocumentRevision before = canvas.current_revision();
  std::uint16_t gesture_id = 6'000;
  for (const auto& center : kOverlayCenters) {
    const std::array<CompactOperationSample, 1> sample{{{
        .x_quarter = static_cast<std::uint16_t>(center[0] * 4),
        .y_quarter = static_cast<std::uint16_t>(center[1] * 4),
        .radius_256 = 12U * 256U,
        .elapsed_ms = 0,
    }}};
    if (!vector_v2::append_incrementally(log, canvas,
                                         {.tool = OperationTool::kPen,
                                          .color = 0xF800U,
                                          .gesture_id = gesture_id++,
                                          .samples = sample},
                                         workspace)
             .has_value()) {
      return false;
    }
  }
  const auto refresh = presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us());
  const bool preserved = refresh.passed && presenter.verify_staging_preserves_canvas(chrome);
  const bool authority = canvas.current_revision().value == before.value + kOverlayCenters.size() &&
                         log.current_revision() == canvas.current_revision();
  const bool passed = preserved && authority;
  std::printf(
      "TINYDRAW_GATE1_OVERLAY_CANVAS regions=zoom,minimap,toolbar,battery operations=%lu "
      "presentation_mutations=%u authority=%u pass=%u\n",
      static_cast<unsigned long>(kOverlayCenters.size()), !preserved, authority, passed);
  std::fflush(stdout);
  return passed;
}

bool run_live_ink_overlay_gate(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome) {
  if (!presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const auto clear = measure_live_ink_circle(presenter, chrome, {150.0F, 180.0F}, 44.0F);
  if (!presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const auto overlay = measure_live_ink_circle(presenter, chrome, {276.0F, 304.0F}, 48.0F);
  const bool passed = clear.failures == 0U && overlay.failures == 0U &&
                      clear.presented_updates > 0U && overlay.presented_updates > 0U &&
                      overlay.maximum_chrome_us < 3'000 && overlay.maximum_submit_us < 16'667 &&
                      overlay.maximum_complete_us < 33'333;
  std::printf(
      "TINYDRAW_GATE1_LIVE_OVERLAY clear_updates=%lu clear_wall_max_us=%lld "
      "clear_submit_max_us=%lld clear_complete_max_us=%lld overlay_updates=%lu "
      "overlay_wall_max_us=%lld overlay_submit_max_us=%lld overlay_complete_max_us=%lld "
      "overlay_chrome_max_us=%lld clear_failures=%lu overlay_failures=%lu pass=%u\n",
      static_cast<unsigned long>(clear.presented_updates),
      static_cast<long long>(clear.maximum_wall_us),
      static_cast<long long>(clear.maximum_submit_us),
      static_cast<long long>(clear.maximum_complete_us),
      static_cast<unsigned long>(overlay.presented_updates),
      static_cast<long long>(overlay.maximum_wall_us),
      static_cast<long long>(overlay.maximum_submit_us),
      static_cast<long long>(overlay.maximum_complete_us),
      static_cast<long long>(overlay.maximum_chrome_us), static_cast<unsigned long>(clear.failures),
      static_cast<unsigned long>(overlay.failures), passed);
  std::fflush(stdout);
  return passed;
}

bool run_draw_while_fill_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const IncrementalDocumentWorkspace& workspace,
                              std::span<CompactOperationSample> interaction_samples) {
  const auto fallback = presenter.set_view(ZoomLevel::k400Percent, 0, 0, chrome, now_us());
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k400Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  const auto initial = producer.produce_next(view);
  if (!initial.has_value() || initial->complete || initial->tiles_published != 0U) {
    return false;
  }

  const std::uint32_t event_us = now_us();
  const std::int64_t blocked_started = esp_timer_get_time();
  const auto blocked_step = producer.produce_next(view);
  const std::int64_t poll_gap_us = esp_timer_get_time() - blocked_started;
  if (!blocked_step.has_value()) {
    return false;
  }
  const InkPoint preview{.position = {20.0F, 200.0F},
                         .pressure = 1.0F,
                         .radius = 20.0F,
                         .distance = 0.0F,
                         .running_length = 0.0F,
                         .timestamp_us = event_us};
  const auto live = presenter.show_start(preview, 0x001FU, chrome, event_us);

  if (interaction_samples.size() < 8U) {
    return false;
  }
  auto fast_xl = interaction_samples.first(8U);
  for (std::size_t index = 0; index < fast_xl.size(); ++index) {
    fast_xl[index] = {
        .x_quarter = static_cast<std::uint16_t>(20U + index * 48U),
        .y_quarter = static_cast<std::uint16_t>(index % 2U == 0U ? 180U : 240U),
        // XL is 20 screen pixels; at 400% that is 5 world units.
        .radius_256 = 1'280U,
        .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
    };
  }
  const std::int64_t append_started = esp_timer_get_time();
  const auto append = vector_v2::append_incrementally(
      log, canvas, {.tool = OperationTool::kPen, .color = 0x001FU, .samples = fast_xl}, workspace,
      {.priority_view = view});
  const std::int64_t append_us = esp_timer_get_time() - append_started;
  // A revision change now restarts stale producer state within produce_next;
  // the old contract returned nullopt and required a caller retry.
  const bool revision_restarted = producer.produce_next(view).has_value() &&
                                  log.current_revision() == canvas.current_revision();

  std::int64_t maximum_slice_us = poll_gap_us;
  std::int64_t maximum_compute_slice_us = poll_gap_us;
  std::int64_t fill_started = esp_timer_get_time();
  bool fill_complete = false;
  while (!fill_complete) {
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    const std::int64_t compute_slice_us = esp_timer_get_time() - step_started;
    maximum_slice_us = std::max(maximum_slice_us, compute_slice_us);
    if (!step.has_value()) {
      return false;
    }
    // Publication copies four packed tiles from PSRAM. It is bounded but not
    // replay compute; track replay-only slices separately from total blocking.
    if (step->tiles_published == 0U) {
      maximum_compute_slice_us = std::max(maximum_compute_slice_us, compute_slice_us);
    }
    if (step->tiles_published != 0U) {
      const std::int64_t present_started = esp_timer_get_time();
      if (!presenter.refresh_region(step->level_bounds, chrome).passed) {
        return false;
      }
      maximum_slice_us = std::max(maximum_slice_us, esp_timer_get_time() - present_started);
    }
    fill_complete = step->complete;
  }
  const std::int64_t fill_us = esp_timer_get_time() - fill_started;
  const bool passed = append.has_value() && revision_restarted && live.passed &&
                      live.first_submit_us < 100'000 && poll_gap_us < 35'000 &&
                      maximum_compute_slice_us < 30'000 && maximum_slice_us < 75'000;
  std::printf(
      "TINYDRAW_GATE1_DRAW_FILL zoom=400 revision=%lu append_us=%lld poll_gap_us=%lld "
      "event_submit_us=%lld event_complete_us=%lld max_compute_slice_us=%lld "
      "max_display_slice_us=%lld fill_us=%lld "
      "revision_restarted=%u pass=%u\n",
      static_cast<unsigned long>(canvas.current_revision().value),
      static_cast<long long>(append_us), static_cast<long long>(poll_gap_us),
      static_cast<long long>(live.first_submit_us), static_cast<long long>(live.first_complete_us),
      static_cast<long long>(maximum_compute_slice_us), static_cast<long long>(maximum_slice_us),
      static_cast<long long>(fill_us), revision_restarted, passed);
  return passed;
}

struct LongGestureMeasurement {
  std::size_t samples = 0;
  std::size_t chunks = 0;
  std::int64_t append_total_us = 0;
  std::int64_t append_max_us = 0;
  std::size_t fallback_pixels = 0;
  std::size_t settled_fallback_pixels = 0;
  bool committed = false;
  bool authority_match = false;
  bool refresh_passed = false;
};

// Streams one deterministic 400% XL gesture through the chained builder with
// the interactive 64-sample chunk policy and the caller-selected commit
// implementation, measuring every intermediate chunk commit.
template <typename CommitChunk>
bool run_long_gesture_pass(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                           OperationLog& log, MaterializedCanvas& canvas,
                           const vector_v2::ChromeState& chrome,
                           std::span<const std::uint16_t> blank_snapshot,
                           std::span<CompactOperationSample> builder_storage,
                           std::size_t chunk_sample_limit, CommitChunk&& commit_chunk,
                           LongGestureMeasurement& measurement) {
  const DocumentRevision baseline{canvas.current_revision().value + 1U};
  if (!vector_v2::restore_document_snapshot(log, canvas, baseline, blank_snapshot) ||
      !producer.reset_uniform_baseline(baseline)) {
    return false;
  }
  if (!presenter.set_view(ZoomLevel::k400Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k400Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  for (std::size_t step = 0; step < 100'000U; ++step) {
    if (step % 50U == 49U) {
      vTaskDelay(1);
    }
    const auto produced = producer.produce_next(view);
    if (!produced.has_value()) {
      return false;
    }
    if (produced->complete) {
      break;
    }
  }

  vector_v2::ChainedOperationBuilder builder(builder_storage, chunk_sample_limit);
  constexpr std::size_t kGestureSamples = 1'600;
  constexpr float kRadius = 5.0F;  // XL at 400%: 20 screen pixels.
  std::uint32_t timestamp_us = now_us();
  float x = 6.0F;
  float y = 8.0F;
  float direction = 1.0F;
  std::optional<vector_v2::PixelRect> world_bounds;
  const auto commit_ready = [&](vector_v2::ChainedOperationStatus status)
      -> std::optional<vector_v2::ChainedOperationStatus> {
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto pending = builder.pending_append();
      if (!pending.has_value()) {
        return std::nullopt;
      }
      const std::int64_t started_us = esp_timer_get_time();
      const auto committed = commit_chunk(*pending, view);
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      if (!committed.has_value()) {
        return std::nullopt;
      }
      measurement.append_total_us += elapsed_us;
      measurement.append_max_us = std::max(measurement.append_max_us, elapsed_us);
      ++measurement.chunks;
      if (!world_bounds.has_value()) {
        world_bounds = committed->affected_world_bounds;
      } else {
        world_bounds->x0 = std::min(world_bounds->x0, committed->affected_world_bounds.x0);
        world_bounds->y0 = std::min(world_bounds->y0, committed->affected_world_bounds.y0);
        world_bounds->x1 = std::max(world_bounds->x1, committed->affected_world_bounds.x1);
        world_bounds->y1 = std::max(world_bounds->y1, committed->affected_world_bounds.y1);
      }
      status = builder.acknowledge_commit();
    }
    if (status != vector_v2::ChainedOperationStatus::kAccepted &&
        status != vector_v2::ChainedOperationStatus::kComplete) {
      return std::nullopt;
    }
    return status;
  };

  if (!builder.begin(
          OperationTool::kPen, 0x001FU, 1U,
          {.world_x = x, .world_y = y, .radius = kRadius, .timestamp_us = timestamp_us})) {
    return false;
  }
  ++measurement.samples;
  for (std::size_t index = 1; index < kGestureSamples; ++index) {
    x += 1.6F * direction;
    if (x > 86.0F || x < 6.0F) {
      direction = -direction;
      x = std::clamp(x, 6.0F, 86.0F);
      y += 2.5F;
    }
    timestamp_us += 8'000U;
    const vector_v2::OperationPoint point{
        .world_x = x, .world_y = y, .radius = kRadius, .timestamp_us = timestamp_us};
    const bool final_sample = index + 1U == kGestureSamples;
    const auto status = final_sample ? builder.finish(point) : builder.add(point);
    const auto continued = commit_ready(status);
    if (!continued.has_value()) {
      return false;
    }
    ++measurement.samples;
  }
  measurement.committed = !builder.active();
  measurement.authority_match = log.current_revision() == canvas.current_revision();
  if (world_bounds.has_value()) {
    // Visible tiles are budget-exempt, so the pen-up refresh must already
    // show zero fallback; the settled refresh after producer completion
    // re-proves it.
    const auto refresh = presenter.refresh_region(
        vector_v2::operation_level_bounds(*world_bounds, ZoomLevel::k400Percent), chrome, now_us());
    measurement.fallback_pixels = refresh.fallback_pixels;
    for (std::size_t step = 0; step < 100'000U; ++step) {
      if (step % 50U == 49U) {
        vTaskDelay(1);
      }
      const auto produced = producer.produce_next(view);
      if (!produced.has_value()) {
        return false;
      }
      if (produced->complete) {
        break;
      }
    }
    const auto settled = presenter.refresh_region(
        vector_v2::operation_level_bounds(*world_bounds, ZoomLevel::k400Percent), chrome, now_us());
    measurement.refresh_passed = refresh.passed && settled.passed;
    measurement.settled_fallback_pixels = settled.fallback_pixels;
  }
  return true;
}

bool run_long_gesture_commit_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                  OperationLog& log, MaterializedCanvas& canvas,
                                  const vector_v2::ChromeState& chrome,
                                  const IncrementalDocumentWorkspace& workspace,
                                  const vector_v2::InPlaceAppendWorkspace& in_place_workspace,
                                  std::span<const std::uint16_t> blank_snapshot,
                                  std::span<CompactOperationSample> builder_storage) {
  LongGestureMeasurement reference{};
  const bool reference_ok = run_long_gesture_pass(
      presenter, producer, log, canvas, chrome, blank_snapshot, builder_storage, 64U,
      [&](const vector_v2::OperationAppend& chunk, const vector_v2::ViewRequest& view) {
        return vector_v2::append_incrementally(
            log, canvas, chunk, workspace,
            {.priority_view = view,
             .publication_scope = vector_v2::IncrementalPublicationScope::kPriorityView});
      },
      reference);
  LongGestureMeasurement in_place{};
  const bool in_place_ok = run_long_gesture_pass(
      presenter, producer, log, canvas, chrome, blank_snapshot, builder_storage,
      kInteractiveChunkSampleLimit,
      [&](const vector_v2::OperationAppend& chunk, const vector_v2::ViewRequest& view) {
        return vector_v2::append_incrementally_in_place(
            log, canvas, chunk, in_place_workspace, view,
            {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs});
      },
      in_place);
  const auto print_pass = [](const char* path, const LongGestureMeasurement& measurement,
                             bool run_ok) {
    std::printf(
        "TINYDRAW_GATE1_LONG_GESTURE path=%s samples=%lu chunks=%lu append_total_us=%lld "
        "append_max_us=%lld append_avg_us=%lld refresh_fallback_pixels=%lu "
        "settled_fallback_pixels=%lu committed=%u "
        "authority=%u refresh=%u run_ok=%u\n",
        path, static_cast<unsigned long>(measurement.samples),
        static_cast<unsigned long>(measurement.chunks),
        static_cast<long long>(measurement.append_total_us),
        static_cast<long long>(measurement.append_max_us),
        static_cast<long long>(measurement.chunks == 0U
                                   ? 0
                                   : measurement.append_total_us /
                                         static_cast<std::int64_t>(measurement.chunks)),
        static_cast<unsigned long>(measurement.fallback_pixels),
        static_cast<unsigned long>(measurement.settled_fallback_pixels), measurement.committed,
        measurement.authority_match, measurement.refresh_passed, run_ok);
  };
  print_pass("reference", reference, reference_ok);
  print_pass("in_place", in_place, in_place_ok);
  std::fflush(stdout);
  const auto correct = [](const LongGestureMeasurement& measurement) {
    return measurement.committed && measurement.authority_match && measurement.refresh_passed &&
           measurement.settled_fallback_pixels == 0U && measurement.chunks >= 24U;
  };
  // Visible tiles are exempt from the commit budget, so the interactive
  // path's pen-up refresh must show zero fallback: any visible fallback is
  // an on-glass blur, not an allowed transient. Intermediate commits must
  // also fit inside a 15 ms input-poll slice; the reference pass is a
  // measured comparison only.
  return reference_ok && in_place_ok && correct(reference) && correct(in_place) &&
         in_place.fallback_pixels == 0U && in_place.append_max_us < 15'000;
}

// Encodes the currently loaded document (the seed-7 realistic workload) to
// the export partition through the streamed authority renderer and verifies
// the stored PNG signature and header dimensions. USB stays untouched so the
// automated serial console survives; presenting the disk is the manual step.
std::uint32_t png_crc32(std::uint32_t crc, std::span<const std::uint8_t> bytes) {
  crc = ~crc;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

// Walks every PNG chunk and verifies its CRC32 plus a terminating IEND: a
// header-only check passed corrupt or truncated IDAT data.
bool verify_png_chunks(VectorV2Export& exporter, std::size_t total_bytes, std::size_t& chunks) {
  std::array<std::uint8_t, 4'096> buffer{};
  std::size_t offset = 8;  // signature
  bool saw_iend = false;
  while (offset + 12U <= total_bytes && !saw_iend) {
    std::array<std::uint8_t, 8> chunk_header{};
    if (!exporter.read_image(offset, chunk_header)) {
      return false;
    }
    const std::size_t length = static_cast<std::size_t>(chunk_header[0]) << 24U |
                               static_cast<std::size_t>(chunk_header[1]) << 16U |
                               static_cast<std::size_t>(chunk_header[2]) << 8U |
                               static_cast<std::size_t>(chunk_header[3]);
    if (offset + 12U + length > total_bytes) {
      return false;
    }
    // CRC covers the type tag and the payload.
    std::uint32_t crc = png_crc32(0U, std::span(chunk_header).subspan(4));
    std::size_t remaining = length;
    std::size_t payload_offset = offset + 8U;
    while (remaining > 0U) {
      const std::size_t take = std::min(remaining, buffer.size());
      if (!exporter.read_image(payload_offset, std::span(buffer).first(take))) {
        return false;
      }
      crc = png_crc32(crc, std::span<const std::uint8_t>(buffer).first(take));
      payload_offset += take;
      remaining -= take;
      // Feed the idle task; a 500 KiB IDAT walk would otherwise trip the
      // CPU0 watchdog.
      vTaskDelay(1);
    }
    std::array<std::uint8_t, 4> stored_crc{};
    if (!exporter.read_image(offset + 8U + length, stored_crc)) {
      return false;
    }
    const std::uint32_t expected = static_cast<std::uint32_t>(stored_crc[0]) << 24U |
                                   static_cast<std::uint32_t>(stored_crc[1]) << 16U |
                                   static_cast<std::uint32_t>(stored_crc[2]) << 8U |
                                   static_cast<std::uint32_t>(stored_crc[3]);
    if (crc != expected) {
      return false;
    }
    saw_iend = chunk_header[4] == 'I' && chunk_header[5] == 'E' && chunk_header[6] == 'N' &&
               chunk_header[7] == 'D';
    ++chunks;
    offset += 12U + length;
  }
  return saw_iend;
}

bool run_export_encode_gate(VectorV2Export& exporter, OperationLog& log) {
  const VectorV2ExportStats stats = exporter.encode(log);
  std::array<std::uint8_t, 24> header{};
  const bool header_read = stats.encoded && exporter.read_image(0, header);
  constexpr std::array<std::uint8_t, 8> kPngSignature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                      0x0DU, 0x0AU, 0x1AU, 0x0AU};
  const bool signature_ok =
      header_read && std::equal(kPngSignature.begin(), kPngSignature.end(), header.begin());
  const auto big_endian = [&header](std::size_t offset) {
    return static_cast<std::uint32_t>(header[offset]) << 24U |
           static_cast<std::uint32_t>(header[offset + 1U]) << 16U |
           static_cast<std::uint32_t>(header[offset + 2U]) << 8U |
           static_cast<std::uint32_t>(header[offset + 3U]);
  };
  const bool dimensions_ok =
      signature_ok && big_endian(16U) == static_cast<std::uint32_t>(vector_v2::kWorldWidth) &&
      big_endian(20U) == static_cast<std::uint32_t>(vector_v2::kWorldHeight);
  std::size_t chunks = 0;
  const bool chunks_ok = dimensions_ok && verify_png_chunks(exporter, stats.bytes, chunks);
  const bool passed =
      stats.encoded && stats.bytes > 64U && signature_ok && dimensions_ok && chunks_ok;
  std::printf(
      "TINYDRAW_GATE1_EXPORT encoded=%u bytes=%lu elapsed_us=%lld workspace_bytes=%lu "
      "band_bytes=%lu free_psram=%lu free_internal=%lu signature=%u dimensions=%u chunks=%lu "
      "chunks_ok=%u pass=%u\n",
      stats.encoded, static_cast<unsigned long>(stats.bytes),
      static_cast<long long>(stats.elapsed_us), static_cast<unsigned long>(stats.workspace_bytes),
      static_cast<unsigned long>(stats.band_bytes),
      static_cast<unsigned long>(stats.free_psram_after),
      static_cast<unsigned long>(stats.free_internal_after), signature_ok, dimensions_ok,
      static_cast<unsigned long>(chunks), chunks_ok, passed);
  std::fflush(stdout);
  return passed;
}

// Measures cache-retention value under eviction pressure: cold-fill a home
// view, tour distinct 400% viewports across the inked world, then tour back
// and count what must be re-produced. The return-trip refill work is the
// user-visible "cold render after panning back" cost; the protected home
// footprint must additionally return with zero missing tiles and zero
// fallback pixels. Runs against the loaded seed-7 document.
bool run_cache_tour_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                         MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome) {
  struct TourTotals {
    std::size_t steps = 0;
    std::size_t tiles_published = 0;
    std::int64_t wall_us = 0;
  };
  const auto fill_view = [&](const vector_v2::ViewRequest& view, TourTotals& totals) -> bool {
    const std::int64_t started = esp_timer_get_time();
    for (std::size_t step_index = 0; step_index < 100'000U; ++step_index) {
      const auto step = producer.produce_next(view);
      if (!step.has_value()) {
        return false;
      }
      ++totals.steps;
      totals.tiles_published += step->tiles_published;
      if (step->complete) {
        totals.wall_us += esp_timer_get_time() - started;
        return true;
      }
    }
    return false;
  };
  const auto view_at = [](ZoomLevel zoom, int x, int y) {
    return vector_v2::ViewRequest{
        .zoom = zoom,
        .level_pixels = {x, y, x + vector_v2::kOverviewWidth, y + vector_v2::kOverviewHeight},
    };
  };

  // Home view at 100% over inked content; presenter.set_view also registers
  // the protected footprint exactly like real navigation.
  if (!canvas.discard_tiles() ||
      !presenter.set_view(ZoomLevel::k100Percent, 63, 63, chrome, now_us()).passed) {
    return false;
  }
  TourTotals home_fill{};
  if (!fill_view(view_at(ZoomLevel::k100Percent, 63, 63), home_fill)) {
    return false;
  }

  constexpr std::array<int, 4> kTourX{200, 1'900, 3'600, 5'300};
  constexpr std::array<int, 4> kTourY{300, 2'400, 4'500, 6'600};
  TourTotals forward{};
  for (const int y : kTourY) {
    for (const int x : kTourX) {
      if (!presenter.set_view(ZoomLevel::k400Percent, x, y, chrome, now_us()).passed ||
          !fill_view(view_at(ZoomLevel::k400Percent, x, y), forward)) {
        return false;
      }
    }
  }

  // Return trip in reverse order: count what was evicted before refilling.
  TourTotals return_trip{};
  std::size_t return_missing_tiles = 0;
  for (std::size_t stop = kTourX.size() * kTourY.size(); stop-- > 0U;) {
    const int x = kTourX[stop % kTourX.size()];
    const int y = kTourY[stop / kTourX.size()];
    const auto view = view_at(ZoomLevel::k400Percent, x, y);
    const auto missing = producer.visible_tiles_remaining(view);
    if (!missing.has_value() ||
        !presenter.set_view(ZoomLevel::k400Percent, x, y, chrome, now_us()).passed) {
      return false;
    }
    return_missing_tiles += *missing;
    if (!fill_view(view, return_trip)) {
      return false;
    }
  }

  // Protected home footprint must return sharp with no producer work.
  const auto home_view = view_at(ZoomLevel::k100Percent, 63, 63);
  const auto home_missing = producer.visible_tiles_remaining(home_view);
  const auto home_return = presenter.set_view(ZoomLevel::k100Percent, 63, 63, chrome, now_us());
  const bool home_sharp = home_missing.has_value() && *home_missing == 0U && home_return.passed &&
                          home_return.fallback_pixels == 0U;
  const bool passed = home_sharp;
  std::printf(
      "TINYDRAW_GATE1_CACHE_TOUR slots=%lu stops=%lu forward_steps=%lu forward_tiles=%lu "
      "forward_wall_us=%lld return_missing_tiles=%lu return_steps=%lu return_tiles=%lu "
      "return_wall_us=%lld home_missing=%lu home_fallback_pixels=%lu pass=%u\n",
      static_cast<unsigned long>(canvas.slot_capacity()),
      static_cast<unsigned long>(kTourX.size() * kTourY.size()),
      static_cast<unsigned long>(forward.steps),
      static_cast<unsigned long>(forward.tiles_published), static_cast<long long>(forward.wall_us),
      static_cast<unsigned long>(return_missing_tiles),
      static_cast<unsigned long>(return_trip.steps),
      static_cast<unsigned long>(return_trip.tiles_published),
      static_cast<long long>(return_trip.wall_us),
      static_cast<unsigned long>(home_missing.value_or(999U)),
      static_cast<unsigned long>(home_return.fallback_pixels), passed);
  std::fflush(stdout);
  return passed;
}

// ---- Mixed-zoom drawing gate ----
//
// Deterministic reproduction of the manual-session drawing regression: a
// helpfully warm multi-zoom cache makes every committed chunk eagerly mutate
// resident raw tiles at every zoom, so interactive drawing gets slower the
// better the cache is doing its job. The gate warms four tiled-zoom viewports
// over the same dense seed-7 world corner, then draws and erases a
// boustrophedon XL gesture at every zoom through the product chunk policy and
// the product in-place commit call. Each 48-sample chunk spans a full
// viewport-width band, maximizing per-chunk cross-zoom tile fanout. The
// product alarm is 15 ms per chunk; the target is 10-12 ms.

struct MixedDrawStrokeStats {
  std::size_t chunks = 0;
  std::int64_t append_total_us = 0;
  std::int64_t append_max_us = 0;
  vector_v2::InPlaceAppendPhases phase_max{};
  std::size_t affected_tiles = 0;
  std::size_t published_tiles = 0;
  std::size_t fallback_tiles = 0;
  std::size_t visible_fallback_tiles = 0;
  bool committed = false;
  bool authority = false;
  bool refresh_passed = false;
};

const char* tool_name(OperationTool tool) {
  return tool == OperationTool::kEraser ? "eraser" : "pen";
}

// Every mixed-draw viewport is anchored at world (48, 48) so each zoom's
// resident footprint overlaps every stroke's world bounds. 25% is the
// complete overview and keeps its native origin.
int mixed_draw_level_origin(ZoomLevel zoom) {
  return zoom == ZoomLevel::k25Percent ? 0 : 48 * vector_v2::zoom_percent(zoom) / 100;
}

vector_v2::ViewRequest mixed_draw_view(ZoomLevel zoom) {
  const int origin = mixed_draw_level_origin(zoom);
  return {
      .zoom = zoom,
      .level_pixels = {origin, origin, origin + vector_v2::kOverviewWidth,
                       origin + vector_v2::kOverviewHeight},
  };
}

bool fill_view_to_completion(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                             std::size_t& tiles, std::int64_t& wall_us) {
  const std::int64_t started = esp_timer_get_time();
  for (std::size_t step_index = 0; step_index < 100'000U; ++step_index) {
    // Yield periodically so the CPU0 idle task feeds the task watchdog.
    if (step_index % 50U == 49U) {
      vTaskDelay(1);
    }
    const auto step = producer.produce_next(view);
    if (!step.has_value()) {
      return false;
    }
    tiles += step->tiles_published;
    if (step->complete) {
      wall_us += esp_timer_get_time() - started;
      return true;
    }
  }
  return false;
}

struct MixedDrawCensus {
  std::size_t raw = 0;
  std::size_t uniform = 0;
};

// lookup is const and does not touch recency, so the census cannot perturb
// eviction order.
MixedDrawCensus census_zoom_tiles(const MaterializedCanvas& canvas, ZoomLevel zoom) {
  MixedDrawCensus census;
  const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      if (!source.has_value()) {
        continue;
      }
      if (source->kind == vector_v2::SourceKind::kTileSlot) {
        ++census.raw;
      } else if (source->kind == vector_v2::SourceKind::kUniform) {
        ++census.uniform;
      }
    }
  }
  return census;
}

bool run_mixed_zoom_stroke(VectorV2Presenter& presenter, OperationLog& log,
                           MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome,
                           const vector_v2::InPlaceAppendWorkspace& workspace,
                           std::span<CompactOperationSample> builder_storage, ZoomLevel zoom,
                           OperationTool tool, std::uint16_t color, std::uint16_t gesture_id,
                           MixedDrawStrokeStats& stats) {
  const int origin = mixed_draw_level_origin(zoom);
  if (!presenter.set_view(zoom, origin, origin, chrome, now_us()).passed) {
    return false;
  }
  const auto view = mixed_draw_view(zoom);
  // Mirror the product coordinator exactly: no priority view at 25%.
  const std::optional<vector_v2::ViewRequest> priority_view =
      zoom == ZoomLevel::k25Percent ? std::optional<vector_v2::ViewRequest>{} : std::optional{view};
  const float scale = 100.0F / static_cast<float>(vector_v2::zoom_percent(zoom));
  // XL brush: 20 screen pixels at every zoom, like the product tool.
  const float radius = 20.0F * scale;
  // The eraser pass shifts down half a brush so it crosses the pen bands
  // instead of retracing identical pixels.
  const float start_offset = tool == OperationTool::kEraser ? radius * 0.5F : 0.0F;
  const float margin = radius + 2.0F;
  const float wx0 = static_cast<float>(origin) * scale;
  const float wy0 = wx0;
  const float x_min = wx0 + margin;
  const float x_max = wx0 + static_cast<float>(vector_v2::kOverviewWidth) * scale - margin;
  const float y_min = wy0 + margin + start_offset;
  const float y_max = wy0 + static_cast<float>(vector_v2::kOverviewHeight) * scale - margin;
  constexpr std::size_t kStrokeSamples = 1'536;
  constexpr std::size_t kSweeps = 32;
  // 48 samples per horizontal sweep: one interactive chunk spans one full
  // viewport-width band.
  const float dx = (x_max - x_min) / 47.0F;
  const float dy = (y_max - y_min) / static_cast<float>(kSweeps);

  vector_v2::ChainedOperationBuilder builder(builder_storage, kInteractiveChunkSampleLimit);
  std::optional<vector_v2::PixelRect> world_bounds;
  const auto commit_ready = [&](vector_v2::ChainedOperationStatus status)
      -> std::optional<vector_v2::ChainedOperationStatus> {
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto pending = builder.pending_append();
      if (!pending.has_value()) {
        return std::nullopt;
      }
      const std::int64_t started_us = esp_timer_get_time();
      // Mirror the product coordinator's commit budget exactly.
      const auto committed = vector_v2::append_incrementally_in_place(
          log, canvas, *pending, workspace, priority_view,
          {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs});
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      if (!committed.has_value()) {
        return std::nullopt;
      }
      stats.append_total_us += elapsed_us;
      stats.append_max_us = std::max(stats.append_max_us, elapsed_us);
      stats.phase_max.prepare_us =
          std::max(stats.phase_max.prepare_us, committed->phases.prepare_us);
      stats.phase_max.overview_us =
          std::max(stats.phase_max.overview_us, committed->phases.overview_us);
      stats.phase_max.enumerate_us =
          std::max(stats.phase_max.enumerate_us, committed->phases.enumerate_us);
      stats.phase_max.uniform_retain_us =
          std::max(stats.phase_max.uniform_retain_us, committed->phases.uniform_retain_us);
      stats.phase_max.raw_retain_us =
          std::max(stats.phase_max.raw_retain_us, committed->phases.raw_retain_us);
      stats.phase_max.commit_us = std::max(stats.phase_max.commit_us, committed->phases.commit_us);
      ++stats.chunks;
      stats.affected_tiles += committed->affected_resident_tiles;
      stats.published_tiles += committed->published_tiles;
      stats.fallback_tiles += committed->fallback_tiles;
      stats.visible_fallback_tiles += committed->visible_fallback_tiles;
      if (!world_bounds.has_value()) {
        world_bounds = committed->affected_world_bounds;
      } else {
        world_bounds->x0 = std::min(world_bounds->x0, committed->affected_world_bounds.x0);
        world_bounds->y0 = std::min(world_bounds->y0, committed->affected_world_bounds.y0);
        world_bounds->x1 = std::max(world_bounds->x1, committed->affected_world_bounds.x1);
        world_bounds->y1 = std::max(world_bounds->y1, committed->affected_world_bounds.y1);
      }
      status = builder.acknowledge_commit();
    }
    if (status != vector_v2::ChainedOperationStatus::kAccepted &&
        status != vector_v2::ChainedOperationStatus::kComplete) {
      return std::nullopt;
    }
    return status;
  };

  float x = x_min;
  float y = y_min;
  float direction = 1.0F;
  std::uint32_t timestamp_us = now_us();
  if (!builder.begin(
          tool, color, gesture_id,
          {.world_x = x, .world_y = y, .radius = radius, .timestamp_us = timestamp_us})) {
    return false;
  }
  for (std::size_t index = 1; index < kStrokeSamples; ++index) {
    x += dx * direction;
    if (x > x_max || x < x_min) {
      direction = -direction;
      x = std::clamp(x, x_min, x_max);
      y = std::min(y + dy, y_max);
    }
    timestamp_us += 8'000U;
    const vector_v2::OperationPoint point{
        .world_x = x, .world_y = y, .radius = radius, .timestamp_us = timestamp_us};
    const bool final_sample = index + 1U == kStrokeSamples;
    if (!commit_ready(final_sample ? builder.finish(point) : builder.add(point)).has_value()) {
      return false;
    }
  }
  stats.committed = !builder.active();
  stats.authority = log.current_revision() == canvas.current_revision();
  if (world_bounds.has_value()) {
    stats.refresh_passed =
        presenter
            .refresh_region(vector_v2::operation_level_bounds(*world_bounds, zoom), chrome,
                            now_us())
            .passed;
  }
  return true;
}

bool run_mixed_zoom_draw_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const vector_v2::InPlaceAppendWorkspace& workspace,
                              std::span<CompactOperationSample> builder_storage) {
  constexpr std::array kWarmZooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent,
                                  ZoomLevel::k200Percent, ZoomLevel::k400Percent};
  constexpr std::array kDrawZooms{ZoomLevel::k25Percent, ZoomLevel::k50Percent,
                                  ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                                  ZoomLevel::k400Percent};
  // Self-contained cache state: discard, then warm deterministically so the
  // measurement does not depend on whichever gate ran before this one.
  if (!canvas.discard_tiles()) {
    return false;
  }
  for (const ZoomLevel zoom : kWarmZooms) {
    const int origin = mixed_draw_level_origin(zoom);
    if (!presenter.set_view(zoom, origin, origin, chrome, now_us()).passed) {
      return false;
    }
    std::size_t tiles = 0;
    std::int64_t wall_us = 0;
    if (!fill_view_to_completion(producer, mixed_draw_view(zoom), tiles, wall_us)) {
      return false;
    }
    const MixedDrawCensus census = census_zoom_tiles(canvas, zoom);
    std::printf(
        "TINYDRAW_GATE1_MIXED_DRAW_WARM zoom=%s fill_tiles=%lu fill_us=%lld raw=%lu "
        "uniform=%lu\n",
        zoom_name(zoom), static_cast<unsigned long>(tiles), static_cast<long long>(wall_us),
        static_cast<unsigned long>(census.raw), static_cast<unsigned long>(census.uniform));
  }

  bool strokes_correct = true;
  bool timing_pass = true;
  std::int64_t worst_append_us = 0;
  std::size_t strokes = 0;
  std::uint16_t gesture_id = 1;
  for (const ZoomLevel zoom : kDrawZooms) {
    // Re-warm every tiled viewport before each zoom's stroke pair. Strokes
    // drop affected resident tiles (that is the accepted policy), so without
    // re-warming the later zooms would measure an empty cache instead of the
    // product nightmare: drawing at the active zoom with a fully warm
    // multi-zoom cache underneath.
    for (const ZoomLevel warm_zoom : kWarmZooms) {
      const int warm_origin = mixed_draw_level_origin(warm_zoom);
      if (!presenter.set_view(warm_zoom, warm_origin, warm_origin, chrome, now_us()).passed) {
        return false;
      }
      std::size_t warm_tiles = 0;
      std::int64_t warm_wall_us = 0;
      if (!fill_view_to_completion(producer, mixed_draw_view(warm_zoom), warm_tiles,
                                   warm_wall_us)) {
        return false;
      }
    }
    for (const OperationTool tool : {OperationTool::kPen, OperationTool::kEraser}) {
      MixedDrawStrokeStats stats{};
      const bool run_ok = run_mixed_zoom_stroke(
          presenter, log, canvas, chrome, workspace, builder_storage, zoom, tool,
          tool == OperationTool::kPen ? 0x001FU : 0x0000U, gesture_id++, stats);
      const bool correct = run_ok && stats.committed && stats.authority && stats.refresh_passed &&
                           stats.chunks >= 24U;
      // Visible tiles are budget-exempt: any dropped tile intersecting the
      // priority view is an on-glass blur. Off-view drops at the active zoom
      // are the accepted budget behavior (brush bleed past the viewport) and
      // idle repair rebuilds them. 25% has no priority view and stays exempt.
      const bool visible_sharp =
          zoom == ZoomLevel::k25Percent || stats.visible_fallback_tiles == 0U;
      const bool stroke_pass = correct && visible_sharp && stats.append_max_us < 15'000;
      std::printf(
          "TINYDRAW_GATE1_MIXED_DRAW zoom=%s tool=%s chunks=%lu append_max_us=%lld "
          "append_avg_us=%lld append_total_us=%lld affected_tiles=%lu published=%lu "
          "fallback=%lu visible_fallback=%lu ph_prepare_max_us=%lld ph_overview_max_us=%lld "
          "ph_enumerate_max_us=%lld ph_uniform_max_us=%lld ph_raw_max_us=%lld "
          "ph_commit_max_us=%lld committed=%u authority=%u refresh=%u run_ok=%u "
          "pass=%u\n",
          zoom_name(zoom), tool_name(tool), static_cast<unsigned long>(stats.chunks),
          static_cast<long long>(stats.append_max_us),
          static_cast<long long>(stats.chunks == 0U ? 0
                                                    : stats.append_total_us /
                                                          static_cast<std::int64_t>(stats.chunks)),
          static_cast<long long>(stats.append_total_us),
          static_cast<unsigned long>(stats.affected_tiles),
          static_cast<unsigned long>(stats.published_tiles),
          static_cast<unsigned long>(stats.fallback_tiles),
          static_cast<unsigned long>(stats.visible_fallback_tiles),
          static_cast<long long>(stats.phase_max.prepare_us),
          static_cast<long long>(stats.phase_max.overview_us),
          static_cast<long long>(stats.phase_max.enumerate_us),
          static_cast<long long>(stats.phase_max.uniform_retain_us),
          static_cast<long long>(stats.phase_max.raw_retain_us),
          static_cast<long long>(stats.phase_max.commit_us), stats.committed, stats.authority,
          stats.refresh_passed, run_ok, stroke_pass);
      std::fflush(stdout);
      worst_append_us = std::max(worst_append_us, stats.append_max_us);
      ++strokes;
      strokes_correct = strokes_correct && correct;
      timing_pass = timing_pass && stroke_pass;
      if (!run_ok) {
        return false;
      }
    }
  }

  // What did drawing cost the warm cache? A future mutation policy that
  // invalidates instead of painting must pay here, visibly.
  for (const ZoomLevel zoom : kWarmZooms) {
    const auto view = mixed_draw_view(zoom);
    const auto missing = producer.visible_tiles_remaining(view);
    const int origin = mixed_draw_level_origin(zoom);
    if (!missing.has_value() ||
        !presenter.set_view(zoom, origin, origin, chrome, now_us()).passed) {
      return false;
    }
    std::size_t tiles = 0;
    std::int64_t wall_us = 0;
    if (!fill_view_to_completion(producer, view, tiles, wall_us)) {
      return false;
    }
    std::printf(
        "TINYDRAW_GATE1_MIXED_DRAW_REVISIT zoom=%s missing=%lu refill_tiles=%lu "
        "refill_us=%lld\n",
        zoom_name(zoom), static_cast<unsigned long>(*missing), static_cast<unsigned long>(tiles),
        static_cast<long long>(wall_us));
  }

  const bool passed = strokes_correct && timing_pass;
  std::printf(
      "TINYDRAW_GATE1_MIXED_DRAW_SUMMARY slots=%lu strokes=%lu worst_append_us=%lld "
      "pass=%u\n",
      static_cast<unsigned long>(canvas.slot_capacity()), static_cast<unsigned long>(strokes),
      static_cast<long long>(worst_append_us), passed);
  std::fflush(stdout);
  return passed;
}

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
      "requested_clock_mhz=%d effective_clock_mhz=%d "
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
      selected_tear_edge_name(), requested_panel_clock_mhz(), effective_panel_clock_mhz(),
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

// FNV-1a over every identity's lookup kind at one zoom: a repair pass that
// swaps identities while preserving the resident count changes this.
std::uint64_t zoom_identity_signature(MaterializedCanvas& canvas, ZoomLevel zoom) {
  const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1'099'511'628'211ULL;
  };
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      mix(static_cast<std::uint64_t>(row) << 32U | static_cast<std::uint64_t>(column));
      mix(source.has_value() ? static_cast<std::uint64_t>(source->kind) + 1U : 0U);
    }
  }
  return hash;
}

std::size_t count_zoom_fallback(MaterializedCanvas& canvas, ZoomLevel zoom) {
  const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
  std::size_t fallback = 0;
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      if (!source.has_value() || source->kind == vector_v2::SourceKind::kOverview) {
        ++fallback;
      }
    }
  }
  return fallback;
}

// Deterministic pseudo-random stream for the hairline corpus: reproducible
// receipts without grid regularity.
struct HairlineRandom {
  std::uint32_t state = 0x5EED7u;
  std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  // Uniform in [low, high).
  float range(float low, float high) {
    return low + (high - low) * (static_cast<float>(next() & 0xFFFFFFu) / 16'777'216.0F);
  }
};

struct HairlineAppendStats {
  std::size_t operations = 0;
  std::size_t samples = 0;
  std::int64_t append_total_us = 0;
  std::int64_t append_max_us = 0;
};

// One wandering stroke committed as chained operations of at most 12 samples
// sharing endpoints, mirroring interactive chunked commits.
bool append_hairline_stroke(OperationLog& log, MaterializedCanvas& canvas,
                            const IncrementalDocumentWorkspace& workspace, HairlineRandom& random,
                            float radius, std::uint16_t color, OperationTool tool,
                            std::uint16_t gesture_id, float length, HairlineAppendStats& stats) {
  constexpr std::size_t kChunkSamples = 12;
  const float margin = radius + 2.0F;
  float x = random.range(margin, static_cast<float>(vector_v2::kWorldWidth) - margin);
  float y = random.range(margin, static_cast<float>(vector_v2::kWorldHeight) - margin);
  float angle = random.range(0.0F, 6.2831853F);
  float remaining = length;
  std::uint16_t elapsed_ms = 0;
  bool continuing = false;
  std::array<CompactOperationSample, kChunkSamples> chunk{};
  while (remaining > 0.0F) {
    std::size_t count = 0;
    if (continuing) {
      // Chained chunks share endpoints: the first sample repeats the
      // previous chunk's final position so no segment is skipped.
      chunk[count++] = {
          .x_quarter = static_cast<std::uint16_t>(x * 4.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 4.0F),
          .radius_256 = static_cast<std::uint16_t>(radius * 256.0F),
          .elapsed_ms = elapsed_ms,
      };
    }
    while (count < kChunkSamples && remaining > 0.0F) {
      if (continuing || count != 0U) {
        const float step = random.range(24.0F, 40.0F);
        angle += random.range(-0.15F, 0.15F);
        x = std::clamp(x + step * std::cos(angle), margin,
                       static_cast<float>(vector_v2::kWorldWidth) - margin);
        y = std::clamp(y + step * std::sin(angle), margin,
                       static_cast<float>(vector_v2::kWorldHeight) - margin);
        elapsed_ms = static_cast<std::uint16_t>(elapsed_ms + 8U);
        remaining -= step;
      }
      chunk[count++] = {
          .x_quarter = static_cast<std::uint16_t>(x * 4.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 4.0F),
          .radius_256 = static_cast<std::uint16_t>(radius * 256.0F),
          .elapsed_ms = elapsed_ms,
      };
    }
    continuing = true;
    const std::int64_t append_started = esp_timer_get_time();
    const auto result = vector_v2::append_incrementally(log, canvas,
                                                        {.tool = tool,
                                                         .color = color,
                                                         .gesture_id = gesture_id,
                                                         .samples = std::span(chunk.data(), count)},
                                                        workspace);
    const std::int64_t append_us = esp_timer_get_time() - append_started;
    if (!result.has_value()) {
      return false;
    }
    ++stats.operations;
    stats.samples += count;
    stats.append_total_us += append_us;
    stats.append_max_us = std::max(stats.append_max_us, append_us);
  }
  return true;
}

// Sarah's evil corpus: lots of somewhat-random thin strokes drawn at 25%
// (1.3-2 world px), a thin layer at 50% pen width (3.5-4.7 px), and a few
// thick sweeps with erasers mixed in. Dense hairlines defeat uniform-tile
// coverage, so this is the capacity worst case for the raw slot pool.
bool append_hairline_document(OperationLog& log, MaterializedCanvas& canvas,
                              const IncrementalDocumentWorkspace& workspace,
                              HairlineAppendStats& stats) {
  constexpr std::array<std::uint16_t, 6> kColors{0x0000U, 0x001FU, 0xF800U,
                                                 0x07E0U, 0x4208U, 0x8010U};
  HairlineRandom random;
  std::uint16_t gesture_id = 7'000;
  for (int stroke = 0; stroke < 220; ++stroke) {
    const bool eraser = (random.next() % 12U) == 0U;
    if (!append_hairline_stroke(log, canvas, workspace, random, random.range(1.3F, 2.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? OperationTool::kEraser : OperationTool::kPen, gesture_id++,
                                random.range(300.0F, 1'400.0F), stats)) {
      return false;
    }
  }
  for (int stroke = 0; stroke < 60; ++stroke) {
    if (!append_hairline_stroke(log, canvas, workspace, random, random.range(3.5F, 4.7F),
                                kColors[random.next() % kColors.size()], OperationTool::kPen,
                                gesture_id++, random.range(200.0F, 800.0F), stats)) {
      return false;
    }
  }
  for (int stroke = 0; stroke < 10; ++stroke) {
    const bool eraser = stroke == 4 || stroke == 9;
    if (!append_hairline_stroke(log, canvas, workspace, random, random.range(40.0F, 80.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? OperationTool::kEraser : OperationTool::kPen, gesture_id++,
                                random.range(800.0F, 2'000.0F), stats)) {
      return false;
    }
  }
  return true;
}

bool append_general_cold_document(OperationLog& log, MaterializedCanvas& canvas,
                                  const IncrementalDocumentWorkspace& workspace) {
  if (!append_adversarial_tapered_document(log, canvas, workspace)) {
    return false;
  }
  HairlineAppendStats hairline_stats{};
  const std::int64_t started = esp_timer_get_time();
  const bool appended = append_hairline_document(log, canvas, workspace, hairline_stats);
  std::printf(
      "TINYDRAW_GENERAL_COLD_WORKLOAD operations=%lu samples=%lu hairline_operations=%lu "
      "hairline_samples=%lu hairline_load_us=%lld appended=%u\n",
      static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()),
      static_cast<unsigned long>(hairline_stats.operations),
      static_cast<unsigned long>(hairline_stats.samples),
      static_cast<long long>(esp_timer_get_time() - started), appended);
  return appended;
}

struct MeasuredFill {
  std::size_t tiles = 0;
  std::size_t steps = 0;
  std::int64_t wall_us = 0;
  std::int64_t worst_step_us = 0;
  bool complete = false;
};

bool fill_view_measured(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                        MeasuredFill& fill) {
  const std::int64_t started = esp_timer_get_time();
  for (std::size_t step_index = 0; step_index < 200'000U; ++step_index) {
    // Yield periodically so the CPU0 idle task feeds the task watchdog; the
    // yield sits outside step timing so worst_step_us stays honest.
    if (step_index % 50U == 49U) {
      vTaskDelay(1);
    }
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    const std::int64_t step_us = esp_timer_get_time() - step_started;
    if (!step.has_value()) {
      return false;
    }
    ++fill.steps;
    fill.tiles += step->tiles_published;
    fill.worst_step_us = std::max(fill.worst_step_us, step_us);
    if (step->complete) {
      fill.wall_us = esp_timer_get_time() - started;
      fill.complete = true;
      return true;
    }
  }
  return false;
}

// The capacity worst case on the battery: dense hairline documents exceed
// the raw slot pool at 100%, so this gate records the fresh-cold fill
// costs, proves the idle-repair saturation guard stops without churn, and
// measures the felt cost of edge panning at 100% after a guarded repair.
bool run_hairline_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                       OperationLog& log, MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                       const vector_v2::ChromeState& chrome,
                       const IncrementalDocumentWorkspace& workspace,
                       std::span<const std::uint16_t> blank_snapshot) {
  const DocumentRevision baseline{canvas.current_revision().value + 1U};
  if (!vector_v2::restore_document_snapshot(log, canvas, baseline, blank_snapshot) ||
      !producer.reset_uniform_baseline(baseline)) {
    return false;
  }
  HairlineAppendStats append_stats{};
  if (!append_hairline_document(log, canvas, workspace, append_stats)) {
    return false;
  }
  std::printf("TINYDRAW_HAIRLINE_DOC ops=%lu samples=%lu append_total_us=%lld append_max_us=%lld\n",
              static_cast<unsigned long>(append_stats.operations),
              static_cast<unsigned long>(append_stats.samples),
              static_cast<long long>(append_stats.append_total_us),
              static_cast<long long>(append_stats.append_max_us));
  // Fresh-cold single-view fills, the adversarial-comparable numbers.
  const vector_v2::ViewRequest center_100{
      .zoom = ZoomLevel::k100Percent,
      .level_pixels = {552, 672, 552 + vector_v2::kOverviewWidth, 672 + vector_v2::kOverviewHeight},
  };
  bool fills_ok =
      run_paced_cold_gate(presenter, producer, canvas, touch, chrome, ZoomLevel::k100Percent, 552,
                          672, "evil_hairlines_capacity", contract::kColdViewportRequiredUs);
  fills_ok =
      run_paced_cold_gate(presenter, producer, canvas, touch, chrome, ZoomLevel::k400Percent, 2'760,
                          3'360, "evil_hairlines_capacity", contract::kColdViewportRequiredUs) &&
      fills_ok;
  // Guarded idle repair from a quiet moment at 100%, then a second plan
  // pass: saturation must stop the sweep without churning the pool.
  bool repair_ok = canvas.discard_tiles() &&
                   presenter.set_view(ZoomLevel::k100Percent, 552, 672, chrome, now_us()).passed;
  MeasuredFill active_fill{};
  repair_ok = repair_ok && fill_view_measured(producer, center_100, active_fill);
  std::int64_t repair_wall = 0;
  std::size_t repair_steps = 0;
  std::int64_t repair_worst = 0;
  bool grid_stopped = false;
  const auto run_guarded_plan = [&](std::size_t& steps, std::int64_t& wall,
                                    std::int64_t& worst) -> bool {
    const auto plan = vector_v2::plan_idle_repair(center_100, canvas.recent_views());
    const std::int64_t plan_started = esp_timer_get_time();
    for (std::size_t index = 0; index < plan.count; ++index) {
      if (index >= plan.grid_start &&
          canvas.resident_raw_tiles() + kRepairSaturationHeadroomTiles >= canvas.slot_capacity()) {
        grid_stopped = true;
        break;
      }
      MeasuredFill view_fill{};
      if (!fill_view_measured(producer, plan.views[index], view_fill)) {
        return false;
      }
      steps += view_fill.steps;
      worst = std::max(worst, view_fill.worst_step_us);
    }
    wall += esp_timer_get_time() - plan_started;
    return true;
  };
  repair_ok = repair_ok && run_guarded_plan(repair_steps, repair_wall, repair_worst);
  const std::size_t resident_after_pass1 = canvas.resident_raw_tiles();
  const std::uint64_t signature_pass1 = zoom_identity_signature(canvas, ZoomLevel::k100Percent);
  std::size_t pass2_steps = 0;
  std::int64_t pass2_wall = 0;
  std::int64_t pass2_worst = 0;
  repair_ok = repair_ok && run_guarded_plan(pass2_steps, pass2_wall, pass2_worst);
  const std::size_t resident_after_pass2 = canvas.resident_raw_tiles();
  // Identity signature, not resident count: replacing every identity while
  // preserving the count is still churn.
  const bool no_churn = resident_after_pass1 == resident_after_pass2 &&
                        signature_pass1 == zoom_identity_signature(canvas, ZoomLevel::k100Percent);
  std::printf(
      "TINYDRAW_HAIRLINE_REPAIR steps=%lu wall_us=%lld worst_step_us=%lld resident=%lu/%lu "
      "grid_stopped=%u pass2_steps=%lu pass2_wall_us=%lld no_churn=%u\n",
      static_cast<unsigned long>(repair_steps), static_cast<long long>(repair_wall),
      static_cast<long long>(repair_worst), static_cast<unsigned long>(resident_after_pass1),
      static_cast<unsigned long>(canvas.slot_capacity()), grid_stopped,
      static_cast<unsigned long>(pass2_steps), static_cast<long long>(pass2_wall), no_churn);
  // Edge tour at 100% after the guarded repair: the felt cost of edge
  // panning on a dense document. Producer-only by design: it prices the
  // cold compute, not presentation.
  constexpr std::array<std::array<int, 2>, 8> kTourStops{{{0, 0},
                                                          {1'104, 0},
                                                          {0, 1'344},
                                                          {1'104, 1'344},
                                                          {552, 0},
                                                          {0, 672},
                                                          {1'104, 672},
                                                          {552, 1'344}}};
  std::int64_t tour_total = 0;
  std::int64_t tour_worst_stop = 0;
  std::int64_t tour_worst_step = 0;
  bool tour_ok = repair_ok;
  for (const auto& stop : kTourStops) {
    if (!tour_ok) {
      break;
    }
    const vector_v2::ViewRequest view{
        .zoom = ZoomLevel::k100Percent,
        .level_pixels = {stop[0], stop[1], stop[0] + vector_v2::kOverviewWidth,
                         stop[1] + vector_v2::kOverviewHeight},
    };
    MeasuredFill stop_fill{};
    tour_ok =
        presenter.set_view(ZoomLevel::k100Percent, stop[0], stop[1], chrome, now_us()).passed &&
        fill_view_measured(producer, view, stop_fill);
    tour_total += stop_fill.wall_us;
    tour_worst_stop = std::max(tour_worst_stop, stop_fill.wall_us);
    tour_worst_step = std::max(tour_worst_step, stop_fill.worst_step_us);
  }
  std::printf(
      "TINYDRAW_HAIRLINE_TOUR stops=%lu total_us=%lld worst_stop_us=%lld worst_step_us=%lld\n",
      static_cast<unsigned long>(kTourStops.size()), static_cast<long long>(tour_total),
      static_cast<long long>(tour_worst_stop), static_cast<long long>(tour_worst_step));
  const MixedDrawCensus census = census_zoom_tiles(canvas, ZoomLevel::k100Percent);
  const std::size_t fallback = count_zoom_fallback(canvas, ZoomLevel::k100Percent);
  const std::int64_t worst_step = std::max({repair_worst, pass2_worst, tour_worst_step});
  // This corpus always saturates the pool, so the guard must have engaged.
  const bool passed =
      fills_ok && repair_ok && tour_ok && no_churn && grid_stopped && worst_step < 15'000;
  std::printf(
      "TINYDRAW_GATE1_HAIRLINE raw=%lu uniform=%lu fallback=%lu capacity=%lu worst_step_us=%lld "
      "grid_stopped=%u pass=%u\n",
      static_cast<unsigned long>(census.raw), static_cast<unsigned long>(census.uniform),
      static_cast<unsigned long>(fallback), static_cast<unsigned long>(canvas.slot_capacity()),
      static_cast<long long>(worst_step), grid_stopped, passed);
  return passed;
}

// Encodes the idle-repair product promise: drawing at 25% drops cross-zoom
// tiles by design (the in-place commit budget), so after one quiet moment
// the 100% level must be fully repaired and edge panning meets zero cold
// fallback. Every producer slice stays under the 15 ms input-poll alarm.
bool run_idle_repair_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                          OperationLog& log, MaterializedCanvas& canvas,
                          const vector_v2::ChromeState& chrome,
                          const vector_v2::InPlaceAppendWorkspace& workspace,
                          std::span<CompactOperationSample> builder_storage) {
  if (!canvas.discard_tiles()) {
    return false;
  }
  const int origin = mixed_draw_level_origin(ZoomLevel::k100Percent);
  if (!presenter.set_view(ZoomLevel::k100Percent, origin, origin, chrome, now_us()).passed) {
    return false;
  }
  std::size_t warm_tiles = 0;
  std::int64_t warm_us = 0;
  if (!fill_view_to_completion(producer, mixed_draw_view(ZoomLevel::k100Percent), warm_tiles,
                               warm_us)) {
    return false;
  }
  // The Sarah scenario: an XL 25% stroke sweeps the world and drops warm
  // tiles at every other zoom.
  MixedDrawStrokeStats stroke_stats{};
  if (!run_mixed_zoom_stroke(presenter, log, canvas, chrome, workspace, builder_storage,
                             ZoomLevel::k25Percent, OperationTool::kPen, 0x001FU, 5'000,
                             stroke_stats) ||
      !stroke_stats.committed) {
    return false;
  }
  // Back at 100% with the visible fill complete: exactly the state the
  // product loop reaches before its idle-repair branch runs.
  if (!presenter.set_view(ZoomLevel::k100Percent, origin, origin, chrome, now_us()).passed) {
    return false;
  }
  const auto active_view = mixed_draw_view(ZoomLevel::k100Percent);
  std::size_t refill_tiles = 0;
  std::int64_t refill_us = 0;
  if (!fill_view_to_completion(producer, active_view, refill_tiles, refill_us)) {
    return false;
  }
  const std::size_t damaged = count_zoom_fallback(canvas, ZoomLevel::k100Percent);
  const auto plan = vector_v2::plan_idle_repair(active_view, canvas.recent_views());
  const std::int64_t repair_started = esp_timer_get_time();
  std::size_t repair_steps = 0;
  std::int64_t worst_step_us = 0;
  for (std::size_t index = 0; index < plan.count; ++index) {
    for (std::size_t guard = 0; guard < 100'000U; ++guard) {
      // Yield periodically so the CPU0 idle task feeds the task watchdog.
      if (guard % 50U == 49U) {
        vTaskDelay(1);
      }
      const std::int64_t step_started = esp_timer_get_time();
      const auto step = producer.produce_next(plan.views[index]);
      const std::int64_t step_us = esp_timer_get_time() - step_started;
      worst_step_us = std::max(worst_step_us, step_us);
      if (!step.has_value()) {
        return false;
      }
      ++repair_steps;
      if (step->complete) {
        break;
      }
    }
  }
  const std::int64_t repair_us = esp_timer_get_time() - repair_started;
  const std::size_t remaining = count_zoom_fallback(canvas, ZoomLevel::k100Percent);
  // Zero fallback identities across the whole 100% level means any pan
  // destination composes without cold work: the border tour is implied.
  const bool passed = damaged != 0U && remaining == 0U && worst_step_us < 15'000;
  std::printf(
      "TINYDRAW_GATE1_IDLE_REPAIR damaged=%lu remaining=%lu plan_views=%lu steps=%lu "
      "repair_us=%lld worst_step_us=%lld warm_tiles=%lu pass=%u\n",
      static_cast<unsigned long>(damaged), static_cast<unsigned long>(remaining),
      static_cast<unsigned long>(plan.count), static_cast<unsigned long>(repair_steps),
      static_cast<long long>(repair_us), static_cast<long long>(worst_step_us),
      static_cast<unsigned long>(warm_tiles), passed);
  return passed;
}

bool verify_export_reserve() {
  const std::size_t free_before = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_before = heap_caps_get_largest_free_block(kExternalCaps);
  void* reserve = heap_caps_malloc(vector_v2::kTargetContiguousReserveBytes, kExternalCaps);
  const std::size_t free_held = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_held = heap_caps_get_largest_free_block(kExternalCaps);
  const bool passed = reserve != nullptr;
  heap_caps_free(reserve);
  std::printf(
      "TINYDRAW_EXPORT_RESERVE requested=%lu free_before=%lu largest_before=%lu free_held=%lu "
      "largest_held=%lu pass=%u\n",
      static_cast<unsigned long>(vector_v2::kTargetContiguousReserveBytes),
      static_cast<unsigned long>(free_before), static_cast<unsigned long>(largest_before),
      static_cast<unsigned long>(free_held), static_cast<unsigned long>(largest_held), passed);
  return passed;
}

bool append_stress_document(OperationLog& log, MaterializedCanvas& canvas,
                            const IncrementalDocumentWorkspace& workspace) {
  std::array<CompactOperationSample, kStressSamplesPerOperation> samples{};
  const std::int64_t started = esp_timer_get_time();
  std::int64_t maximum_us = 0;
  for (std::uint32_t operation = 0; operation < kStressOperations; ++operation) {
    const float base_x = 24.0F + static_cast<float>((operation * 47U) % 1'360U);
    const float base_y = 24.0F + static_cast<float>((operation * 73U) % 1'680U);
    for (std::uint32_t index = 0; index < kStressSamplesPerOperation; ++index) {
      const float x = std::clamp(base_x + static_cast<float>(index) * 2.25F, 0.0F,
                                 static_cast<float>(vector_v2::kWorldWidth));
      const int wave = static_cast<int>((operation + index) % 9U) - 4;
      const float y = std::clamp(base_y + static_cast<float>(wave * 3), 0.0F,
                                 static_cast<float>(vector_v2::kWorldHeight));
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>(x * 4.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 4.0F),
          .radius_256 = static_cast<std::uint16_t>((3U + operation % 6U) * 256U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    const std::int64_t append_started = esp_timer_get_time();
    const auto result = vector_v2::append_incrementally(
        log, canvas,
        {.tool = operation % 11U == 10U ? OperationTool::kEraser : OperationTool::kPen,
         .color = static_cast<std::uint16_t>(0x1800U + (operation * 97U) % 0xCFFFU),
         .samples = samples},
        workspace);
    maximum_us = std::max(maximum_us, esp_timer_get_time() - append_started);
    if (!result.has_value()) {
      std::printf("TINYDRAW_LIVE_STRESS_FAIL operation=%lu revision=%lu\n",
                  static_cast<unsigned long>(operation),
                  static_cast<unsigned long>(canvas.current_revision().value));
      return false;
    }
  }
  const std::int64_t elapsed = esp_timer_get_time() - started;
  std::printf(
      "TINYDRAW_LIVE_STRESS operations=%lu samples=%lu total_us=%lld average_us=%lld "
      "maximum_us=%lld free_psram=%lu largest_psram=%lu\n",
      static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<long long>(elapsed),
      static_cast<long long>(elapsed / kStressOperations), static_cast<long long>(maximum_us),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
  return true;
}

}  // namespace

bool run_vector_v2_gate_harness(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                OperationLog& log, MaterializedCanvas& canvas,
                                VectorV2TouchSampler& touch, const vector_v2::ChromeState& chrome,
                                const IncrementalDocumentWorkspace& workspace,
                                const vector_v2::InPlaceAppendWorkspace& in_place_workspace,
                                VectorV2Export& exporter,
                                std::span<const std::uint16_t> blank_snapshot,
                                std::span<CompactOperationSample> conversion_storage,
                                std::span<std::uint16_t> packed_tile_pixels) {
  const bool stress_ready = append_stress_document(log, canvas, workspace);
  const bool stress_100 = stress_ready && run_tile_gate(presenter, producer, log, canvas, chrome,
                                                        ZoomLevel::k100Percent);
  const bool stress_400 =
      stress_100 && run_tile_gate(presenter, producer, log, canvas, chrome, ZoomLevel::k400Percent);
  const DocumentRevision overlap_baseline{canvas.current_revision().value + 1U};
  const bool reset_for_overlap =
      stress_400 &&
      vector_v2::restore_document_snapshot(log, canvas, overlap_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(overlap_baseline);
  const bool overlap_ready =
      reset_for_overlap && append_overlapping_scribble(log, canvas, workspace);
  const bool overlap_cold =
      overlap_ready && run_overlap_cold_gates(presenter, producer, canvas, touch, chrome);

  const DocumentRevision general_cold_baseline{canvas.current_revision().value + 1U};
  const bool reset_for_general_cold =
      overlap_ready &&
      vector_v2::restore_document_snapshot(log, canvas, general_cold_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(general_cold_baseline);
  const bool general_cold_ready =
      reset_for_general_cold && append_general_cold_document(log, canvas, workspace);
  const bool general_cold =
      general_cold_ready && run_general_cold_gates(presenter, producer, canvas, touch, chrome);

  const DocumentRevision realistic_baseline{canvas.current_revision().value + 1U};
  // Continue collecting independent receipts even when the general cold timing
  // gate is red; the final verdict still requires it.
  const bool reset_for_realistic =
      general_cold_ready &&
      vector_v2::restore_document_snapshot(log, canvas, realistic_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(realistic_baseline);

  auto realistic_strokes = allocate_external<VectorStroke>(kRealisticStrokeCapacity);
  auto realistic_samples = allocate_external<StrokeSample>(kRealisticSampleCapacity);
  const bool corpus_allocated = realistic_strokes != nullptr && realistic_samples != nullptr;
  const bool workload_ready =
      reset_for_realistic && corpus_allocated &&
      load_realistic_document(
          log, canvas, workspace, std::span(realistic_strokes.get(), kRealisticStrokeCapacity),
          std::span(realistic_samples.get(), kRealisticSampleCapacity), conversion_storage);
  // The generator slabs are startup-only. Release them before the live memory
  // and export-reserve gates so the harness measures the product allocation.
  realistic_strokes.reset();
  realistic_samples.reset();
#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
  const bool census =
      workload_ready && run_vector_v2_tile_census(producer, canvas, packed_tile_pixels);
  std::printf("TINYDRAW_TILE_CENSUS_APP_DONE workload=%u census=%u revision=%lu\n", workload_ready,
              census, static_cast<unsigned long>(canvas.current_revision().value));
  std::fflush(stdout);
  return census;
#elif defined(TINYDRAW_VECTOR_V2_TEARING_PROBE)
  return workload_ready && run_tearing_probe(presenter, chrome);
#else
  constexpr int kUnalignedOrigin = vector_v2::kTileWidth - 1;
  const bool paced_cold =
      workload_ready &&
      run_paced_cold_gate(presenter, producer, canvas, touch, chrome, ZoomLevel::k400Percent,
                          kUnalignedOrigin, kUnalignedOrigin, "seed7",
                          contract::kColdViewportRequiredUs);
  const bool gate_100 =
      paced_cold && run_tile_gate(presenter, producer, log, canvas, chrome, ZoomLevel::k100Percent);
  const bool live_overlay =
      gate_100 && run_overlay_canvas_purity_gate(presenter, log, canvas, chrome, workspace) &&
      run_live_ink_overlay_gate(presenter, chrome) &&
      run_edge_ink_case(presenter, producer, log, canvas, chrome, workspace, packed_tile_pixels);
  // Pan gates are part of the final verdict; downstream gates still key on
  // the last state-producing gate so a red pan number cannot stop later
  // receipts from reporting.
  const bool pan_100 =
      live_overlay && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k100Percent);
  const bool gate_400 = live_overlay && run_tile_gate(presenter, producer, log, canvas, chrome,
                                                      ZoomLevel::k400Percent);
  const bool pan_400 =
      gate_400 && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k400Percent);
  const bool pan_sequence_100 =
      gate_400 && run_pan_sequence_gate(presenter, producer, chrome, ZoomLevel::k100Percent);
  const bool pan_sequence_400 =
      gate_400 && run_pan_sequence_gate(presenter, producer, chrome, ZoomLevel::k400Percent);
  const bool pan_sequence = pan_sequence_100 && pan_sequence_400;
  const bool pan_boundary_100 =
      gate_400 && run_pan_boundary_gate(presenter, chrome, ZoomLevel::k100Percent);
  const bool pan_boundary_400 =
      gate_400 && run_pan_boundary_gate(presenter, chrome, ZoomLevel::k400Percent);
  const bool pan_boundary = pan_boundary_100 && pan_boundary_400;
  const bool draw_fill =
      gate_400 && run_draw_while_fill_gate(presenter, producer, log, canvas, chrome, workspace,
                                           conversion_storage);
  // Cache gates run against the rich seed-7 document; the long-gesture gate
  // resets the document and therefore runs after them.
  const bool cache_retention =
      draw_fill && run_cache_retention_gate(presenter, producer, canvas, chrome);
  const bool full_world_cache = cache_retention && run_full_world_cache_gate(producer, canvas);
  const auto print_rerender_ledger = [&canvas](const char* site) {
    if (canvas.rerender_ledger() == nullptr) {
      return;
    }
    const auto ledger_totals = canvas.rerender_ledger()->totals();
    std::printf(
        "TINYDRAW_RERENDER_LEDGER site=%s renders=%lu unique=%lu amplification=%.3f "
        "cold=%lu damage=%lu evict=%lu stale=%lu unexplained=%lu\n",
        site, static_cast<unsigned long>(ledger_totals.renders),
        static_cast<unsigned long>(ledger_totals.unique_groups), ledger_totals.amplification(),
        static_cast<unsigned long>(ledger_totals.cold_miss),
        static_cast<unsigned long>(ledger_totals.expected_damage),
        static_cast<unsigned long>(ledger_totals.eviction),
        static_cast<unsigned long>(ledger_totals.stale_revision),
        static_cast<unsigned long>(ledger_totals.unexplained));
    std::fflush(stdout);
  };
  const bool cache_tour =
      full_world_cache && run_cache_tour_gate(presenter, producer, canvas, chrome);
  print_rerender_ledger("cache_tour");
  // The mixed-zoom drawing gate is part of the final verdict: warm-cache
  // interactive chunk commits must stay under the 15 ms alarm at every zoom.
  // It still must not stop later receipts when red.
  const bool mixed_draw =
      cache_tour && run_mixed_zoom_draw_gate(presenter, producer, log, canvas, chrome,
                                             in_place_workspace, conversion_storage);
  // Idle repair rides the same rich document; it discards and rebuilds its
  // own cache state, so mixed-draw's drops cannot skew it.
  const bool idle_repair =
      cache_tour && run_idle_repair_gate(presenter, producer, log, canvas, chrome,
                                         in_place_workspace, conversion_storage);
  // Cold timing already includes the evil hairlines. This later reset is the
  // specialized cache-capacity and repair-saturation gate for that corpus.
  const bool hairline_capacity =
      workload_ready &&
      run_hairline_gate(presenter, producer, log, canvas, touch, chrome, workspace, blank_snapshot);
  const bool long_gesture =
      cache_tour &&
      run_long_gesture_commit_gate(presenter, producer, log, canvas, chrome, workspace,
                                   in_place_workspace, blank_snapshot, conversion_storage);
  const bool export_encode = long_gesture && run_export_encode_gate(exporter, log);
  const bool export_reserve = export_encode && verify_export_reserve();
  const auto return_overview = presenter.set_view(ZoomLevel::k25Percent, 0, 0, chrome, now_us());
  print_rerender_ledger("final");
  std::printf(
      "TINYDRAW_GATE1_AUTOMATED_DONE stress=%u stress_100=%u stress_400=%u overlap_ready=%u "
      "overlap_cold=%u general_cold_ready=%u general_cold=%u workload=%u paced_cold=%u "
      "hard_100=%u hard_400=%u pan_100=%u "
      "pan_400=%u pan_seq=%u pan_boundary=%u live_overlay=%u draw_fill=%u cache=%u "
      "full_world_cache=%u "
      "cache_tour=%u mixed_draw=%u idle_repair=%u hairline_capacity=%u long_gesture=%u "
      "export_encode=%u export_reserve=%u return=%u ssaa_receipt=yellow\n",
      stress_ready, stress_100, stress_400, overlap_ready, overlap_cold, general_cold_ready,
      general_cold, workload_ready, paced_cold, gate_100, gate_400, pan_100, pan_400, pan_sequence,
      pan_boundary, live_overlay, draw_fill, cache_retention, full_world_cache, cache_tour,
      mixed_draw, idle_repair, hairline_capacity, long_gesture, export_encode, export_reserve,
      return_overview.passed);
  return return_overview.passed && export_reserve && overlap_cold && general_cold && mixed_draw &&
         idle_repair && hairline_capacity && pan_100 && pan_400 && pan_sequence && pan_boundary;
#endif
}

}  // namespace tinydraw::esp32
