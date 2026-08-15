#ifndef TINYDRAW_VECTOR_V2_RENDER_ACCOUNTING_H
#define TINYDRAW_VECTOR_V2_RENDER_ACCOUNTING_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

struct RenderGroupKey {
  DocumentRevision revision{};
  ZoomLevel zoom = ZoomLevel::k50Percent;
  std::uint16_t group_column = 0;
  std::uint16_t group_row = 0;
  bool operator==(const RenderGroupKey&) const = default;
};

struct RenderAccountingEntry {
  RenderGroupKey key{};
  std::size_t attempts = 0;
  std::size_t completions = 0;
  std::size_t reuses = 0;
  std::size_t discards = 0;
};

struct RenderAccountingTotals {
  std::size_t unique_groups = 0;
  std::size_t attempts = 0;
  std::size_t completions = 0;
  std::size_t reuses = 0;
  std::size_t discards = 0;
  std::size_t dropped_keys = 0;

  [[nodiscard]] double amplification() const {
    return unique_groups == 0U ? 0.0
                               : static_cast<double>(attempts) / static_cast<double>(unique_groups);
  }
};

// Caller-funded durable counters keyed by revision, zoom, and 2x2 tile group.
// Reuse only applies to a previously observed render key, so certainly-paper
// publication cannot dilute render amplification.
class RenderAccounting {
 public:
  explicit RenderAccounting(std::span<RenderAccountingEntry> entries) : entries_(entries) {}

  void record_attempt(RenderGroupKey key);
  void record_completion(RenderGroupKey key);
  void record_reuse(RenderGroupKey key);
  void record_discard(RenderGroupKey key);
  void reset();

  [[nodiscard]] std::span<const RenderAccountingEntry> entries() const;
  [[nodiscard]] RenderAccountingTotals totals() const;

 private:
  [[nodiscard]] RenderAccountingEntry* find(RenderGroupKey key);
  [[nodiscard]] RenderAccountingEntry* find_or_insert(RenderGroupKey key);

  std::span<RenderAccountingEntry> entries_{};
  std::size_t used_ = 0;
  std::size_t dropped_keys_ = 0;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_RENDER_ACCOUNTING_H
