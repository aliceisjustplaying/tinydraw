#include "tinydraw/vector_v2/display_scheduler.h"

#include <limits>

namespace tinydraw::vector_v2 {
DisplayScheduler::DisplayScheduler(std::span<DisplayStrip> queue_storage) : queue_(queue_storage) {}

bool DisplayScheduler::ready() const { return !queue_.empty(); }

std::optional<std::uint32_t> DisplayScheduler::schedule(const DisplayStrip& strip) {
  if (!valid_strip(strip) || count_ == queue_.size()) {
    ++rejected_;
    if (strip.revision != required_revision_) {
      ++stale_rejected_;
    }
    return std::nullopt;
  }
  const std::size_t tail = (head_ + count_) % queue_.size();
  queue_[tail] = strip;
  const std::uint32_t sequence = next_sequence_++;
  if (next_sequence_ == 0U) {
    next_sequence_ = 1U;
  }
  if (count_ == 0U) {
    front_sequence_ = sequence;
  }
  ++count_;
  ++accepted_;
  return sequence;
}

std::optional<ScheduledStrip> DisplayScheduler::front() {
  if (count_ == 0U || in_flight_sequence_ != 0U) {
    return std::nullopt;
  }
  in_flight_sequence_ = front_sequence_;
  return ScheduledStrip{.sequence = front_sequence_, .strip = queue_[head_]};
}

bool DisplayScheduler::complete(std::uint32_t sequence) {
  if (count_ == 0U || sequence != front_sequence_ || sequence != in_flight_sequence_) {
    return false;
  }
  head_ = (head_ + 1U) % queue_.size();
  --count_;
  ++completed_;
  in_flight_sequence_ = 0U;
  front_sequence_ = count_ == 0U ? 0U : sequence + 1U;
  if (front_sequence_ == 0U && count_ != 0U) {
    front_sequence_ = 1U;
  }
  drop_stale_front();
  return true;
}

bool DisplayScheduler::abort(std::uint32_t sequence) {
  if (count_ == 0U || sequence != front_sequence_ || sequence != in_flight_sequence_) {
    return false;
  }
  head_ = (head_ + 1U) % queue_.size();
  --count_;
  ++rejected_;
  in_flight_sequence_ = 0U;
  front_sequence_ = count_ == 0U ? 0U : sequence + 1U;
  if (front_sequence_ == 0U && count_ != 0U) {
    front_sequence_ = 1U;
  }
  drop_stale_front();
  return true;
}

void DisplayScheduler::require_revision(DocumentRevision revision) {
  required_revision_ = revision;
  drop_stale_front();
}

void DisplayScheduler::drop_stale_front() {
  while (count_ != 0U && in_flight_sequence_ == 0U &&
         queue_[head_].revision != required_revision_) {
    head_ = (head_ + 1U) % queue_.size();
    --count_;
    ++rejected_;
    ++stale_rejected_;
    front_sequence_ = count_ == 0U ? 0U : front_sequence_ + 1U;
    if (front_sequence_ == 0U && count_ != 0U) {
      front_sequence_ = 1U;
    }
  }
}

DisplaySchedulerStats DisplayScheduler::stats() const {
  return {.accepted = accepted_,
          .completed = completed_,
          .rejected = rejected_,
          .stale_rejected = stale_rejected_,
          .queued = count_};
}

bool DisplayScheduler::valid_strip(const DisplayStrip& strip) const {
  const int width = strip.panel_bounds.x1 - strip.panel_bounds.x0;
  const int height = strip.panel_bounds.y1 - strip.panel_bounds.y0;
  const std::size_t required =
      height > 0 && strip.stride > 0
          ? static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(strip.stride) +
                static_cast<std::size_t>(width)
          : 0U;
  return ready() && strip.revision == required_revision_ && width > 0 && height > 0 &&
         strip.panel_bounds.x0 >= 0 && strip.panel_bounds.y0 >= 0 &&
         strip.panel_bounds.x1 <= kOverviewWidth && strip.panel_bounds.y1 <= kOverviewHeight &&
         ((strip.panel_bounds.x0 | strip.panel_bounds.y0 | width | height) & 1) == 0 &&
         strip.stride >= width && strip.pixels.size() >= required;
}

}  // namespace tinydraw::vector_v2
