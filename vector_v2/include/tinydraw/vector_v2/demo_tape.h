#ifndef TINYDRAW_VECTOR_V2_DEMO_TAPE_H
#define TINYDRAW_VECTOR_V2_DEMO_TAPE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/touch_event_buffer.h"

namespace tinydraw::vector_v2 {

enum class DemoEventKind : std::uint8_t {
  kTouchDown,
  kTouchMove,
  kTouchUp,
  kZoom,
};

struct DemoSample {
  std::uint32_t offset_us = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  DemoEventKind kind = DemoEventKind::kTouchUp;
};

static_assert(sizeof(DemoSample) <= 12U);

struct DemoEvent {
  TouchContactPoint point{};
  std::uint32_t timestamp_us = 0;
  DemoEventKind kind = DemoEventKind::kTouchUp;
};

// Caller-funded, allocation-free capture of the semantic input stream seen by
// the V2 application. The tape stores only accepted touch events and physical
// zoom-button releases, so recording is one bounded sequential write per
// application input event.
class DemoTape {
 public:
  explicit DemoTape(std::span<DemoSample> storage) : storage_(storage) {}

  void begin_recording(std::uint32_t started_us);
  [[nodiscard]] bool record_touch(const TouchEvent& event);
  [[nodiscard]] bool record_zoom(std::uint32_t timestamp_us);
  void stop_recording();

  [[nodiscard]] bool begin_replay(std::uint32_t started_us);
  [[nodiscard]] bool replay_due(std::uint32_t now_us) const;
  [[nodiscard]] std::optional<DemoEvent> pop_replay(std::uint32_t now_us);
  void stop_replay();

  [[nodiscard]] bool recording() const { return recording_; }
  [[nodiscard]] bool replaying() const { return replaying_; }
  [[nodiscard]] bool overflowed() const { return overflowed_; }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] std::size_t replay_index() const { return replay_index_; }
  [[nodiscard]] std::optional<std::uint32_t> next_replay_offset_us() const;
  [[nodiscard]] std::span<const DemoSample> samples() const;

 private:
  [[nodiscard]] bool record(DemoSample sample, std::uint32_t timestamp_us);

  std::span<DemoSample> storage_{};
  std::size_t size_ = 0;
  std::size_t replay_index_ = 0;
  std::uint32_t recording_started_us_ = 0;
  std::uint32_t replay_started_us_ = 0;
  bool recording_ = false;
  bool replaying_ = false;
  bool overflowed_ = false;
};

[[nodiscard]] std::optional<TouchEventKind> demo_touch_kind(DemoEventKind kind);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_DEMO_TAPE_H
