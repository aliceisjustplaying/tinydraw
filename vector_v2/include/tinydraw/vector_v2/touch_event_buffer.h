#ifndef TINYDRAW_VECTOR_V2_TOUCH_EVENT_BUFFER_H
#define TINYDRAW_VECTOR_V2_TOUCH_EVENT_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {

enum class TouchContactRead : std::uint8_t {
  kPoint,
  kNoTouch,
  kError,
};

enum class TouchEventKind : std::uint8_t {
  kDown,
  kMove,
  kUp,
};

struct TouchContactPoint {
  float x = 0.0F;
  float y = 0.0F;
};

struct TouchEvent {
  TouchContactPoint point{};
  std::uint32_t timestamp_us = 0;
  std::uint32_t sequence = 0;
  TouchEventKind kind = TouchEventKind::kUp;
};

enum class TouchOfferResult : std::uint8_t {
  kIgnored,
  kQueued,
  kMoveCoalesced,
  kErrorHeld,
  kOverflow,
};

// Converts noisy physical contact reads into ordered semantic events. Caller-
// owned storage keeps allocation policy outside this module. Down and Up edges
// are never coalesced; redundant Moves collapse to the newest point. When an
// edge arrives at capacity, an older Move is discarded before any edge is
// refused. Access must be externally serialized when producer and consumer run
// on different cores.
class TouchEventBuffer {
 public:
  explicit TouchEventBuffer(std::span<TouchEvent> storage);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] TouchOfferResult offer(TouchContactRead read, TouchContactPoint point,
                                       std::uint32_t timestamp_us);
  [[nodiscard]] std::optional<TouchEvent> pop();
  [[nodiscard]] std::size_t pending() const;

 private:
  [[nodiscard]] std::size_t physical_index(std::size_t logical_index) const;
  [[nodiscard]] bool remove_oldest_move();
  [[nodiscard]] TouchOfferResult enqueue(const TouchEvent& event);

  std::span<TouchEvent> storage_{};
  std::size_t head_ = 0;
  std::size_t size_ = 0;
  std::uint32_t sequence_ = 0;
  TouchContactPoint last_point_{};
  std::uint8_t no_touch_reads_ = 0;
  bool touching_ = false;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_TOUCH_EVENT_BUFFER_H
