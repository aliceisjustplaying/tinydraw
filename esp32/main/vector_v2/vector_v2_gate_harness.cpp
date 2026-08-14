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
#include "tinydraw/vector_v2/memory_layout.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::CompactOperationSample;
using vector_v2::DocumentRevision;
using vector_v2::IncrementalDocumentWorkspace;
using vector_v2::MaterializedCanvas;
using vector_v2::OperationLog;
using vector_v2::OperationTool;
using vector_v2::ZoomLevel;

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
      "exposed_compose_us=%lld chrome_us=%lld read_submit_us=%lld read_complete_us=%lld "
      "transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu pushes=%lu tear_wait_us=%lld tear_sync=%u frame_reused=%u pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.scroll_us),
      static_cast<long long>(timing.exposed_compose_us), static_cast<long long>(timing.chrome_us),
      static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.tile_pixels),
      static_cast<unsigned long>(timing.uniform_pixels),
      static_cast<unsigned long>(timing.overview_pixels),
      static_cast<unsigned long>(timing.fallback_pixels),
      static_cast<unsigned long>(timing.resident_tiles),
      static_cast<unsigned long>(timing.fallback_tiles), static_cast<unsigned long>(timing.pushes),
      static_cast<long long>(timing.tear_wait_us), timing.tear_synchronized, timing.frame_reused,
      timing.passed);
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
bool run_tearing_probe(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome) {
  constexpr std::array positions{
      vector_v2::NavigationPoint{0, 0},
      vector_v2::NavigationPoint{240, 0},
      vector_v2::NavigationPoint{240, 240},
      vector_v2::NavigationPoint{0, 240},
  };
  const auto initial = presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us());
  if (!initial.passed || !initial.tear_synchronized) {
    return false;
  }
  for (std::size_t cycle = 0; cycle < 20U; ++cycle) {
    for (const auto position : positions) {
      const auto timing =
          presenter.set_view(ZoomLevel::k100Percent, position.x, position.y, chrome, now_us());
      if (!timing.passed || !timing.tear_synchronized) {
        return false;
      }
    }
  }
  std::printf("TINYDRAW_TEARING_PROBE_DONE frames=%lu pass=1\n",
              static_cast<unsigned long>(positions.size() * 20U));
  std::fflush(stdout);
  return true;
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
  // The current hard-edged cold path is explicitly accepted up to 0.75 s;
  // cache hits and interaction slices retain their tighter gates.
  const bool passed = total_us < 750'000 && maximum_supertask_us < 30'000;
  std::printf(
      "TINYDRAW_GATE1_HARD zoom=%s cold=1 operations=%lu samples=%lu steps=%lu tiles=%lu "
      "scanned=%lu rendered=%lu max_supertask_us=%lld presentation_us=%lld total_us=%lld "
      "pass=%u\n",
      zoom_name(zoom), static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<unsigned long>(steps),
      static_cast<unsigned long>(tiles_published), static_cast<unsigned long>(operations_scanned),
      static_cast<unsigned long>(operations_rendered), static_cast<long long>(maximum_supertask_us),
      static_cast<long long>(presentation_us), static_cast<long long>(total_us), passed);
  return passed;
}

