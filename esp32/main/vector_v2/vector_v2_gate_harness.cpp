#include "vector_v2_gate_harness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
#include "vector_v2_tile_census.h"
#endif
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/adversarial_tapered_corpus.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/ink_trace.h"
#include "tinydraw/vector_v2/live_ink_coordinator.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/raster_census.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "vector_v2_live_stroke_session.h"

// Canonical recorded owner traces embedded by the gate-harness build
// (esp32/main/CMakeLists.txt). under-overlay (9,284 events, ~190 KiB) is
// deliberately not embedded; it needs streamed delivery and is covered by
// the capture-side receipts for now.
extern "C" {
extern const char _binary_fast_curve_dense_25_csv_start[];
extern const char _binary_fast_curve_dense_25_csv_end[];
extern const char _binary_fast_curve_400_csv_start[];
extern const char _binary_fast_curve_400_csv_end[];
extern const char _binary_fast_curve_400_xl_csv_start[];
extern const char _binary_fast_curve_400_xl_csv_end[];
extern const char _binary_slow_precise_100_csv_start[];
extern const char _binary_slow_precise_100_csv_end[];
extern const char _binary_scribble_multistroke_csv_start[];
extern const char _binary_scribble_multistroke_csv_end[];
}
#include "vector_v2_ship_contract.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::CompactOperationSample;
using vector_v2::DocumentRevision;
using vector_v2::InPlaceAppendWorkspace;
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
constexpr std::int64_t kMixedDrawAbsorbSliceBudgetUs = 1'500;
constexpr std::size_t kMixedDrawAbsorbRasterWorkPixels = 256U;
constexpr std::int64_t kMixedDrawAbsorbSliceGuardUs = 4'000;
constexpr std::int64_t kInkTraceAbsorbSliceBudgetUs = 1'500;
constexpr std::size_t kInkTraceAbsorbRasterWorkPixels = 256U;
constexpr std::int64_t kInkTraceAbsorbSliceGuardUs = 4'000;

struct MixedDrawAbsorbLimit {
  std::int64_t deadline_us = 0;

  static bool requested(const void* context) {
    return esp_timer_get_time() >= static_cast<const MixedDrawAbsorbLimit*>(context)->deadline_us;
  }
};

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

[[gnu::noinline]] bool classify_minimap_navigation(VectorV2Presenter& presenter,
                                                   const vector_v2::ChromeState& chrome) {
  const auto initial = presenter.set_view(ZoomLevel::k100Percent, 400, 600, chrome, now_us());
  const auto tap = presenter.pan_minimap_from(400, 600, {312.0F, 307.0F}, chrome, now_us());
  const bool tap_position = presenter.level_x() == 552 && presenter.level_y() == 710;
  const int drag_start_x = presenter.level_x();
  const int drag_start_y = presenter.level_y();
  const auto drag =
      presenter.pan_minimap_from(drag_start_x, drag_start_y, {316.0F, 311.0F}, chrome, now_us());
  const bool drag_position = presenter.level_x() == 626 && presenter.level_y() == 782;
  const auto acquire_initial =
      presenter.set_view(ZoomLevel::k400Percent, 5'520, 6'796, chrome, now_us());
  const auto acquire = presenter.pan_minimap_from(5'520, 6'796, {312.0F, 307.0F}, chrome, now_us());
  const bool acquire_position = presenter.level_x() == 2'760 && presenter.level_y() == 3'398;
  const int edge_start_x = presenter.level_x();
  const int edge_start_y = presenter.level_y();
  const auto edge =
      presenter.pan_minimap_from(edge_start_x, edge_start_y, {272.0F, 258.0F}, chrome, now_us());
  const bool edge_position = presenter.level_x() == 0 && presenter.level_y() == 0;
  const bool passed = initial.passed && tap.passed && tap_position && drag.passed &&
                      drag.frame_reused && drag_position && acquire_initial.passed &&
                      acquire.passed && acquire_position && edge.passed && edge_position;
  std::printf(
      "TINYDRAW_GATE1_MINIMAP_NAV mode=absolute tap_x=552 tap_y=710 "
      "tap_complete_us=%lld tap_pass=%u drag_x=626 drag_y=782 drag_complete_us=%lld "
      "drag_reused=%u drag_pass=%u bottom_right_to_center_x=2760 "
      "bottom_right_to_center_y=3398 acquire_complete_us=%lld acquire_pass=%u "
      "edge_x=0 edge_y=0 edge_complete_us=%lld edge_pass=%u pass=%u\n",
      static_cast<long long>(tap.complete_us), tap.passed && tap_position,
      static_cast<long long>(drag.complete_us), drag.frame_reused, drag.passed && drag_position,
      static_cast<long long>(acquire.complete_us), acquire.passed && acquire_position,
      static_cast<long long>(edge.complete_us), edge.passed && edge_position, passed);
  std::fflush(stdout);
  return passed;
}

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld scroll_us=%lld "
      "exposed_compose_us=%lld chrome_us=%lld chrome_prepare_us=%lld chrome_stage_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld "
      "transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu submitted_pixels=%lu pushes=%lu tear_wait_us=%lld "
      "tear_edge_isr_to_resume_us=%lu "
      "tear_edge_observed=%u tear_edge_wait_resumed=%u tear_edge_timeout=%u "
      "tear_heal_attempted=%u "
      "tear_heal_command_sent=%u presentation_experiment=%s te_edge=%s "
      "clock_mhz=%d "
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
      static_cast<unsigned long>(timing.fallback_tiles),
      static_cast<unsigned long>(timing.submitted_pixels),
      static_cast<unsigned long>(timing.pushes), static_cast<long long>(timing.tear_wait_us),
      static_cast<unsigned long>(timing.tear_edge_isr_to_resume_us), timing.tear_edge_observed,
      timing.tear_edge_wait_resumed, timing.tear_edge_timed_out, timing.tear_heal_attempted,
      timing.tear_heal_command_sent, presentation_experiment_name(), selected_tear_edge_name(),
      kCo5300ClockMHz, timing.frame_reused, timing.passed);
}

// Gate setup follows the production authority-first path. This helper is
// deliberately local to the harness: production input publishes authority,
// then the background pipeline performs the same absorption step later.
template <typename Operation>
std::optional<vector_v2::IncrementalAppendResult> append_and_absorb(
    OperationLog& log, MaterializedCanvas& canvas, const Operation& operation,
    const InPlaceAppendWorkspace& workspace,
    std::optional<vector_v2::ViewRequest> priority_view = std::nullopt,
    vector_v2::InPlaceRetentionBudget budget = {}) {
  if (vector_v2::pending_operation_count(log, canvas) != 0U ||
      !vector_v2::append_authority_only(log, operation, budget).has_value()) {
    return std::nullopt;
  }
  return vector_v2::absorb_pending_operation(log, canvas, workspace, priority_view, budget);
}

