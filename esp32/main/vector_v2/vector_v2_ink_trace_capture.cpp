#include "vector_v2_ink_trace_capture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

std::uint16_t quantize(float value, std::uint16_t maximum) {
  const long rounded = std::lroundf(value);
  if (rounded < 0L) {
    return 0U;
  }
  if (rounded > static_cast<long>(maximum)) {
    return maximum;
  }
  return static_cast<std::uint16_t>(rounded);
}

const char* kind_text(vector_v2::TouchEventKind kind) {
  switch (kind) {
    case vector_v2::TouchEventKind::kDown:
      return "Down";
    case vector_v2::TouchEventKind::kMove:
      return "Move";
    case vector_v2::TouchEventKind::kUp:
      return "Up";
  }
  return "Up";
}

}  // namespace

InkTraceCaptureRing::InkTraceCaptureRing(std::span<InkTraceCaptureRecord> storage)
    : storage_(storage), drain_(drain_storage_) {}

bool InkTraceCaptureRing::ready() const { return !storage_.empty() && drain_.ready(); }

void InkTraceCaptureRing::record_contact_read(vector_v2::TouchContactRead read,
                                              vector_v2::TouchContactPoint point,
                                              std::uint32_t timestamp_us) {
  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  // The drain buffer receives the identical offer stream as the production
  // buffer, so its Down/Move/Up derivation and lift debounce stay in
  // lockstep. Popping every event immediately means no Move can coalesce.
  static_cast<void>(drain_.offer(read, point, timestamp_us));
  while (const auto event = drain_.pop()) {
    append(*event);
  }
}

void InkTraceCaptureRing::append(const vector_v2::TouchEvent& event) {
  touching_.store(event.kind != vector_v2::TouchEventKind::kUp, std::memory_order_relaxed);
  last_activity_us_.store(event.timestamp_us, std::memory_order_relaxed);
  if (event.kind == vector_v2::TouchEventKind::kUp) {
    strokes_.fetch_add(1U, std::memory_order_relaxed);
  }
  const std::size_t index = count_.load(std::memory_order_relaxed);
  if (index >= storage_.size()) {
    overflowed_.store(true, std::memory_order_relaxed);
    return;
  }
  storage_[index] = {
      .timestamp_us = event.timestamp_us,
      .x = quantize(event.point.x, vector_v2::kInkTraceWidth - 1U),
      .y = quantize(event.point.y, vector_v2::kInkTraceHeight - 1U),
      .kind = event.kind,
  };
  count_.store(index + 1U, std::memory_order_release);
}

std::size_t InkTraceCaptureRing::size() const { return count_.load(std::memory_order_acquire); }

bool InkTraceCaptureRing::overflowed() const { return overflowed_.load(std::memory_order_relaxed); }

std::uint32_t InkTraceCaptureRing::stroke_count() const {
  return strokes_.load(std::memory_order_relaxed);
}

bool InkTraceCaptureRing::touching() const { return touching_.load(std::memory_order_relaxed); }

std::uint32_t InkTraceCaptureRing::last_activity_us() const {
  return last_activity_us_.load(std::memory_order_relaxed);
}

void InkTraceCaptureRing::dump_and_reset() {
  enabled_.store(false, std::memory_order_relaxed);
  // The 1 kHz producer iteration is microseconds long; two ticks guarantee
  // any in-flight append has finished before the consumer reads.
  vTaskDelay(pdMS_TO_TICKS(2));

  const std::size_t count = count_.load(std::memory_order_acquire);
  std::printf("TINYDRAW_INKTRACE_CAPTURE_BEGIN events=%u strokes=%u overflow=%u\n",
              static_cast<unsigned>(count), static_cast<unsigned>(strokes_.load()),
              overflowed_.load() ? 1U : 0U);
  std::printf("magic,version,name,source,sample_rate_note\n");
  std::printf("%.*s,%u,captured,recorded,captured 1kHz sampler stream\n",
              static_cast<int>(vector_v2::kInkTraceMagic.size()), vector_v2::kInkTraceMagic.data(),
              static_cast<unsigned>(vector_v2::kInkTraceVersion));
  std::printf("t_us,kind,x,y\n");
  // Trace timestamps are monotonic from trace start. Accumulating the
  // consecutive u32 deltas is wrap-safe for any real gesture spacing.
  std::uint64_t relative_us = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const InkTraceCaptureRecord& record = storage_[index];
    if (index != 0U) {
      relative_us += record.timestamp_us - storage_[index - 1U].timestamp_us;
    }
    std::printf("%llu,%s,%u,%u\n", static_cast<unsigned long long>(relative_us),
                kind_text(record.kind), static_cast<unsigned>(record.x),
                static_cast<unsigned>(record.y));
    if ((index & 0xFFU) == 0xFFU) {
      // Long dumps otherwise starve the idle task until the task watchdog
      // fires and interleaves its report into the CSV stream; yielding also
      // lets the USB console buffer drain instead of dropping bytes.
      std::fflush(stdout);
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
  std::printf("TINYDRAW_INKTRACE_CAPTURE_END events=%u\n", static_cast<unsigned>(count));

  count_.store(0U, std::memory_order_relaxed);
  overflowed_.store(false, std::memory_order_relaxed);
  strokes_.store(0U, std::memory_order_relaxed);
  enabled_.store(true, std::memory_order_release);
}

}  // namespace tinydraw::esp32
