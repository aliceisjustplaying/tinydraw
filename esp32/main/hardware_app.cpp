#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "demo_recording.h"
#include "drawing_store.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "firmware_canvas.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "image_export_store.h"
#include "physical_display.h"
#include "physical_touch.h"
#include "power_manager.h"
#include "rtc_clock.h"
#include "time_sync.h"
#include "tinydraw/demo/demo_tape.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"
#include "usb_export.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr gpio_num_t kDemoButton = GPIO_NUM_0;
constexpr std::uint32_t kDemoLongPressUs = 800'000U;
constexpr std::uint32_t kPowerRefreshUs = 1'000'000U;
constexpr std::size_t kDemoCapacity = 8192U;
using tinydraw::esp32::PhysicalTouch;
using tinydraw::esp32::TouchRead;

std::uint32_t timestamp_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

using TouchEvent = tinydraw::DemoInputEvent;

enum class AppEventKind : std::uint8_t {
  kTouch,
  kResetForDemo,
  kDemoRecordingStarted,
  kDemoRecordingStopped,
  kDemoReplayStarted,
  kDemoReplayStopped,
  kPowerStatusChanged,
};

struct AppEvent {
  TouchEvent touch{};
  tinydraw::esp32::PowerStatus power{};
  AppEventKind kind = AppEventKind::kTouch;
};

struct TouchTaskContext {
  PhysicalTouch* touch = nullptr;
  QueueHandle_t queue = nullptr;
  tinydraw::DemoTape* tape = nullptr;
  std::span<const tinydraw::DemoSample> built_in_demo;
  tinydraw::esp32::PowerManager* power = nullptr;
  tinydraw::esp32::PowerStatus power_status{};
};

void enqueue_latest(QueueHandle_t queue, const AppEvent& event) {
  if (xQueueSend(queue, &event, 0) == pdTRUE) {
    return;
  }
  AppEvent discarded;
  static_cast<void>(xQueueReceive(queue, &discarded, 0));
  static_cast<void>(xQueueSend(queue, &event, 0));
}

