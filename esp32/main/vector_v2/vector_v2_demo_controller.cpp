#include "vector_v2_demo_controller.h"

#include <cstdint>
#include <cstdlib>

namespace tinydraw::esp32 {

VectorV2DemoController::VectorV2DemoController(std::span<vector_v2::DemoSample> storage)
    : tape_(storage) {
  if (storage.empty()) {
    return;
  }
  replay_ready_ = xSemaphoreCreateBinaryStatic(&replay_ready_storage_);
  timer_drained_ = xSemaphoreCreateBinaryStatic(&timer_drained_storage_);
  const esp_timer_create_args_t timer_args{
      .callback = timer_callback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "v2_demo",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&timer_args, &timer_) != ESP_OK) {
    timer_ = nullptr;
    return;
  }
  const esp_timer_create_args_t drain_timer_args{
      .callback = drain_callback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "v2_demo_drain",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&drain_timer_args, &drain_timer_) != ESP_OK) {
    static_cast<void>(esp_timer_delete(timer_));
    timer_ = nullptr;
    drain_timer_ = nullptr;
  }
}

VectorV2DemoController::~VectorV2DemoController() {
  cancel_timer();
  if (timer_ != nullptr) {
    // Both callbacks run on the serialized ESP timer task. Once this barrier
    // fires, no callback for timer_ can still be using this controller.
    while (xSemaphoreTake(timer_drained_, 0) == pdTRUE) {
    }
    if (esp_timer_start_once(drain_timer_, 1U) != ESP_OK ||
        xSemaphoreTake(timer_drained_, portMAX_DELAY) != pdTRUE) {
      std::abort();
    }
    static_cast<void>(esp_timer_delete(timer_));
  }
  if (drain_timer_ != nullptr) {
    static_cast<void>(esp_timer_delete(drain_timer_));
  }
}

void VectorV2DemoController::begin_recording(std::uint32_t started_us) {
  cancel_timer();
  replay_failed_ = false;
  tape_.begin_recording(started_us);
  mode_ = VectorV2DemoMode::kRecording;
}

bool VectorV2DemoController::record_touch(const vector_v2::TouchEvent& event) {
  const bool recorded = tape_.record_touch(event);
  finish_if_full(recorded);
  return recorded;
}

bool VectorV2DemoController::record_zoom(std::uint32_t timestamp_us) {
  const bool recorded = tape_.record_zoom(timestamp_us);
  finish_if_full(recorded);
  return recorded;
}

void VectorV2DemoController::finish_if_full(bool recorded) {
  if (!recorded && tape_.overflowed()) {
    mode_ = tape_.size() == 0U ? VectorV2DemoMode::kEmpty : VectorV2DemoMode::kReady;
  }
}

void VectorV2DemoController::stop_recording() {
  tape_.stop_recording();
  mode_ = tape_.size() == 0U ? VectorV2DemoMode::kEmpty : VectorV2DemoMode::kReady;
}

bool VectorV2DemoController::begin_replay(std::uint32_t started_us) {
  if (!ready() || !tape_.begin_replay(started_us)) {
    mode_ = tape_.size() == 0U ? VectorV2DemoMode::kEmpty : VectorV2DemoMode::kReady;
    return false;
  }
  replay_failed_ = false;
  replay_started_us_ = started_us;
  mode_ = VectorV2DemoMode::kReplaying;
  return arm_next(started_us);
}

std::optional<vector_v2::DemoEvent> VectorV2DemoController::pop_due(std::uint32_t now_us) {
  if (!replaying() || !replay_urgent()) {
    return std::nullopt;
  }
  replay_urgent_.store(false, std::memory_order_release);
  const auto event = tape_.pop_replay(now_us);
  if (tape_.replaying()) {
    if (!arm_next(now_us)) {
      tape_.stop_replay();
      mode_ = VectorV2DemoMode::kReady;
      replay_failed_ = true;
    }
  } else {
    mode_ = VectorV2DemoMode::kReady;
  }
  return event;
}

void VectorV2DemoController::timer_callback(void* argument) {
  auto& controller = *static_cast<VectorV2DemoController*>(argument);
  controller.signal_replay_ready();
}

void VectorV2DemoController::drain_callback(void* argument) {
  auto& controller = *static_cast<VectorV2DemoController*>(argument);
  static_cast<void>(xSemaphoreGive(controller.timer_drained_));
}

bool VectorV2DemoController::arm_next(std::uint32_t now_us) {
  cancel_timer();
  const auto offset = tape_.next_replay_offset_us();
  if (!offset.has_value()) {
    return true;
  }
  const std::uint32_t target_us = replay_started_us_ + *offset;
  const std::int32_t remaining_us = static_cast<std::int32_t>(target_us - now_us);
  if (remaining_us <= 0) {
    signal_replay_ready();
    return true;
  }
  if (esp_timer_start_once(timer_, static_cast<std::uint64_t>(remaining_us)) != ESP_OK) {
    replay_failed_ = true;
    tape_.stop_replay();
    mode_ = VectorV2DemoMode::kReady;
    return false;
  }
  return true;
}

void VectorV2DemoController::signal_replay_ready() {
  replay_urgent_.store(true, std::memory_order_release);
  if (replay_ready_ != nullptr) {
    static_cast<void>(xSemaphoreGive(replay_ready_));
  }
}

void VectorV2DemoController::cancel_timer() {
  replay_urgent_.store(false, std::memory_order_release);
  if (replay_ready_ != nullptr) {
    while (xSemaphoreTake(replay_ready_, 0) == pdTRUE) {
    }
  }
  if (timer_ != nullptr) {
    static_cast<void>(esp_timer_stop(timer_));
  }
}

bool VectorV2DemoController::wait_for_replay_event(TickType_t timeout_ticks) {
  if (replay_urgent()) {
    return true;
  }
  return replay_ready_ != nullptr && xSemaphoreTake(replay_ready_, timeout_ticks) == pdTRUE;
}

}  // namespace tinydraw::esp32