bool run_paced_cold_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                         MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                         const vector_v2::ChromeState& chrome, ZoomLevel zoom, int level_x,
                         int level_y, const char* corpus, std::int64_t maximum_wall_us) {
  if (!canvas.discard_tiles()) {
    return false;
  }
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
  const bool passed = wall_us < maximum_wall_us && maximum_tick_us < kMaximumTickUs &&
                      sampler.maximum_interval_us < kMaximumTouchIntervalUs &&
                      sampler.errors == 0U && sampler.queue_overflows == 0U;
  std::printf(
      "TINYDRAW_GATE1_PACED_COLD corpus=%s zoom=%s x=%d y=%d steps=%lu tiles=%lu "
      "compute_us=%lld present_us=%lld touch_us=%lld pacing_us=%lld wall_us=%lld "
      "max_tick_us=%lld touch_samples=%lu touch_interval_max_us=%lu touch_read_max_us=%lu "
      "touch_events=%lu touch_down=%lu touch_up=%lu touch_events_ge_8ms=%lu "
      "touch_event_age_max_us=%lu touch_errors=%lu touch_overflows=%lu pass=%u\n",
      corpus, zoom_name(zoom), presenter.level_x(), presenter.level_y(),
      static_cast<unsigned long>(steps), static_cast<unsigned long>(tiles),
      static_cast<long long>(compute_us), static_cast<long long>(present_us),
      static_cast<long long>(touch_us), static_cast<long long>(pacing_us),
      static_cast<long long>(wall_us), static_cast<long long>(maximum_tick_us),
      static_cast<unsigned long>(sampler.samples),
      static_cast<unsigned long>(sampler.maximum_interval_us),
      static_cast<unsigned long>(sampler.maximum_read_us),
      static_cast<unsigned long>(sampler.events_consumed),
      static_cast<unsigned long>(sampler.down_events),
      static_cast<unsigned long>(sampler.up_events),
      static_cast<unsigned long>(sampler.events_at_least_8ms_old),
      static_cast<unsigned long>(sampler.maximum_event_age_us),
      static_cast<unsigned long>(sampler.errors),
      static_cast<unsigned long>(sampler.queue_overflows), passed);
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
                                 1'000'000) &&
             passed;
  }
  return passed;
}

bool run_adversarial_tapered_cold_gates(VectorV2Presenter& presenter,
                                        vector_v2::TileProducer& producer,
                                        MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                                        const vector_v2::ChromeState& chrome) {
  struct Gate {
    ZoomLevel zoom;
    std::int64_t maximum_wall_us;
  };
  constexpr std::array gates{
      Gate{ZoomLevel::k50Percent, 1'000'000},
      Gate{ZoomLevel::k100Percent, 1'000'000},
      Gate{ZoomLevel::k200Percent, 1'500'000},
      Gate{ZoomLevel::k400Percent, 2'000'000},
  };
  bool passed = true;
  for (const Gate gate : gates) {
    passed = run_paced_cold_gate(presenter, producer, canvas, touch, chrome, gate.zoom, 0, 0,
                                 "adversarial_tapered_4x", gate.maximum_wall_us) &&
             passed;
  }
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
    const auto refresh = presenter.refresh_region(
        vector_v2::operation_level_bounds(*world_bounds, ZoomLevel::k400Percent), chrome, now_us());
    measurement.refresh_passed = refresh.passed;
    measurement.fallback_pixels = refresh.fallback_pixels;
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
        return vector_v2::append_incrementally_in_place(log, canvas, chunk, in_place_workspace,
                                                        view);
      },
      in_place);
  const auto print_pass = [](const char* path, const LongGestureMeasurement& measurement,
                             bool run_ok) {
    std::printf(
        "TINYDRAW_GATE1_LONG_GESTURE path=%s samples=%lu chunks=%lu append_total_us=%lld "
        "append_max_us=%lld append_avg_us=%lld refresh_fallback_pixels=%lu committed=%u "
        "authority=%u refresh=%u run_ok=%u\n",
        path, static_cast<unsigned long>(measurement.samples),
        static_cast<unsigned long>(measurement.chunks),
        static_cast<long long>(measurement.append_total_us),
        static_cast<long long>(measurement.append_max_us),
        static_cast<long long>(measurement.chunks == 0U
                                   ? 0
                                   : measurement.append_total_us /
                                         static_cast<std::int64_t>(measurement.chunks)),
        static_cast<unsigned long>(measurement.fallback_pixels), measurement.committed,
        measurement.authority_match, measurement.refresh_passed, run_ok);
  };
  print_pass("reference", reference, reference_ok);
  print_pass("in_place", in_place, in_place_ok);
  std::fflush(stdout);
  const auto correct = [](const LongGestureMeasurement& measurement) {
    return measurement.committed && measurement.authority_match && measurement.refresh_passed &&
           measurement.fallback_pixels == 0U && measurement.chunks >= 24U;
  };
  // The interactive path must fit intermediate commits inside a 15 ms
  // input-poll slice; the reference pass is a measured comparison only.
  return reference_ok && in_place_ok && correct(reference) && correct(in_place) &&
         in_place.append_max_us < 15'000;
}

