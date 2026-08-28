#ifndef TINYDRAW_ESP32_VECTOR_V2_APP_H
#define TINYDRAW_ESP32_VECTOR_V2_APP_H

#include <cstdint>
#include <memory>
#include <optional>

#include "co5300_panel_transport.h"
#include "esp_heap_caps.h"
#include "physical_touch.h"
#include "power_manager.h"
#include "rtc_clock.h"
#include "time_sync.h"
#include "tinydraw/geometry.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/operation_spatial_index.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
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

enum class InteractionMode : std::uint8_t {
  kIdle,
  kDismissOverlay,
  kMinimapPan,
  kMinimapDockCandidate,
  kToolbarCandidate,
  kPan,
  kStroke,
};

// One running instance of the V2 application. Heavy members are optional so
// their spans and references can be wired only after AppStorage::allocate().
struct VectorV2AppSession {
  AppStorage storage;
  std::optional<vector_v2::MaterializedCanvas> canvas;
  std::optional<vector_v2::OperationSpatialIndex> operation_spatial_index;
  std::optional<vector_v2::OperationLog> log;
  vector_v2::NavigationState navigation;
  std::optional<VectorV2AutosaveStore> autosave;
  std::optional<Co5300PanelTransport> display;
  std::optional<PhysicalTouch> touch;
  std::optional<PowerManager> power;
  std::optional<RtcClock> clock;
  std::optional<TimeSyncController> time_sync;
  std::optional<VectorV2TouchSampler> touch_sampler;
  std::optional<VectorV2Export> exporter;
  std::optional<VectorV2Presenter> presenter;
  std::optional<vector_v2::TileProducer> producer;
  vector_v2::InPlaceAppendWorkspace workspace{};
  std::optional<LiveStrokeSession> stroke;
  vector_v2::ChromeState chrome;
  std::optional<VectorV2ChromeController> chrome_controller;
  std::optional<VectorV2BackgroundPipeline> background;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  vector_v2::RerenderLedgerTotals ledger_printed{};
  std::optional<vector_v2::RerenderLedger> rerender_ledger;
#endif
#ifdef TINYDRAW_VECTOR_V2_DEMO
  std::unique_ptr<vector_v2::DemoSample, decltype(&heap_caps_free)> demo_samples{nullptr,
                                                                                 &heap_caps_free};
  std::optional<VectorV2DemoController> demo;
#endif

  InteractionMode interaction = InteractionMode::kIdle;
  std::optional<std::uint32_t> time_sync_terminal_since;
  PowerStatus current_power{};
  Point last_touch{};
  Point toolbar_start{};
  Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  std::optional<std::uint32_t> minimap_release_us;
  Point minimap_release_point{};
  Point pan_start{};
  int pan_start_x = 0;
  int pan_start_y = 0;
  PanMetrics pan_metrics{};
  std::uint32_t poll_previous_us = 0;
  std::uint32_t poll_max_us = 0;
  std::uint32_t power_sampled_us = 0;
  bool button_down = false;
#ifdef TINYDRAW_VECTOR_V2_DEMO
  std::uint32_t button_pressed_us = 0U;
  std::uint32_t demo_replay_sequence = 0U;
  std::optional<std::uint32_t> demo_pointer_hide_us;
  bool demo_sampler_stopped = false;
#endif
  std::uint16_t next_gesture = 1U;
  std::uint32_t next_lift_id = 1U;
  std::uint32_t lift_reports_dropped = 0U;
  PendingStrokeReport stroke_report{};
  std::uint32_t autosave_checkpoint_retry_us = 0U;
  std::uint8_t background_ticks = 0U;
  std::uint8_t drained_touch_events = 0U;
  bool running = false;
};

[[nodiscard]] bool vector_v2_app_start(VectorV2AppSession& session);
void vector_v2_app_step(VectorV2AppSession& session);
void run_vector_v2_app();

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_APP_H
