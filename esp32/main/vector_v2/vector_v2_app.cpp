#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#include "power_manager.h"
#include "rtc_clock.h"
#include "time_sync.h"
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
#include "vector_v2_gate_harness.h"
#endif
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_app_diagnostics.h"
#include "vector_v2_app_storage.h"
#include "vector_v2_autosave_store.h"
#include "vector_v2_background_pipeline.h"
#include "vector_v2_chrome_controller.h"
#include "vector_v2_export.h"
#include "vector_v2_live_stroke_session.h"
#include "vector_v2_presenter.h"
#include "vector_v2_touch_sampler.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::CompactOperationSample;
using vector_v2::DocumentRevision;
using vector_v2::MaterializedCanvas;
using vector_v2::MaterializedSlotStorage;
using vector_v2::MaterializedUniformStorage;
using vector_v2::OperationLog;
using vector_v2::OperationRecord;
using vector_v2::OperationTool;
using vector_v2::TileKey;
using vector_v2::ZoomLevel;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr gpio_num_t kModeButton = GPIO_NUM_0;
constexpr std::uint32_t kPowerRefreshUs = 30'000'000U;
constexpr int kStartupPresentationAttempts = 3;
constexpr TickType_t kStartupPresentationRetryDelay = pdMS_TO_TICKS(20);

struct LiftBaselineTiming {
  std::uint32_t id = 0;
  std::int64_t detected_us = 0;
  std::int64_t finish_preview_us = 0;
  std::int64_t builder_finish_us = 0;
  std::int64_t append_us = 0;
  std::int64_t append_max_us = 0;
  std::int64_t refresh_wall_us = 0;
  vector_v2::PixelRect refresh_level_bounds{};
  std::int64_t stroke_logging_us = 0;
  LivePresentationTiming refresh{};
  std::uint32_t chunks = 0;
  bool committed = false;
  bool commit_failed = false;
  bool pending = false;
};

struct PanMetrics {
  std::uint64_t compose_total_us = 0;
  std::uint64_t present_total_us = 0;
  std::uint64_t tear_wait_total_us = 0;
  std::uint32_t frames = 0;
  std::uint32_t reused_frames = 0;
  std::uint32_t compose_max_us = 0;
  std::uint32_t present_max_us = 0;
  std::uint32_t tear_wait_max_us = 0;
  std::uint32_t failures = 0;

  void include(const LivePresentationTiming& timing) {
    if (!timing.passed) {
      ++failures;
      return;
    }
    const auto compose = static_cast<std::uint32_t>(timing.compose_us);
    const auto present = static_cast<std::uint32_t>(timing.complete_us);
    const auto tear_wait = static_cast<std::uint32_t>(timing.tear_wait_us);
    compose_total_us += compose;
    present_total_us += present;
    tear_wait_total_us += tear_wait;
    ++frames;
    reused_frames += timing.frame_reused;
    compose_max_us = std::max(compose_max_us, compose);
    present_max_us = std::max(present_max_us, present);
    tear_wait_max_us = std::max(tear_wait_max_us, tear_wait);
  }

  void reset() { *this = {}; }
};

struct PendingStrokeReport {
  DocumentRevision revision{};
  std::size_t operation_count = 0;
  std::size_t sample_count = 0;
  std::int64_t append_us = 0;
  std::int64_t append_max_us = 0;
  LivePresentationTiming refresh{};
  LiveStrokeMetrics metrics{};
  std::uint32_t poll_max_us = 0;
  TouchSamplerMetrics touch{};
  vector_v2::InPlaceAppendPhases phase_max{};
  vector_v2::InPlaceRetainDrops drops{};
  std::uint32_t chunks = 0;
  std::size_t free_psram = 0;
  std::size_t largest_psram = 0;
  bool authority_match = false;
  bool committed = false;
  bool commit_failed = false;
  bool pending = false;
};

std::uint16_t next_gesture_id(const OperationLog& log) {
  if (log.operation_count() == 0U) {
    return 1U;
  }
  const auto operation = log.operation(log.operation_count() - 1U);
  if (!operation.has_value()) {
    return 1U;
  }
  const auto next = static_cast<std::uint16_t>(operation->gesture_id + 1U);
  return next == 0U ? 1U : next;
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

void print_pan_baseline(const VectorV2Presenter& presenter, const PanMetrics& metrics) {
  std::printf(
      "TINYDRAW_PAN_BASELINE zoom=%s x=%d y=%d frames=%lu reused=%lu "
      "compose_avg_us=%llu compose_max_us=%lu present_avg_us=%llu present_max_us=%lu "
      "tear_wait_avg_us=%llu tear_wait_max_us=%lu failures=%lu\n",
      zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<unsigned long>(metrics.frames), static_cast<unsigned long>(metrics.reused_frames),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.compose_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.compose_max_us),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.present_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.present_max_us),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.tear_wait_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.tear_wait_max_us),
      static_cast<unsigned long>(metrics.failures));
}

