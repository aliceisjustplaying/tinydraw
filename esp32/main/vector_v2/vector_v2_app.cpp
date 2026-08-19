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

enum class InteractionMode : std::uint8_t {
  kIdle,
  kDismissOverlay,
  kMinimapPan,
  kMinimapDockCandidate,
  kToolbarCandidate,
  kPan,
  kStroke,
};

constexpr bool interaction_active(InteractionMode mode) { return mode != InteractionMode::kIdle; }

constexpr bool navigation_active(InteractionMode mode) {
  return mode == InteractionMode::kMinimapPan || mode == InteractionMode::kPan;
}

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
      "TINYDRAW_PRODUCER_SCRATCH supertask_internal=%u settle_internal=%u free_internal=%lu "
      "free_psram=%lu\n",
      storage.supertask_internal, storage.settle_internal,
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)));
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  std::fill_n(storage.snapshot, vector_v2::kOverviewPixels, 0xFFFFU);
#endif
  std::fill_n(storage.overview, vector_v2::kOverviewPixels, 0xFFFFU);

  MaterializedCanvas canvas({
      .overview_pixels = std::span(storage.overview, vector_v2::kOverviewPixels),
      .uniform_catalog = std::span(storage.uniforms, vector_v2::kMaterializedTileIdentityCount),
      .occupancy_bits = std::span(storage.occupancy, vector_v2::kOccupancyBytes),
      .slots = std::span(storage.slots, vector_v2::kTileSlotCount),
      .tile_pixels =
          std::span(storage.tile_pixels, vector_v2::kTileSlotCount * vector_v2::kTilePixels),
      .initial_revision = DocumentRevision{},
      .raw_slot_directory =
          std::span(storage.raw_slot_directory, vector_v2::kMaterializedTileIdentityCount),
  });
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
      .operation_chord_plans = std::as_writable_bytes(
          std::span(storage.producer_chord_plans, vector_v2::kOperationChordStorageBytes / 4U)),
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
  const bool harness_verdict = run_vector_v2_gate_harness(
      presenter, producer, log, canvas, touch_sampler, chrome, workspace, exporter,
      std::span(storage.snapshot, vector_v2::kOverviewPixels),
      std::span(storage.input_samples, kInputSampleCapacity),
      std::span(storage.harness_tile_scratch, vector_v2::kTilePixels),
      std::span(storage.overview_scratch, vector_v2::kOverviewPixels),
      {.operation_alpha = std::span(storage.settle_op_alpha, vector_v2::kTilePixels),
       .accumulated_alpha = std::span(storage.settle_accumulated, vector_v2::kTilePixels),
       .red = std::span(storage.settle_red, vector_v2::kTilePixels),
       .green = std::span(storage.settle_green, vector_v2::kTilePixels),
       .blue = std::span(storage.settle_blue, vector_v2::kTilePixels),
       .candidate_indices = std::span(storage.operation_candidates, vector_v2::kOperationCapacity)},
      std::span(storage.settle_pixels, vector_v2::kTilePixels));
  std::printf("TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=%u\n", harness_verdict);
#endif
  const std::size_t overview_bytes = vector_v2::kOverviewPixels *
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
                                     4U *
#else
                                     3U *
