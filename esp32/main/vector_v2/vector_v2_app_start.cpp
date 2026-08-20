#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vector_v2_app.h"
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
#include "vector_v2_gate_harness.h"
#endif
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_app_diagnostics.h"
#include "vector_v2_app_storage.h"
#include "vector_v2_autosave_store.h"
#include "vector_v2_background_pipeline.h"
#include "vector_v2_chrome_controller.h"
#ifdef TINYDRAW_VECTOR_V2_DEMO
#include "vector_v2_demo_controller.h"
#endif
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
using vector_v2::TileKey;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr gpio_num_t kModeButton = GPIO_NUM_0;
constexpr int kStartupPresentationAttempts = 3;
constexpr TickType_t kStartupPresentationRetryDelay = pdMS_TO_TICKS(20);
#ifdef TINYDRAW_VECTOR_V2_DEMO
constexpr std::size_t kDemoCapacity = 16'384U;
#endif

class TouchSamplerStartGuard {
 public:
  explicit TouchSamplerStartGuard(VectorV2TouchSampler& sampler) : sampler_(sampler) {}

  ~TouchSamplerStartGuard() {
    if (started_) {
      sampler_.stop();
    }
  }

  [[nodiscard]] bool start() {
    started_ = sampler_.start();
    return started_;
  }

  void release() { started_ = false; }

 private:
  VectorV2TouchSampler& sampler_;
  bool started_ = false;
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

}  // namespace

