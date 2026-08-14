#ifndef TINYDRAW_ESP32_VECTOR_V2_TOUCH_SAMPLER_H
#define TINYDRAW_ESP32_VECTOR_V2_TOUCH_SAMPLER_H

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#include "tinydraw/vector_v2/touch_event_buffer.h"

namespace tinydraw::esp32 {

inline constexpr std::size_t kVectorV2TouchEventCapacity = 16U;

struct SampledTouch {
  Point point{};
  std::uint32_t timestamp_us = 0;
  std::uint32_t sequence = 0;
  vector_v2::TouchEventKind kind = vector_v2::TouchEventKind::kUp;
};

struct TouchSamplerMetrics {
  std::uint32_t samples = 0;
  std::uint32_t errors = 0;
  std::uint32_t queue_overflows = 0;
  std::uint32_t moves_coalesced = 0;
  std::uint32_t maximum_interval_us = 0;
  std::uint32_t maximum_read_us = 0;
  std::uint32_t events_consumed = 0;
  std::uint32_t down_events = 0;
  std::uint32_t up_events = 0;
  std::uint32_t events_at_least_8ms_old = 0;
  std::uint32_t maximum_event_age_us = 0;
};

// Sole owner of physical touch I/O after start(). Core 1 samples I2C and feeds
// an ordered, caller-owned semantic event buffer; core 0 never waits for I2C.
// Destruction synchronously stops the task before either dependency can die.
class VectorV2TouchSampler {
 public:
  VectorV2TouchSampler(PhysicalTouch& touch, std::span<vector_v2::TouchEvent> event_storage)
      : touch_(touch), events_(event_storage) {}
  ~VectorV2TouchSampler();

  VectorV2TouchSampler(const VectorV2TouchSampler&) = delete;
  VectorV2TouchSampler& operator=(const VectorV2TouchSampler&) = delete;

  [[nodiscard]] bool start();
  void stop();
  [[nodiscard]] std::optional<SampledTouch> read_next();
  [[nodiscard]] TouchSamplerMetrics take_metrics();

 private:
  static void task_entry(void* argument);
  void run();

  PhysicalTouch& touch_;
  vector_v2::TouchEventBuffer events_;
  TaskHandle_t task_ = nullptr;
  TaskHandle_t stop_waiter_ = nullptr;
  portMUX_TYPE event_lock_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::uint32_t> samples_{0};
  std::atomic<std::uint32_t> errors_{0};
  std::atomic<std::uint32_t> queue_overflows_{0};
  std::atomic<std::uint32_t> moves_coalesced_{0};
  std::atomic<std::uint32_t> maximum_interval_us_{0};
  std::atomic<std::uint32_t> maximum_read_us_{0};
  std::atomic<std::uint32_t> maximum_event_age_us_{0};
  // Only the core-0 consumer reads or resets these acceptance counters.
  std::uint32_t events_consumed_ = 0;
  std::uint32_t down_events_ = 0;
  std::uint32_t up_events_ = 0;
  std::uint32_t events_at_least_8ms_old_ = 0;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_TOUCH_SAMPLER_H