void enqueue_control(QueueHandle_t queue, AppEventKind kind) {
  const AppEvent event{.kind = kind};
  ESP_ERROR_CHECK(xQueueSend(queue, &event, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL);
}

void emit_touch(TouchTaskContext& context, const TouchEvent& event) {
  if (context.tape->recording()) {
    static_cast<void>(context.tape->record(event));
  }
  enqueue_latest(context.queue, {.touch = event});
}

void dump_demo(std::span<const tinydraw::DemoSample> samples, bool overflowed) {
  std::printf("TINYDRAW_DEMO_BEGIN count=%lu overflow=%u\n",
              static_cast<unsigned long>(samples.size()), overflowed);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto& sample = samples[index];
    std::printf("TINYDRAW_DEMO %lu %u %u %u\n", static_cast<unsigned long>(sample.offset_us),
                sample.x, sample.y, sample.touching);
    if (index % 64U == 63U) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  std::printf("TINYDRAW_DEMO_END\n");
}

void wait_for_demo_offset(std::uint32_t replay_started_us, std::uint32_t offset_us) {
  while (timestamp_us() - replay_started_us < offset_us) {
    const std::uint32_t elapsed = timestamp_us() - replay_started_us;
    const std::uint32_t remaining = offset_us - elapsed;
    if (remaining > 2'000U) {
      vTaskDelay(pdMS_TO_TICKS((remaining - 1'000U) / 1'000U));
    } else {
      esp_rom_delay_us(remaining);
    }
  }
}

void replay_demo(TouchTaskContext& context, std::span<const tinydraw::DemoSample> samples) {
  if (samples.empty()) {
    std::printf("TINYDRAW_DEMO_EMPTY\n");
    return;
  }
  xQueueReset(context.queue);
  enqueue_control(context.queue, AppEventKind::kDemoReplayStarted);
  enqueue_control(context.queue, AppEventKind::kResetForDemo);

  std::printf("TINYDRAW_DEMO_REPLAY_BEGIN count=%lu\n", static_cast<unsigned long>(samples.size()));
  const std::uint32_t replay_started_us = timestamp_us();
  for (const auto& sample : samples) {
    wait_for_demo_offset(replay_started_us, sample.offset_us);
    enqueue_latest(context.queue,
                   {.touch = tinydraw::replay_demo_sample(sample, replay_started_us)});
  }
  enqueue_control(context.queue, AppEventKind::kDemoReplayStopped);
  std::printf("TINYDRAW_DEMO_REPLAY_END\n");
}

void touch_task(void* argument) {
  auto& context = *static_cast<TouchTaskContext*>(argument);
  tinydraw::Point last_point{};
  bool touching = false;
  std::uint32_t no_touch_started_us = 0;
  bool raw_button_down = false;
  bool button_down = false;
  bool long_press_handled = false;
  std::uint32_t raw_button_changed_us = timestamp_us();
  std::uint32_t button_pressed_us = 0;
  std::uint32_t power_sampled_us = timestamp_us();
  while (true) {
    tinydraw::Point point{};
    const TouchRead read = context.touch->read(point);
    const std::uint32_t now = timestamp_us();
    if (read == TouchRead::kPoint) {
      no_touch_started_us = 0;
      if (!touching || point.x != last_point.x || point.y != last_point.y) {
        emit_touch(context, {.point = point, .timestamp_us = now, .touching = true});
        last_point = point;
      }
      touching = true;
    } else if (read == TouchRead::kNoTouch && touching) {
      if (no_touch_started_us == 0U) {
        no_touch_started_us = now;
      } else if (now - no_touch_started_us >= 20'000U) {
        emit_touch(context, {.point = last_point, .timestamp_us = now, .touching = false});
        touching = false;
        no_touch_started_us = 0;
      }
    }

    const bool next_raw_button_down = gpio_get_level(kDemoButton) == 0;
    if (next_raw_button_down != raw_button_down) {
      raw_button_down = next_raw_button_down;
      raw_button_changed_us = now;
    }
    if (raw_button_down != button_down && now - raw_button_changed_us >= 25'000U) {
      button_down = raw_button_down;
      if (button_down) {
        button_pressed_us = now;
        long_press_handled = false;
      } else if (context.tape->recording()) {
        context.tape->stop_recording();
        enqueue_control(context.queue, AppEventKind::kDemoRecordingStopped);
        std::printf("TINYDRAW_DEMO_RECORDING_END count=%lu overflow=%u\n",
                    static_cast<unsigned long>(context.tape->size()), context.tape->overflowed());
        dump_demo(context.tape->samples(), context.tape->overflowed());
      } else if (!long_press_handled) {
        context.tape->begin_recording(now);
        enqueue_control(context.queue, AppEventKind::kDemoRecordingStarted);
        std::printf("TINYDRAW_DEMO_RECORDING_BEGIN capacity=%lu\n",
                    static_cast<unsigned long>(kDemoCapacity));
      }
    }
    if (button_down && !context.tape->recording() && !long_press_handled &&
        now - button_pressed_us >= kDemoLongPressUs) {
      const auto recorded = context.tape->samples();
      replay_demo(context, recorded.empty() ? context.built_in_demo : recorded);
      long_press_handled = true;
    }
    if (!touching && context.power != nullptr && context.power->ready() &&
        now - power_sampled_us >= kPowerRefreshUs) {
      const auto power_status = context.power->read();
      power_sampled_us = now;
      if (power_status.valid && power_status != context.power_status) {
        const AppEvent event{.power = power_status, .kind = AppEventKind::kPowerStatusChanged};
        if (xQueueSend(context.queue, &event, 0) == pdTRUE) {
          context.power_status = power_status;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace

void run_hardware_app() {
  tinydraw::esp32::PhysicalDisplay display;
  PhysicalTouch touch;
  tinydraw::esp32::PowerManager power(touch.bus());
  tinydraw::esp32::RtcClock clock(touch.bus());
  if (!display.ready() || !touch.ready()) {
    std::printf("TINYDRAW_HARDWARE_FAIL display=%u touch=%u\n", display.ready(), touch.ready());
    return;
  }

  tinydraw::esp32::FirmwareCanvas canvas(display);
  if (!canvas.ready() || !canvas.capabilities_valid()) {
    std::printf("TINYDRAW_HARDWARE_FAIL canvas=0\n");
    return;
  }

  tinydraw::esp32::DrawingStore drawing_store;
  tinydraw::esp32::ImageExportStore image_export_store;
  tinydraw::esp32::UsbExport usb_export(image_export_store);
  if (drawing_store.ready()) {
    if (!drawing_store.restore(canvas.world(), canvas.committed(), canvas.visible())) {
      std::printf("TINYDRAW_AUTOSAVE_RESTORE_FAIL\n");
    } else {
      std::printf("TINYDRAW_AUTOSAVE_READY\n");
    }
  } else {
    std::printf("TINYDRAW_AUTOSAVE_DISABLED\n");
  }
  std::printf(
      "TINYDRAW_PSRAM world_bytes=%lu free=%lu largest=%lu minimum=%lu\n",
      static_cast<unsigned long>(tinydraw::WorldCanvas::kRequiredPixels * sizeof(std::uint16_t)),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));

  const auto initial_power_status = power.read();
  tinydraw::esp32::PowerStatus current_power_status = initial_power_status;
  tinydraw::ToolbarState toolbar;
  toolbar.can_export = image_export_store.ready();
  toolbar.battery_percentage = initial_power_status.percentage;
  toolbar.battery_charging = initial_power_status.charging;
  toolbar.external_power = initial_power_status.external_power;
  std::printf(
      "TINYDRAW_POWER_READY ready=%u button=%u valid=%u battery=%d voltage_mv=%u "
      "charging=%u vbus=%u\n",
      power.ready(), power.power_button_ready(), initial_power_status.valid,
      initial_power_status.percentage, initial_power_status.battery_mv,
      initial_power_status.charging, initial_power_status.external_power);
  display.set_toolbar(toolbar);
  display.push_canvas(canvas.committed());
  static_cast<void>(tinydraw::esp32::start_time_sync(clock));

  tinydraw::InkConfig brush;
  brush.size = tinydraw::brush_size(toolbar.size);
  tinydraw::InkStream ink(brush);
  tinydraw::CurvedRibbonStream ribbon;
  tinydraw::InkPoint last_ink{};
  tinydraw::Point last_touch{};
  std::uint16_t stroke_color = tinydraw::rgb565(toolbar.color);
  bool pressed = false;
  bool panning = false;
  bool demo_replaying = false;
  bool persistence_resync_needed = false;
  tinydraw::Point previous_saved_touch{};
  tinydraw::Point pan_start_touch{};
  tinydraw::ViewOrigin pan_start_origin{};
  bool toolbar_pressed = false;
  tinydraw::Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  std::uint32_t stroke_samples = 0;
  std::uint32_t stroke_started_us = 0;
  std::uint32_t previous_touch_us = 0;
  std::uint64_t touch_intervals_us = 0;
  std::uint32_t maximum_touch_interval_us = 0;
  std::uint32_t maximum_input_lag_us = 0;
  UBaseType_t maximum_queue_depth = 0;
  std::int64_t stroke_render_us = 0;
  std::int64_t maximum_render_us = 0;
  std::uint32_t pan_frames = 0;
  std::int64_t pan_render_us = 0;
  std::int64_t maximum_pan_render_us = 0;
  std::uint32_t export_toast_until_us = 0;

  const auto close_popups = [&] {
    toolbar.tools_open = false;
    toolbar.colors_open = false;
    toolbar.sizes_open = false;
  };
  const auto reset_stroke = [&] {
    ink.end();
    ribbon.reset();
    canvas.raster().cancel();
  };
  const auto update_toolbar = [&] {
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    display.refresh_toolbar(canvas.committed());
  };
  struct DisplayTimingSnapshot {
    std::int64_t prepare_us = 0;
    std::int64_t transfer_us = 0;
    std::uint32_t pushes = 0;
  };
  const auto reset_display_timing = [&] { display.reset_timing(); };
  const auto display_timing = [&] {
    return DisplayTimingSnapshot{display.prepare_us(), display.transfer_us(), display.push_count()};
  };
  const auto select_size = [&](tinydraw::PenSize size) {
    toolbar.size = size;
    auto config = ink.config();
    config.size = tinydraw::brush_size(size);
    ink.set_config(config);
    close_popups();
  };
  const auto pan_to = [&](tinydraw::Point point, std::uint32_t event_us) {
    const int delta_x = static_cast<int>(std::lround(point.x - pan_start_touch.x));
    const int delta_y = static_cast<int>(std::lround(point.y - pan_start_touch.y));
    const auto started = esp_timer_get_time();
    const tinydraw::ViewOrigin requested_origin{pan_start_origin.x - delta_x,
                                                pan_start_origin.y - delta_y};
    if (!canvas.world().move_to(requested_origin)) {
      return;
    }
    display.push_world(canvas.world().pixels(), canvas.world().origin(),
                       tinydraw::esp32::kPhysicalMainOverlayTop);
    const auto finished = esp_timer_get_time();
    const auto elapsed = finished - started;
    ++pan_frames;
    pan_render_us += elapsed;
    maximum_pan_render_us = std::max(maximum_pan_render_us, elapsed);
  };
  const auto new_drawing = [&] {
    reset_stroke();
    canvas.undo_history().begin_entry(canvas.world().origin());
    canvas.undo_history().capture_canvas(canvas.committed());
    static_cast<void>(canvas.undo_history().commit_entry());
    static_cast<void>(canvas.world().clear(canvas.committed(), canvas.visible()));
    toolbar.export_ready = false;
    toolbar.confirm_new = false;
    close_popups();
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    display.push_canvas(canvas.committed());
    if (!demo_replaying) {
      drawing_store.save_all(canvas.world());
      persistence_resync_needed = false;
    }
  };
  const auto reset_for_demo = [&] {
    reset_stroke();
    pressed = false;
    panning = false;
    toolbar_pressed = false;
    toolbar_samples = 0;
    toolbar = {};
    toolbar.can_export = image_export_store.ready();
    toolbar.battery_percentage = current_power_status.percentage;
    toolbar.battery_charging = current_power_status.charging;
    toolbar.external_power = current_power_status.external_power;
    auto config = ink.config();
    config.size = tinydraw::brush_size(toolbar.size);
    ink.set_config(config);
    stroke_color = tinydraw::rgb565(toolbar.color);
    canvas.undo_history().clear();
    static_cast<void>(canvas.world().clear(canvas.committed(), canvas.visible()));
    display.set_toolbar(toolbar);
    display.push_canvas(canvas.committed());
  };
  const auto undo = [&] {
    if (!canvas.undo_history().can_undo()) {
      return;
    }
    reset_stroke();
    reset_display_timing();
    const auto started = esp_timer_get_time();
    static_cast<void>(canvas.world().capture(canvas.committed()));
    const auto undo_origin = canvas.undo_history().next_undo_origin();
    const bool view_changed = undo_origin.has_value() && *undo_origin != canvas.world().origin();
    if (view_changed) {
      static_cast<void>(canvas.world().show(*undo_origin, canvas.committed(), canvas.visible()));
    }
    const auto stats = canvas.undo_history().undo(canvas.committed(), canvas.visible(),
                                                  view_changed ? nullptr : &display);
    static_cast<void>(canvas.world().capture(canvas.committed()));
    close_popups();
    toolbar.export_ready = false;
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    if (view_changed) {
      display.push_canvas(canvas.committed());
    } else {
      display.refresh_toolbar(canvas.committed());
    }
    if (!demo_replaying) {
      if (persistence_resync_needed) {
        drawing_store.save_all(canvas.world());
      } else {
        drawing_store.save_viewport(canvas.world(), canvas.committed());
      }
      persistence_resync_needed = false;
    }
    const DisplayTimingSnapshot timing = display_timing();
    std::printf(
        "[DEBUG-undo1] tiles=%lu bytes=%lu view_changed=%u elapsed_us=%lld prepare_us=%lld "
        "transfer_us=%lld pushes=%lu\n",
        static_cast<unsigned long>(stats.tiles_restored),
        static_cast<unsigned long>(stats.display_bytes), view_changed,
        static_cast<long long>(esp_timer_get_time() - started),
        static_cast<long long>(timing.prepare_us), static_cast<long long>(timing.transfer_us),
        static_cast<unsigned long>(timing.pushes));
  };
  const auto export_image = [&] {
    reset_stroke();
    if (!usb_export.prepare_export()) {
      toolbar.exporting = false;
      toolbar.export_ready = false;
      toolbar.export_toast = true;
      export_toast_until_us = timestamp_us() + 3'000'000U;
      close_popups();
      update_toolbar();
      std::printf("TINYDRAW_EXPORT success=0 reason=usb_stop\n");
      std::fflush(stdout);
      return;
    }
    tinydraw::FatDateTime modified_time;
    if (clock.read(modified_time)) {
      usb_export.set_modified_time(modified_time);
    }
    static_cast<void>(canvas.world().capture(canvas.committed()));
    toolbar.exporting = true;
    toolbar.export_ready = false;
    toolbar.export_toast = true;
    close_popups();
    update_toolbar();
    vTaskDelay(pdMS_TO_TICKS(20));
    const auto stats = image_export_store.encode(canvas.world().pixels());
    std::printf("TINYDRAW_EXPORT success=%u bytes=%lu elapsed_us=%lld free_psram=%lu\n",
                stats.success, static_cast<unsigned long>(stats.bytes),
                static_cast<long long>(stats.elapsed_us),
                static_cast<unsigned long>(stats.free_psram));
    std::fflush(stdout);
    const bool usb_ready = usb_export.finish_export(stats.success);
    toolbar.exporting = false;
    toolbar.export_ready = stats.success && usb_ready;
    toolbar.export_toast = true;
    export_toast_until_us = timestamp_us() + 3'000'000U;
    close_popups();
    update_toolbar();
  };
  const auto toolbar_action = [&](tinydraw::Point point) {
    const auto action = tinydraw::toolbar_action_at(point, toolbar);
    switch (action) {
      case tinydraw::ToolbarAction::kSelectPen:
        toolbar.tool = tinydraw::DrawingTool::kPen;
        close_popups();
        break;
      case tinydraw::ToolbarAction::kSelectPan:
        toolbar.tool = tinydraw::DrawingTool::kPan;
        close_popups();
        break;
      case tinydraw::ToolbarAction::kSelectEraser:
        toolbar.tool = tinydraw::DrawingTool::kEraser;
        close_popups();
        break;
      case tinydraw::ToolbarAction::kSelectColor:
        toolbar.color = tinydraw::toolbar_color_at(point, toolbar).value_or(toolbar.color);
        toolbar.tool = tinydraw::DrawingTool::kPen;
        close_popups();
        break;
      case tinydraw::ToolbarAction::kToggleTools:
        toolbar.tools_open = !toolbar.tools_open;
        toolbar.colors_open = false;
        toolbar.sizes_open = false;
        break;
      case tinydraw::ToolbarAction::kToggleColors:
        toolbar.colors_open = !toolbar.colors_open;
        toolbar.tools_open = false;
        toolbar.sizes_open = false;
        break;
      case tinydraw::ToolbarAction::kToggleSizes:
        toolbar.sizes_open = !toolbar.sizes_open;
        toolbar.tools_open = false;
        toolbar.colors_open = false;
        break;
      case tinydraw::ToolbarAction::kSelectSmall:
        select_size(tinydraw::PenSize::kSmall);
        break;
      case tinydraw::ToolbarAction::kSelectMedium:
        select_size(tinydraw::PenSize::kMedium);
        break;
      case tinydraw::ToolbarAction::kSelectLarge:
        select_size(tinydraw::PenSize::kLarge);
        break;
      case tinydraw::ToolbarAction::kSelectExtraLarge:
        select_size(tinydraw::PenSize::kExtraLarge);
        break;
      case tinydraw::ToolbarAction::kExport:
        export_image();
        return;
      case tinydraw::ToolbarAction::kUndo:
        undo();
        return;
      case tinydraw::ToolbarAction::kNewDrawing:
        close_popups();
        toolbar.confirm_new = true;
        break;
      case tinydraw::ToolbarAction::kCancelNewDrawing:
        toolbar.confirm_new = false;
        break;
      case tinydraw::ToolbarAction::kConfirmNewDrawing:
        new_drawing();
        return;
      case tinydraw::ToolbarAction::kNone:
        break;
    }
    update_toolbar();
  };

  auto* demo_storage = static_cast<tinydraw::DemoSample*>(heap_caps_malloc(
      kDemoCapacity * sizeof(tinydraw::DemoSample), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  QueueHandle_t touch_queue = xQueueCreate(32U, sizeof(AppEvent));
  if (demo_storage == nullptr || touch_queue == nullptr) {
    std::printf("TINYDRAW_HARDWARE_FAIL demo_storage=%u touch_queue=%u\n", demo_storage != nullptr,
                touch_queue != nullptr);
    return;
  }
  tinydraw::DemoTape demo_tape(std::span(demo_storage, kDemoCapacity));
  gpio_config_t demo_button_config{};
  demo_button_config.pin_bit_mask = 1ULL << static_cast<unsigned>(kDemoButton);
  demo_button_config.mode = GPIO_MODE_INPUT;
  demo_button_config.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&demo_button_config));

  TouchTaskContext touch_context{
      .touch = &touch,
      .queue = touch_queue,
      .tape = &demo_tape,
      .built_in_demo = tinydraw::esp32::demo::kBuiltInDemo,
      .power = &power,
      .power_status = initial_power_status,
  };
  if (xTaskCreatePinnedToCore(touch_task, "tinydraw_touch", 4096U, &touch_context, 5U, nullptr,
                              1) != pdPASS) {
    std::printf("TINYDRAW_HARDWARE_FAIL touch_task=0\n");
    return;
  }

  std::printf("TINYDRAW_HARDWARE_OK display=CO5300 touch=CST820 demo_capacity=%lu\n",
              static_cast<unsigned long>(kDemoCapacity));
  while (true) {
    if (toolbar.export_toast &&
        static_cast<std::int32_t>(timestamp_us() - export_toast_until_us) >= 0) {
      toolbar.export_toast = false;
      update_toolbar();
    }
    AppEvent app_event;
    const TickType_t wait = toolbar.export_toast ? pdMS_TO_TICKS(50) : portMAX_DELAY;
    if (xQueueReceive(touch_queue, &app_event, wait) != pdTRUE) {
      continue;
    }
    if (app_event.kind == AppEventKind::kDemoReplayStarted ||
        app_event.kind == AppEventKind::kDemoReplayStopped) {
      demo_replaying = app_event.kind == AppEventKind::kDemoReplayStarted;
      if (demo_replaying) {
        drawing_store.suspend();
      } else {
        persistence_resync_needed = true;
      }
      continue;
    }
    if (app_event.kind == AppEventKind::kResetForDemo) {
      reset_for_demo();
      continue;
    }
    if (app_event.kind == AppEventKind::kPowerStatusChanged) {
      current_power_status = app_event.power;
      toolbar.battery_percentage = current_power_status.percentage;
      toolbar.battery_charging = current_power_status.charging;
      toolbar.external_power = current_power_status.external_power;
      update_toolbar();
      std::printf("TINYDRAW_POWER battery=%d voltage_mv=%u charging=%u vbus=%u\n",
                  current_power_status.percentage, current_power_status.battery_mv,
                  current_power_status.charging, current_power_status.external_power);
      continue;
    }
    if (app_event.kind == AppEventKind::kDemoRecordingStarted ||
        app_event.kind == AppEventKind::kDemoRecordingStopped) {
      toolbar.recording = app_event.kind == AppEventKind::kDemoRecordingStarted;
      update_toolbar();
      continue;
    }
    TouchEvent event = app_event.touch;
    if (toolbar.export_toast) {
      toolbar.export_toast = false;
      update_toolbar();
    }
    if (panning && event.touching) {
      AppEvent latest;
      while (xQueuePeek(touch_queue, &latest, 0) == pdTRUE && latest.kind == AppEventKind::kTouch) {
        static_cast<void>(xQueueReceive(touch_queue, &latest, 0));
        event = latest.touch;
        if (!event.touching) {
          break;
        }
      }
    }
    const tinydraw::Point point = event.point;
    const bool touching = event.touching;
    if (!demo_replaying) {
      drawing_store.activity();
    }
    const std::uint32_t input_lag_us = timestamp_us() - event.timestamp_us;
    const UBaseType_t queue_depth = uxQueueMessagesWaiting(touch_queue);
    if (touching && !pressed) {
      std::printf("[DEBUG-hw1] down x=%.0f y=%.0f\n", static_cast<double>(point.x),
                  static_cast<double>(point.y));
      pressed = true;
      last_touch = point;
      if (toolbar.confirm_new) {
        const auto action = tinydraw::toolbar_action_at(point, toolbar);
        if (action == tinydraw::ToolbarAction::kCancelNewDrawing ||
            action == tinydraw::ToolbarAction::kConfirmNewDrawing) {
          toolbar_action(point);
          continue;
        }
      }
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_pressed = true;
        toolbar_sum = point;
        toolbar_samples = 1;
      } else {
        close_popups();
        if (toolbar.tool == tinydraw::DrawingTool::kPan) {
          static_cast<void>(canvas.world().capture(canvas.committed()));
          pan_start_touch = point;
          pan_start_origin = canvas.world().origin();
          pan_frames = 0;
          pan_render_us = 0;
          maximum_pan_render_us = 0;
          reset_display_timing();
          panning = true;
          continue;
        }
        stroke_color = toolbar.tool == tinydraw::DrawingTool::kEraser
                           ? kBackground
                           : tinydraw::rgb565(toolbar.color);
        previous_saved_touch = point;
        if (!demo_replaying) {
          drawing_store.include_segment(point, point, ink.config().size + 4.0F,
                                        canvas.world().origin());
        }
        last_ink = ink.begin({.x = point.x, .y = point.y, .timestamp_us = event.timestamp_us});
        stroke_samples = 1;
        stroke_started_us = event.timestamp_us;
        previous_touch_us = event.timestamp_us;
        touch_intervals_us = 0;
        maximum_touch_interval_us = 0;
        maximum_input_lag_us = input_lag_us;
        maximum_queue_depth = queue_depth;
        stroke_render_us = 0;
        maximum_render_us = 0;
        reset_display_timing();
        const auto started = esp_timer_get_time();
        {
          static_cast<void>(canvas.raster().update(ribbon.append(last_ink), stroke_color));
        }
        const auto elapsed = esp_timer_get_time() - started;
        stroke_render_us += elapsed;
        maximum_render_us = std::max(maximum_render_us, elapsed);
      }
    } else if (touching && pressed && toolbar_pressed &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_sum.x += point.x;
        toolbar_sum.y += point.y;
        ++toolbar_samples;
      }
    } else if (touching && pressed && panning &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      pan_to(point, event.timestamp_us);
    } else if (touching && pressed && ink.active() &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      if (!demo_replaying) {
        drawing_store.include_segment(previous_saved_touch, point, ink.config().size + 4.0F,
                                      canvas.world().origin());
      }
      previous_saved_touch = point;
      last_ink = ink.update({.x = point.x, .y = point.y, .timestamp_us = event.timestamp_us});
      const std::uint32_t touch_interval_us = event.timestamp_us - previous_touch_us;
      previous_touch_us = event.timestamp_us;
      touch_intervals_us += touch_interval_us;
      maximum_touch_interval_us = std::max(maximum_touch_interval_us, touch_interval_us);
      ++stroke_samples;
      maximum_input_lag_us = std::max(maximum_input_lag_us, input_lag_us);
      maximum_queue_depth = std::max(maximum_queue_depth, queue_depth);
      const auto started = esp_timer_get_time();
      {
        static_cast<void>(canvas.raster().update(ribbon.append(last_ink), stroke_color));
      }
      const auto elapsed = esp_timer_get_time() - started;
      stroke_render_us += elapsed;
      maximum_render_us = std::max(maximum_render_us, elapsed);
    } else if (!touching && pressed) {
      pressed = false;
      if (toolbar_pressed) {
        toolbar_pressed = false;
        const float divisor = static_cast<float>(toolbar_samples == 0 ? 1 : toolbar_samples);
        const tinydraw::Point tap{.x = toolbar_sum.x / divisor, .y = toolbar_sum.y / divisor};
        toolbar_samples = 0;
        if (tinydraw::toolbar_contains(tap, toolbar)) {
          toolbar_action(tap);
        }
        continue;
      }
      if (panning) {
        pan_to(point, event.timestamp_us);
        panning = false;
        std::int64_t settle_us = 0;
        const auto settle_started = esp_timer_get_time();
        static_cast<void>(
            canvas.world().show(canvas.world().origin(), canvas.committed(), canvas.visible()));
        settle_us = esp_timer_get_time() - settle_started;
        if (!demo_replaying && !persistence_resync_needed) {
          drawing_store.save_origin(canvas.world());
        }
        const auto bytes = static_cast<std::uint64_t>(pan_frames) * tinydraw::kCanvasWidth *
                           tinydraw::esp32::kPhysicalMainOverlayTop * sizeof(std::uint16_t);
        const DisplayTimingSnapshot timing = display_timing();
        std::printf(
            "TINYDRAW_PAN_PERF frames=%lu bytes=%llu average_us=%lld max_us=%lld "
            "settle_us=%lld prepare_us=%lld transfer_us=%lld pushes=%lu\n",
            static_cast<unsigned long>(pan_frames), static_cast<unsigned long long>(bytes),
            static_cast<long long>(pan_frames == 0 ? 0 : pan_render_us / pan_frames),
            static_cast<long long>(maximum_pan_render_us), static_cast<long long>(settle_us),
            static_cast<long long>(timing.prepare_us), static_cast<long long>(timing.transfer_us),
            static_cast<unsigned long>(timing.pushes));
        continue;
      }
      if (ink.active()) {
        last_ink =
            ink.finish({.x = last_touch.x, .y = last_touch.y, .timestamp_us = event.timestamp_us});
        const auto started = esp_timer_get_time();
        static_cast<void>(canvas.raster().finish(ribbon.finish(last_ink), stroke_color,
                                                 &canvas.undo_history(), canvas.world().origin()));
        const auto finish_us = esp_timer_get_time() - started;
        const DisplayTimingSnapshot timing = display_timing();
        toolbar.export_ready = false;
        if (!demo_replaying) {
          if (persistence_resync_needed) {
            static_cast<void>(canvas.world().capture(canvas.committed()));
            drawing_store.save_all(canvas.world());
          } else {
            drawing_store.save_stroke(canvas.world(), canvas.committed());
          }
          persistence_resync_needed = false;
        }
        std::printf(
            "[DEBUG-perf1] samples=%lu updates_us=%lld average_us=%lld max_us=%lld "
            "finish_us=%lld display_prepare_us=%lld display_transfer_us=%lld pushes=%lu "
            "stroke_us=%lu touch_average_us=%llu touch_max_us=%lu "
            "max_input_lag_us=%lu max_queue=%lu\n",
            static_cast<unsigned long>(stroke_samples), static_cast<long long>(stroke_render_us),
            static_cast<long long>(stroke_samples == 0 ? 0 : stroke_render_us / stroke_samples),
            static_cast<long long>(maximum_render_us), static_cast<long long>(finish_us),
            static_cast<long long>(timing.prepare_us), static_cast<long long>(timing.transfer_us),
            static_cast<unsigned long>(timing.pushes),
            static_cast<unsigned long>(event.timestamp_us - stroke_started_us),
            static_cast<unsigned long long>(
                stroke_samples <= 1 ? 0 : touch_intervals_us / (stroke_samples - 1U)),
            static_cast<unsigned long>(maximum_touch_interval_us),
            static_cast<unsigned long>(maximum_input_lag_us),
            static_cast<unsigned long>(maximum_queue_depth));
        update_toolbar();
      }
    }
  }
}
