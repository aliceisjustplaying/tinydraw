#ifndef TINYDRAW_PRODUCTION_DISPLAY_SCHEDULER_H
#define TINYDRAW_PRODUCTION_DISPLAY_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/materialized_canvas.h"

namespace tinydraw::production {

struct DisplayStrip {
  DocumentRevision revision{};
  PixelRect panel_bounds{};
  std::span<const std::uint16_t> pixels{};
  int stride = 0;
};

struct ScheduledStrip {
  std::uint32_t sequence = 0;
  DisplayStrip strip{};
};

struct DisplaySchedulerStats {
  std::uint32_t accepted = 0;
  std::uint32_t completed = 0;
  std::uint32_t rejected = 0;
  std::uint32_t stale_rejected = 0;
  std::size_t queued = 0;
};

// Allocation-free single-owner scheduling state. Submission and completion are
// deliberately separate from panel transport; the adapter stages pixels before
// calling complete. Queue entries reference caller-owned immutable pixels that
// must remain valid until completion. Callers must serialize every method.
class DisplayScheduler {
 public:
  explicit DisplayScheduler(std::span<DisplayStrip> queue_storage);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::optional<std::uint32_t> schedule(const DisplayStrip& strip);
  // front marks the returned strip in flight until complete succeeds.
  [[nodiscard]] std::optional<ScheduledStrip> front();
  [[nodiscard]] bool complete(std::uint32_t sequence);
  // Retires an in-flight strip that transport could not stage.
  [[nodiscard]] bool abort(std::uint32_t sequence);
  void require_revision(DocumentRevision revision);
  [[nodiscard]] DisplaySchedulerStats stats() const;

 private:
  void drop_stale_front();
  [[nodiscard]] bool valid_strip(const DisplayStrip& strip) const;

  std::span<DisplayStrip> queue_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
  std::uint32_t next_sequence_ = 1;
  std::uint32_t front_sequence_ = 0;
  std::uint32_t in_flight_sequence_ = 0;
  DocumentRevision required_revision_{};
  std::uint32_t accepted_ = 0;
  std::uint32_t completed_ = 0;
  std::uint32_t rejected_ = 0;
  std::uint32_t stale_rejected_ = 0;
};

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_DISPLAY_SCHEDULER_H