// Encodes the currently loaded document (the seed-7 realistic workload) to
// the export partition through the streamed authority renderer and verifies
// the stored PNG signature and header dimensions. USB stays untouched so the
// automated serial console survives; presenting the disk is the manual step.
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
  const bool passed = stats.encoded && stats.bytes > 64U && signature_ok && dimensions_ok;
  std::printf(
      "TINYDRAW_GATE1_EXPORT encoded=%u bytes=%lu elapsed_us=%lld workspace_bytes=%lu "
      "band_bytes=%lu free_psram=%lu free_internal=%lu signature=%u dimensions=%u pass=%u\n",
      stats.encoded, static_cast<unsigned long>(stats.bytes),
      static_cast<long long>(stats.elapsed_us), static_cast<unsigned long>(stats.workspace_bytes),
      static_cast<unsigned long>(stats.band_bytes),
      static_cast<unsigned long>(stats.free_psram_after),
      static_cast<unsigned long>(stats.free_internal_after), signature_ok, dimensions_ok, passed);
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

  const DocumentRevision adversarial_baseline{canvas.current_revision().value + 1U};
  const bool reset_for_adversarial =
      overlap_cold &&
      vector_v2::restore_document_snapshot(log, canvas, adversarial_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(adversarial_baseline);
  const bool adversarial_ready =
      reset_for_adversarial && append_adversarial_tapered_document(log, canvas, workspace);
  const bool adversarial_cold =
      adversarial_ready &&
      run_adversarial_tapered_cold_gates(presenter, producer, canvas, touch, chrome);

  const DocumentRevision realistic_baseline{canvas.current_revision().value + 1U};
  // Continue collecting independent receipts even when the adversarial timing
  // gate is red; the final verdict still requires it.
  const bool reset_for_realistic =
      adversarial_ready &&
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
                          kUnalignedOrigin, kUnalignedOrigin, "seed7", 2'000'000);
  const bool gate_100 =
      paced_cold && run_tile_gate(presenter, producer, log, canvas, chrome, ZoomLevel::k100Percent);
  const bool pan_100 =
      gate_100 && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k100Percent);
  const bool gate_400 =
      pan_100 && run_tile_gate(presenter, producer, log, canvas, chrome, ZoomLevel::k400Percent);
  const bool pan_400 =
      gate_400 && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k400Percent);
  const bool draw_fill = pan_400 && run_draw_while_fill_gate(presenter, producer, log, canvas,
                                                             chrome, workspace, conversion_storage);
  const bool long_gesture =
      draw_fill &&
      run_long_gesture_commit_gate(presenter, producer, log, canvas, chrome, workspace,
                                   in_place_workspace, blank_snapshot, conversion_storage);
  const bool cache_retention =
      long_gesture && run_cache_retention_gate(presenter, producer, canvas, chrome);
  const bool full_world_cache = cache_retention && run_full_world_cache_gate(producer, canvas);
  const bool export_encode = full_world_cache && run_export_encode_gate(exporter, log);
  const bool export_reserve = export_encode && verify_export_reserve();
  const auto return_overview = presenter.set_view(ZoomLevel::k25Percent, 0, 0, chrome, now_us());
  std::printf(
      "TINYDRAW_GATE1_AUTOMATED_DONE stress=%u stress_100=%u stress_400=%u overlap_ready=%u "
      "overlap_cold=%u adversarial_ready=%u adversarial_cold=%u workload=%u paced_cold=%u "
      "hard_100=%u hard_400=%u pan_100=%u "
      "pan_400=%u draw_fill=%u long_gesture=%u cache=%u full_world_cache=%u export_encode=%u "
      "export_reserve=%u return=%u ssaa_receipt=yellow\n",
      stress_ready, stress_100, stress_400, overlap_ready, overlap_cold, adversarial_ready,
      adversarial_cold, workload_ready, paced_cold, gate_100, gate_400, pan_100, pan_400, draw_fill,
      long_gesture, cache_retention, full_world_cache, export_encode, export_reserve,
      return_overview.passed);
  return return_overview.passed && export_reserve && overlap_cold && adversarial_cold;
#endif
}

}  // namespace tinydraw::esp32