bool load_realistic_document(OperationLog& log, MaterializedCanvas& canvas,
                             const InPlaceAppendWorkspace& workspace,
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
          .x_quarter = static_cast<std::uint16_t>(std::lround(input[index].x * 16.0F)),
          .y_quarter = static_cast<std::uint16_t>(std::lround(input[index].y * 16.0F)),
          .radius_256 = static_cast<std::uint16_t>(std::lround(input[index].radius * 256.0F)),
          .elapsed_ms = static_cast<std::uint16_t>(index * 15U),
      };
    }
    const auto result =
        append_and_absorb(log, canvas,
                          vector_v2::OperationAppend{
                              .tool = stroke.tool == VectorTool::kEraser ? OperationTool::kEraser
                                                                         : OperationTool::kPen,
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

bool run_cooperative_compose_gate(VectorV2Presenter& presenter,
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
  const bool pass = incomplete_without_push && timing.passed && calls == 56U &&
                    timing.compose_slices == calls && timing.compose_slice_max_us > 0 &&
                    timing.submitted_pixels == vector_v2::kOverviewPixels &&
                    presenter.display().push_count() > pushes_before;
  std::printf(
      "TINYDRAW_GATE1_COOPERATIVE_COMPOSE calls=%lu slices=%lu max_slice_us=%lld compose_us=%lld "
      "incomplete_no_push=%u submitted=%lu pass=%u\n",
      static_cast<unsigned long>(calls), static_cast<unsigned long>(timing.compose_slices),
      static_cast<long long>(timing.compose_slice_max_us),
      static_cast<long long>(timing.compose_us), incomplete_without_push,
      static_cast<unsigned long>(timing.submitted_pixels), pass);
  std::fflush(stdout);
  return pass;
}

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
                       const InPlaceAppendWorkspace& workspace,
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
        .x_quarter = static_cast<std::uint16_t>(corner[0] * 16),
        .y_quarter = static_cast<std::uint16_t>(corner[1] * 16),
        .radius_256 = static_cast<std::uint16_t>(kRadius * 256.0F),
        .elapsed_ms = 0,
    }}};
    if (!append_and_absorb(log, canvas,
                           vector_v2::OperationAppend{.color = kColor, .samples = samples},
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
                                    const InPlaceAppendWorkspace& workspace) {
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
        .x_quarter = static_cast<std::uint16_t>(center[0] * 16),
        .y_quarter = static_cast<std::uint16_t>(center[1] * 16),
        .radius_256 = 12U * 256U,
        .elapsed_ms = 0,
    }}};
    if (!append_and_absorb(log, canvas,
                           vector_v2::OperationAppend{.tool = OperationTool::kPen,
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
                              const InPlaceAppendWorkspace& workspace,
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
  std::size_t priming_steps = 0U;
  std::size_t priming_publications = 0U;
  bool fill_primed = false;
  while (!fill_primed && priming_steps < 64U) {
    const auto initial = producer.produce_next(view);
    if (!initial.has_value() || initial->complete) {
      return false;
    }
    ++priming_steps;
    priming_publications += initial->tiles_published;
    fill_primed = initial->tiles_published == 0U;
  }
  if (!fill_primed) {
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
        .x_quarter = static_cast<std::uint16_t>(80U + index * 192U),
        .y_quarter = static_cast<std::uint16_t>(index % 2U == 0U ? 720U : 960U),
        // XL is 20 screen pixels; at 400% that is 5 world units.
        .radius_256 = 1'280U,
        .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
    };
  }
  const std::int64_t append_started = esp_timer_get_time();
  const auto append = append_and_absorb(
      log, canvas,
      vector_v2::OperationAppend{.tool = OperationTool::kPen, .color = 0x001FU, .samples = fast_xl},
      workspace, view);
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
      "max_display_slice_us=%lld fill_us=%lld priming_steps=%lu priming_publications=%lu "
      "revision_restarted=%u pass=%u\n",
      static_cast<unsigned long>(canvas.current_revision().value),
      static_cast<long long>(append_us), static_cast<long long>(poll_gap_us),
      static_cast<long long>(live.first_submit_us), static_cast<long long>(live.first_complete_us),
      static_cast<long long>(maximum_compute_slice_us), static_cast<long long>(maximum_slice_us),
      static_cast<long long>(fill_us), static_cast<unsigned long>(priming_steps),
      static_cast<unsigned long>(priming_publications), revision_restarted, passed);
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
                                  const InPlaceAppendWorkspace& workspace,
                                  std::span<const std::uint16_t> blank_snapshot,
                                  std::span<CompactOperationSample> builder_storage) {
  LongGestureMeasurement production{};
  const bool run_ok = run_long_gesture_pass(
      presenter, producer, log, canvas, chrome, blank_snapshot, builder_storage,
      kInteractiveChunkSampleLimit,
      [&](const vector_v2::BuiltOperation& chunk, const vector_v2::ViewRequest& view) {
        return append_and_absorb(
            log, canvas, chunk, workspace, view,
            {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs});
      },
      production);
  std::printf(
      "TINYDRAW_GATE1_LONG_GESTURE path=committed_overlay samples=%lu chunks=%lu "
      "append_total_us=%lld append_max_us=%lld append_avg_us=%lld "
      "refresh_fallback_pixels=%lu settled_fallback_pixels=%lu committed=%u "
      "authority=%u refresh=%u run_ok=%u\n",
      static_cast<unsigned long>(production.samples), static_cast<unsigned long>(production.chunks),
      static_cast<long long>(production.append_total_us),
      static_cast<long long>(production.append_max_us),
      static_cast<long long>(production.chunks == 0U
                                 ? 0
                                 : production.append_total_us /
                                       static_cast<std::int64_t>(production.chunks)),
      static_cast<unsigned long>(production.fallback_pixels),
      static_cast<unsigned long>(production.settled_fallback_pixels), production.committed,
      production.authority_match, production.refresh_passed, run_ok);
  std::fflush(stdout);
  return run_ok && production.committed && production.authority_match &&
         production.refresh_passed && production.fallback_pixels == 0U &&
         production.settled_fallback_pixels == 0U && production.chunks >= 24U &&
         production.append_max_us < 15'000;
}

// Encodes the currently loaded seed-7 authority directly to SVG and verifies
// the complete stored byte stream without presenting USB. This preserves the
// automated serial console and never puts the device in mass-storage mode.
constexpr std::array<std::uint32_t, 256> make_export_crc32_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t value = 0; value < table.size(); ++value) {
    std::uint32_t entry = value;
    for (int bit = 0; bit < 8; ++bit) {
      entry = (entry >> 1U) ^ (0xEDB88320U & (0U - (entry & 1U)));
    }
    table[value] = entry;
  }
  return table;
}

constexpr auto kExportCrc32Table = make_export_crc32_table();

std::uint32_t export_crc32(std::uint32_t crc, std::span<const std::uint8_t> bytes) {
  crc = ~crc;
  for (const std::uint8_t byte : bytes) {
    crc = (crc >> 8U) ^ kExportCrc32Table[(crc ^ byte) & 0xFFU];
  }
  return ~crc;
}

std::uint32_t big_endian_u32(std::span<const std::uint8_t> bytes) {
  return static_cast<std::uint32_t>(bytes[0]) << 24U | static_cast<std::uint32_t>(bytes[1]) << 16U |
         static_cast<std::uint32_t>(bytes[2]) << 8U | static_cast<std::uint32_t>(bytes[3]);
}

struct PngVerification {
  bool signature = false;
  bool dimensions = false;
  bool chunks = false;
  std::size_t chunk_count = 0;
};

PngVerification verify_png(VectorV2Export& exporter, std::size_t total_bytes) {
  PngVerification result;
  std::array<std::uint8_t, 24> header{};
  constexpr std::array<std::uint8_t, 8> kSignature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                   0x0DU, 0x0AU, 0x1AU, 0x0AU};
  if (total_bytes < 45U || !exporter.read_png(0, header)) {
    return result;
  }
  result.signature = std::equal(kSignature.begin(), kSignature.end(), header.begin());
  result.dimensions = big_endian_u32(std::span(header).subspan(16U, 4U)) ==
                          static_cast<std::uint32_t>(vector_v2::kWorldWidth) &&
                      big_endian_u32(std::span(header).subspan(20U, 4U)) ==
                          static_cast<std::uint32_t>(vector_v2::kWorldHeight);

  std::array<std::uint8_t, 4'096> buffer{};
  std::size_t offset = 8U;
  bool saw_iend = false;
  while (offset + 12U <= total_bytes && !saw_iend) {
    std::array<std::uint8_t, 8> chunk_header{};
    if (!exporter.read_png(offset, chunk_header)) {
      return result;
    }
    const std::size_t length = big_endian_u32(std::span(chunk_header).first(4U));
    if (length > total_bytes - offset - 12U) {
      return result;
    }
    std::uint32_t crc = export_crc32(0U, std::span(chunk_header).subspan(4U));
    std::size_t payload_offset = offset + 8U;
    std::size_t remaining = length;
    while (remaining > 0U) {
      const std::size_t count = std::min(remaining, buffer.size());
      if (!exporter.read_png(payload_offset, std::span(buffer).first(count))) {
        return result;
      }
      crc = export_crc32(crc, std::span<const std::uint8_t>(buffer).first(count));
      payload_offset += count;
      remaining -= count;
      vTaskDelay(1);
    }
    std::array<std::uint8_t, 4> stored_crc{};
    if (!exporter.read_png(offset + 8U + length, stored_crc) || crc != big_endian_u32(stored_crc)) {
      return result;
    }
    constexpr std::array<std::uint8_t, 4> kIend{'I', 'E', 'N', 'D'};
    saw_iend = std::equal(kIend.begin(), kIend.end(), chunk_header.begin() + 4);
    offset += length + 12U;
    ++result.chunk_count;
  }
  result.chunks = saw_iend && offset == total_bytes;
  return result;
}

