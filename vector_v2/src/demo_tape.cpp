#include "tinydraw/vector_v2/demo_tape.h"

#include <algorithm>
#include <cmath>

namespace tinydraw::vector_v2 {
namespace {

std::uint16_t coordinate(float value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0.0F, 65535.0F));
}

DemoEventKind demo_kind(TouchEventKind kind) {
  switch (kind) {
    case TouchEventKind::kDown:
      return DemoEventKind::kTouchDown;
    case TouchEventKind::kMove:
      return DemoEventKind::kTouchMove;
    case TouchEventKind::kUp:
      return DemoEventKind::kTouchUp;
  }
  return DemoEventKind::kTouchUp;
}

bool reached(std::uint32_t now_us, std::uint32_t target_us) {
  return static_cast<std::int32_t>(now_us - target_us) >= 0;
}

}  // namespace

void DemoTape::begin_recording(std::uint32_t started_us) {
  size_ = 0;
  replay_index_ = 0;
  recording_started_us_ = started_us;
  recording_ = true;
  replaying_ = false;
  overflowed_ = false;
}

bool DemoTape::record_touch(const TouchEvent& event) {
  if (!std::isfinite(event.point.x) || !std::isfinite(event.point.y)) {
    return false;
  }
  // The physical sampler supplies uint16 panel coordinates through Point's
  // float-shaped interface, so this compact conversion is exact on-device.
  return record({.x = coordinate(event.point.x),
                 .y = coordinate(event.point.y),
                 .kind = demo_kind(event.kind)},
                event.timestamp_us);
}

bool DemoTape::record_chrome_toggle(std::uint32_t timestamp_us) {
  return record({.kind = DemoEventKind::kChromeToggle}, timestamp_us);
}

bool DemoTape::record(DemoSample sample, std::uint32_t timestamp_us) {
  if (!recording_) {
    return false;
  }
  const std::uint32_t offset_us = timestamp_us - recording_started_us_;
  if (size_ == storage_.size() || offset_us > kMaximumDemoDurationUs) {
    overflowed_ = true;
    recording_ = false;
    return false;
  }
  sample.offset_us = offset_us;
  storage_[size_++] = sample;
  return true;
}

void DemoTape::stop_recording() { recording_ = false; }

bool DemoTape::begin_replay(std::uint32_t started_us) {
  recording_ = false;
  replay_index_ = 0;
  replay_started_us_ = started_us;
  replaying_ = size_ != 0U;
  return replaying_;
}

bool DemoTape::replay_due(std::uint32_t now_us) const {
  return replaying_ && replay_index_ < size_ &&
         reached(now_us, replay_started_us_ + storage_[replay_index_].offset_us);
}

std::optional<DemoEvent> DemoTape::pop_replay(std::uint32_t now_us) {
  if (!replay_due(now_us)) {
    return std::nullopt;
  }
  const DemoSample sample = storage_[replay_index_++];
  if (replay_index_ == size_) {
    replaying_ = false;
  }
  return DemoEvent{
      .point = {static_cast<float>(sample.x), static_cast<float>(sample.y)},
      .timestamp_us = replay_started_us_ + sample.offset_us,
      .kind = sample.kind,
  };
}

void DemoTape::stop_replay() {
  replaying_ = false;
  replay_index_ = 0;
}

std::optional<std::uint32_t> DemoTape::next_replay_offset_us() const {
  if (!replaying_ || replay_index_ >= size_) {
    return std::nullopt;
  }
  return storage_[replay_index_].offset_us;
}

std::span<const DemoSample> DemoTape::samples() const { return storage_.first(size_); }

std::optional<TouchEventKind> demo_touch_kind(DemoEventKind kind) {
  switch (kind) {
    case DemoEventKind::kTouchDown:
      return TouchEventKind::kDown;
    case DemoEventKind::kTouchMove:
      return TouchEventKind::kMove;
    case DemoEventKind::kTouchUp:
      return TouchEventKind::kUp;
    case DemoEventKind::kChromeToggle:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace tinydraw::vector_v2