void print_lift_baseline(const LiftBaselineTiming& timing, std::int64_t poll_started_us,
                         std::int64_t poll_completed_us, std::uint32_t reports_dropped) {
  const std::int64_t measured_phase_us = timing.finish_preview_us + timing.builder_finish_us +
                                         timing.append_us + timing.refresh_wall_us +
                                         timing.stroke_logging_us;
  const std::int64_t detected_to_poll_us = poll_started_us - timing.detected_us;
  std::printf(
      "TINYDRAW_LIFT_BASELINE id=%lu finish_preview_us=%lld builder_finish_us=%lld "
      "append_us=%lld append_max_us=%lld refresh_wall_us=%lld refresh_x0=%d refresh_y0=%d "
      "refresh_x1=%d refresh_y1=%d refresh_compose_us=%lld "
      "refresh_first_submit_us=%lld refresh_first_complete_us=%lld "
      "refresh_transfer_wait_us=%lld stroke_logging_us=%lld "
      "detected_to_poll_start_us=%lld detected_to_poll_complete_us=%lld poll_read_us=%lld "
      "unattributed_tail_us=%lld reports_dropped=%lu chunks=%lu committed=%u refresh=%u "
      "commit_failed=%u\n",
      static_cast<unsigned long>(timing.id), static_cast<long long>(timing.finish_preview_us),
      static_cast<long long>(timing.builder_finish_us), static_cast<long long>(timing.append_us),
      static_cast<long long>(timing.append_max_us), static_cast<long long>(timing.refresh_wall_us),
      timing.refresh_level_bounds.x0, timing.refresh_level_bounds.y0,
      timing.refresh_level_bounds.x1, timing.refresh_level_bounds.y1,
      static_cast<long long>(timing.refresh.compose_us),
      static_cast<long long>(timing.refresh.first_submit_us),
      static_cast<long long>(timing.refresh.first_complete_us),
      static_cast<long long>(timing.refresh.complete_us),
      static_cast<long long>(timing.stroke_logging_us), static_cast<long long>(detected_to_poll_us),
      static_cast<long long>(poll_completed_us - timing.detected_us),
      static_cast<long long>(poll_completed_us - poll_started_us),
      static_cast<long long>(detected_to_poll_us - measured_phase_us),
      static_cast<unsigned long>(reports_dropped), static_cast<unsigned long>(timing.chunks),
      timing.committed, timing.refresh.passed, timing.commit_failed);
}

void print_stroke(const PendingStrokeReport& report) {
  std::printf(
      "TINYDRAW_LIVE_STROKE revision=%lu operations=%lu samples=%lu append_us=%lld "
      "append_max_us=%lld refresh_compose_us=%lld refresh_complete_us=%lld ink_samples=%lu "
      "read_submit_avg_us=%llu read_submit_max_us=%lu read_complete_avg_us=%llu "
      "read_complete_max_us=%lu submit_over_16ms=%lu complete_over_33ms=%lu "
      "presentation_failures=%lu poll_max_us=%lu touch_errors=%lu touch_overflows=%lu "
      "touch_moves_coalesced=%lu touch_events=%lu touch_down=%lu touch_up=%lu "
      "touch_events_ge_8ms=%lu touch_event_age_max_us=%lu chunks=%lu "
      "ph_prepare_max_us=%lld ph_overview_max_us=%lld ph_enumerate_max_us=%lld "
      "ph_uniform_max_us=%lld ph_raw_max_us=%lld ph_offscreen_max_us=%lld "
      "ph_commit_max_us=%lld "
      "drop_uni_slot=%lu drop_uni_paint=%lu drop_raw_edit=%lu drop_raw_paint=%lu "
      "off_skip=%lu free_psram=%lu "
      "largest_psram=%lu authority_match=%u\n",
      static_cast<unsigned long>(report.revision.value),
      static_cast<unsigned long>(report.operation_count),
      static_cast<unsigned long>(report.sample_count), static_cast<long long>(report.append_us),
      static_cast<long long>(report.append_max_us),
      static_cast<long long>(report.refresh.compose_us),
      static_cast<long long>(report.refresh.complete_us),
      static_cast<unsigned long>(report.metrics.samples),
      static_cast<unsigned long long>(
          report.metrics.samples == 0U ? 0U
                                       : report.metrics.submit_total_us / report.metrics.samples),
      static_cast<unsigned long>(report.metrics.submit_max_us),
      static_cast<unsigned long long>(
          report.metrics.samples == 0U ? 0U
                                       : report.metrics.complete_total_us / report.metrics.samples),
      static_cast<unsigned long>(report.metrics.complete_max_us),
      static_cast<unsigned long>(report.metrics.submit_over_16ms),
      static_cast<unsigned long>(report.metrics.complete_over_33ms),
      static_cast<unsigned long>(report.metrics.failures),
      static_cast<unsigned long>(report.poll_max_us),
      static_cast<unsigned long>(report.touch.errors),
      static_cast<unsigned long>(report.touch.queue_overflows),
      static_cast<unsigned long>(report.touch.moves_coalesced),
      static_cast<unsigned long>(report.touch.events_consumed),
      static_cast<unsigned long>(report.touch.down_events),
      static_cast<unsigned long>(report.touch.up_events),
      static_cast<unsigned long>(report.touch.events_at_least_8ms_old),
      static_cast<unsigned long>(report.touch.maximum_event_age_us),
      static_cast<unsigned long>(report.chunks),
      static_cast<long long>(report.phase_max.prepare_us),
      static_cast<long long>(report.phase_max.overview_us),
      static_cast<long long>(report.phase_max.enumerate_us),
      static_cast<long long>(report.phase_max.uniform_retain_us),
      static_cast<long long>(report.phase_max.raw_retain_us),
      static_cast<long long>(report.phase_max.offscreen_retain_us),
      static_cast<long long>(report.phase_max.commit_us),
      static_cast<unsigned long>(report.drops.visible_uniform_no_slot),
      static_cast<unsigned long>(report.drops.visible_uniform_paint_fail),
      static_cast<unsigned long>(report.drops.visible_raw_edit_fail),
      static_cast<unsigned long>(report.drops.visible_raw_paint_fail),
      static_cast<unsigned long>(report.drops.offscreen_skipped),
      static_cast<unsigned long>(report.free_psram),
      static_cast<unsigned long>(report.largest_psram), report.authority_match);
}

}  // namespace

