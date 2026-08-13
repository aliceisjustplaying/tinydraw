#ifndef TINYDRAW_PRODUCTION_STORAGE_OVERLAP_H
#define TINYDRAW_PRODUCTION_STORAGE_OVERLAP_H

#include <cstddef>
#include <functional>
#include <span>

namespace tinydraw::production {

inline bool storage_overlaps(std::span<const std::byte> left, std::span<const std::byte> right) {
  if (left.empty() || right.empty()) {
    return false;
  }
  const auto* left_end = left.data() + left.size();
  const auto* right_end = right.data() + right.size();
  const std::less<const std::byte*> less;
  return less(left.data(), right_end) && less(right.data(), left_end);
}

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_STORAGE_OVERLAP_H
