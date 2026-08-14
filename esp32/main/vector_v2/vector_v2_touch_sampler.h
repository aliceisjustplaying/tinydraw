#ifndef TINYDRAW_ESP32_VECTOR_V2_TOUCH_SAMPLER_H
#define TINYDRAW_ESP32_VECTOR_V2_TOUCH_SAMPLER_H

#include <atomic>
#include <cstdint>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "physical_touch.h"

namespace tinydraw::esp32 {

struct SampledTouch {
  Point point{};
  std::uint32_t timestamp_us = 0;
  std::uint32_t sequence = 0;
  TouchRead read = TouchRead::kNoTouch;
};

struct TouchSamplerMetrics {
  std::uint32_t samples = 0;
  std::uint32_t errors = 0;
  std::uint32_t maximum_interval_us = 0;
  std::uint32_t maximum_read_us = 0;
};

// Sole owner of physical touch I/O after start(). Core 1 continuously replaces
// one mailbox entry; core 0 consumes only the newest state and never waits for
// I2C. Rendering remains single-threaded.
class VectorV2TouchSampler {
 public:
  explicit VectorV2TouchSampler(PhysicalTouch& touch) : touch_(touch) {}

  VectorV2TouchSampler(const VectorV2TouchSampler&) = delete;
  VectorV2TouchSampler& operator=(const VectorV2TouchSampler&) = delete;

  [[nodiscard]] bool start();
  [[nodiscard]] std::optional<SampledTouch> read_latest();
  [[nodiscard]] TouchSamplerMetrics take_metrics();

 private:
  static void task_entry(void* argument);
  void run();

  PhysicalTouch& touch_;
  QueueHandle_t mailbox_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::atomic<std::uint32_t> samples_{0};
  std::atomic<std::uint32_t> errors_{0};
  std::atomic<std::uint32_t> maximum_interval_us_{0};
  std::atomic<std::uint32_t> maximum_read_us_{0};
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_TOUCH_SAMPLER_H
