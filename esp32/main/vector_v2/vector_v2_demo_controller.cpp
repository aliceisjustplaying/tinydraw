#include "vector_v2_demo_controller.h"

#include <cstdint>

namespace tinydraw::esp32 {

VectorV2DemoController::VectorV2DemoController(std::span<vector_v2::DemoSample> storage)
    : tape_(storage) {
  const esp_timer_create_args_t timer_args{
      .callback = timer_callback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "v2_demo",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&timer_args, &timer_) != ESP_OK) {
    timer_ = nullptr;
  }
}

VectorV2DemoController::~VectorV2DemoController() {
  cancel_timer();
  if (timer_ != nullptr) {
    static_cast<void>(esp_timer_delete(timer_));
  }
}

void VectorV2DemoController::begin_recording(std::uint32_t started_us) {
  cancel_timer();
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
  replay_started_us_ = started_us;
  mode_ = VectorV2DemoMode::kReplaying;
  arm_next(started_us);
  return true;
}

std::optional<vector_v2::DemoEvent> VectorV2DemoController::pop_due(std::uint32_t now_us) {
  if (!replaying() || !replay_urgent()) {
    return std::nullopt;
  }
  replay_urgent_.store(false, std::memory_order_release);
  const auto event = tape_.pop_replay(now_us);
  if (tape_.replaying()) {
    arm_next(now_us);
  } else {
    mode_ = VectorV2DemoMode::kReady;
  }
  return event;
}

void VectorV2DemoController::timer_callback(void* argument) {
  auto& controller = *static_cast<VectorV2DemoController*>(argument);
  controller.replay_urgent_.store(true, std::memory_order_release);
}

void VectorV2DemoController::arm_next(std::uint32_t now_us) {
  cancel_timer();
  const auto offset = tape_.next_replay_offset_us();
  if (!offset.has_value()) {
    return;
  }
  const std::uint32_t target_us = replay_started_us_ + *offset;
  const std::int32_t remaining_us = static_cast<std::int32_t>(target_us - now_us);
  if (remaining_us <= 0) {
    replay_urgent_.store(true, std::memory_order_release);
    return;
  }
  if (esp_timer_start_once(timer_, static_cast<std::uint64_t>(remaining_us)) != ESP_OK) {
    replay_urgent_.store(true, std::memory_order_release);
  }
}

void VectorV2DemoController::cancel_timer() {
  replay_urgent_.store(false, std::memory_order_release);
  if (timer_ != nullptr) {
    static_cast<void>(esp_timer_stop(timer_));
  }
}

}  // namespace tinydraw::esp32
