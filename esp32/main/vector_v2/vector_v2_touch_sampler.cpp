#include "vector_v2_touch_sampler.h"

#include <algorithm>
#include <utility>

#include "esp_timer.h"

namespace tinydraw::esp32 {
namespace {

void include_max(std::atomic<std::uint32_t>& maximum, std::uint32_t value) {
  std::uint32_t prior = maximum.load(std::memory_order_relaxed);
  while (prior < value && !maximum.compare_exchange_weak(prior, value, std::memory_order_relaxed)) {
  }
}

vector_v2::TouchContactRead contact_read(TouchRead read) {
  switch (read) {
    case TouchRead::kPoint:
      return vector_v2::TouchContactRead::kPoint;
    case TouchRead::kNoTouch:
      return vector_v2::TouchContactRead::kNoTouch;
    case TouchRead::kError:
      return vector_v2::TouchContactRead::kError;
  }
  return vector_v2::TouchContactRead::kError;
}

}  // namespace

VectorV2TouchSampler::~VectorV2TouchSampler() { stop(); }

bool VectorV2TouchSampler::start() {
  if (task_ != nullptr) {
    return true;
  }
  if (!touch_.ready() || !events_.ready()) {
    return false;
  }
  if (event_ready_ == nullptr) {
    event_ready_ = xSemaphoreCreateBinaryStatic(&event_ready_storage_);
  }
  if (stopped_ == nullptr) {
    stopped_ = xSemaphoreCreateBinaryStatic(&stopped_storage_);
  }
  if (event_ready_ == nullptr || stopped_ == nullptr) {
    return false;
  }
  while (xSemaphoreTake(event_ready_, 0) == pdTRUE) {
  }
  while (xSemaphoreTake(stopped_, 0) == pdTRUE) {
  }
  portENTER_CRITICAL(&event_lock_);
  const bool has_pending = events_.pending() != 0U;
  touch_urgent_.store(has_pending, std::memory_order_release);
  portEXIT_CRITICAL(&event_lock_);
  if (has_pending) {
    static_cast<void>(xSemaphoreGive(event_ready_));
  }
  stop_requested_.store(false, std::memory_order_release);
  if (xTaskCreatePinnedToCore(task_entry, "v2_touch", 3'072U, this, 5U, &task_, 1) != pdPASS) {
    task_ = nullptr;
    return false;
  }
  return true;
}

void VectorV2TouchSampler::stop() {
  if (task_ == nullptr) {
    return;
  }
  stop_requested_.store(true, std::memory_order_release);
  static_cast<void>(xSemaphoreTake(stopped_, portMAX_DELAY));
  task_ = nullptr;
}

std::size_t VectorV2TouchSampler::pending() const {
  portENTER_CRITICAL(&event_lock_);
  const std::size_t count = events_.pending();
  portEXIT_CRITICAL(&event_lock_);
  return count;
}

bool VectorV2TouchSampler::touch_urgent() const {
  return touch_urgent_.load(std::memory_order_acquire);
}

bool VectorV2TouchSampler::wait_for_event(TickType_t timeout_ticks) {
  if (touch_urgent()) {
    return true;
  }
  if (event_ready_ == nullptr) {
    return false;
  }

  TimeOut_t timeout{};
  vTaskSetTimeOutState(&timeout);
  TickType_t remaining_ticks = timeout_ticks;
  for (;;) {
    if (xSemaphoreTake(event_ready_, remaining_ticks) != pdTRUE) {
      return touch_urgent();
    }
    if (touch_urgent()) {
      return true;
    }
    if (timeout_ticks != portMAX_DELAY &&
        xTaskCheckForTimeOut(&timeout, &remaining_ticks) == pdTRUE) {
      return touch_urgent();
    }
  }
}

std::optional<SampledTouch> VectorV2TouchSampler::read_next() {
  portENTER_CRITICAL(&event_lock_);
  const auto event = events_.pop();
  const bool has_pending = events_.pending() != 0U;
  touch_urgent_.store(has_pending, std::memory_order_release);
  portEXIT_CRITICAL(&event_lock_);
  if (!has_pending && event_ready_ != nullptr) {
    static_cast<void>(xSemaphoreTake(event_ready_, 0));
  }
  if (!event.has_value()) {
    return std::nullopt;
  }
  const std::uint32_t event_age_us =
      static_cast<std::uint32_t>(esp_timer_get_time()) - event->timestamp_us;
  include_max(maximum_event_age_us_, event_age_us);
  ++events_consumed_;
  down_events_ += event->kind == vector_v2::TouchEventKind::kDown;
  up_events_ += event->kind == vector_v2::TouchEventKind::kUp;
  events_at_least_8ms_old_ += event_age_us >= 8'000U;
  return SampledTouch{
      .point = {.x = event->point.x, .y = event->point.y},
      .timestamp_us = event->timestamp_us,
      .sequence = event->sequence,
      .kind = event->kind,
  };
}

TouchSamplerMetrics VectorV2TouchSampler::take_metrics() {
  return {
      .samples = samples_.exchange(0U, std::memory_order_relaxed),
      .errors = errors_.exchange(0U, std::memory_order_relaxed),
      .queue_overflows = queue_overflows_.exchange(0U, std::memory_order_relaxed),
      .queue_resyncs = queue_resyncs_.exchange(0U, std::memory_order_relaxed),
      .moves_coalesced = moves_coalesced_.exchange(0U, std::memory_order_relaxed),
      .maximum_interval_us = maximum_interval_us_.exchange(0U, std::memory_order_relaxed),
      .maximum_read_us = maximum_read_us_.exchange(0U, std::memory_order_relaxed),
      .events_consumed = std::exchange(events_consumed_, 0U),
      .down_events = std::exchange(down_events_, 0U),
      .up_events = std::exchange(up_events_, 0U),
      .events_at_least_8ms_old = std::exchange(events_at_least_8ms_old_, 0U),
      .maximum_event_age_us = maximum_event_age_us_.exchange(0U, std::memory_order_relaxed),
  };
}

void VectorV2TouchSampler::task_entry(void* argument) {
  static_cast<VectorV2TouchSampler*>(argument)->run();
  // The worker owns its cross-core teardown. Once run() signals stopped_ it
  // touches no sampler state again, and self-deletion avoids deleting a task
  // that may still be returning on the other core.
  vTaskDelete(nullptr);
}

void VectorV2TouchSampler::run() {
  std::uint32_t previous_us = 0;
  while (!stop_requested_.load(std::memory_order_acquire)) {
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

    portENTER_CRITICAL(&event_lock_);
    const bool was_empty = events_.pending() == 0U;
    const vector_v2::TouchOfferResult offered =
        events_.offer(contact_read(read), {.x = point.x, .y = point.y}, completed_us);
    const bool became_nonempty = was_empty && events_.pending() != 0U;
    if (became_nonempty) {
      touch_urgent_.store(true, std::memory_order_release);
    }
    portEXIT_CRITICAL(&event_lock_);
    if (became_nonempty) {
      static_cast<void>(xSemaphoreGive(event_ready_));
    }
    queue_overflows_.fetch_add(offered == vector_v2::TouchOfferResult::kOverflow,
                               std::memory_order_relaxed);
    queue_resyncs_.fetch_add(offered == vector_v2::TouchOfferResult::kResynchronized,
                             std::memory_order_relaxed);
    moves_coalesced_.fetch_add(offered == vector_v2::TouchOfferResult::kMoveCoalesced,
                               std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  static_cast<void>(xSemaphoreGive(stopped_));
}

}  // namespace tinydraw::esp32
