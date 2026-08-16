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
  stop_waiter_ = nullptr;
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
  stop_waiter_ = xTaskGetCurrentTaskHandle();
  stop_requested_.store(true, std::memory_order_release);
  static_cast<void>(ulTaskNotifyTake(pdTRUE, portMAX_DELAY));
  vTaskDelete(task_);
  task_ = nullptr;
  stop_waiter_ = nullptr;
}

void VectorV2TouchSampler::reset_pending() {
  if (task_ != nullptr) {
    return;
  }
  portENTER_CRITICAL(&event_lock_);
  events_.reset_pending();
  portEXIT_CRITICAL(&event_lock_);
}

std::optional<SampledTouch> VectorV2TouchSampler::read_next() {
  portENTER_CRITICAL(&event_lock_);
  const auto event = events_.pop();
  portEXIT_CRITICAL(&event_lock_);
  if (!event.has_value()) {
    return std::nullopt;
  }
#ifdef TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE
  if (capture_ring_ != nullptr) {
    capture_ring_->record_consumed_event(*event);
  }
#endif
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
  vTaskSuspend(nullptr);
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
    const vector_v2::TouchOfferResult offered =
        events_.offer(contact_read(read), {.x = point.x, .y = point.y}, completed_us);
    portEXIT_CRITICAL(&event_lock_);
    queue_overflows_.fetch_add(offered == vector_v2::TouchOfferResult::kOverflow,
                               std::memory_order_relaxed);
    moves_coalesced_.fetch_add(offered == vector_v2::TouchOfferResult::kMoveCoalesced,
                               std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  const TaskHandle_t waiter = stop_waiter_;
  if (waiter != nullptr) {
    xTaskNotifyGive(waiter);
  }
}

}  // namespace tinydraw::esp32