void run_vector_v2_app() {
  AppStorage storage;
  if (!storage.allocate()) {
    std::printf("TINYDRAW_LIVE_FAIL reason=allocation free_psram=%lu largest_psram=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
    return;
  }
  std::printf(
      "TINYDRAW_PRODUCER_SCRATCH supertask_internal=%u free_internal=%lu free_psram=%lu\n",
      storage.supertask_internal,
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)));
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  std::fill_n(storage.snapshot, vector_v2::kOverviewPixels, 0xFFFFU);
#endif
  std::fill_n(storage.overview, vector_v2::kOverviewPixels, 0xFFFFU);

  MaterializedCanvas canvas(
      std::span(storage.overview, vector_v2::kOverviewPixels),
      std::span(storage.uniforms, vector_v2::kMaterializedTileIdentityCount),
      std::span(storage.occupancy, vector_v2::kOccupancyBytes),
      std::span(storage.slots, vector_v2::kTileSlotCount),
      std::span(storage.tile_pixels, vector_v2::kTileSlotCount * vector_v2::kTilePixels),
      DocumentRevision{},
      std::span(storage.raw_slot_directory, vector_v2::kMaterializedTileIdentityCount));
  vector_v2::OperationSpatialIndex operation_spatial_index(
      vector_v2::kOperationCapacity,
      std::span(storage.operation_spatial_cells,
                vector_v2::operation_spatial_cell_word_count(vector_v2::kOperationCapacity)),
      std::span(storage.operation_spatial_large,
                vector_v2::operation_spatial_word_count(vector_v2::kOperationCapacity)));
  OperationLog log(std::span(storage.records, vector_v2::kOperationCapacity),
                   std::span(storage.samples, vector_v2::kOperationSampleCapacity),
                   &operation_spatial_index);
  static vector_v2::NavigationState navigation;
  VectorV2AutosaveStore autosave;
  const VectorV2AutosaveRestoreStatus autosave_restore = autosave.restore(log);
  const bool autosave_restored = autosave_restore == VectorV2AutosaveRestoreStatus::kRestored ||
                                 autosave_restore == VectorV2AutosaveRestoreStatus::kRecoveredTail;
  std::uint16_t next_gesture = next_gesture_id(log);
  if (autosave_restore == VectorV2AutosaveRestoreStatus::kUnavailable ||
      autosave_restore == VectorV2AutosaveRestoreStatus::kError) {
    std::printf("TINYDRAW_AUTOSAVE_DISABLED status=%u\n", static_cast<unsigned>(autosave_restore));
  } else {
    std::printf("TINYDRAW_AUTOSAVE_RESTORE status=%u generation=%lu active=%lu retained=%lu\n",
                static_cast<unsigned>(autosave_restore),
                static_cast<unsigned long>(log.current_revision().value),
                static_cast<unsigned long>(log.operation_count()),
                static_cast<unsigned long>(log.read_view().retained_operation_count));
  }
  Co5300PanelTransport display;
  PhysicalTouch touch;
  PowerManager power(touch.bus());
  RtcClock clock(touch.bus());
  TimeSyncController time_sync(clock);
  VectorV2TouchSampler touch_sampler(touch,
                                     std::span(storage.touch_events, kVectorV2TouchEventCapacity));
  VectorV2Export exporter;
  VectorV2Presenter presenter(
      canvas, navigation, display, std::span(storage.frame, vector_v2::kOverviewPixels),
      std::span(storage.region_scratch, kLiveRegionScratchPixels),
      std::span(storage.chrome_cache, vector_v2::kChromeStagingCachePixels));
  presenter.attach_authority(log);
  vector_v2::TileProducer producer(
      log, canvas,
      {.supertask_pixels = std::span(storage.producer_supertask, vector_v2::kTileProducerPixels),
       .finalized_pixels = std::span(storage.producer_mask, vector_v2::kTileProducerMaskBytes),
       .summary_row_unset =
           std::span(storage.producer_summary_rows, vector_v2::kTileProducerSummaryRows),
       .summary_saturated_words =
           std::span(storage.producer_summary_words, vector_v2::kTileProducerSummaryWords),
       .operation_chord_plans = std::as_writable_bytes(
           std::span(storage.producer_chord_plans, vector_v2::kOperationChordStorageBytes / 4U)),
       .candidate_indices =
           std::span(storage.operation_candidates, vector_v2::kOperationCapacity)});
  // Re-render truth: every completed group render is classified against the
  // damage/eviction state the canvas reports (déjà-vu oracle; ~27.5 KiB).
  // The gate-only re-render ledger can speak during diagnostic glass sessions.
  // Deltas since the previous print attribute each transition's re-renders to a
  // cause; cumulative amplification rides along.
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  vector_v2::RerenderLedgerTotals ledger_printed{};
  vector_v2::RerenderLedger rerender_ledger(
      std::span(storage.rerender_entries, vector_v2::kRerenderLedgerEntryCount));
  canvas.set_rerender_ledger(&rerender_ledger);
  const auto print_live_ledger = [&rerender_ledger, &ledger_printed](const char* site) {
    const auto totals = rerender_ledger.totals();
    std::printf(
        "TINYDRAW_LIVE_LEDGER site=%s renders=%lu cold=%lu damage=%lu evict=%lu stale=%lu "
        "unexplained=%lu total_renders=%lu unique=%lu amplification=%.3f\n",
        site, static_cast<unsigned long>(totals.renders - ledger_printed.renders),
        static_cast<unsigned long>(totals.cold_miss - ledger_printed.cold_miss),
        static_cast<unsigned long>(totals.expected_damage - ledger_printed.expected_damage),
        static_cast<unsigned long>(totals.eviction - ledger_printed.eviction),
        static_cast<unsigned long>(totals.stale_revision - ledger_printed.stale_revision),
        static_cast<unsigned long>(totals.unexplained - ledger_printed.unexplained),
        static_cast<unsigned long>(totals.renders),
        static_cast<unsigned long>(totals.unique_groups), totals.amplification());
    std::fflush(stdout);
    ledger_printed = totals;
  };
  producer.set_rerender_ledger(&rerender_ledger);