#endif
                                     sizeof(std::uint16_t);
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

  InteractionMode interaction = InteractionMode::kIdle;
  std::optional<std::uint32_t> time_sync_terminal_since;
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
  std::uint32_t next_lift_id = 1U;
  std::uint32_t lift_reports_dropped = 0U;
  PendingStrokeReport stroke_report{};
  std::uint32_t autosave_checkpoint_retry_us = 0U;
  std::uint8_t background_ticks = 0U;
  std::uint8_t drained_touch_events = 0U;

  const auto advance_full_refresh = [&](const char* kind, std::uint32_t event_us) {
    const auto timing = presenter.refresh_slice(chrome, event_us, interaction_active(interaction));
    if (!timing.compose_pending) {
      std::printf(
          "TINYDRAW_COOPERATIVE_COMPOSE kind=%s slices=%lu max_slice_us=%lld compose_us=%lld "
          "passed=%u\n",
          kind, static_cast<unsigned long>(timing.compose_slices),
          static_cast<long long>(timing.compose_slice_max_us),
          static_cast<long long>(timing.compose_us), timing.passed);
      print_presentation(kind, presenter, timing);
    }
    return timing;
  };

  const auto begin_pan = [&](Point start, InteractionMode mode) {
    interaction = mode;
    pan_metrics.reset();
    pan_start = start;
    pan_start_x = presenter.level_x();
    pan_start_y = presenter.level_y();
  };

  for (;;) {
    const std::uint32_t loop_us = now_us();
    poll_max_us = std::max(poll_max_us, loop_us - poll_previous_us);
    poll_previous_us = loop_us;

    Point point{};
    const bool idle_before_poll = !interaction_active(interaction);
    const std::int64_t poll_started_us = esp_timer_get_time();
    const auto sampled_touch = touch_sampler.read_next();
    const std::int64_t poll_completed_us = esp_timer_get_time();
    const bool sample_ready = sampled_touch.has_value();
    if (sample_ready) {
      point = sampled_touch->point;
    }
    const bool lift_event = sample_ready && sampled_touch->kind == vector_v2::TouchEventKind::kUp;
    const bool point_event = sample_ready && (!lift_event || interaction_active(interaction));
    const std::uint32_t event_us = sample_ready ? sampled_touch->timestamp_us : loop_us;
    bool cosmetic_work = false;

    if (!sample_ready && !touch_sampler.touch_urgent() && presenter.refresh_pending()) {
      static_cast<void>(advance_full_refresh("deferred-full", loop_us));
      // One input poll per bounded compose slice. Background canvas mutation
      // waits until the complete frame has entered its single panel sweep.
      if (presenter.refresh_composing()) {
        continue;
      }
    }

    if (!sample_ready && chrome.export_status == vector_v2::ChromeExportStatus::kPresented &&
        !touch_sampler.touch_urgent() && exporter.usb_host_ejected()) {
      chrome.export_status = exporter.stop_usb() ? vector_v2::ChromeExportStatus::kIdle
                                                 : vector_v2::ChromeExportStatus::kExitError;
      static_cast<void>(advance_full_refresh("export-host-ejected", loop_us));
      cosmetic_work = true;
    }

    const auto next_time_sync_status = chrome_time_sync_status(time_sync.status());
    if (!sample_ready && !cosmetic_work && !touch_sampler.touch_urgent() &&
        next_time_sync_status != chrome.time_sync_status) {
      chrome.time_sync_status = next_time_sync_status;
      chrome.popup = vector_v2::ChromePopup::kNone;
      if (next_time_sync_status == vector_v2::ChromeTimeSyncStatus::kSaved ||
          next_time_sync_status == vector_v2::ChromeTimeSyncStatus::kError) {
        time_sync_terminal_since = loop_us;
      } else {
        time_sync_terminal_since.reset();
      }
      static_cast<void>(advance_full_refresh("time-sync", loop_us));
      cosmetic_work = true;
    }
    if (!sample_ready && !cosmetic_work && !touch_sampler.touch_urgent() &&
        time_sync_terminal_since.has_value() &&
        vector_v2::chrome_time_sync_status_after(chrome.time_sync_status,
                                                 loop_us - *time_sync_terminal_since) ==
            vector_v2::ChromeTimeSyncStatus::kIdle) {
      time_sync.dismiss();
      chrome.time_sync_status = vector_v2::ChromeTimeSyncStatus::kIdle;
      time_sync_terminal_since.reset();
      static_cast<void>(advance_full_refresh("time-sync-dismiss", loop_us));
      cosmetic_work = true;
    }

    // The battery redraw is a full-frame present (60-140 ms on dense
    // content); it is cosmetic and must never ride the post-lift window or
    // the drain (owner glass receipt: it was the 85 ms "lift spike").
    if (!sample_ready && !cosmetic_work && !touch_sampler.touch_urgent() &&
        !interaction_active(interaction) && !stroke_report.pending &&
        vector_v2::pending_operation_count(log, canvas) == 0U && power.ready() &&
        loop_us - power_sampled_us >= kPowerRefreshUs) {
      cosmetic_work = true;
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

    if (!sample_ready && !cosmetic_work && !touch_sampler.touch_urgent()) {
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
          cosmetic_work = true;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
          print_live_ledger("zoom");
#endif
        }
      }
    }
    if (cosmetic_work) {
      continue;
    }
    if (point_event) {
      if (!interaction_active(interaction) &&
          sampled_touch->kind == vector_v2::TouchEventKind::kDown) {
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
          interaction = InteractionMode::kDismissOverlay;
          static_cast<void>(advance_full_refresh("toast-dismiss", loop_us));
        }
        if (interaction == InteractionMode::kDismissOverlay) {
          // Consume the complete gesture that dismisses a terminal toast so
          // it cannot also begin a stroke or navigation gesture beneath it.
        } else if (vector_v2::chrome_minimap_contains({point.x, point.y}, chrome)) {
          // The minimap is an absolute pointer for every tool: Down acquires
          // immediately, then every changed Move follows the finger without a
          // tiny-viewport grab requirement or a delayed promotion threshold.
          toolbar_start = point;
          begin_pan(point, InteractionMode::kMinimapPan);
          pan_metrics.include(
              presenter.pan_minimap_from(pan_start_x, pan_start_y, point, chrome, event_us));
        } else if (vector_v2::chrome_minimap_dock_drag_candidate({point.x, point.y}, chrome)) {
          // This physical finger zone overlaps the size/document buttons.
          // Preserve a stationary toolbar tap, but let deliberate movement
          // promote the captured gesture into absolute minimap navigation.
          interaction = InteractionMode::kMinimapDockCandidate;
          toolbar_start = point;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (vector_v2::chrome_contains({point.x, point.y}, chrome)) {
          interaction = InteractionMode::kToolbarCandidate;
          toolbar_start = point;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (chrome.popup != vector_v2::ChromePopup::kNone) {
          // A tap outside a compact popup dismisses it and consumes the
          // complete gesture. It must never leak through as a stroke or pan.
          chrome.popup = vector_v2::ChromePopup::kNone;
          interaction = InteractionMode::kDismissOverlay;
          static_cast<void>(advance_full_refresh("chrome-dismiss", loop_us));
        } else if (chrome.tool == vector_v2::ChromeTool::kPan) {
          begin_pan(point, InteractionMode::kPan);
        } else {
          interaction = InteractionMode::kStroke;
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
      } else if (interaction == InteractionMode::kMinimapPan &&
                 (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        pan_metrics.include(
            presenter.pan_minimap_from(pan_start_x, pan_start_y, point, chrome, event_us));
      } else if (interaction == InteractionMode::kMinimapDockCandidate &&
                 (point.x != last_touch.x || point.y != last_touch.y)) {
        if (vector_v2::chrome_promotes_minimap_dock_drag({toolbar_start.x, toolbar_start.y},
                                                         {point.x, point.y}, chrome)) {
          toolbar_samples = 0;
          begin_pan(toolbar_start, InteractionMode::kMinimapPan);
          last_touch = point;
          pan_metrics.include(
              presenter.pan_minimap_from(pan_start_x, pan_start_y, point, chrome, event_us));
        } else {
          toolbar_sum.x += point.x;
          toolbar_sum.y += point.y;
          ++toolbar_samples;
          last_touch = point;
        }
      } else if (interaction == InteractionMode::kToolbarCandidate &&
                 (point.x != last_touch.x || point.y != last_touch.y)) {
        if (vector_v2::chrome_promotes_pan_drag({toolbar_start.x, toolbar_start.y},
                                                {point.x, point.y}, chrome)) {
          toolbar_samples = 0;
          begin_pan(toolbar_start, InteractionMode::kPan);
          last_touch = point;
          pan_metrics.include(
              presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, event_us));
        } else {
          toolbar_sum.x += point.x;
          toolbar_sum.y += point.y;
          ++toolbar_samples;
          last_touch = point;
        }
      } else if (interaction == InteractionMode::kPan &&
                 (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        const auto timing =
            presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, event_us);
        pan_metrics.include(timing);
      } else if (interaction == InteractionMode::kStroke && stroke.active() &&
                 (point.x != last_touch.x || point.y != last_touch.y)) {
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
    if (lift_event && interaction_active(interaction)) {
      const InteractionMode completed_interaction = interaction;
      interaction = InteractionMode::kIdle;
      if (completed_interaction == InteractionMode::kDismissOverlay) {
        // The gesture was fully consumed when the overlay was dismissed.
      } else if (completed_interaction == InteractionMode::kMinimapPan) {
        print_pan_baseline(presenter, pan_metrics);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
        print_live_ledger("minimap_pointer_end");
#endif
        std::fflush(stdout);
      } else if (completed_interaction == InteractionMode::kToolbarCandidate ||
                 completed_interaction == InteractionMode::kMinimapDockCandidate) {
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
            // Export is already a non-interactive ownership boundary. Finish
            // an unusual outstanding checkpoint here so one tap still saves.
            bool queued = false;
            for (std::size_t attempt = 0U; attempt < 2U && !queued; ++attempt) {
              queued = autosave.submit_checkpoint(log);
              while (!queued && autosave.checkpoint_staging()) {
                queued = autosave.submit_checkpoint(log);
              }
            }
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
            if (const auto damage = chrome_controller.take_history_damage();
                history_action && damage.has_value()) {
              // The refill of this damage presents once, exactly, when done.
              background.hold_history_damage(*damage);
            }
          }
        }
      } else if (completed_interaction == InteractionMode::kPan) {
        print_pan_baseline(presenter, pan_metrics);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
        print_live_ledger("pan_end");
#endif
        std::fflush(stdout);
      } else if (completed_interaction == InteractionMode::kStroke && stroke.active()) {
        PendingStrokeReport report{};
        report.detected_us = esp_timer_get_time();
        report.id = next_lift_id++;
        const std::uint32_t finished_us = event_us;
        const LiveStrokeFinishResult finished = stroke.finish(finished_us, chrome);
        report.finish_preview_us = finished.finish_preview_us;
        report.builder_finish_us = finished.builder_finish_us;
        report.committed = finished.committed;
        report.commit_failed = finished.commit_failed;
        report.chunks = finished.chunks;
        report.append_us = finished.append_us;
        report.append_max_us = finished.append_max_us;
        if (finished.world_bounds.has_value()) {
          report.refresh_level_bounds =
              vector_v2::operation_level_bounds(*finished.world_bounds, presenter.zoom());
        }
        if (sync_history_controls(chrome, log)) {
          background.mark_history_controls_dirty();
        }
        report.refresh = finished.refresh;
        if (!finished.committed) {
          if (report.refresh.passed) {
            background.history_controls_presented();
          }
        } else if (finished.world_bounds.has_value()) {
          // Committed-overlay lift (design §5.4): no synchronous refresh.
          // Glass keeps the preview; the idle drain absorbs the pending
          // range and then runs one exact swap refresh over the union of
          // undrained stroke bounds.
          background.note_committed_bounds(*finished.world_bounds);
        }
        report.refresh_wall_us = finished.refresh_wall_us;
        if (log.operation_count() > finished.first_operation) {
          const std::int64_t autosave_started = esp_timer_get_time();
          const bool queued = autosave.submit({.kind = vector_v2::JournalChangeKind::kAppendStroke,
                                               .first_operation = finished.first_operation},
                                              log);
          report.stroke_logging_us = esp_timer_get_time() - autosave_started;
          if (!queued) {
            std::printf("TINYDRAW_AUTOSAVE_QUEUE_FAIL site=stroke generation=%lu\n",
                        static_cast<unsigned long>(log.current_revision().value));
          }
        }
        if (stroke_report.pending) {
          ++lift_reports_dropped;
        }
        const TouchSamplerMetrics touch_metrics = touch_sampler.take_metrics();
        report.revision = canvas.current_revision();
        report.operation_count = log.operation_count();
        report.sample_count = log.sample_count();
        report.metrics = finished.metrics;
        report.poll_max_us = poll_max_us;
        report.touch = touch_metrics;
        report.phase_max = finished.phase_max;
        report.drops = finished.drops;
        report.free_psram = heap_caps_get_free_size(kExternalCaps);
        report.largest_psram = heap_caps_get_largest_free_block(kExternalCaps);
        report.authority_match = log.current_revision() == canvas.current_revision();
        report.pending = true;
        stroke_report = report;
        poll_max_us = 0;
      }
    }

    if (presenter.refresh_composing()) {
      // The first slice can be requested from inside input handling. Keep the
      // canvas source epoch stable until the next input poll advances it.
      continue;
    }

    // Queue/resync failures collapse into one complete checkpoint after input
    // is quiet. Flash work remains on the worker; this call only snapshots
    // coherent vector authority into immutable PSRAM.
    const bool touch_backlog = touch_sampler.touch_urgent();
    if (!interaction_active(interaction) && !sample_ready && !touch_backlog &&
        !touch_sampler.touch_urgent() && !stroke_report.pending && autosave.ready() &&
        autosave.checkpoint_required() &&
        static_cast<std::int32_t>(loop_us - autosave_checkpoint_retry_us) >= 0) {
      const std::int64_t checkpoint_started = esp_timer_get_time();
      const bool queued = autosave.submit_checkpoint(log);
      const bool staging = autosave.checkpoint_staging();
      if (queued || !staging) {
        std::printf("TINYDRAW_AUTOSAVE_CHECKPOINT queued=%u stage_us=%lld generation=%lu\n", queued,
                    static_cast<long long>(esp_timer_get_time() - checkpoint_started),
                    static_cast<unsigned long>(log.current_revision().value));
        std::fflush(stdout);
      }
      if (!queued && !staging) {
        autosave_checkpoint_retry_us = loop_us + 500'000U;
      }
    }

    const bool background_urgent = touch_backlog || touch_sampler.touch_urgent();
    const BackgroundSliceResult background_result = background.run_slice({
        .loop_us = loop_us,
        .pressed = interaction_active(interaction),
        .panning = navigation_active(interaction),
        .sample_ready = sample_ready || background_urgent,
        .lift_report_pending = stroke_report.pending,
        .touch_urgency = touch_sampler.urgency_probe(),
    });
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
    if (background_result.drain_completed) {
      print_live_ledger("drain");
    }
#endif
    if (stroke_report.pending && idle_before_poll && !interaction_active(interaction) &&
        !touch_sampler.touch_urgent()) {
      print_stroke(stroke_report);
      std::printf("TINYDRAW_LIVE_STROKE_DONE committed=%u refresh=%u commit_failed=%u\n",
                  stroke_report.committed, stroke_report.refresh.passed,
                  stroke_report.commit_failed);
      print_lift_baseline(stroke_report, poll_started_us, poll_completed_us, lift_reports_dropped);
      std::fflush(stdout);
      lift_reports_dropped = 0U;
      stroke_report.pending = false;
      // This diagnostic write happens only after a completed input poll. Keep
      // it out of the pre-existing stroke poll-gap metric.
      poll_previous_us = now_us();
    }
    if (background_urgent || touch_sampler.touch_urgent()) {
      // Drain queued semantic edges and the newest coalesced Move before any
      // more background work, diagnostics, or cosmetic presentation.
      background_ticks = 0U;
      if (++drained_touch_events == 8U) {
        drained_touch_events = 0U;
        taskYIELD();
      }
      continue;
    }
    drained_touch_events = 0U;
    if (background_result.fill_busy) {
      // Producer calls are bounded input-poll slices. Avoid paying a fixed
      // two-millisecond tax after every slice, but periodically unblock idle.
      if (++background_ticks == 8U) {
        background_ticks = 0U;
        static_cast<void>(touch_sampler.wait_for_event(pdMS_TO_TICKS(1)));
      }
    } else {
      background_ticks = 0U;
      static_cast<void>(touch_sampler.wait_for_event(pdMS_TO_TICKS(2)));
    }
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_vector_v2_app(); }
