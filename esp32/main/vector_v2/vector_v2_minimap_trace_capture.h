#ifndef TINYDRAW_ESP32_VECTOR_V2_MINIMAP_TRACE_CAPTURE_H
#define TINYDRAW_ESP32_VECTOR_V2_MINIMAP_TRACE_CAPTURE_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/touch_event_buffer.h"

namespace tinydraw::esp32 {

enum class MinimapTraceFlag : std::uint8_t {
  kPressed = 1U << 0U,
  kMinimap = 1U << 1U,
  kPanning = 1U << 2U,
  kToolbar = 1U << 3U,
  kInk = 1U << 4U,
  kHit = 1U << 5U,
};

constexpr std::uint8_t minimap_trace_flag(MinimapTraceFlag flag) {
  return static_cast<std::uint8_t>(flag);
}

struct MinimapTraceState {
  std::int16_t level_x = 0;
  std::int16_t level_y = 0;
  std::uint8_t flags = 0;
};

// Product-consumed semantic event plus routing/camera state on both sides of
// the app coordinator. At most one record is written per consumed event.
struct MinimapTraceRecord {
  std::uint32_t timestamp_us = 0;
  std::uint32_t sequence = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t zoom_percent = 0;
  std::int16_t before_x = 0;
  std::int16_t before_y = 0;
  std::int16_t after_x = 0;
  std::int16_t after_y = 0;
  std::uint8_t before_flags = 0;
  std::uint8_t after_flags = 0;
  vector_v2::TouchEventKind kind = vector_v2::TouchEventKind::kUp;
  std::uint8_t reserved = 0;
};
static_assert(sizeof(MinimapTraceRecord) <= 32U);

// 2,048 product-consumed events cover about 28 seconds at the measured 74 Hz
// controller cadence. Storage is allocated last in PSRAM so capture cannot
// move any product workspace onto different cache sets.
inline constexpr std::size_t kMinimapTraceCapacity = 2'048U;

class MinimapTraceCapture {
 public:
  explicit MinimapTraceCapture(std::span<MinimapTraceRecord> storage) : storage_(storage) {}

  [[nodiscard]] bool ready() const { return !storage_.empty(); }
  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool overflowed() const { return overflowed_; }
  [[nodiscard]] std::uint32_t last_activity_us() const { return last_activity_us_; }

  void record(std::uint32_t timestamp_us, std::uint32_t sequence,
              vector_v2::TouchEventKind kind, float x, float y, int zoom_percent,
              MinimapTraceState before, MinimapTraceState after);
  void note_activity(std::uint32_t timestamp_us) { last_activity_us_ = timestamp_us; }
  void include_append_us(std::uint32_t duration_us);
  void dump_and_reset();

 private:
  std::span<MinimapTraceRecord> storage_;
  std::size_t count_ = 0;
  std::uint64_t append_total_us_ = 0;
  std::uint32_t append_max_us_ = 0;
  std::uint32_t offers_ = 0;
  std::uint32_t duplicate_moves_ = 0;
  std::uint32_t last_activity_us_ = 0;
  std::uint16_t last_x_ = 0;
  std::uint16_t last_y_ = 0;
  bool have_last_point_ = false;
  bool overflowed_ = false;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_MINIMAP_TRACE_CAPTURE_H