bool vector_v2_app_start(VectorV2AppSession& session) {
  AppStorage& storage = session.storage;
  if (!storage.allocate()) {
    std::printf("TINYDRAW_LIVE_FAIL reason=allocation free_psram=%lu largest_psram=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
    return false;
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

  session.canvas.emplace(vector_v2::MaterializedCanvasStorage{
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
  MaterializedCanvas& canvas = *session.canvas;
  session.operation_spatial_index.emplace(
      vector_v2::kOperationCapacity,
      std::span(storage.operation_spatial_cells,
                vector_v2::operation_spatial_cell_word_count(vector_v2::kOperationCapacity)),
      std::span(storage.operation_spatial_large,
                vector_v2::operation_spatial_word_count(vector_v2::kOperationCapacity)));
  session.log.emplace(std::span(storage.records, vector_v2::kOperationCapacity),
                      std::span(storage.samples, vector_v2::kOperationSampleCapacity),
                      &*session.operation_spatial_index);
  OperationLog& log = *session.log;
  vector_v2::NavigationState& navigation = session.navigation;
  session.autosave.emplace();
  VectorV2AutosaveStore& autosave = *session.autosave;
  const VectorV2AutosaveRestoreStatus autosave_restore = autosave.restore(log);
  const bool autosave_restored = autosave_restore == VectorV2AutosaveRestoreStatus::kRestored ||
                                 autosave_restore == VectorV2AutosaveRestoreStatus::kRecoveredTail;
  session.next_gesture = next_gesture_id(log);
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
  session.display.emplace();
  Co5300PanelTransport& display = *session.display;
  session.touch.emplace();
  PhysicalTouch& touch = *session.touch;
  session.power.emplace(touch.bus());
  PowerManager& power = *session.power;
  session.clock.emplace(touch.bus());
  RtcClock& clock = *session.clock;
  session.time_sync.emplace(clock);
  TimeSyncController& time_sync = *session.time_sync;
  session.touch_sampler.emplace(touch,
                                std::span(storage.touch_events, kVectorV2TouchEventCapacity));
  VectorV2TouchSampler& touch_sampler = *session.touch_sampler;
  session.exporter.emplace();
  VectorV2Export& exporter = *session.exporter;
  session.presenter.emplace(canvas, navigation, display,
                            std::span(storage.frame, vector_v2::kOverviewPixels),
                            std::span(storage.region_scratch, kLiveRegionScratchPixels),
                            std::span(storage.chrome_cache, vector_v2::kChromeStagingCachePixels));
  VectorV2Presenter& presenter = *session.presenter;
  presenter.attach_authority(log);
  session.producer.emplace(
      log, canvas,
      vector_v2::TileProducerWorkspace{
          .supertask_pixels = std::span(storage.producer_supertask, vector_v2::kTileProducerPixels),
          .finalized_pixels = std::span(storage.producer_mask, vector_v2::kTileProducerMaskBytes),
          .summary_row_unset =
              std::span(storage.producer_summary_rows, vector_v2::kTileProducerSummaryRows),
          .summary_saturated_words =
              std::span(storage.producer_summary_words, vector_v2::kTileProducerSummaryWords),
          .operation_chord_plans = std::as_writable_bytes(
              std::span(storage.producer_chord_plans, vector_v2::kOperationChordStorageBytes / 4U)),
          .candidate_indices =
              std::span(storage.operation_candidates, vector_v2::kOperationCapacity)});
  vector_v2::TileProducer& producer = *session.producer;
  // Re-render truth: every completed group render is classified against the
  // damage/eviction state the canvas reports (déjà-vu oracle; ~27.5 KiB).
  // The gate-only re-render ledger can speak during diagnostic glass sessions.
  // Deltas since the previous print attribute each transition's re-renders to a
  // cause; cumulative amplification rides along.
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  session.rerender_ledger.emplace(
      std::span(storage.rerender_entries, vector_v2::kRerenderLedgerEntryCount));
  canvas.set_rerender_ledger(&*session.rerender_ledger);
  producer.set_rerender_ledger(&*session.rerender_ledger);
#endif
  session.workspace = vector_v2::InPlaceAppendWorkspace{
      .overview_scratch = std::span(storage.overview_scratch, vector_v2::kOverviewPixels),
      .affected_keys = std::span(storage.affected_keys,
                                 vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles),
      .tile_mask = std::span(storage.chunk_mask, vector_v2::kInPlaceTileMaskBytes),
      .operation_chord_plans = std::as_writable_bytes(
          std::span(storage.producer_chord_plans, vector_v2::kOperationChordStorageBytes / 4U)),
  };
  session.stroke.emplace(std::span(storage.input_samples, kInputSampleCapacity), log, presenter);
  LiveStrokeSession& stroke = *session.stroke;
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
  TouchSamplerStartGuard touch_sampler_start{touch_sampler};
  if (!canvas_bootstrapped || !log.ready() || !presenter.ready() || !touch.ready() ||
      !touch_sampler_start.start() || !stroke.ready() || !producer.ready()) {
    std::printf(
        "TINYDRAW_LIVE_FAIL reason=bootstrap canvas=%u log=%u presenter=%u touch=%u builder=%u "
        "producer=%u\n",
        canvas.ready(), log.ready(), presenter.ready(), touch.ready(), stroke.ready(),
        producer.ready());
    return false;
  }

  gpio_config_t button_config{};
  button_config.pin_bit_mask = 1ULL << static_cast<unsigned>(kModeButton);
  button_config.mode = GPIO_MODE_INPUT;
  button_config.pull_up_en = GPIO_PULLUP_ENABLE;
  static_cast<void>(gpio_config(&button_config));

  const PowerStatus initial_power = power.read();
  session.current_power = initial_power;
  vector_v2::ChromeState& chrome = session.chrome;
  chrome.tool = vector_v2::ChromeTool::kDraw;
  chrome.size = vector_v2::ChromeSize::kLarge;
  chrome.palette_page = 0U;
  chrome.color_index = 12U;
  chrome.can_export = exporter.ready();
  chrome.can_sync_time = time_sync.available();
  static_cast<void>(sync_history_controls(chrome, log));
  chrome.battery_percentage = initial_power.percentage;
  chrome.battery_charging = initial_power.charging;
  session.chrome_controller.emplace(
      chrome, log, canvas, producer,
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
      std::span<const std::uint16_t>(storage.snapshot, vector_v2::kOverviewPixels),
#endif
      std::span<std::uint16_t>(storage.overview_scratch, vector_v2::kOverviewPixels), presenter,
      exporter, time_sync, clock);
  session.background.emplace(
      log, canvas, producer, session.workspace,
      vector_v2::SettledTileWorkspace{
          .operation_alpha = std::span(storage.settle_op_alpha, vector_v2::kTilePixels),
          .accumulated_alpha = std::span(storage.settle_accumulated, vector_v2::kTilePixels),
          .red = std::span(storage.settle_red, vector_v2::kTilePixels),
          .green = std::span(storage.settle_green, vector_v2::kTilePixels),
          .blue = std::span(storage.settle_blue, vector_v2::kTilePixels),
          .candidate_indices =
              std::span(storage.operation_candidates, vector_v2::kOperationCapacity)},
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
    return false;
  }
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // The harness leaves the realistic document loaded for manual glass
  // testing, so a future red verdict must not exit the app before a human can
  // inspect the glass. Automation keys on the DONE-line verdict, not liveness.
  const bool harness_verdict = run_vector_v2_gate_harness(
      presenter, producer, log, canvas, touch_sampler, chrome, session.workspace, exporter,
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

#ifdef TINYDRAW_VECTOR_V2_DEMO
  session.demo_samples.reset(static_cast<vector_v2::DemoSample*>(
      heap_caps_malloc(kDemoCapacity * sizeof(vector_v2::DemoSample), kExternalCaps)));
  session.demo.emplace(session.demo_samples == nullptr
                           ? std::span<vector_v2::DemoSample>{}
                           : std::span(session.demo_samples.get(), kDemoCapacity));
  if (session.demo_samples == nullptr) {
    std::printf("TINYDRAW_DEMO_DISABLED reason=allocation bytes=%lu free_psram=%lu\n",
                static_cast<unsigned long>(kDemoCapacity * sizeof(vector_v2::DemoSample)),
                static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)));
  } else if (!session.demo->ready()) {
    session.demo_samples.reset();
    std::printf("TINYDRAW_DEMO_DISABLED reason=timer\n");
  } else {
    std::printf(
        "TINYDRAW_DEMO_READY capacity=%lu bytes=%lu controls=long_record_stop_replay "
        "short=zoom\n",
        static_cast<unsigned long>(kDemoCapacity),
        static_cast<unsigned long>(kDemoCapacity * sizeof(vector_v2::DemoSample)));
  }
#endif

  session.interaction = InteractionMode::kIdle;
  session.time_sync_terminal_since.reset();
  session.last_touch = {};
  session.toolbar_start = {};
  session.toolbar_sum = {};
  session.toolbar_samples = 0U;
  session.pan_start = {};
  session.pan_start_x = 0;
  session.pan_start_y = 0;
  session.pan_metrics.reset();
  session.poll_previous_us = now_us();
  session.poll_max_us = 0U;
  session.power_sampled_us = now_us();
  session.button_down = false;
#ifdef TINYDRAW_VECTOR_V2_DEMO
  session.button_pressed_us = 0U;
  session.demo_replay_sequence = 0U;
  session.demo_sampler_stopped = false;
#endif
  session.next_lift_id = 1U;
  session.lift_reports_dropped = 0U;
  session.stroke_report = {};
  session.autosave_checkpoint_retry_us = 0U;
  session.background_ticks = 0U;
  session.drained_touch_events = 0U;
  session.running = true;
  touch_sampler_start.release();
  return true;
}

}  // namespace tinydraw::esp32
