#ifndef TINYDRAW_ESP32_VECTOR_V2_INK_TRACE_CAPTURE_H
#define TINYDRAW_ESP32_VECTOR_V2_INK_TRACE_CAPTURE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/ink_trace.h"
#include "tinydraw/vector_v2/touch_event_buffer.h"

namespace tinydraw::esp32 {

// Compact in-RAM capture record; converted to a canonical TraceEvent with a
// trace-relative timestamp at dump time.
struct InkTraceCaptureRecord {
  std::uint32_t timestamp_us = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  vector_v2::TouchEventKind kind = vector_v2::TouchEventKind::kUp;
};

// ~144 KiB of PSRAM; covers roughly 12 s of continuous contact at the 1 kHz
// sampler cadence, comfortably above every canonical gesture.
inline constexpr std::size_t kInkTraceCaptureCapacity = 12'288U;

// Records the semantic touch stream upstream of coalescing, per
// docs/INK_TRACE_HARNESS.md §1. The core-1 sampler task feeds every raw
// contact read through a private, immediately drained TouchEventBuffer, so
// Down/Move/Up derivation and the lift debounce match production exactly
// while no Move is ever coalesced (coalescing only merges *pending* moves).
// The core-0 app dumps the accumulated events as canonical trace CSV between
// serial markers once the owner has been idle, then resets.
//
// Concurrency: single producer (sampler task), single consumer (app task).
// The consumer must pause capture before reading; dump_and_reset() does.
class InkTraceCaptureRing {
 public:
  explicit InkTraceCaptureRing(std::span<InkTraceCaptureRecord> storage);

  [[nodiscard]] bool ready() const;

  // Producer side: called from the sampler task for every raw contact read.
  void record_contact_read(vector_v2::TouchContactRead read, vector_v2::TouchContactPoint point,
                           std::uint32_t timestamp_us);

  // Consumer side.
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool overflowed() const;
  [[nodiscard]] std::uint32_t stroke_count() const;
  [[nodiscard]] bool touching() const;
  [[nodiscard]] std::uint32_t last_activity_us() const;
  // Pauses capture, waits out any in-flight producer iteration, prints the
  // accumulated events as canonical trace CSV between
  // TINYDRAW_INKTRACE_CAPTURE_BEGIN/END markers, resets, and resumes.
  void dump_and_reset();

 private:
  void append(const vector_v2::TouchEvent& event);

  std::span<InkTraceCaptureRecord> storage_;
  std::array<vector_v2::TouchEvent, 8> drain_storage_{};
  vector_v2::TouchEventBuffer drain_;
  std::atomic<std::size_t> count_{0};
  std::atomic<bool> enabled_{true};
  std::atomic<bool> overflowed_{false};
  std::atomic<bool> touching_{false};
  std::atomic<std::uint32_t> strokes_{0};
  std::atomic<std::uint32_t> last_activity_us_{0};
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_INK_TRACE_CAPTURE_H
