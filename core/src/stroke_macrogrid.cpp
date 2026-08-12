#include "tinydraw/document/stroke_macrogrid.h"

#include <algorithm>
#include <cmath>

namespace tinydraw {
namespace {

constexpr std::size_t kBitsPerWord = 64U;

}  // namespace

StrokeMacrogrid::StrokeMacrogrid(std::span<std::uint64_t> cell_words,
                                 std::span<std::uint64_t> query_words, std::size_t stroke_capacity,
                                 float world_x, float world_y)
    : cell_words_(cell_words),
      query_words_(query_words),
      stroke_capacity_(stroke_capacity),
      word_count_((stroke_capacity + kBitsPerWord - 1U) / kBitsPerWord),
      world_x_(world_x),
      world_y_(world_y) {}

bool StrokeMacrogrid::valid() const {
  return stroke_capacity_ != 0U && query_words_.size() >= word_count_ &&
         cell_words_.size() >= kCellCount * word_count_;
}

bool StrokeMacrogrid::rebuild(const VectorDocument& document) {
  if (!valid() || document.stroke_count() > stroke_capacity_) {
    return false;
  }
  std::fill_n(cell_words_.begin(), kCellCount * word_count_, 0U);
  indexed_strokes_ = 0U;
  for (const VectorStroke& stroke : document.strokes()) {
    if (!append(indexed_strokes_, stroke.bounds)) {
      return false;
    }
  }
  return true;
}

bool StrokeMacrogrid::append(std::size_t stroke_index, RectF bounds) {
  if (!valid() || stroke_index != indexed_strokes_ || stroke_index >= stroke_capacity_ ||
      !std::isfinite(bounds.x0) || !std::isfinite(bounds.y0) || !std::isfinite(bounds.x1) ||
      !std::isfinite(bounds.y1) || bounds.x0 > bounds.x1 || bounds.y0 > bounds.y1) {
    return false;
  }

  if (covers(bounds)) {
    const int first_x = std::clamp(static_cast<int>(std::floor((bounds.x0 - world_x_) / kCellSize)),
                                   0, kColumns - 1);
    const int first_y =
        std::clamp(static_cast<int>(std::floor((bounds.y0 - world_y_) / kCellSize)), 0, kRows - 1);
    const int last_x = std::clamp(static_cast<int>(std::floor((bounds.x1 - world_x_) / kCellSize)),
                                  0, kColumns - 1);
    const int last_y =
        std::clamp(static_cast<int>(std::floor((bounds.y1 - world_y_) / kCellSize)), 0, kRows - 1);
    const std::size_t word = stroke_index / kBitsPerWord;
    const std::uint64_t bit = std::uint64_t{1} << (stroke_index % kBitsPerWord);
    for (int y = first_y; y <= last_y; ++y) {
      for (int x = first_x; x <= last_x; ++x) {
        cell_words_[(static_cast<std::size_t>(y * kColumns + x) * word_count_) + word] |= bit;
      }
    }
  }
  ++indexed_strokes_;
  return true;
}

std::span<const std::uint64_t> StrokeMacrogrid::query(RectF bounds) {
  if (!valid()) {
    return {};
  }
  std::fill_n(query_words_.begin(), word_count_, 0U);
  if (!covers(bounds)) {
    set_all_indexed();
    return query_words_.first(word_count_);
  }

  const int first_x =
      std::clamp(static_cast<int>(std::floor((bounds.x0 - world_x_) / kCellSize)), 0, kColumns - 1);
  const int first_y =
      std::clamp(static_cast<int>(std::floor((bounds.y0 - world_y_) / kCellSize)), 0, kRows - 1);
  const int last_x =
      std::clamp(static_cast<int>(std::floor((bounds.x1 - world_x_) / kCellSize)), 0, kColumns - 1);
  const int last_y =
      std::clamp(static_cast<int>(std::floor((bounds.y1 - world_y_) / kCellSize)), 0, kRows - 1);
  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      const auto cell = cell_words_.subspan(
          static_cast<std::size_t>(y * kColumns + x) * word_count_, word_count_);
      for (std::size_t word = 0; word < word_count_; ++word) {
        query_words_[word] |= cell[word];
      }
    }
  }
  return query_words_.first(word_count_);
}

bool StrokeMacrogrid::covers(RectF bounds) const {
  return bounds.x0 >= world_x_ && bounds.y0 >= world_y_ &&
         bounds.x1 < world_x_ + static_cast<float>(kColumns * kCellSize) &&
         bounds.y1 < world_y_ + static_cast<float>(kRows * kCellSize);
}

void StrokeMacrogrid::set_all_indexed() {
  std::fill_n(query_words_.begin(), word_count_, ~std::uint64_t{0});
  if (indexed_strokes_ % kBitsPerWord != 0U) {
    query_words_[indexed_strokes_ / kBitsPerWord] =
        (std::uint64_t{1} << (indexed_strokes_ % kBitsPerWord)) - 1U;
  }
  for (std::size_t word = (indexed_strokes_ + kBitsPerWord - 1U) / kBitsPerWord; word < word_count_;
       ++word) {
    query_words_[word] = 0U;
  }
}

}  // namespace tinydraw
