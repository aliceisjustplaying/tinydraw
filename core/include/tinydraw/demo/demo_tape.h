#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/geometry.h"

namespace tinydraw {

struct DemoInputEvent {
  Point point;
  std::uint32_t timestamp_us;
  bool touching;
};

struct DemoSample {
  std::uint32_t offset_us = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint8_t touching = 0;
};

static_assert(sizeof(DemoSample) <= 12U);

[[nodiscard]] DemoInputEvent replay_demo_sample(const DemoSample& sample,
                                                std::uint32_t replay_started_us);

class DemoTape {
 public:
  explicit DemoTape(std::span<DemoSample> storage);

  void begin_recording(std::uint32_t started_us);
  [[nodiscard]] bool record(const DemoInputEvent& event);
  void stop_recording();

  [[nodiscard]] bool recording() const { return recording_; }
  [[nodiscard]] bool overflowed() const { return overflowed_; }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] std::span<const DemoSample> samples() const;
  [[nodiscard]] std::optional<DemoInputEvent> event_at(std::size_t index,
                                                       std::uint32_t replay_started_us) const;

 private:
  std::span<DemoSample> storage_;
  std::size_t size_ = 0;
  std::uint32_t started_us_ = 0;
  bool recording_ = false;
  bool overflowed_ = false;
};

}  // namespace tinydraw
