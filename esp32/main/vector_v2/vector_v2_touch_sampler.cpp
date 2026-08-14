#include "vector_v2_touch_sampler.h"

#include <algorithm>

#include "esp_timer.h"

namespace tinydraw::esp32 {
namespace {

void include_max(std::atomic<std::uint32_t>& maximum, std::uint32_t value) {
  std::uint32_t prior = maximum.load(std::memory_order_relaxed);
  while (prior < value && !maximum.compare_exchange_weak(prior, value, std::memory_order_relaxed)) {
  }
}

}  // namespace

bool VectorV2TouchSampler::start() {
  if (task_ != nullptr) {
    return true;
  }
  if (!touch_.ready()) {
    return false;
  }
  if (mailbox_ == nullptr) {
    mailbox_ = xQueueCreate(1U, sizeof(SampledTouch));
  }
  if (mailbox_ == nullptr) {
    return false;
  }
  return xTaskCreatePinnedToCore(task_entry, "v2_touch", 3'072U, this, 5U, &task_, 1) == pdPASS;
}

std::optional<SampledTouch> VectorV2TouchSampler::read_latest() {
  if (mailbox_ == nullptr) {
    return std::nullopt;
  }
  SampledTouch sample{};
  if (xQueueReceive(mailbox_, &sample, 0) != pdTRUE) {
    return std::nullopt;
  }
  return sample;
}

TouchSamplerMetrics VectorV2TouchSampler::take_metrics() {
  return {
      .samples = samples_.exchange(0U, std::memory_order_relaxed),
      .errors = errors_.exchange(0U, std::memory_order_relaxed),
      .maximum_interval_us = maximum_interval_us_.exchange(0U, std::memory_order_relaxed),
      .maximum_read_us = maximum_read_us_.exchange(0U, std::memory_order_relaxed),
  };
}

void VectorV2TouchSampler::task_entry(void* argument) {
  static_cast<VectorV2TouchSampler*>(argument)->run();
}

void VectorV2TouchSampler::run() {
  std::uint32_t sequence = 0;
  std::uint32_t previous_us = 0;
  while (true) {
    Point point{};
    const std::int64_t started_us = esp_timer_get_time();
    const TouchRead read = touch_.read(point);
    const std::uint32_t completed_us = static_cast<std::uint32_t>(esp_timer_get_time());
    const std::uint32_t read_us = completed_us - static_cast<std::uint32_t>(started_us);
    if (previous_us != 0U) {
      include_max(maximum_interval_us_, completed_us - previous_us);
    }
    previous_us = completed_us;
    include_max(maximum_read_us_, read_us);
    samples_.fetch_add(1U, std::memory_order_relaxed);
    errors_.fetch_add(read == TouchRead::kError, std::memory_order_relaxed);
    const SampledTouch sample{
        .point = point,
        .timestamp_us = completed_us,
        .sequence = ++sequence,
        .read = read,
    };
    static_cast<void>(xQueueOverwrite(mailbox_, &sample));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace tinydraw::esp32