#endif
  const vector_v2::InPlaceAppendWorkspace workspace{
      .overview_scratch = std::span(storage.overview_scratch, vector_v2::kOverviewPixels),
      .affected_keys = std::span(storage.affected_keys,
                                 vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles),
      .tile_mask = std::span(storage.chunk_mask, vector_v2::kInPlaceTileMaskBytes),
  };
  LiveStrokeSession stroke(std::span(storage.input_samples, kInputSampleCapacity), log, canvas,
                           workspace, presenter);
  const auto startup_overview =
      autosave_restored ? std::span(storage.overview_scratch, vector_v2::kOverviewPixels)
                        : std::span(storage.overview, vector_v2::kOverviewPixels);
  const bool restored_overview =
      !autosave_restored || vector_v2::replay_active_overview(log, startup_overview);
  const auto startup_tiled_may_ink =
      std::span(reinterpret_cast<std::uint8_t*>(storage.affected_keys), vector_v2::kOccupancyBytes);
  const bool restored_may_ink =
      !autosave_restored || vector_v2::build_tiled_may_ink(log, startup_tiled_may_ink);
  const bool canvas_bootstrapped =
      restored_overview && restored_may_ink &&
      (autosave_restored ? canvas.restore_snapshot(log.current_revision(), startup_overview,
                                                   startup_tiled_may_ink)
                         : canvas.reset_blank(log.current_revision()));
  if (!canvas_bootstrapped || !log.ready() || !presenter.ready() || !touch.ready() ||
      !touch_sampler.start() || !stroke.ready() || !producer.ready()) {
    std::printf(
        "TINYDRAW_LIVE_FAIL reason=bootstrap canvas=%u log=%u presenter=%u touch=%u builder=%u "
        "producer=%u\n",
        canvas.ready(), log.ready(), presenter.ready(), touch.ready(), stroke.ready(),
        producer.ready());
    return;
  }

  gpio_config_t button_config{};
  button_config.pin_bit_mask = 1ULL << static_cast<unsigned>(kModeButton);
  button_config.mode = GPIO_MODE_INPUT;
  button_config.pull_up_en = GPIO_PULLUP_ENABLE;
  static_cast<void>(gpio_config(&button_config));

  const PowerStatus initial_power = power.read();
  PowerStatus current_power = initial_power;
  vector_v2::ChromeState chrome;
  chrome.tool = vector_v2::ChromeTool::kDraw;
  chrome.size = vector_v2::ChromeSize::kLarge;
  chrome.palette_page = 0U;
  chrome.color_index = 12U;
  chrome.can_export = exporter.ready();
  chrome.can_sync_time = time_sync.available();
  static_cast<void>(sync_history_controls(chrome, log));
  chrome.battery_percentage = initial_power.percentage;
  chrome.battery_charging = initial_power.charging;
  VectorV2ChromeController chrome_controller(
      chrome, log, canvas, producer,
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
      std::span<const std::uint16_t>(storage.snapshot, vector_v2::kOverviewPixels),
#endif
      std::span<std::uint16_t>(storage.overview_scratch, vector_v2::kOverviewPixels), presenter,
      exporter, time_sync, clock);
  VectorV2BackgroundPipeline background(
      log, canvas, producer, workspace,
      {.operation_alpha = std::span(storage.settle_op_alpha, vector_v2::kTilePixels),
       .accumulated_alpha = std::span(storage.settle_accumulated, vector_v2::kTilePixels),
       .red = std::span(storage.settle_red, vector_v2::kTilePixels),
       .green = std::span(storage.settle_green, vector_v2::kTilePixels),
       .blue = std::span(storage.settle_blue, vector_v2::kTilePixels),
       .candidate_indices = std::span(storage.operation_candidates, vector_v2::kOperationCapacity)},
      std::span(storage.settle_pixels, vector_v2::kTilePixels), presenter, chrome);
  auto initial = presenter.refresh(chrome);
  print_presentation("startup", presenter, initial);
  for (int attempt = 1; !initial.passed && attempt < kStartupPresentationAttempts; ++attempt) {
    vTaskDelay(kStartupPresentationRetryDelay);
    initial = presenter.refresh(chrome);
    print_presentation("startup-retry", presenter, initial);
  }
  if (!initial.passed) {
    std::printf("TINYDRAW_LIVE_FAIL reason=startup_presentation attempts=%d\n",
                kStartupPresentationAttempts);
    return;
  }
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // The harness leaves the realistic document loaded for manual glass
  // testing, so a future red verdict must not exit the app before a human can
  // inspect the glass. Automation keys on the DONE-line verdict, not liveness.
  const bool harness_verdict =
      run_vector_v2_gate_harness(presenter, producer, log, canvas, touch_sampler, chrome, workspace,
                                 exporter, std::span(storage.snapshot, vector_v2::kOverviewPixels),
                                 std::span(storage.input_samples, kInputSampleCapacity),
                                 std::span(storage.harness_tile_scratch, vector_v2::kTilePixels));
  std::printf("TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=%u\n", harness_verdict);
