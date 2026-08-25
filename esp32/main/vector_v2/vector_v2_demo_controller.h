#ifndef TINYDRAW_ESP32_VECTOR_V2_DEMO_CONTROLLER_H
#define TINYDRAW_ESP32_VECTOR_V2_DEMO_CONTROLLER_H

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tinydraw/vector_v2/demo_tape.h"

namespace tinydraw::esp32 {

enum class VectorV2DemoMode : std::uint8_t {
  kEmpty,
  kRecording,
  kReady,
  kReplaying,
};

class VectorV2DemoController {
 public:
  explicit VectorV2DemoController(std::span<vector_v2::DemoSample> storage);
  ~VectorV2DemoController();

  VectorV2DemoController(const VectorV2DemoController&) = delete;
  VectorV2DemoController& operator=(const VectorV2DemoController&) = delete;

  [[nodiscard]] bool ready() const {
    return timer_ != nullptr && drain_timer_ != nullptr && replay_ready_ != nullptr &&
           timer_drained_ != nullptr;
  }
  [[nodiscard]] VectorV2DemoMode mode() const { return mode_; }
  [[nodiscard]] bool active() const { return mode_ != VectorV2DemoMode::kEmpty; }
  [[nodiscard]] bool recording() const { return mode_ == VectorV2DemoMode::kRecording; }
  [[nodiscard]] bool replaying() const { return mode_ == VectorV2DemoMode::kReplaying; }
  [[nodiscard]] bool replay_failed() const { return replay_failed_; }
  [[nodiscard]] bool tape_ready() const { return mode_ == VectorV2DemoMode::kReady; }
  [[nodiscard]] bool overflowed() const { return tape_.overflowed(); }
  [[nodiscard]] std::size_t sample_count() const { return tape_.size(); }

  void begin_recording(std::uint32_t started_us);
  [[nodiscard]] bool record_touch(const vector_v2::TouchEvent& event);
  [[nodiscard]] bool record_chrome_toggle(std::uint32_t timestamp_us);
  void stop_recording();

  [[nodiscard]] bool begin_replay(std::uint32_t started_us);
  [[nodiscard]] std::optional<vector_v2::DemoEvent> pop_due(std::uint32_t now_us);
  [[nodiscard]] bool replay_urgent() const {
    return replay_urgent_.load(std::memory_order_acquire);
  }
  [[nodiscard]] const std::atomic<bool>* replay_urgency_flag() const { return &replay_urgent_; }
  [[nodiscard]] bool wait_for_replay_event(TickType_t timeout_ticks);

 private:
  static void timer_callback(void* argument);
  static void drain_callback(void* argument);
  [[nodiscard]] bool arm_next(std::uint32_t now_us);
  void signal_replay_ready();
  void cancel_timer();
  void finish_if_full(bool recorded);

  vector_v2::DemoTape tape_;
  esp_timer_handle_t timer_ = nullptr;
  esp_timer_handle_t drain_timer_ = nullptr;
  StaticSemaphore_t replay_ready_storage_{};
  SemaphoreHandle_t replay_ready_ = nullptr;
  StaticSemaphore_t timer_drained_storage_{};
  SemaphoreHandle_t timer_drained_ = nullptr;
  std::atomic<bool> replay_urgent_{false};
  std::uint32_t replay_started_us_ = 0;
  VectorV2DemoMode mode_ = VectorV2DemoMode::kEmpty;
  bool replay_failed_ = false;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_DEMO_CONTROLLER_H
