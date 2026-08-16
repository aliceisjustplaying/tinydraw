#ifndef TINYDRAW_VECTOR_V2_RERENDER_LEDGER_H
#define TINYDRAW_VECTOR_V2_RERENDER_LEDGER_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

// Why a 2x2-tile group was rendered. The déjà-vu ship gate cares about the
// difference between renders the document forced (damage), renders capacity
// forced (eviction), and renders nothing forced (stale revision on an
// unchanged group, or a same-revision unexplained repeat — both spatially
// unnecessary and previously invisible to revision-keyed accounting).
enum class RerenderCause : std::uint8_t {
  kColdMiss,        // first render of this group this session
  kExpectedDamage,  // the document changed under this group since its render
  kEviction,        // the group's content left the cache since its render
  kStaleRevision,   // revision advanced but this group was never damaged
  kUnexplained,     // same revision, no damage, no eviction: a rerender bug
};

// 8 bytes per group; the full five-zoom world is ~27.5 KiB.
struct RerenderLedgerEntry {
  std::uint32_t revision = 0;
  std::uint16_t renders = 0;
  std::uint8_t flags = 0;
  std::uint8_t last_cause = 0;
};

struct RerenderLedgerTotals {
  std::size_t renders = 0;
  std::size_t unique_groups = 0;
  std::size_t cold_miss = 0;
  std::size_t expected_damage = 0;
  std::size_t eviction = 0;
  std::size_t stale_revision = 0;
  std::size_t unexplained = 0;

  [[nodiscard]] double amplification() const {
    return unique_groups == 0U ? 0.0
                               : static_cast<double>(renders) / static_cast<double>(unique_groups);
  }
};

// One entry per 128x128-level-pixel group across every committed zoom.
[[nodiscard]] constexpr std::size_t rerender_ledger_entry_count() {
  constexpr int kPercents[] = {25, 50, 100, 200, 400};
  std::size_t total = 0;
  for (const int percent : kPercents) {
    const int width = (kWorldWidth * percent + 99) / 100;
    const int height = (kWorldHeight * percent + 99) / 100;
    total += static_cast<std::size_t>((width + 127) / 128) *
             static_cast<std::size_t>((height + 127) / 128);
  }
  return total;
}

inline constexpr std::size_t kRerenderLedgerEntryCount = rerender_ledger_entry_count();

// Spatial re-render truth: direct-indexed by (zoom, group), so no key is ever
// dropped. The canvas reports damage (finish_revision world bounds) and
// evictions (slot reuse); the producer reports completed group renders; the
// ledger classifies each render into exactly one cause. Caller-owned storage;
// single-threaded like the canvas it observes.
class RerenderLedger {
 public:
  explicit RerenderLedger(std::span<RerenderLedgerEntry> entries);

  [[nodiscard]] bool ready() const;

  // Marks every group intersecting the affected world bounds, at every zoom,
  // as legitimately damaged.
  void mark_world_damage(PixelRect world_bounds);
  // Marks the group containing this 64-px tile as having lost cached content.
  void mark_evicted(TileKey key);
  // Records one completed group render (origin in 64-px tile coordinates)
  // and returns its classified cause.
  RerenderCause record_group_render(ZoomLevel zoom, std::uint16_t origin_tile_column,
                                    std::uint16_t origin_tile_row, DocumentRevision revision);

  void reset();
  [[nodiscard]] RerenderLedgerTotals totals() const;

 private:
  [[nodiscard]] RerenderLedgerEntry* entry_at(ZoomLevel zoom, int group_column, int group_row);

  std::span<RerenderLedgerEntry> entries_;
  RerenderLedgerTotals totals_{};
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_RERENDER_LEDGER_H
