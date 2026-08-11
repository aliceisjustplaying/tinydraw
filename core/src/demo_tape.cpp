#include "tinydraw/demo/demo_tape.h"

#include <algorithm>

namespace tinydraw {
namespace {

std::uint16_t coordinate(float value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0.0F, 65535.0F));
}

}  // namespace

DemoInputEvent replay_demo_sample(const DemoSample& sample, std::uint32_t replay_started_us) {
  return {
      .point = {static_cast<float>(sample.x), static_cast<float>(sample.y)},
      .timestamp_us = replay_started_us + sample.offset_us,
      .touching = sample.touching != 0U,
  };
}

DemoTape::DemoTape(std::span<DemoSample> storage) : storage_(storage) {}

void DemoTape::begin_recording(std::uint32_t started_us) {
  size_ = 0;
  started_us_ = started_us;
  recording_ = true;
  overflowed_ = false;
}

bool DemoTape::record(const DemoInputEvent& event) {
  if (!recording_) {
    return false;
  }
  if (size_ == storage_.size()) {
    overflowed_ = true;
    return false;
  }
  storage_[size_] = {
      .offset_us = event.timestamp_us - started_us_,
      .x = coordinate(event.point.x),
      .y = coordinate(event.point.y),
      .touching = static_cast<std::uint8_t>(event.touching),
  };
  ++size_;
  return true;
}

void DemoTape::stop_recording() { recording_ = false; }

std::span<const DemoSample> DemoTape::samples() const { return storage_.first(size_); }

std::optional<DemoInputEvent> DemoTape::event_at(std::size_t index,
                                                 std::uint32_t replay_started_us) const {
  if (index >= size_) {
    return std::nullopt;
  }
  return replay_demo_sample(storage_[index], replay_started_us);
}

}  // namespace tinydraw
