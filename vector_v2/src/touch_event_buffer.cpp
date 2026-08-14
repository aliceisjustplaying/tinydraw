#include "tinydraw/vector_v2/touch_event_buffer.h"

#include <algorithm>

namespace tinydraw::vector_v2 {
namespace {

constexpr std::size_t kMinimumEventCapacity = 4U;
constexpr std::uint8_t kLiftConfirmationReads = 2U;

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
    last_point_ = point;
    no_touch_reads_ = 0U;
    const TouchEventKind kind = touching_ ? TouchEventKind::kMove : TouchEventKind::kDown;
    const TouchOfferResult result = enqueue({point, timestamp_us, sequence, kind});
    if (kind == TouchEventKind::kDown && result != TouchOfferResult::kOverflow) {
      touching_ = true;
    }
    return result;
  }
  if (!touching_) {
    return TouchOfferResult::kIgnored;
  }
  no_touch_reads_ = std::min<std::uint8_t>(static_cast<std::uint8_t>(no_touch_reads_ + 1U),
                                           kLiftConfirmationReads);
  if (no_touch_reads_ < kLiftConfirmationReads) {
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

TouchOfferResult TouchEventBuffer::enqueue(const TouchEvent& event) {
  if (event.kind == TouchEventKind::kMove && size_ != 0U) {
    const std::size_t newest = physical_index(size_ - 1U);
    if (storage_[newest].kind == TouchEventKind::kMove) {
      storage_[newest] = event;
      return TouchOfferResult::kMoveCoalesced;
    }
  }
  if (size_ == storage_.size() && !remove_oldest_move()) {
    return TouchOfferResult::kOverflow;
  }
  storage_[physical_index(size_)] = event;
  ++size_;
  return TouchOfferResult::kQueued;
}

}  // namespace tinydraw::vector_v2