struct SvgVerification {
  bool prolog = false;
  bool dimensions = false;
  bool terminator = false;
  bool crc = false;
  bool path_only = false;
  std::size_t paths = 0;
};

SvgVerification verify_svg(VectorV2Export& exporter, std::size_t total_bytes,
                           std::size_t expected_paths, std::uint32_t expected_crc) {
  SvgVerification result;
  if (total_bytes < 7U) {
    return result;
  }

  std::array<std::uint8_t, 512> header{};
  const std::size_t header_bytes = std::min(header.size(), total_bytes);
  if (!exporter.read_file(0, std::span(header).first(header_bytes))) {
    return result;
  }
  const std::string_view header_text(reinterpret_cast<const char*>(header.data()), header_bytes);
  result.prolog = header_text.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg ");
  result.dimensions =
      header_text.find("width=\"1472\" height=\"1792\" viewBox=\"0 0 1472 1792\"") !=
      std::string_view::npos;

  std::array<std::uint8_t, 7> tail{};
  result.terminator =
      exporter.read_file(total_bytes - tail.size(), tail) &&
      std::string_view(reinterpret_cast<const char*>(tail.data()), tail.size()) == "</svg>\n";

  constexpr std::size_t kCarryBytes = 32U;
  // Keep the verifier below the product main-task stack headroom. The file is
  // still checked in full; a smaller streaming window only adds read calls.
  std::array<std::uint8_t, 512> buffer{};
  std::array<char, 512 + kCarryBytes> searchable{};
  std::array<char, kCarryBytes> carry{};
  std::size_t carry_size = 0;
  std::size_t offset = 0;
  std::uint32_t crc = 0;
  bool path_only = true;
  while (offset < total_bytes) {
    const std::size_t count = std::min(buffer.size(), total_bytes - offset);
    if (!exporter.read_file(offset, std::span(buffer).first(count))) {
      return result;
    }
    crc = export_crc32(crc, std::span<const std::uint8_t>(buffer).first(count));
    std::copy_n(carry.begin(), carry_size, searchable.begin());
    std::copy_n(reinterpret_cast<const char*>(buffer.data()), count,
                searchable.begin() + static_cast<std::ptrdiff_t>(carry_size));
    const std::string_view text(searchable.data(), carry_size + count);
    constexpr std::string_view kPathMarker = "<path fill=\"";
    for (std::size_t found = text.find(kPathMarker); found != std::string_view::npos;
         found = text.find(kPathMarker, found + 1U)) {
      if (found + kPathMarker.size() > carry_size) {
        ++result.paths;
      }
    }
    path_only = path_only && text.find("<circle") == std::string_view::npos &&
                text.find("stroke-width") == std::string_view::npos;
    carry_size = std::min(kCarryBytes, text.size());
    std::copy_n(text.end() - static_cast<std::ptrdiff_t>(carry_size), carry_size, carry.begin());
    offset += count;
    vTaskDelay(1);
  }
  result.crc = crc == expected_crc;
  result.path_only = path_only && result.paths == expected_paths;
  return result;
}