#endif
  const std::size_t overview_bytes = vector_v2::kOverviewPixels * 4U * sizeof(std::uint16_t);
  const std::size_t raw_tile_bytes =
      vector_v2::kTileSlotCount * vector_v2::kTilePixels * sizeof(std::uint16_t);
  const std::size_t tile_metadata_bytes =
      vector_v2::kTileSlotCount * sizeof(MaterializedSlotStorage) +
      vector_v2::kMaterializedTileIdentityCount * sizeof(MaterializedUniformStorage) +
      vector_v2::kMaterializedTileIdentityCount * sizeof(std::uint16_t) +
      vector_v2::kOccupancyBytes;
  const std::size_t operation_bytes =
      vector_v2::kOperationCapacity * sizeof(OperationRecord) +
      vector_v2::kOperationSampleCapacity * sizeof(CompactOperationSample);
  const std::size_t operation_index_bytes =
      (vector_v2::operation_spatial_cell_word_count(vector_v2::kOperationCapacity) +
       vector_v2::operation_spatial_word_count(vector_v2::kOperationCapacity)) *
          sizeof(std::uint64_t) +
      vector_v2::kOperationCapacity * sizeof(std::uint16_t);
  const std::size_t live_scratch_bytes =
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
      vector_v2::kTilePixels * sizeof(std::uint16_t) +
