#include "tinydraw/vector_v2/touch_event_buffer.h"

#include <algorithm>

namespace tinydraw::vector_v2 {
namespace {

constexpr std::size_t kMinimumEventCapacity = 4U;

}  // namespace

TouchEventBuffer::TouchEventBuffer(std::span<TouchEvent> storage) : storage_(storage) {}

bool TouchEventBuffer::ready() const { return storage_.size() >= kMinimumEventCapacity; }

TouchOfferResult TouchEventBuffer::offer(TouchContactRead read, TouchContactPoint point,
                                         std::uint32_t timestamp_us) {
  const std::uint32_t sequence = ++sequence_;
  if (!ready()) {
    return TouchOfferResult::kOverflow;
  }
  if (read == TouchContactRead::kError) {
    return TouchOfferResult::kErrorHeld;
  }
  if (read == TouchContactRead::kPoint) {
    no_touch_reads_ = 0U;
    const TouchEventKind kind = touching_ ? TouchEventKind::kMove : TouchEventKind::kDown;
    const TouchEvent event{point, timestamp_us, sequence, kind};
    const TouchOfferResult result = enqueue(event);
    last_point_ = point;
    if (kind == TouchEventKind::kDown && result != TouchOfferResult::kOverflow) {
      touching_ = true;
      active_down_ = storage_[physical_index(size_ - 1U)];
    }
    return result;
  }
  if (!touching_) {
    return TouchOfferResult::kIgnored;
  }
  no_touch_reads_ = std::min<std::uint8_t>(static_cast<std::uint8_t>(no_touch_reads_ + 1U),
                                           kTouchLiftConfirmationReads);
  if (no_touch_reads_ < kTouchLiftConfirmationReads) {
    return TouchOfferResult::kIgnored;
  }
  const TouchOfferResult result =
      enqueue({last_point_, timestamp_us, sequence, TouchEventKind::kUp});
  if (result != TouchOfferResult::kOverflow) {
    touching_ = false;
    no_touch_reads_ = 0U;
  }
  return result;
}

std::optional<TouchEvent> TouchEventBuffer::pop() {
  if (size_ == 0U) {
    return std::nullopt;
  }
  const TouchEvent event = storage_[head_];
  head_ = (head_ + 1U) % storage_.size();
  --size_;
  if (event.kind == TouchEventKind::kDown) {
    consumer_touching_ = true;
  } else if (event.kind == TouchEventKind::kUp) {
    consumer_touching_ = false;
  }
  return event;
}

std::size_t TouchEventBuffer::pending() const { return size_; }

std::size_t TouchEventBuffer::physical_index(std::size_t logical_index) const {
  return (head_ + logical_index) % storage_.size();
}

bool TouchEventBuffer::remove_oldest_move() {
  for (std::size_t logical = 0; logical < size_; ++logical) {
    if (storage_[physical_index(logical)].kind != TouchEventKind::kMove) {
      continue;
    }
    for (std::size_t shifted = logical; shifted + 1U < size_; ++shifted) {
      storage_[physical_index(shifted)] = storage_[physical_index(shifted + 1U)];
    }
    --size_;
    return true;
  }
  return false;
}

void TouchEventBuffer::append_unchecked(const TouchEvent& event) {
  storage_[physical_index(size_)] = event;
  ++size_;
}

void TouchEventBuffer::resynchronize(const TouchEvent& event) {
  head_ = 0U;
  size_ = 0U;
  if (event.kind == TouchEventKind::kDown) {
    if (consumer_touching_) {
      append_unchecked({last_point_, event.timestamp_us, event.sequence, TouchEventKind::kUp});
      TouchEvent down = event;
      down.sequence = ++sequence_;
      append_unchecked(down);
      return;
    }
    append_unchecked(event);
    return;
  }

  if (!consumer_touching_) {
    append_unchecked(active_down_);
  }
  append_unchecked(event);
}

TouchOfferResult TouchEventBuffer::enqueue(const TouchEvent& event) {
  if (event.kind == TouchEventKind::kMove && size_ != 0U) {
    const std::size_t newest = physical_index(size_ - 1U);
    if (storage_[newest].kind == TouchEventKind::kMove) {
      storage_[newest] = event;
      return TouchOfferResult::kMoveCoalesced;
    }
  }
  if (size_ == storage_.size() && !remove_oldest_move()) {
    if (event.kind == TouchEventKind::kMove) {
      return TouchOfferResult::kOverflow;
    }
    resynchronize(event);
    return TouchOfferResult::kResynchronized;
  }
  append_unchecked(event);
  return TouchOfferResult::kQueued;
}

}  // namespace tinydraw::vector_v2