bool run_export_encode_gate(VectorV2Export& exporter, OperationLog& log) {
  const auto expected_paths = vector_v2::svg_path_count(log);
  const VectorV2ExportStats stats = exporter.encode(log);
  const SvgVerification svg =
      stats.encoded && expected_paths.has_value()
          ? verify_svg(exporter, stats.bytes, *expected_paths, stats.content_crc32)
          : SvgVerification{};
  const PngVerification png =
      stats.encoded ? verify_png(exporter, stats.png_bytes) : PngVerification{};
  const bool passed = stats.encoded && stats.bytes > 64U && svg.prolog && svg.dimensions &&
                      svg.terminator && svg.crc && svg.path_only && png.signature &&
                      png.dimensions && png.chunks;
  std::printf(
      "TINYDRAW_GATE1_EXPORT formats=svg,png encoded=%u svg_bytes=%lu png_bytes=%lu "
      "elapsed_us=%lld svg_workspace_bytes=%lu png_workspace_bytes=%lu "
      "render_workspace_bytes=%lu peak_workspace_bytes=%lu operations=%lu sink_calls=%lu "
      "flash_pages=%lu crc32=%08lx free_psram=%lu free_internal=%lu prolog=%u "
      "svg_dimensions=%u terminator=%u crc_ok=%u paths=%lu path_only=%u png_signature=%u "
      "png_dimensions=%u png_chunks=%lu png_chunks_ok=%u pass=%u\n",
      stats.encoded, static_cast<unsigned long>(stats.bytes),
      static_cast<unsigned long>(stats.png_bytes), static_cast<long long>(stats.elapsed_us),
      static_cast<unsigned long>(stats.workspace_bytes),
      static_cast<unsigned long>(stats.png_workspace_bytes),
      static_cast<unsigned long>(stats.render_workspace_bytes),
      static_cast<unsigned long>(stats.peak_workspace_bytes),
      static_cast<unsigned long>(stats.operation_count),
      static_cast<unsigned long>(stats.sink_calls), static_cast<unsigned long>(stats.flash_pages),
      static_cast<unsigned long>(stats.content_crc32),
      static_cast<unsigned long>(stats.free_psram_after),
      static_cast<unsigned long>(stats.free_internal_after), svg.prolog, svg.dimensions,
      svg.terminator, svg.crc, static_cast<unsigned long>(svg.paths), svg.path_only, png.signature,
      png.dimensions, static_cast<unsigned long>(png.chunk_count), png.chunks, passed);
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
  vector_v2::InPlaceRetainDrops drops{};
  std::size_t affected_tiles = 0;
  std::size_t published_tiles = 0;
  std::size_t fallback_tiles = 0;
  std::size_t visible_fallback_tiles = 0;
  // Committed-overlay drain receipts: absorption work that ran off the
  // input path in cooperative product-sized slices.
  std::size_t drain_ops = 0;
  std::size_t drain_slices = 0;
  std::size_t max_pending_operations = 0;
  std::int64_t drain_total_us = 0;
  std::int64_t drain_max_slice_us = 0;
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

bool run_mixed_zoom_stroke(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                           OperationLog& log, MaterializedCanvas& canvas,
                           const vector_v2::ChromeState& chrome,
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
  const auto accumulate = [&](const vector_v2::IncrementalAppendResult& result) {
    stats.phase_max.prepare_us = std::max(stats.phase_max.prepare_us, result.phases.prepare_us);
    stats.phase_max.overview_us = std::max(stats.phase_max.overview_us, result.phases.overview_us);
    stats.phase_max.enumerate_us =
        std::max(stats.phase_max.enumerate_us, result.phases.enumerate_us);
    stats.phase_max.uniform_retain_us =
        std::max(stats.phase_max.uniform_retain_us, result.phases.uniform_retain_us);
    stats.phase_max.raw_retain_us =
        std::max(stats.phase_max.raw_retain_us, result.phases.raw_retain_us);
    stats.phase_max.offscreen_retain_us =
        std::max(stats.phase_max.offscreen_retain_us, result.phases.offscreen_retain_us);
    stats.phase_max.commit_us = std::max(stats.phase_max.commit_us, result.phases.commit_us);
    stats.affected_tiles += result.affected_resident_tiles;
    stats.published_tiles += result.published_tiles;
    stats.fallback_tiles += result.fallback_tiles;
    stats.visible_fallback_tiles += result.visible_fallback_tiles;
    stats.drops.visible_uniform_no_slot += result.drops.visible_uniform_no_slot;
    stats.drops.visible_uniform_paint_fail += result.drops.visible_uniform_paint_fail;
    stats.drops.visible_raw_edit_fail += result.drops.visible_raw_edit_fail;
    stats.drops.visible_raw_paint_fail += result.drops.visible_raw_paint_fail;
    stats.drops.offscreen_skipped += result.drops.offscreen_skipped;
    if (!world_bounds.has_value()) {
      world_bounds = result.affected_world_bounds;
    } else {
      world_bounds->x0 = std::min(world_bounds->x0, result.affected_world_bounds.x0);
      world_bounds->y0 = std::min(world_bounds->y0, result.affected_world_bounds.y0);
      world_bounds->x1 = std::max(world_bounds->x1, result.affected_world_bounds.x1);
      world_bounds->y1 = std::max(world_bounds->y1, result.affected_world_bounds.y1);
    }
  };
  vector_v2::PendingOperationAbsorption absorption;
  const auto absorb_slice = [&]() -> bool {
    if (!absorption.active() && vector_v2::pending_operation_count(log, canvas) == 0U) {
      return true;
    }
    if (!absorption.active()) {
      // Producer batches retain prepared chords between calls. An absorption
      // commit supersedes their canvas revision and shares the plan storage,
      // so abandon unpublished producer work before the first slice.
      producer.cancel_pending_work();
    }
    const std::int64_t started_us = esp_timer_get_time();
    const MixedDrawAbsorbLimit limit{.deadline_us = started_us + kMixedDrawAbsorbSliceBudgetUs};
    const auto absorbed = vector_v2::absorb_pending_operation_slice(
        log, canvas, workspace, absorption, priority_view,
        {.requested = &MixedDrawAbsorbLimit::requested,
         .context = &limit,
         .raster_work_px = kMixedDrawAbsorbRasterWorkPixels},
        {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs});
    const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
    ++stats.drain_slices;
    stats.drain_total_us += elapsed_us;
    stats.drain_max_slice_us = std::max(stats.drain_max_slice_us, elapsed_us);
    if (absorbed.status == vector_v2::PendingAbsorptionStatus::kError) {
      absorption.cancel();
      return false;
    }
    if (absorbed.status == vector_v2::PendingAbsorptionStatus::kComplete) {
      ++stats.drain_ops;
      accumulate(absorbed.result);
    }
    return true;
  };
  const auto commit_ready = [&](vector_v2::ChainedOperationStatus status)
      -> std::optional<vector_v2::ChainedOperationStatus> {
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto pending = builder.pending_append();
      if (!pending.has_value()) {
        return std::nullopt;
      }
      // Product input publishes authority directly; absorption never blocks a
      // high-water append now that the pending overlay stays exact.
      const std::int64_t started_us = esp_timer_get_time();
      const auto committed =
          vector_v2::append_authority_only(log, *pending, {.now_us = &esp_timer_get_time});
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      if (!committed.has_value()) {
        return std::nullopt;
      }
      stats.append_total_us += elapsed_us;
      stats.append_max_us = std::max(stats.append_max_us, elapsed_us);
      ++stats.chunks;
      accumulate(*committed);
      status = builder.acknowledge_commit();
      stats.max_pending_operations =
          std::max(stats.max_pending_operations, vector_v2::pending_operation_count(log, canvas));
      // Mirror the background opportunity after a handled input sample: one
      // deadline-bounded slice, with remaining work carried to later ticks.
      if (!absorb_slice()) {
        return std::nullopt;
      }
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
  // Post-stroke drain: production absorbs in idle slices; the gate
  // compresses that into one receipted loop before the exact swap refresh.
  while (absorption.active() || vector_v2::pending_operation_count(log, canvas) != 0U) {
    if (!absorb_slice()) {
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
          presenter, producer, log, canvas, chrome, workspace, builder_storage, zoom, tool,
          tool == OperationTool::kPen ? 0x001FU : 0x0000U, gesture_id++, stats);
      const bool correct = run_ok && stats.committed && stats.authority && stats.refresh_passed &&
                           stats.chunks >= 24U;
      // Visible tiles are budget-exempt: any dropped tile intersecting the
      // priority view is an on-glass blur. Off-view drops at the active zoom
      // are the accepted budget behavior (brush bleed past the viewport) and
      // idle repair rebuilds them. 25% has no priority view and stays exempt.
      const bool visible_sharp =
          zoom == ZoomLevel::k25Percent || stats.visible_fallback_tiles == 0U;
      const bool cooperative_evidence = stats.drain_slices > stats.drain_ops &&
                                        stats.drain_max_slice_us <= kMixedDrawAbsorbSliceGuardUs &&
                                        stats.max_pending_operations <= kPendingOperationHighWater;
      const bool stroke_pass =
          correct && visible_sharp && stats.append_max_us < 15'000 && cooperative_evidence;
      std::printf(
          "TINYDRAW_GATE1_MIXED_DRAW zoom=%s tool=%s chunks=%lu append_max_us=%lld "
          "append_avg_us=%lld append_total_us=%lld affected_tiles=%lu published=%lu "
          "fallback=%lu visible_fallback=%lu drop_uni_slot=%lu drop_uni_paint=%lu "
          "drop_raw_edit=%lu drop_raw_paint=%lu off_skip=%lu "
          "drain_ops=%lu drain_slices=%lu max_pending=%lu drain_total_us=%lld "
          "drain_max_slice_us=%lld "
          "ph_prepare_max_us=%lld ph_overview_max_us=%lld "
          "ph_enumerate_max_us=%lld ph_uniform_max_us=%lld ph_raw_max_us=%lld "
          "ph_offscreen_max_us=%lld "
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
          static_cast<unsigned long>(stats.drops.visible_uniform_no_slot),
          static_cast<unsigned long>(stats.drops.visible_uniform_paint_fail),
          static_cast<unsigned long>(stats.drops.visible_raw_edit_fail),
          static_cast<unsigned long>(stats.drops.visible_raw_paint_fail),
          static_cast<unsigned long>(stats.drops.offscreen_skipped),
          static_cast<unsigned long>(stats.drain_ops),
          static_cast<unsigned long>(stats.drain_slices),
          static_cast<unsigned long>(stats.max_pending_operations),
          static_cast<long long>(stats.drain_total_us),
          static_cast<long long>(stats.drain_max_slice_us),
          static_cast<long long>(stats.phase_max.prepare_us),
          static_cast<long long>(stats.phase_max.overview_us),
          static_cast<long long>(stats.phase_max.enumerate_us),
          static_cast<long long>(stats.phase_max.uniform_retain_us),
          static_cast<long long>(stats.phase_max.raw_retain_us),
          static_cast<long long>(stats.phase_max.offscreen_retain_us),
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
                            const InPlaceAppendWorkspace& workspace, HairlineRandom& random,
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
          .x_quarter = static_cast<std::uint16_t>(x * 16.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 16.0F),
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
          .x_quarter = static_cast<std::uint16_t>(x * 16.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 16.0F),
          .radius_256 = static_cast<std::uint16_t>(radius * 256.0F),
          .elapsed_ms = elapsed_ms,
      };
    }
    continuing = true;
    const std::int64_t append_started = esp_timer_get_time();
    const auto result =
        append_and_absorb(log, canvas,
                          vector_v2::OperationAppend{.tool = tool,
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

// Alice's evil corpus: lots of somewhat-random thin strokes drawn at 25%
// (1.3-2 world px), a thin layer at 50% pen width (3.5-4.7 px), and a few
// thick sweeps with erasers mixed in. Dense hairlines defeat uniform-tile
// coverage, so this is the capacity worst case for the raw slot pool.
bool append_hairline_document(OperationLog& log, MaterializedCanvas& canvas,
                              const InPlaceAppendWorkspace& workspace, HairlineAppendStats& stats) {
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
                                  const InPlaceAppendWorkspace& workspace) {
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
                       const InPlaceAppendWorkspace& workspace,
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
  // The Alice scenario: an XL 25% stroke sweeps the world and drops warm
  // tiles at every other zoom.
  MixedDrawStrokeStats stroke_stats{};
  if (!run_mixed_zoom_stroke(presenter, producer, log, canvas, chrome, workspace, builder_storage,
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
                            const InPlaceAppendWorkspace& workspace) {
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
          .x_quarter = static_cast<std::uint16_t>(x * 16.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 16.0F),
          .radius_256 = static_cast<std::uint16_t>((3U + operation % 6U) * 256U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    const std::int64_t append_started = esp_timer_get_time();
    const auto result = append_and_absorb(
        log, canvas,
        vector_v2::OperationAppend{
            .tool = operation % 11U == 10U ? OperationTool::kEraser : OperationTool::kPen,
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

// Replays a recorded trace through the production TouchEventBuffer::offer()
// path from a core-1 task at the original relative timestamps, mirroring the
// product sampler's core, priority, and lift debounce. Downstream coalescing
// and consumption behave exactly as production because they ARE production.
class TouchTraceReplayer {
 public:
  ~TouchTraceReplayer() { stop(); }

  [[nodiscard]] bool start(std::span<const vector_v2::TraceEvent> events) {
    if (task_ != nullptr) {
      return false;
    }
    trace_ = events;
    buffer_ = vector_v2::TouchEventBuffer(storage_);
    done_.store(false, std::memory_order_release);
    offered_ = 0;
    coalesced_ = 0;
    overflows_ = 0;
    resyncs_ = 0;
    if (xTaskCreatePinnedToCore(task_entry, "v2_replay", 3'072U, this, 5U, &task_, 1) != pdPASS) {
      task_ = nullptr;
      return false;
    }
    return true;
  }

  void stop() {
    if (task_ == nullptr) {
      return;
    }
    while (!done_.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    vTaskDelay(pdMS_TO_TICKS(2));  // Let the task reach its suspend point.
    vTaskDelete(task_);
    task_ = nullptr;
  }

  [[nodiscard]] std::optional<SampledTouch> read_next() {
    portENTER_CRITICAL(&lock_);
    const auto event = buffer_.pop();
    portEXIT_CRITICAL(&lock_);
    if (!event.has_value()) {
      return std::nullopt;
    }
    return SampledTouch{
        .point = {.x = event->point.x, .y = event->point.y},
        .timestamp_us = event->timestamp_us,
        .sequence = event->sequence,
        .kind = event->kind,
    };
  }

  [[nodiscard]] bool exhausted() {
    if (!done_.load(std::memory_order_acquire)) {
      return false;
    }
    portENTER_CRITICAL(&lock_);
    const std::size_t pending = buffer_.pending();
    portEXIT_CRITICAL(&lock_);
    return pending == 0U;
  }

  [[nodiscard]] bool sample_ready() {
    portENTER_CRITICAL(&lock_);
    const bool ready = buffer_.pending() != 0U;
    portEXIT_CRITICAL(&lock_);
    return ready;
  }

  [[nodiscard]] std::uint32_t offered() const { return offered_; }
  [[nodiscard]] std::uint32_t coalesced() const { return coalesced_; }
  [[nodiscard]] std::uint32_t overflows() const { return overflows_; }
  [[nodiscard]] std::uint32_t resyncs() const { return resyncs_; }

 private:
  static void task_entry(void* argument) {
    static_cast<TouchTraceReplayer*>(argument)->run();
    vTaskSuspend(nullptr);
  }

  void offer(vector_v2::TouchContactRead read, vector_v2::TouchContactPoint point) {
    const auto stamp = static_cast<std::uint32_t>(esp_timer_get_time());
    portENTER_CRITICAL(&lock_);
    const auto result = buffer_.offer(read, point, stamp);
    portEXIT_CRITICAL(&lock_);
    coalesced_ += result == vector_v2::TouchOfferResult::kMoveCoalesced;
    overflows_ += result == vector_v2::TouchOfferResult::kOverflow;
    resyncs_ += result == vector_v2::TouchOfferResult::kResynchronized;
  }

  void run() {
    const std::int64_t base_us = esp_timer_get_time();
    for (const vector_v2::TraceEvent& event : trace_) {
      const std::int64_t target_us = base_us + static_cast<std::int64_t>(event.t_us);
      std::int64_t now_us = esp_timer_get_time();
      while (now_us < target_us) {
        const std::int64_t remaining_us = target_us - now_us;
        vTaskDelay(remaining_us >= 2'000 ? pdMS_TO_TICKS(remaining_us / 1'000) : 1);
        now_us = esp_timer_get_time();
      }
      ++offered_;
      if (event.kind == vector_v2::TraceEventKind::kUp) {
        // Two consecutive no-touch reads, one sampler period apart, mirror
        // the production lift-confirmation debounce.
        offer(vector_v2::TouchContactRead::kNoTouch, {});
        vTaskDelay(pdMS_TO_TICKS(1));
        offer(vector_v2::TouchContactRead::kNoTouch, {});
      } else {
        offer(vector_v2::TouchContactRead::kPoint,
              {.x = static_cast<float>(event.x), .y = static_cast<float>(event.y)});
      }
    }
    done_.store(true, std::memory_order_release);
  }

  std::span<const vector_v2::TraceEvent> trace_{};
  std::array<vector_v2::TouchEvent, kVectorV2TouchEventCapacity> storage_{};
  vector_v2::TouchEventBuffer buffer_{storage_};
  TaskHandle_t task_ = nullptr;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<bool> done_{false};
  std::uint32_t offered_ = 0;
  std::uint32_t coalesced_ = 0;
  std::uint32_t overflows_ = 0;
  std::uint32_t resyncs_ = 0;
};

struct InkTraceSpec {
  const char* name;
  const char* begin;
  const char* end;
  ZoomLevel zoom;
  float brush_size;
};

struct LatencyDeltas {
  std::uint32_t* values = nullptr;
  std::size_t count = 0;

  void push(std::uint32_t value, std::size_t capacity) {
    if (count < capacity) {
      values[count++] = value;
    }
  }
};

struct LatencySummaryLine {
  std::uint32_t p50 = 0;
  std::uint32_t p95 = 0;
  std::uint32_t max = 0;
};

LatencySummaryLine summarize_deltas(LatencyDeltas& deltas) {
  if (deltas.count == 0U) {
    return {};
  }
  std::sort(deltas.values, deltas.values + deltas.count);
  const auto rank = [&](double percentile) {
    const auto index =
        static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(deltas.count)));
    return deltas.values[index == 0U ? 0U : std::min(deltas.count, index) - 1U];
  };
  return {.p50 = rank(0.50), .p95 = rank(0.95), .max = deltas.values[deltas.count - 1U]};
}

// Counts viewport tile identities whose lookup() falls back to the
// pixelated overview (or has no source at all) at a tiled zoom. This is the
// mid-stroke pixelation oracle: an append commit that invalidates a
// viewport identity without retaining it drops that tile from raw/uniform
// to SourceKind::kOverview until a producer pass repairs it. 25% has no
// tile identities by design (the overview IS the authority), so the probe
// reports zero there. ~56 O(1) lookups per call; cheap enough to run after
// every chunk commit.
struct ViewportFallbackProbe {
  std::uint32_t tiles = 0;
  std::uint32_t fallback = 0;
};

ViewportFallbackProbe probe_viewport_overview_fallback(const VectorV2Presenter& presenter,
                                                       const MaterializedCanvas& canvas,
                                                       ZoomLevel zoom) {
  ViewportFallbackProbe probe{};
  if (zoom == ZoomLevel::k25Percent) {
    return probe;
  }
  const int x0 = presenter.level_x();
  const int y0 = presenter.level_y();
  const int first_column = x0 / vector_v2::kTileWidth;
  const int last_column = (x0 + vector_v2::kOverviewWidth - 1) / vector_v2::kTileWidth;
  const int first_row = y0 / vector_v2::kTileHeight;
  const int last_row = (y0 + vector_v2::kOverviewHeight - 1) / vector_v2::kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      ++probe.tiles;
      const auto selection = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      probe.fallback += static_cast<std::uint32_t>(
          !selection.has_value() || selection->kind == vector_v2::SourceKind::kOverview);
    }
  }
  return probe;
}

// Replays one recorded trace through the production interaction pipeline
// (InkStream -> curved ribbon -> visual-first coordinator -> in-place
// authority commits) and reports the event->consumed->geometry->submit->DMA
// chain per docs/INK_TRACE_HARNESS.md §3. The consumption loop polls
// tighter than the product loop; the receipt notes that cadence.
bool run_ink_trace_replay_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                               OperationLog& log, MaterializedCanvas& canvas,
                               const vector_v2::ChromeState& chrome,
                               const vector_v2::InPlaceAppendWorkspace& in_place_workspace,
                               std::span<CompactOperationSample> builder_storage) {
  const std::array<InkTraceSpec, 5> specs{{
      {"fast-curve-dense-25", _binary_fast_curve_dense_25_csv_start,
       _binary_fast_curve_dense_25_csv_end, ZoomLevel::k25Percent, 5.0F},
      {"fast-curve-400", _binary_fast_curve_400_csv_start, _binary_fast_curve_400_csv_end,
       ZoomLevel::k400Percent, 5.0F},
      {"fast-curve-400-xl", _binary_fast_curve_400_xl_csv_start, _binary_fast_curve_400_xl_csv_end,
       ZoomLevel::k400Percent, 20.0F},
      {"slow-precise-100", _binary_slow_precise_100_csv_start, _binary_slow_precise_100_csv_end,
       ZoomLevel::k100Percent, 5.0F},
      {"scribble-multistroke", _binary_scribble_multistroke_csv_start,
       _binary_scribble_multistroke_csv_end, ZoomLevel::k100Percent, 5.0F},
  }};
  constexpr std::size_t kMaximumTraceEvents = 4'096;
  constexpr std::size_t kMaximumLatencySamples = 4'096;
  auto* events = static_cast<vector_v2::TraceEvent*>(
      heap_caps_malloc(kMaximumTraceEvents * sizeof(vector_v2::TraceEvent), kExternalCaps));
  auto* delta_storage = static_cast<std::uint32_t*>(
      heap_caps_malloc(4U * kMaximumLatencySamples * sizeof(std::uint32_t), kExternalCaps));
  if (events == nullptr || delta_storage == nullptr) {
    heap_caps_free(events);
    heap_caps_free(delta_storage);
    std::printf("TINYDRAW_INKTRACE_FAIL reason=allocation\n");
    return false;
  }
  bool all_pass = true;
  std::uint16_t gesture_id = 40'000U;
  for (const InkTraceSpec& spec : specs) {
    const std::size_t csv_size = static_cast<std::size_t>(spec.end - spec.begin);
    const std::string_view csv(
        spec.begin, csv_size != 0U && spec.begin[csv_size - 1U] == '\0' ? csv_size - 1U : csv_size);
    const auto parsed = vector_v2::parse_ink_trace_csv(csv, std::span(events, kMaximumTraceEvents));
    if (!parsed.ok()) {
      std::printf("TINYDRAW_INKTRACE_FAIL trace=%s reason=parse line=%u\n", spec.name,
                  static_cast<unsigned>(parsed.line));
      all_pass = false;
      continue;
    }
    if (!presenter.set_view(spec.zoom, 0, 0, chrome, now_us()).passed) {
      std::printf("TINYDRAW_INKTRACE_FAIL trace=%s reason=set_view\n", spec.name);
      all_pass = false;
      continue;
    }
    LiveStrokeSession stroke(builder_storage, log, canvas, in_place_workspace, presenter);
    // Mid-stroke pixelation observability: fb_start is the pre-ink state of
    // the viewport, fb_mid_max the worst overview fallback seen right after
    // any chunk commit, fb_up_max the worst state at any lift, fb_end the
    // state after the whole trace. Counts, not verdicts: pass is unchanged.
    const ViewportFallbackProbe fallback_start =
        probe_viewport_overview_fallback(presenter, canvas, spec.zoom);
    std::uint32_t fallback_mid_max = 0;
    std::uint32_t fallback_up_max = 0;
    vector_v2::InPlaceRetainDrops trace_drops{};
    std::uint32_t drain_ops = 0;
    std::uint32_t drain_slices = 0;
    std::uint32_t drain_skipped_ready = 0;
    std::size_t max_pending_operations = 0;
    std::int64_t drain_total_us = 0;
    std::int64_t drain_max_slice_us = 0;
    vector_v2::PendingOperationAbsorption absorption;
    const std::optional<vector_v2::ViewRequest> priority_view =
        spec.zoom == ZoomLevel::k25Percent
            ? std::optional<vector_v2::ViewRequest>{}
            : std::optional{vector_v2::ViewRequest{
                  .zoom = spec.zoom,
                  .level_pixels = {presenter.level_x(), presenter.level_y(),
                                   presenter.level_x() + vector_v2::kOverviewWidth,
                                   presenter.level_y() + vector_v2::kOverviewHeight}}};
    const auto observe_pending = [&]() {
      max_pending_operations =
          std::max(max_pending_operations, vector_v2::pending_operation_count(log, canvas));
    };
    const auto absorb_slice = [&]() -> bool {
      if (!absorption.active() && vector_v2::pending_operation_count(log, canvas) == 0U) {
        return true;
      }
      if (!absorption.active()) {
        producer.cancel_pending_work();
      }
      const std::int64_t started_us = esp_timer_get_time();
      const MixedDrawAbsorbLimit limit{.deadline_us = started_us + kInkTraceAbsorbSliceBudgetUs};
      const auto absorbed = vector_v2::absorb_pending_operation_slice(
          log, canvas, in_place_workspace, absorption, priority_view,
          {.requested = &MixedDrawAbsorbLimit::requested,
           .context = &limit,
           .raster_work_px = kInkTraceAbsorbRasterWorkPixels},
          {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs});
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      ++drain_slices;
      drain_total_us += elapsed_us;
      drain_max_slice_us = std::max(drain_max_slice_us, elapsed_us);
      if (absorbed.status == vector_v2::PendingAbsorptionStatus::kError) {
        absorption.cancel();
        return false;
      }
      if (absorbed.status == vector_v2::PendingAbsorptionStatus::kComplete) {
        ++drain_ops;
        trace_drops.visible_uniform_no_slot += absorbed.result.drops.visible_uniform_no_slot;
        trace_drops.visible_uniform_paint_fail += absorbed.result.drops.visible_uniform_paint_fail;
        trace_drops.visible_raw_edit_fail += absorbed.result.drops.visible_raw_edit_fail;
        trace_drops.visible_raw_paint_fail += absorbed.result.drops.visible_raw_paint_fail;
        trace_drops.offscreen_skipped += absorbed.result.drops.offscreen_skipped;
      }
      return true;
    };
    LatencyDeltas event_to_consumed{delta_storage, 0};
    LatencyDeltas event_to_geometry{delta_storage + kMaximumLatencySamples, 0};
    LatencyDeltas event_to_submit{delta_storage + 2U * kMaximumLatencySamples, 0};
    LatencyDeltas event_to_complete{delta_storage + 3U * kMaximumLatencySamples, 0};
    vector_v2::InkStrokeCounters counters{};
    for (const vector_v2::TraceEvent& event : std::span(events, parsed.event_count)) {
      counters.trace_down_events += event.kind == vector_v2::TraceEventKind::kDown;
      counters.trace_up_events += event.kind == vector_v2::TraceEventKind::kUp;
    }

    TouchTraceReplayer replayer;
    if (!replayer.start(std::span(events, parsed.event_count))) {
      std::printf("TINYDRAW_INKTRACE_FAIL trace=%s reason=replayer_start\n", spec.name);
      all_pass = false;
      continue;
    }
    const auto absorb_between_samples = [&]() -> bool {
      observe_pending();
      if (replayer.sample_ready()) {
        ++drain_skipped_ready;
        return true;
      }
      return absorb_slice();
    };
    const std::uint16_t color = 0x0000U;
    bool pressed = false;
    std::uint32_t presentation_failures = 0;
    std::uint32_t commit_failures = 0;
    std::uint32_t previous_consumed_us = 0;
    Point previous_point{};
    bool have_previous = false;
    while (true) {
      const auto sampled = replayer.read_next();
      if (!sampled.has_value()) {
        if (!pressed && replayer.exhausted()) {
          break;
        }
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        vTaskDelay(1);
        continue;
      }
      const auto consumed_us = static_cast<std::uint32_t>(esp_timer_get_time());
      const std::uint32_t event_us = sampled->timestamp_us;
      ++counters.consumed_events;
      counters.consumed_down_events += sampled->kind == vector_v2::TouchEventKind::kDown;
      counters.consumed_up_events += sampled->kind == vector_v2::TouchEventKind::kUp;
      event_to_consumed.push(consumed_us - event_us, kMaximumLatencySamples);
      if (pressed && have_previous) {
        counters.max_consumed_sample_time_gap_us = std::max<std::uint64_t>(
            counters.max_consumed_sample_time_gap_us, consumed_us - previous_consumed_us);
        const float gap_x = sampled->point.x - previous_point.x;
        const float gap_y = sampled->point.y - previous_point.y;
        counters.max_consumed_sample_space_gap_px = std::max(
            counters.max_consumed_sample_space_gap_px, std::sqrt(gap_x * gap_x + gap_y * gap_y));
      }
      previous_consumed_us = consumed_us;
      previous_point = sampled->point;
      have_previous = true;

      if (sampled->kind == vector_v2::TouchEventKind::kDown && !pressed) {
        pressed = true;
        const LiveStrokeStartResult started =
            stroke.begin(sampled->point, event_us, spec.brush_size, OperationTool::kPen, color,
                         gesture_id, chrome);
        if (!started.accepted) {
          pressed = false;
          if (!absorb_between_samples()) {
            ++commit_failures;
          }
          continue;
        }
        ++gesture_id;
        presentation_failures += !started.presentation.passed;
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        continue;
      }
      if (sampled->kind == vector_v2::TouchEventKind::kUp && pressed) {
        const LiveStrokeFinishResult finished = stroke.finish(event_us, chrome);
        presentation_failures += !finished.preview.passed;
        commit_failures += finished.commit_failed;
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        static_cast<void>(presenter.refresh(chrome, now_us()));
        fallback_up_max =
            std::max(fallback_up_max,
                     probe_viewport_overview_fallback(presenter, canvas, spec.zoom).fallback);
        pressed = false;
        continue;
      }
      if (!pressed || !stroke.active()) {
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        continue;
      }
      std::uint32_t geometry_delta = 0;
      std::uint32_t submit_delta = 0;
      std::uint32_t complete_delta = 0;
      bool submitted = false;
      const LiveStrokeMoveResult move = stroke.move(sampled->point, event_us, chrome);
      geometry_delta = move.geometry_us;
      if (move.presented && move.presentation.passed && move.presentation.first_submit_us > 0) {
        submit_delta = static_cast<std::uint32_t>(move.presentation.first_submit_us);
        complete_delta = static_cast<std::uint32_t>(move.presentation.first_complete_us);
        submitted = true;
      }
      presentation_failures += move.presented && !move.presentation.passed;
      commit_failures += move.commit_failed;
      if (move.chunk_committed) {
        fallback_mid_max =
            std::max(fallback_mid_max,
                     probe_viewport_overview_fallback(presenter, canvas, spec.zoom).fallback);
      }
      if (submitted) {
        event_to_geometry.push(geometry_delta, kMaximumLatencySamples);
        event_to_submit.push(submit_delta, kMaximumLatencySamples);
        event_to_complete.push(complete_delta, kMaximumLatencySamples);
      }
      if (!absorb_between_samples()) {
        ++commit_failures;
      }
    }
    replayer.stop();
    // No trace event remains urgent. Finish product-sized background slices
    // before recording the terminal canvas/fallback state.
    while (absorption.active() || vector_v2::pending_operation_count(log, canvas) != 0U) {
      observe_pending();
      if (!absorb_slice()) {
        ++commit_failures;
        break;
      }
    }
    const ViewportFallbackProbe fallback_end =
        probe_viewport_overview_fallback(presenter, canvas, spec.zoom);

    counters.received_events = replayer.offered();
    counters.coalesced_events = replayer.coalesced();
    const auto consumed_line = summarize_deltas(event_to_consumed);
    const auto geometry_line = summarize_deltas(event_to_geometry);
    const auto submit_line = summarize_deltas(event_to_submit);
    const auto complete_line = summarize_deltas(event_to_complete);
    const bool conserved = counters.down_up_conserved();
    const bool latency_pass = complete_line.p95 <= 28'000U;
    const bool cooperative_pass = drain_max_slice_us <= kInkTraceAbsorbSliceGuardUs &&
                                  max_pending_operations <= kPendingOperationHighWater;
    const bool trace_pass = conserved && replayer.overflows() == 0U && replayer.resyncs() == 0U &&
                            commit_failures == 0U && presentation_failures == 0U &&
                            cooperative_pass;
    std::printf(
        "TINYDRAW_INKTRACE trace=%s zoom=%s events=%lu consumed=%lu coalesced=%lu "
        "down=%lu/%lu up=%lu/%lu max_time_gap_us=%llu max_space_gap_px=%.2f "
        "e2c_p50=%lu e2c_p95=%lu e2c_max=%lu e2g_p95=%lu e2s_p95=%lu "
        "e2d_p50=%lu e2d_p95=%lu e2d_max=%lu latency_samples=%lu "
        "presentation_failures=%lu commit_failures=%lu overflows=%lu resyncs=%lu revision=%lu "
        "fb_tiles=%lu fb_start=%lu fb_mid_max=%lu fb_up_max=%lu fb_end=%lu "
        "drop_uni_slot=%lu drop_uni_paint=%lu drop_raw_edit=%lu drop_raw_paint=%lu "
        "off_skip=%lu drain_ops=%lu drain_slices=%lu drain_skipped_ready=%lu "
        "max_pending=%lu drain_total_us=%lld drain_max_slice_us=%lld drain_guard_us=%lld "
        "absorb_cadence=between_samples_skip_ready cooperative_pass=%u latency_pass=%u pass=%u\n",
        spec.name, zoom_name(spec.zoom), static_cast<unsigned long>(counters.received_events),
        static_cast<unsigned long>(counters.consumed_events),
        static_cast<unsigned long>(counters.coalesced_events),
        static_cast<unsigned long>(counters.consumed_down_events),
        static_cast<unsigned long>(counters.trace_down_events),
        static_cast<unsigned long>(counters.consumed_up_events),
        static_cast<unsigned long>(counters.trace_up_events),
        static_cast<unsigned long long>(counters.max_consumed_sample_time_gap_us),
        static_cast<double>(counters.max_consumed_sample_space_gap_px),
        static_cast<unsigned long>(consumed_line.p50),
        static_cast<unsigned long>(consumed_line.p95),
        static_cast<unsigned long>(consumed_line.max),
        static_cast<unsigned long>(geometry_line.p95), static_cast<unsigned long>(submit_line.p95),
        static_cast<unsigned long>(complete_line.p50),
        static_cast<unsigned long>(complete_line.p95),
        static_cast<unsigned long>(complete_line.max),
        static_cast<unsigned long>(event_to_complete.count),
        static_cast<unsigned long>(presentation_failures),
        static_cast<unsigned long>(commit_failures),
        static_cast<unsigned long>(replayer.overflows()),
        static_cast<unsigned long>(replayer.resyncs()),
        static_cast<unsigned long>(canvas.current_revision().value),
        static_cast<unsigned long>(fallback_start.tiles),
        static_cast<unsigned long>(fallback_start.fallback),
        static_cast<unsigned long>(fallback_mid_max), static_cast<unsigned long>(fallback_up_max),
        static_cast<unsigned long>(fallback_end.fallback),
        static_cast<unsigned long>(trace_drops.visible_uniform_no_slot),
        static_cast<unsigned long>(trace_drops.visible_uniform_paint_fail),
        static_cast<unsigned long>(trace_drops.visible_raw_edit_fail),
        static_cast<unsigned long>(trace_drops.visible_raw_paint_fail),
        static_cast<unsigned long>(trace_drops.offscreen_skipped),
        static_cast<unsigned long>(drain_ops), static_cast<unsigned long>(drain_slices),
        static_cast<unsigned long>(drain_skipped_ready),
        static_cast<unsigned long>(max_pending_operations), static_cast<long long>(drain_total_us),
        static_cast<long long>(drain_max_slice_us),
        static_cast<long long>(kInkTraceAbsorbSliceGuardUs), cooperative_pass, latency_pass,
        trace_pass);
    std::fflush(stdout);
    all_pass = all_pass && trace_pass;
  }
  heap_caps_free(events);
  heap_caps_free(delta_storage);
  return all_pass;
}

}  // namespace

bool run_vector_v2_gate_harness(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                OperationLog& log, MaterializedCanvas& canvas,
                                VectorV2TouchSampler& touch, const vector_v2::ChromeState& chrome,
                                const InPlaceAppendWorkspace& workspace, VectorV2Export& exporter,
                                std::span<const std::uint16_t> blank_snapshot,
                                std::span<CompactOperationSample> conversion_storage,
                                std::span<std::uint16_t> tile_scratch) {
  vector_v2::ChromeState palette = chrome;
  palette.popup = vector_v2::ChromePopup::kColors;
  const std::int64_t color_started = esp_timer_get_time();
  auto color_open = presenter.present_frame_region(
      {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, palette, now_us());
  if (!color_open.passed) {
    color_open = presenter.refresh(palette, now_us());
  }
  const std::int64_t color_wall_us = esp_timer_get_time() - color_started;
  constexpr std::int64_t kColorDialogMaximumUs = 40'000;
  const bool color_dialog = color_open.passed && color_wall_us <= kColorDialogMaximumUs;
  std::printf(
      "TINYDRAW_GATE1_COLOR_DIALOG wall_us=%lld maximum_us=%lld compose_us=%lld chrome_us=%lld "
      "chrome_prepare_us=%lld chrome_stage_us=%lld complete_us=%lld pushes=%lu pass=%u\n",
      static_cast<long long>(color_wall_us), static_cast<long long>(kColorDialogMaximumUs),
      static_cast<long long>(color_open.compose_us), static_cast<long long>(color_open.chrome_us),
      static_cast<long long>(color_open.chrome_prepare_us),
      static_cast<long long>(color_open.chrome_stage_us),
      static_cast<long long>(color_open.complete_us), static_cast<unsigned long>(color_open.pushes),
      color_dialog);
  std::fflush(stdout);
  auto color_close = presenter.present_frame_region(
      {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome, now_us());
  if (!color_close.passed) {
    color_close = presenter.refresh(chrome, now_us());
  }
  const bool cooperative_compose =
      color_close.passed && run_cooperative_compose_gate(presenter, chrome);

  const bool minimap_navigation = classify_minimap_navigation(presenter, chrome);

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
  const bool census = workload_ready && run_vector_v2_tile_census(producer, canvas, tile_scratch);
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
      run_edge_ink_case(presenter, producer, log, canvas, chrome, workspace, tile_scratch);
  // Pan gates are part of the final verdict; downstream gates still key on
  // the last state-producing gate so a red pan number cannot stop later
  // receipts from reporting.
  const bool pan_100 =
      live_overlay && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k100Percent);
  const bool gate_400 = live_overlay && run_tile_gate(presenter, producer, log, canvas, chrome,
                                                      ZoomLevel::k400Percent);
  const bool pan_400 =
      gate_400 && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k400Percent);
  const bool ring_local =
      pan_400 && run_ring_locality_gate(presenter, log, canvas, chrome, workspace);
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
  // Scope the tour receipt to the tour itself: the ledger accumulated every
  // earlier gate's renders since the last document restore.
  if (canvas.rerender_ledger() != nullptr) {
    canvas.rerender_ledger()->reset();
    std::printf("TINYDRAW_RERENDER_LEDGER_RESET site=cache_tour_start\n");
  }
  const bool cache_tour =
      full_world_cache && run_cache_tour_gate(presenter, producer, canvas, chrome);
  print_rerender_ledger("cache_tour");
  // The mixed-zoom drawing gate is part of the final verdict: warm-cache
  // interactive chunk commits must stay under the 15 ms alarm at every zoom.
  // It still must not stop later receipts when red.
  const bool mixed_draw =
      cache_tour && run_mixed_zoom_draw_gate(presenter, producer, log, canvas, chrome, workspace,
                                             conversion_storage);
  // Idle repair rides the same rich document; it discards and rebuilds its
  // own cache state, so mixed-draw's drops cannot skew it.
  const bool idle_repair =
      cache_tour &&
      run_idle_repair_gate(presenter, producer, log, canvas, chrome, workspace, conversion_storage);
  // Replays the recorded owner corpus through the production offer() path.
  // Runs after the cache gates on the deterministic post-idle-repair document
  // and before the long-gesture gate resets authority.
  const bool ink_trace_replay =
      cache_tour && run_ink_trace_replay_gate(presenter, producer, log, canvas, chrome, workspace,
                                              conversion_storage);
  // Cold timing already includes the evil hairlines. This later reset is the
  // specialized cache-capacity and repair-saturation gate for that corpus.
  const bool hairline_capacity =
      workload_ready &&
      run_hairline_gate(presenter, producer, log, canvas, touch, chrome, workspace, blank_snapshot);
  const bool long_gesture =
      cache_tour && run_long_gesture_commit_gate(presenter, producer, log, canvas, chrome,
                                                 workspace, blank_snapshot, conversion_storage);
  const bool export_encode = long_gesture && run_export_encode_gate(exporter, log);
  const bool export_reserve = export_encode && verify_export_reserve();
  const auto return_overview = presenter.set_view(ZoomLevel::k25Percent, 0, 0, chrome, now_us());
  print_rerender_ledger("final");
  std::printf(
      "TINYDRAW_GATE1_AUTOMATED_DONE minimap_navigation=%u "
      "color_dialog=%u cooperative_compose=%u stress=%u stress_100=%u stress_400=%u "
      "overlap_ready=%u "
      "overlap_cold=%u general_cold_ready=%u general_cold=%u workload=%u paced_cold=%u "
      "hard_100=%u hard_400=%u pan_100=%u "
      "pan_400=%u ring_local=%u pan_seq=%u pan_boundary=%u live_overlay=%u draw_fill=%u cache=%u "
      "full_world_cache=%u "
      "cache_tour=%u mixed_draw=%u idle_repair=%u ink_trace=%u hairline_capacity=%u "
      "long_gesture=%u "
      "export_encode=%u export_reserve=%u return=%u ssaa_receipt=yellow\n",
      minimap_navigation, color_dialog, cooperative_compose, stress_ready, stress_100, stress_400,
      overlap_ready, overlap_cold, general_cold_ready, general_cold, workload_ready, paced_cold,
      gate_100, gate_400, pan_100, pan_400, ring_local, pan_sequence, pan_boundary, live_overlay,
      draw_fill, cache_retention, full_world_cache, cache_tour, mixed_draw, idle_repair,
      ink_trace_replay, hairline_capacity, long_gesture, export_encode, export_reserve,
      return_overview.passed);
  return minimap_navigation && color_dialog && cooperative_compose && return_overview.passed &&
         export_reserve && overlap_cold && general_cold && mixed_draw && idle_repair &&
         hairline_capacity && pan_100 && pan_400 && ring_local && pan_sequence && pan_boundary;
#endif
}

}  // namespace tinydraw::esp32
