#include "vector_v2_app.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#include "power_manager.h"
#include "time_sync.h"
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
#ifdef TINYDRAW_VECTOR_V2_DEMO
#include "vector_v2_demo_controller.h"
#endif
#include "vector_v2_export.h"
#include "vector_v2_live_stroke_session.h"
#include "vector_v2_presenter.h"
#include "vector_v2_touch_sampler.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::DocumentRevision;
using vector_v2::MaterializedCanvas;
using vector_v2::OperationLog;
using vector_v2::OperationTool;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr gpio_num_t kModeButton = GPIO_NUM_0;
constexpr std::uint32_t kPowerRefreshUs = 30'000'000U;
#ifdef TINYDRAW_VECTOR_V2_DEMO
constexpr std::uint32_t kDemoLongPressUs = 800'000U;
constexpr std::size_t kDemoCapacity = 16'384U;
#endif

constexpr bool interaction_active(InteractionMode mode) { return mode != InteractionMode::kIdle; }

constexpr bool navigation_active(InteractionMode mode) {
  return mode == InteractionMode::kMinimapPan || mode == InteractionMode::kPan;
}

std::uint32_t now_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

}  // namespace

void vector_v2_app_step(VectorV2AppSession& session) {
  if (!session.running) {
    return;
  }
  MaterializedCanvas& canvas = *session.canvas;
  OperationLog& log = *session.log;
  VectorV2AutosaveStore& autosave = *session.autosave;
  PowerManager& power = *session.power;
  TimeSyncController& time_sync = *session.time_sync;
  VectorV2TouchSampler& touch_sampler = *session.touch_sampler;
  VectorV2Export& exporter = *session.exporter;
  VectorV2Presenter& presenter = *session.presenter;
  LiveStrokeSession& stroke = *session.stroke;
  vector_v2::ChromeState& chrome = session.chrome;
  VectorV2ChromeController& chrome_controller = *session.chrome_controller;
  VectorV2BackgroundPipeline& background = *session.background;
  InteractionMode& interaction = session.interaction;
  std::optional<std::uint32_t>& time_sync_terminal_since = session.time_sync_terminal_since;
  PowerStatus& current_power = session.current_power;
  Point& last_touch = session.last_touch;
  Point& toolbar_start = session.toolbar_start;
  Point& toolbar_sum = session.toolbar_sum;
  std::uint32_t& toolbar_samples = session.toolbar_samples;
  Point& pan_start = session.pan_start;
  int& pan_start_x = session.pan_start_x;
  int& pan_start_y = session.pan_start_y;
  PanMetrics& pan_metrics = session.pan_metrics;
  std::uint32_t& poll_previous_us = session.poll_previous_us;
  std::uint32_t& poll_max_us = session.poll_max_us;
  std::uint32_t& power_sampled_us = session.power_sampled_us;
  bool& button_down = session.button_down;
  std::uint16_t& next_gesture = session.next_gesture;
  std::uint32_t& next_lift_id = session.next_lift_id;
  std::uint32_t& lift_reports_dropped = session.lift_reports_dropped;
  PendingStrokeReport& stroke_report = session.stroke_report;
  std::uint32_t& autosave_checkpoint_retry_us = session.autosave_checkpoint_retry_us;
  std::uint8_t& background_ticks = session.background_ticks;
  std::uint8_t& drained_touch_events = session.drained_touch_events;
#ifdef TINYDRAW_VECTOR_V2_DEMO
  vector_v2::NavigationState& navigation = session.navigation;
  vector_v2::TileProducer& producer = *session.producer;
  VectorV2DemoController& demo = *session.demo;
  std::uint32_t& button_pressed_us = session.button_pressed_us;
  std::uint32_t& demo_replay_sequence = session.demo_replay_sequence;
  bool& demo_sampler_stopped = session.demo_sampler_stopped;
#endif
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  const auto print_live_ledger = [&session](const char* site) {
    const auto totals = session.rerender_ledger->totals();
    const auto& previous = session.ledger_printed;
    std::printf(
        "TINYDRAW_LIVE_LEDGER site=%s renders=%lu cold=%lu damage=%lu evict=%lu stale=%lu "
        "unexplained=%lu total_renders=%lu unique=%lu amplification=%.3f\n",
        site, static_cast<unsigned long>(totals.renders - previous.renders),
        static_cast<unsigned long>(totals.cold_miss - previous.cold_miss),
        static_cast<unsigned long>(totals.expected_damage - previous.expected_damage),
        static_cast<unsigned long>(totals.eviction - previous.eviction),
        static_cast<unsigned long>(totals.stale_revision - previous.stale_revision),
        static_cast<unsigned long>(totals.unexplained - previous.unexplained),
        static_cast<unsigned long>(totals.renders),
        static_cast<unsigned long>(totals.unique_groups), totals.amplification());
    std::fflush(stdout);
    session.ledger_printed = totals;
  };
#endif

  const auto wait_for_input = [&](TickType_t timeout_ticks) {
#ifdef TINYDRAW_VECTOR_V2_DEMO
    if (demo_sampler_stopped) {
      return demo.wait_for_replay_event(timeout_ticks);
    }
#endif
    return touch_sampler.wait_for_event(timeout_ticks);
  };

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

  const auto toggle_chrome = [&](std::uint32_t event_us) {
    if (interaction_active(interaction) || !vector_v2::toggle_chrome_visibility(chrome)) {
      return false;
    }
    static_cast<void>(
        advance_full_refresh(chrome.visible ? "chrome-show" : "chrome-hide", event_us));
    return true;
  };

  const auto begin_pan = [&](Point start, InteractionMode mode) {
    interaction = mode;
    pan_metrics.reset();
    pan_start = start;
    pan_start_x = presenter.level_x();
    pan_start_y = presenter.level_y();
  };

#ifdef TINYDRAW_VECTOR_V2_DEMO
  const auto set_recording_indicator = [&](bool recording, std::uint32_t event_us) {
    if (chrome.recording == recording) {
      return;
    }
    chrome.recording = recording;
    if (!chrome.visible) {
      return;
    }
    const vector_v2::ChromeRect region = vector_v2::chrome_recording_region();
    const auto timing = presenter.present_frame_region({region.x0, region.y0, region.x1, region.y1},
                                                       chrome, event_us);
    print_presentation(recording ? "demo-recording-on" : "demo-recording-off", presenter, timing);
  };

  const auto reset_demo_baseline = [&](bool recording) {
    interaction = InteractionMode::kIdle;
    stroke_report.pending = false;
    navigation = vector_v2::NavigationState{};
    const int battery_percentage = chrome.battery_percentage;
    const bool battery_charging = chrome.battery_charging;
    const bool can_export = chrome.can_export;
    const bool can_sync_time = chrome.can_sync_time;
    chrome = {};
    chrome.battery_percentage = battery_percentage;
    chrome.battery_charging = battery_charging;
    chrome.can_export = can_export;
    chrome.can_sync_time = can_sync_time;
    chrome.recording = recording;
    time_sync_terminal_since.reset();

    const std::uint32_t current_generation =
        std::max(log.current_revision().value, canvas.current_revision().value);
    if (current_generation == std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    const vector_v2::DocumentRevision revision{current_generation + 1U};
    if (!vector_v2::reset_blank_document(log, canvas, revision) ||
        !producer.reset_uniform_baseline(revision)) {
      return false;
    }
    background.reset_document_state();
    next_gesture = 1U;
    static_cast<void>(sync_history_controls(chrome, log));
    const auto timing = presenter.refresh(chrome, now_us());
    print_presentation(recording ? "demo-record-baseline" : "demo-replay-baseline", presenter,
                       timing);
    return timing.passed;
  };
#endif

  const std::uint32_t loop_us = now_us();
  poll_max_us = std::max(poll_max_us, loop_us - poll_previous_us);
  poll_previous_us = loop_us;

#ifdef TINYDRAW_VECTOR_V2_DEMO
  if (demo_sampler_stopped && !demo.replaying()) {
    const bool replay_failed = demo.replay_failed();
    touch_sampler.discard_pending();
    if (!touch_sampler.start()) {
      std::printf("TINYDRAW_DEMO_FAIL reason=touch_restart\n");
      session.running = false;
      return;
    }
    demo_sampler_stopped = false;
    std::printf(replay_failed ? "TINYDRAW_DEMO_FAIL reason=replay_timer count=%lu\n"
                              : "TINYDRAW_DEMO_REPLAY_END count=%lu\n",
                static_cast<unsigned long>(demo.sample_count()));
  }
#endif

  Point point{};
  const bool idle_before_poll = !interaction_active(interaction);
  const std::int64_t poll_started_us = esp_timer_get_time();
  std::optional<SampledTouch> sampled_touch;
  bool replay_chrome_toggle = false;
#ifdef TINYDRAW_VECTOR_V2_DEMO
  std::uint32_t replay_event_us = loop_us;
  const TouchUrgencyProbe input_urgency = demo_sampler_stopped
                                              ? TouchUrgencyProbe(demo.replay_urgency_flag())
                                              : touch_sampler.urgency_probe();
  if (demo.replaying()) {
    if (const auto replay_event = demo.pop_due(loop_us); replay_event.has_value()) {
      if (const auto kind = vector_v2::demo_touch_kind(replay_event->kind); kind.has_value()) {
        sampled_touch = SampledTouch{
            .point = {.x = replay_event->point.x, .y = replay_event->point.y},
            .timestamp_us = replay_event->timestamp_us,
            .sequence = ++demo_replay_sequence,
            .kind = *kind,
        };
      } else {
        replay_chrome_toggle = true;
        replay_event_us = replay_event->timestamp_us;
      }
    }
  } else if (!demo_sampler_stopped) {
    sampled_touch = touch_sampler.read_next();
    if (sampled_touch.has_value() && demo.recording()) {
      static_cast<void>(demo.record_touch({
          .point = {.x = sampled_touch->point.x, .y = sampled_touch->point.y},
          .timestamp_us = sampled_touch->timestamp_us,
          .sequence = sampled_touch->sequence,
          .kind = sampled_touch->kind,
      }));
      if (!demo.recording()) {
        set_recording_indicator(false, loop_us);
        std::printf("TINYDRAW_DEMO_RECORDING_END count=%lu overflow=1\n",
                    static_cast<unsigned long>(demo.sample_count()));
      }
    }
  }
#else
  const TouchUrgencyProbe input_urgency = touch_sampler.urgency_probe();
  sampled_touch = touch_sampler.read_next();
#endif
  const std::int64_t poll_completed_us = esp_timer_get_time();
  const bool sample_ready = sampled_touch.has_value();
  const bool input_ready = sample_ready || replay_chrome_toggle;
  if (sample_ready) {
    point = sampled_touch->point;
  }
  const bool lift_event = sample_ready && sampled_touch->kind == vector_v2::TouchEventKind::kUp;
  const bool point_event = sample_ready && (!lift_event || interaction_active(interaction));
  const std::uint32_t event_us = sample_ready ? sampled_touch->timestamp_us
#ifdef TINYDRAW_VECTOR_V2_DEMO
                                 : replay_chrome_toggle ? replay_event_us
#endif
                                               : loop_us;
  bool cosmetic_work = false;
#ifdef TINYDRAW_VECTOR_V2_DEMO
  const bool persist_authority = !demo.active();
#else
  constexpr bool persist_authority = true;
#endif

  if (replay_chrome_toggle) {
    static_cast<void>(toggle_chrome(event_us));
    return;
  }

  if (!input_ready && !input_urgency.requested() && presenter.refresh_pending()) {
    static_cast<void>(advance_full_refresh("deferred-full", loop_us));
    // One input poll per bounded compose slice. Background canvas mutation
    // waits until the complete frame has entered its single panel sweep.
    if (presenter.refresh_composing()) {
      return;
    }
  }

  if (!input_ready && chrome.export_status == vector_v2::ChromeExportStatus::kPresented &&
      !input_urgency.requested() && exporter.usb_host_ejected()) {
    chrome.export_status = exporter.stop_usb() ? vector_v2::ChromeExportStatus::kIdle
                                               : vector_v2::ChromeExportStatus::kExitError;
    static_cast<void>(advance_full_refresh("export-host-ejected", loop_us));
    cosmetic_work = true;
  }

  const auto next_time_sync_status = chrome_time_sync_status(time_sync.status());
  if (!input_ready && !cosmetic_work && !input_urgency.requested() &&
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
  if (!input_ready && !cosmetic_work && !input_urgency.requested() &&
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
  if (!input_ready && !cosmetic_work && !input_urgency.requested() &&
      !interaction_active(interaction) && !stroke_report.pending &&
      vector_v2::pending_operation_count(log, canvas) == 0U && power.ready() &&
#ifdef TINYDRAW_VECTOR_V2_DEMO
      !demo.active() &&
#endif
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
      if (chrome.visible) {
        const vector_v2::ChromeRect battery = vector_v2::chrome_battery_region();
        const auto timing = presenter.refresh_region(
            {presenter.level_x() + battery.x0, presenter.level_y() + battery.y0,
             presenter.level_x() + battery.x1, presenter.level_y() + battery.y1},
            chrome, loop_us);
        print_presentation("power", presenter, timing);
      }
    }
  }

  if (!input_ready && !cosmetic_work && !input_urgency.requested()) {
    const bool button_action_allowed = vector_v2::chrome_can_toggle_visibility(chrome);
    const bool next_button_down = gpio_get_level(kModeButton) == 0;
    if (next_button_down && !button_down) {
      button_down = true;
#ifdef TINYDRAW_VECTOR_V2_DEMO
      button_pressed_us = loop_us;
#endif
    } else if (!next_button_down && button_down) {
      button_down = false;
      if (button_action_allowed) {
#ifdef TINYDRAW_VECTOR_V2_DEMO
        const bool long_press = loop_us - button_pressed_us >= kDemoLongPressUs;
        if (long_press && demo.ready() && !interaction_active(interaction)) {
          if (demo.mode() == VectorV2DemoMode::kEmpty) {
            const bool autosave_flushed = !autosave.ready() || autosave.flush(5'000U);
            touch_sampler.stop();
            touch_sampler.discard_pending();
            if (reset_demo_baseline(true)) {
              demo.begin_recording(now_us());
              if (!touch_sampler.start()) {
                demo.stop_recording();
                chrome.recording = false;
                std::printf("TINYDRAW_DEMO_FAIL reason=touch_restart\n");
                session.running = false;
                return;
              }
              std::printf("TINYDRAW_DEMO_RECORDING_BEGIN capacity=%lu autosave_flushed=%u\n",
                          static_cast<unsigned long>(kDemoCapacity), autosave_flushed);
            } else {
              chrome.recording = false;
              if (!touch_sampler.start()) {
                std::printf("TINYDRAW_DEMO_FAIL reason=touch_restart\n");
                session.running = false;
                return;
              }
              std::printf("TINYDRAW_DEMO_FAIL reason=record_baseline\n");
            }
            cosmetic_work = true;
          } else if (demo.recording()) {
            demo.stop_recording();
            set_recording_indicator(false, loop_us);
            std::printf("TINYDRAW_DEMO_RECORDING_END count=%lu overflow=%u\n",
                        static_cast<unsigned long>(demo.sample_count()), demo.overflowed());
            cosmetic_work = true;
          } else if (demo.tape_ready()) {
            touch_sampler.stop();
            touch_sampler.discard_pending();
            demo_sampler_stopped = true;
            const bool baseline_ready = reset_demo_baseline(false);
            if (baseline_ready && demo.begin_replay(now_us())) {
              demo_replay_sequence = 0U;
              std::printf("TINYDRAW_DEMO_REPLAY_BEGIN count=%lu\n",
                          static_cast<unsigned long>(demo.sample_count()));
            } else {
              if (!touch_sampler.start()) {
                std::printf("TINYDRAW_DEMO_FAIL reason=touch_restart\n");
                session.running = false;
                return;
              }
              demo_sampler_stopped = false;
              std::printf(baseline_ready ? "TINYDRAW_DEMO_FAIL reason=replay_timer\n"
                                         : "TINYDRAW_DEMO_FAIL reason=replay_baseline\n");
            }
            cosmetic_work = true;
          }
        } else if (!long_press && !demo.replaying() && !interaction_active(interaction)) {
          if (demo.recording()) {
            static_cast<void>(demo.record_chrome_toggle(loop_us));
            if (!demo.recording()) {
              set_recording_indicator(false, loop_us);
              std::printf("TINYDRAW_DEMO_RECORDING_END count=%lu overflow=1\n",
                          static_cast<unsigned long>(demo.sample_count()));
            }
          }
          cosmetic_work = toggle_chrome(loop_us);
        }
#else
        cosmetic_work = toggle_chrome(loop_us);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
        if (cosmetic_work) {
          print_live_ledger("chrome-toggle");
        }
#endif
#endif
      }
    }
  }
  if (cosmetic_work) {
    return;
  }
  if (point_event) {
    if (!interaction_active(interaction) &&
        sampled_touch->kind == vector_v2::TouchEventKind::kDown) {
      last_touch = point;
      const bool export_toast = chrome.export_status == vector_v2::ChromeExportStatus::kSaved ||
                                chrome.export_status == vector_v2::ChromeExportStatus::kError;
      const bool time_toast = chrome.time_sync_status == vector_v2::ChromeTimeSyncStatus::kSaved ||
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
        const vector_v2::ChromeAction action = vector_v2::chrome_action_at({tap.x, tap.y}, chrome);
        const bool history_action =
            action == vector_v2::ChromeAction::kUndo || action == vector_v2::ChromeAction::kRedo;
        const vector_v2::AuthorityReadView authority_before = log.read_view();
        if (persist_authority && action == vector_v2::ChromeAction::kExport && autosave.ready() &&
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
        const bool save_ready = !persist_authority || action != vector_v2::ChromeAction::kExport ||
                                !autosave.ready() || autosave.flush(5'000U);
        const bool boundary_ready =
            save_ready &&
            (!history_action || background.drain_boundary(BackgroundDrainBoundary::kHistory));
        const bool applied = boundary_ready && chrome_controller.apply(action, tap);
        if (applied && persist_authority) {
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
        if (applied && (history_action || action == vector_v2::ChromeAction::kConfirmNewDrawing)) {
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
      report.append_us = finished.append_us;
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
      if (persist_authority && log.operation_count() > finished.first_operation) {
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
    return;
  }

  // Queue/resync failures collapse into one complete checkpoint after input
  // is quiet. Flash work remains on the worker; this call only snapshots
  // coherent vector authority into immutable PSRAM.
  const bool touch_backlog = input_urgency.requested();
  if (!interaction_active(interaction) && !input_ready && !touch_backlog &&
      !input_urgency.requested() && !stroke_report.pending && autosave.ready() &&
      persist_authority && autosave.checkpoint_required() &&
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

  const bool background_urgent = touch_backlog || input_urgency.requested();
  const BackgroundSliceResult background_result = background.run_slice({
      .loop_us = loop_us,
      .pressed = interaction_active(interaction),
      .panning = navigation_active(interaction),
      .sample_ready = sample_ready || background_urgent,
      .lift_report_pending = stroke_report.pending,
      .touch_urgency = input_urgency,
  });
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  if (background_result.drain_completed) {
    print_live_ledger("drain");
  }
#endif
  if (stroke_report.pending && idle_before_poll && !interaction_active(interaction) &&
      !input_urgency.requested()) {
    print_stroke(stroke_report);
    std::printf("TINYDRAW_LIVE_STROKE_DONE committed=%u refresh=%u commit_failed=%u\n",
                stroke_report.committed, stroke_report.refresh.passed, stroke_report.commit_failed);
    print_lift_baseline(stroke_report, poll_started_us, poll_completed_us, lift_reports_dropped);
    std::fflush(stdout);
    lift_reports_dropped = 0U;
    stroke_report.pending = false;
    // This diagnostic write happens only after a completed input poll. Keep
    // it out of the pre-existing stroke poll-gap metric.
    poll_previous_us = now_us();
  }
  if (background_urgent || input_urgency.requested()) {
    // Drain queued semantic edges and the newest coalesced Move before any
    // more background work, diagnostics, or cosmetic presentation.
    background_ticks = 0U;
    if (++drained_touch_events == 8U) {
      drained_touch_events = 0U;
      taskYIELD();
    }
    return;
  }
  drained_touch_events = 0U;
  if (background_result.fill_busy) {
    // Producer calls are bounded input-poll slices. Avoid paying a fixed
    // two-millisecond tax after every slice, but periodically unblock idle.
    if (++background_ticks == 8U) {
      background_ticks = 0U;
      static_cast<void>(wait_for_input(pdMS_TO_TICKS(1)));
    }
  } else {
    background_ticks = 0U;
    static_cast<void>(wait_for_input(pdMS_TO_TICKS(2)));
  }
}

void run_vector_v2_app() {
  VectorV2AppSession session;
  if (!vector_v2_app_start(session)) {
    return;
  }
  while (session.running) {
    vector_v2_app_step(session);
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_vector_v2_app(); }