#endif
      (vector_v2::kTileProducerPixels + kLiveRegionScratchPixels) * sizeof(std::uint16_t) +
      vector_v2::kTileProducerMaskBytes +
      vector_v2::kTileProducerSummaryRows * sizeof(std::uint16_t) +
      vector_v2::kTileProducerSummaryWords * sizeof(std::uint32_t) +
      vector_v2::kInPlaceTileMaskBytes + kInputSampleCapacity * sizeof(CompactOperationSample) +
      kVectorV2TouchEventCapacity * sizeof(vector_v2::TouchEvent) +
      (vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles) * sizeof(TileKey);
  const std::size_t live_storage_bytes = overview_bytes + raw_tile_bytes + tile_metadata_bytes +
                                         operation_bytes + operation_index_bytes +
                                         live_scratch_bytes;
  const auto tear_signal = display.tear_signal_timing();
  std::printf(
      "TINYDRAW_VECTOR_V2_READY zoom=25 controls=chrome button=cycle_all_zooms "
      "operations_capacity=%lu samples_capacity=%lu live_storage_bytes=%lu "
      "overview_bytes=%lu raw_tile_bytes=%lu tile_metadata_bytes=%lu operation_bytes=%lu "
      "operation_index_bytes=%lu live_scratch_bytes=%lu free_psram=%lu largest_psram=%lu "
      "te_edges=%lu te_period_us=%lld te_high_us=%lld te_level=%u main_stack_free=%lu\n",
      static_cast<unsigned long>(log.operation_capacity()),
      static_cast<unsigned long>(log.sample_capacity()),
      static_cast<unsigned long>(live_storage_bytes), static_cast<unsigned long>(overview_bytes),
      static_cast<unsigned long>(raw_tile_bytes), static_cast<unsigned long>(tile_metadata_bytes),
      static_cast<unsigned long>(operation_bytes),
      static_cast<unsigned long>(operation_index_bytes),
      static_cast<unsigned long>(live_scratch_bytes),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)),
      static_cast<unsigned long>(tear_signal.rising_edges),
      static_cast<long long>(tear_signal.period_us), static_cast<long long>(tear_signal.high_us),
      tear_signal.level, static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  std::fflush(stdout);

  bool pressed = false;
  bool toolbar_pressed = false;
  bool minimap_pressed = false;
  bool minimap_dock_candidate = false;
  bool popup_dismissed_press = false;
  std::optional<std::uint32_t> time_sync_terminal_since;
  bool panning = false;
  Point last_touch{};
  Point toolbar_start{};
  Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  Point pan_start{};
  int pan_start_x = 0;
  int pan_start_y = 0;
  PanMetrics pan_metrics{};
  std::uint32_t poll_previous_us = now_us();
  std::uint32_t poll_max_us = 0;
  std::uint32_t power_sampled_us = now_us();
  bool button_down = false;
  LiftBaselineTiming lift_timing{};
  std::uint32_t next_lift_id = 1U;
  std::uint32_t lift_reports_dropped = 0U;
  PendingStrokeReport stroke_report{};
  std::uint32_t autosave_checkpoint_retry_us = 0U;
  std::uint8_t background_ticks = 0U;

  const auto begin_pan = [&](Point start) {
    panning = true;
    pan_metrics.reset();
    // Boundary drain (committed-overlay design §3.4): the ring-reuse pan path
    // composes exposed strips without the pending overlay, so the canvas must
    // reach authority before the first pan present.
    if (vector_v2::pending_operation_count(log, canvas) != 0U) {
      static_cast<void>(background.drain_boundary(BackgroundDrainBoundary::kPan));
    }
    pan_start = start;
    pan_start_x = presenter.level_x();
    pan_start_y = presenter.level_y();
  };

  for (;;) {
    const std::uint32_t loop_us = now_us();
    poll_max_us = std::max(poll_max_us, loop_us - poll_previous_us);
    poll_previous_us = loop_us;

    if (chrome.export_status == vector_v2::ChromeExportStatus::kPresented &&
        exporter.usb_host_ejected()) {
      chrome.export_status = vector_v2::ChromeExportStatus::kHostEjected;
      const auto timing = presenter.refresh(chrome, loop_us);
      print_presentation("export-host-ejected", presenter, timing);
    }

    const auto next_time_sync_status = chrome_time_sync_status(time_sync.status());
    if (next_time_sync_status != chrome.time_sync_status) {
      chrome.time_sync_status = next_time_sync_status;
      chrome.popup = vector_v2::ChromePopup::kNone;
      if (next_time_sync_status == vector_v2::ChromeTimeSyncStatus::kSaved ||
          next_time_sync_status == vector_v2::ChromeTimeSyncStatus::kError) {
        time_sync_terminal_since = loop_us;
      } else {
        time_sync_terminal_since.reset();
      }
      const auto timing = presenter.refresh(chrome, loop_us);
      print_presentation("time-sync", presenter, timing);
    }
    if (time_sync_terminal_since.has_value() &&
        vector_v2::chrome_time_sync_status_after(chrome.time_sync_status,
                                                 loop_us - *time_sync_terminal_since) ==
            vector_v2::ChromeTimeSyncStatus::kIdle) {
      time_sync.dismiss();
      chrome.time_sync_status = vector_v2::ChromeTimeSyncStatus::kIdle;
      time_sync_terminal_since.reset();
      const auto timing = presenter.refresh(chrome, loop_us);
      print_presentation("time-sync-dismiss", presenter, timing);
    }

    // The battery redraw is a full-frame present (60-140 ms on dense
    // content); it is cosmetic and must never ride the post-lift window or
    // the drain (owner glass receipt: it was the 85 ms "lift spike").
    if (!pressed && !lift_timing.pending && !stroke_report.pending &&
        vector_v2::pending_operation_count(log, canvas) == 0U && power.ready() &&
        loop_us - power_sampled_us >= kPowerRefreshUs) {
      const PowerStatus next_power = power.read();
      power_sampled_us = loop_us;
      if (next_power.valid && next_power != current_power) {
        current_power = next_power;
        chrome.battery_percentage = current_power.percentage;
        chrome.battery_charging = current_power.charging;
        // A battery change re-presents only the battery overlay region
        // (owner question 2026-08-16: the full-frame refresh here cost
        // 60-140 ms on dense content for a cosmetic glyph).
        const vector_v2::ChromeRect battery = vector_v2::chrome_battery_region();
        const auto timing = presenter.refresh_region(
            {presenter.level_x() + battery.x0, presenter.level_y() + battery.y0,
             presenter.level_x() + battery.x1, presenter.level_y() + battery.y1},
            chrome, loop_us);
        print_presentation("power", presenter, timing);
      }
    }

    const bool export_mode_owns_input =
        chrome.export_status == vector_v2::ChromeExportStatus::kPresented ||
        chrome.export_status == vector_v2::ChromeExportStatus::kHostEjected ||
        chrome.export_status == vector_v2::ChromeExportStatus::kExitError;
    const bool next_button_down = gpio_get_level(kModeButton) == 0;
    if (next_button_down && !button_down) {
      button_down = true;
    } else if (!next_button_down && button_down) {
      button_down = false;
      if (!export_mode_owns_input) {
        const ZoomLevel zoom = vector_v2::next_zoom(presenter.zoom());
        const ZoomLevel target = zoom == presenter.zoom() ? ZoomLevel::k25Percent : zoom;
        const auto timing = presenter.set_zoom(target, chrome, loop_us);
        print_presentation("zoom", presenter, timing);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
        print_live_ledger("zoom");
#endif
      }
    }

    Point point{};
    const bool idle_before_poll = !pressed;
    const std::int64_t poll_started_us = esp_timer_get_time();
    const auto sampled_touch = touch_sampler.read_next();
    const std::int64_t poll_completed_us = esp_timer_get_time();
    const bool sample_ready = sampled_touch.has_value();
    if (sample_ready) {
      point = sampled_touch->point;
    }
    const bool lift_event = sample_ready && sampled_touch->kind == vector_v2::TouchEventKind::kUp;
    const bool point_event = sample_ready && (!lift_event || pressed);
    const std::uint32_t event_us = sample_ready ? sampled_touch->timestamp_us : loop_us;
    if (point_event) {
      if (!pressed && sampled_touch->kind == vector_v2::TouchEventKind::kDown) {
        pressed = true;
        last_touch = point;
        const bool export_toast = chrome.export_status == vector_v2::ChromeExportStatus::kSaved ||
                                  chrome.export_status == vector_v2::ChromeExportStatus::kError;
        const bool time_toast =
            chrome.time_sync_status == vector_v2::ChromeTimeSyncStatus::kSaved ||
            chrome.time_sync_status == vector_v2::ChromeTimeSyncStatus::kError;
        if (export_toast || time_toast) {
          chrome.export_status = vector_v2::ChromeExportStatus::kIdle;
          if (time_toast) {
            time_sync.dismiss();
            time_sync_terminal_since.reset();
          }
          chrome.time_sync_status = vector_v2::ChromeTimeSyncStatus::kIdle;
          popup_dismissed_press = true;
          static_cast<void>(presenter.refresh(chrome, loop_us));
        }
        if (popup_dismissed_press) {
          // Consume the complete gesture that dismisses a terminal toast so
          // it cannot also begin a stroke or navigation gesture beneath it.
        } else if (vector_v2::chrome_minimap_contains({point.x, point.y}, chrome)) {
          // The minimap is an absolute pointer for every tool: Down acquires
          // immediately, then every changed Move follows the finger without a
          // tiny-viewport grab requirement or a delayed promotion threshold.
          minimap_pressed = true;
          toolbar_start = point;
          begin_pan(point);
          pan_metrics.include(
              presenter.pan_minimap_from(pan_start_x, pan_start_y, point, chrome, event_us));
        } else if (vector_v2::chrome_minimap_dock_drag_candidate({point.x, point.y}, chrome)) {
          // This physical finger zone overlaps the size/document buttons.
          // Preserve a stationary toolbar tap, but let deliberate movement
          // promote the captured gesture into absolute minimap navigation.
          minimap_dock_candidate = true;
          toolbar_pressed = true;
          toolbar_start = point;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (vector_v2::chrome_contains({point.x, point.y}, chrome)) {
          toolbar_pressed = true;
          toolbar_start = point;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (chrome.popup != vector_v2::ChromePopup::kNone) {
          // A tap outside a compact popup dismisses it and consumes the
          // complete gesture. It must never leak through as a stroke or pan.
          chrome.popup = vector_v2::ChromePopup::kNone;
          popup_dismissed_press = true;
          const auto timing = presenter.refresh(chrome, loop_us);
          print_presentation("chrome-dismiss", presenter, timing);
        } else if (chrome.tool == vector_v2::ChromeTool::kPan) {
          begin_pan(point);
        } else {
          const OperationTool tool = chrome.tool == vector_v2::ChromeTool::kErase
                                         ? OperationTool::kEraser
                                         : OperationTool::kPen;
          const std::uint16_t color =
              tool == OperationTool::kEraser ? 0xFFFFU : vector_v2::selected_color(chrome);
          const std::uint16_t gesture_id = next_gesture++;
          if (next_gesture == 0U) {
            next_gesture = 1U;
          }
          static_cast<void>(stroke.begin(point, event_us, vector_v2::brush_size(chrome.size), tool,
                                         color, gesture_id, chrome));
        }
      } else if (minimap_pressed && (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        pan_metrics.include(
            presenter.pan_minimap_from(pan_start_x, pan_start_y, point, chrome, event_us));
      } else if (minimap_dock_candidate && (point.x != last_touch.x || point.y != last_touch.y)) {
        if (vector_v2::chrome_promotes_minimap_dock_drag({toolbar_start.x, toolbar_start.y},
                                                         {point.x, point.y}, chrome)) {
          minimap_dock_candidate = false;
          toolbar_pressed = false;
          toolbar_samples = 0;
          minimap_pressed = true;
          begin_pan(toolbar_start);
          last_touch = point;
          pan_metrics.include(
              presenter.pan_minimap_from(pan_start_x, pan_start_y, point, chrome, event_us));
        } else {
          toolbar_sum.x += point.x;
          toolbar_sum.y += point.y;
          ++toolbar_samples;
          last_touch = point;
        }
      } else if (toolbar_pressed && (point.x != last_touch.x || point.y != last_touch.y)) {
        if (vector_v2::chrome_promotes_pan_drag({toolbar_start.x, toolbar_start.y},
                                                {point.x, point.y}, chrome)) {
          toolbar_pressed = false;
          toolbar_samples = 0;
          begin_pan(toolbar_start);
          last_touch = point;
          pan_metrics.include(
              presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, event_us));
        } else {
          toolbar_sum.x += point.x;
          toolbar_sum.y += point.y;
          ++toolbar_samples;
          last_touch = point;
        }
      } else if (panning && (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        const auto timing =
            presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, event_us);
        pan_metrics.include(timing);
      } else if (stroke.active() && (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        const LiveStrokeMoveResult move = stroke.move(point, event_us, chrome);
        if (move.rejected) {
          if (sync_history_controls(chrome, log)) {
            background.mark_history_controls_dirty();
          }
          if (move.rejection_refresh.passed) {
            background.history_controls_presented();
          }
        }
      }
    }
    if (lift_event && pressed) {
      pressed = false;
      minimap_dock_candidate = false;
      if (popup_dismissed_press) {
        popup_dismissed_press = false;
      } else if (minimap_pressed) {
        minimap_pressed = false;
        panning = false;
        print_pan_baseline(presenter, pan_metrics);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
        print_live_ledger("minimap_pointer_end");
#endif
        std::fflush(stdout);
      } else if (toolbar_pressed) {
        toolbar_pressed = false;
        const float divisor = static_cast<float>(std::max<std::uint32_t>(1U, toolbar_samples));
        const Point tap{toolbar_sum.x / divisor, toolbar_sum.y / divisor};
        toolbar_samples = 0;
        if (vector_v2::chrome_contains({tap.x, tap.y}, chrome)) {
          const vector_v2::ChromeAction action =
              vector_v2::chrome_action_at({tap.x, tap.y}, chrome);
          const bool history_action =
              action == vector_v2::ChromeAction::kUndo || action == vector_v2::ChromeAction::kRedo;
          const vector_v2::AuthorityReadView authority_before = log.read_view();
          if (action == vector_v2::ChromeAction::kExport && autosave.ready() &&
              autosave.checkpoint_required()) {
            static_cast<void>(autosave.submit_checkpoint(log));
          }
          const bool save_ready = action != vector_v2::ChromeAction::kExport || !autosave.ready() ||
                                  autosave.flush(5'000U);
          const bool boundary_ready =
              save_ready &&
              (!history_action || background.drain_boundary(BackgroundDrainBoundary::kHistory));
          const bool applied = boundary_ready && chrome_controller.apply(action, tap);
          if (applied) {
            const vector_v2::AuthorityReadView authority_after = log.read_view();
            if (authority_after != authority_before) {
              if (action == vector_v2::ChromeAction::kConfirmNewDrawing) {
                static_cast<void>(autosave.submit_checkpoint(log));
              } else {
                static_cast<void>(
                    autosave.submit({.kind = vector_v2::JournalChangeKind::kUpdate}, log));
              }
            }
          }
          if (applied &&
              (history_action || action == vector_v2::ChromeAction::kConfirmNewDrawing)) {
            background.reset_document_state();
          }
        }
      } else if (panning) {
        panning = false;
        print_pan_baseline(presenter, pan_metrics);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
        print_live_ledger("pan_end");
#endif
        std::fflush(stdout);
      } else if (stroke.active()) {
        LiftBaselineTiming measured_lift{
            .id = next_lift_id++,
            .detected_us = esp_timer_get_time(),
        };
        const std::uint32_t finished_us = event_us;
        const LiveStrokeFinishResult finished = stroke.finish(finished_us, chrome);
        measured_lift.finish_preview_us = finished.finish_preview_us;
        measured_lift.builder_finish_us = finished.builder_finish_us;
        measured_lift.committed = finished.committed;
        measured_lift.commit_failed = finished.commit_failed;
        measured_lift.chunks = finished.chunks;
        measured_lift.append_us = finished.append_us;
        measured_lift.append_max_us = finished.append_max_us;
        if (finished.world_bounds.has_value()) {
          measured_lift.refresh_level_bounds =
              vector_v2::operation_level_bounds(*finished.world_bounds, presenter.zoom());
        }
        if (sync_history_controls(chrome, log)) {
          background.mark_history_controls_dirty();
        }
        measured_lift.refresh = finished.refresh;
        if (!finished.committed) {
          if (measured_lift.refresh.passed) {
            background.history_controls_presented();
          }
        } else if (finished.world_bounds.has_value()) {
          // Committed-overlay lift (design §5.4): no synchronous refresh.
          // Glass keeps the preview; the idle drain absorbs the pending
          // range and then runs one exact swap refresh over the union of
          // undrained stroke bounds.
          background.note_committed_bounds(*finished.world_bounds);
        }
        measured_lift.refresh_wall_us = finished.refresh_wall_us;
        if (log.operation_count() > finished.first_operation) {
          const std::int64_t autosave_started = esp_timer_get_time();
          const bool queued = autosave.submit({.kind = vector_v2::JournalChangeKind::kAppendStroke,
                                               .first_operation = finished.first_operation},
                                              log);
          measured_lift.stroke_logging_us = esp_timer_get_time() - autosave_started;
          if (!queued) {
            std::printf("TINYDRAW_AUTOSAVE_QUEUE_FAIL site=stroke generation=%lu\n",
                        static_cast<unsigned long>(log.current_revision().value));
          }
        }
        if (stroke_report.pending || lift_timing.pending) {
          ++lift_reports_dropped;
        }
        const TouchSamplerMetrics touch_metrics = touch_sampler.take_metrics();
        stroke_report = {
            .revision = canvas.current_revision(),
            .operation_count = log.operation_count(),
            .sample_count = log.sample_count(),
            .append_us = measured_lift.append_us,
            .append_max_us = measured_lift.append_max_us,
            .refresh = measured_lift.refresh,
            .metrics = finished.metrics,
            .poll_max_us = poll_max_us,
            .touch = touch_metrics,
            .phase_max = finished.phase_max,
            .drops = finished.drops,
            .chunks = measured_lift.chunks,
            .free_psram = heap_caps_get_free_size(kExternalCaps),
            .largest_psram = heap_caps_get_largest_free_block(kExternalCaps),
            .authority_match = log.current_revision() == canvas.current_revision(),
            .committed = measured_lift.committed,
            .commit_failed = measured_lift.commit_failed,
            .pending = true,
        };
        measured_lift.pending = true;
        lift_timing = measured_lift;
        poll_max_us = 0;
      }
    }

    // Queue/resync failures collapse into one complete checkpoint after input
    // is quiet. Flash work remains on the worker; this call only snapshots
    // coherent vector authority into immutable PSRAM.
    if (!pressed && !panning && !sample_ready && !lift_timing.pending && autosave.ready() &&
        autosave.checkpoint_required() &&
        static_cast<std::int32_t>(loop_us - autosave_checkpoint_retry_us) >= 0) {
      const std::int64_t checkpoint_started = esp_timer_get_time();
      const bool queued = autosave.submit_checkpoint(log);
      std::printf("TINYDRAW_AUTOSAVE_CHECKPOINT queued=%u encode_us=%lld generation=%lu\n", queued,
                  static_cast<long long>(esp_timer_get_time() - checkpoint_started),
                  static_cast<unsigned long>(log.current_revision().value));
      std::fflush(stdout);
      if (!queued) {
        autosave_checkpoint_retry_us = loop_us + 500'000U;
      }
    }

    const BackgroundSliceResult background_result = background.run_slice({
        .loop_us = loop_us,
        .pressed = pressed,
        .panning = panning,
        .sample_ready = sample_ready,
        .lift_report_pending = lift_timing.pending,
    });
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
    if (background_result.drain_completed) {
      print_live_ledger("drain");
    }
#endif
    if (lift_timing.pending && stroke_report.pending && idle_before_poll && !pressed) {
      print_stroke(stroke_report);
      std::printf("TINYDRAW_LIVE_STROKE_DONE committed=%u refresh=%u commit_failed=%u\n",
                  stroke_report.committed, stroke_report.refresh.passed,
                  stroke_report.commit_failed);
      print_lift_baseline(lift_timing, poll_started_us, poll_completed_us, lift_reports_dropped);
      std::fflush(stdout);
      lift_reports_dropped = 0U;
      stroke_report.pending = false;
      lift_timing.pending = false;
      // This diagnostic write happens only after a completed input poll. Keep
      // it out of the pre-existing stroke poll-gap metric.
      poll_previous_us = now_us();
    }
    if (background_result.fill_busy) {
      // Producer calls are bounded input-poll slices. Avoid paying a fixed
      // two-millisecond tax after every slice, but periodically unblock idle.
      if (++background_ticks == 8U) {
        background_ticks = 0U;
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    } else {
      background_ticks = 0U;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_vector_v2_app(); }
