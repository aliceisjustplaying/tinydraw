#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"

namespace tinydraw {

// Fixed-memory broad-phase index for bounded vector documents. Each cell owns
// a bitset of document-order stroke indices, so queries preserve painter order.
class StrokeMacrogrid {
 public:
  static constexpr int kColumns = 16;
  static constexpr int kRows = 16;
  static constexpr int kCellSize = 256;
  static constexpr std::size_t kCellCount = kColumns * kRows;

  StrokeMacrogrid(std::span<std::uint64_t> cell_words, std::span<std::uint64_t> query_words,
                  std::size_t stroke_capacity, float world_x = 0.0F, float world_y = 0.0F);

  [[nodiscard]] bool valid() const;
  [[nodiscard]] bool rebuild(const VectorDocument& document);
  [[nodiscard]] bool append(std::size_t stroke_index, RectF bounds);

  // Returns a sequence-preserving candidate bitset. A set bit means the
  // corresponding document stroke may intersect the query. If the query
  // extends outside the indexed world, all strokes are returned conservatively.
  [[nodiscard]] std::span<const std::uint64_t> query(RectF bounds);
  [[nodiscard]] std::size_t indexed_strokes() const { return indexed_strokes_; }
  [[nodiscard]] std::size_t word_count() const { return word_count_; }

 private:
  [[nodiscard]] bool covers(RectF bounds) const;
  void set_all_indexed();

  std::span<std::uint64_t> cell_words_;
  std::span<std::uint64_t> query_words_;
  std::size_t stroke_capacity_ = 0;
  std::size_t word_count_ = 0;
  std::size_t indexed_strokes_ = 0;
  float world_x_ = 0.0F;
  float world_y_ = 0.0F;
};

}  // namespace tinydraw
