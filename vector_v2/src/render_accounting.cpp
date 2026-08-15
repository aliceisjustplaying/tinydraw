#include "tinydraw/vector_v2/render_accounting.h"

#include <algorithm>

namespace tinydraw::vector_v2 {

RenderAccountingEntry* RenderAccounting::find(RenderGroupKey key) {
  const auto active = entries_.first(used_);
  const auto found = std::find_if(active.begin(), active.end(),
                                  [key](const auto& entry) { return entry.key == key; });
  return found == active.end() ? nullptr : &*found;
}

RenderAccountingEntry* RenderAccounting::find_or_insert(RenderGroupKey key) {
  if (auto* entry = find(key); entry != nullptr) {
    return entry;
  }
  if (used_ == entries_.size()) {
    ++dropped_keys_;
    return nullptr;
  }
  entries_[used_] = {.key = key};
  return &entries_[used_++];
}

void RenderAccounting::record_attempt(RenderGroupKey key) {
  if (auto* entry = find_or_insert(key); entry != nullptr) {
    ++entry->attempts;
  }
}

void RenderAccounting::record_completion(RenderGroupKey key) {
  if (auto* entry = find_or_insert(key); entry != nullptr) {
    ++entry->completions;
  }
}

void RenderAccounting::record_reuse(RenderGroupKey key) {
  if (auto* entry = find(key); entry != nullptr) {
    ++entry->reuses;
  }
}

void RenderAccounting::record_discard(RenderGroupKey key) {
  if (auto* entry = find_or_insert(key); entry != nullptr) {
    ++entry->discards;
  }
}

void RenderAccounting::reset() {
  std::fill(entries_.begin(), entries_.end(), RenderAccountingEntry{});
  used_ = 0;
  dropped_keys_ = 0;
}

std::span<const RenderAccountingEntry> RenderAccounting::entries() const {
  return entries_.first(used_);
}

RenderAccountingTotals RenderAccounting::totals() const {
  RenderAccountingTotals result{.unique_groups = used_, .dropped_keys = dropped_keys_};
  for (const auto& entry : entries()) {
    result.attempts += entry.attempts;
    result.completions += entry.completions;
    result.reuses += entry.reuses;
    result.discards += entry.discards;
  }
  return result;
}

}  // namespace tinydraw::vector_v2
