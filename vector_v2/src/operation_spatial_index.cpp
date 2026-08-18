#include "tinydraw/vector_v2/operation_spatial_index.h"

#include <algorithm>
#include <bit>
#include <limits>

#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {
namespace {

struct CellRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
};

std::optional<CellRect> covered_cells(PixelRect bounds) {
  bounds.x0 = std::clamp(bounds.x0, 0, kWorldWidth);
  bounds.y0 = std::clamp(bounds.y0, 0, kWorldHeight);
  bounds.x1 = std::clamp(bounds.x1, 0, kWorldWidth);
  bounds.y1 = std::clamp(bounds.y1, 0, kWorldHeight);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return std::nullopt;
  }
  return CellRect{
      .x0 = bounds.x0 / kOperationSpatialCellSize,
      .y0 = bounds.y0 / kOperationSpatialCellSize,
      .x1 = (bounds.x1 - 1) / kOperationSpatialCellSize + 1,
      .y1 = (bounds.y1 - 1) / kOperationSpatialCellSize + 1,
  };
}

std::size_t cell_index(int x, int y) {
  return static_cast<std::size_t>(y) * kOperationSpatialColumns + static_cast<std::size_t>(x);
}

}  // namespace

OperationSpatialIndex::OperationSpatialIndex(std::size_t operation_capacity,
                                             std::span<std::uint64_t> cell_bits,
                                             std::span<std::uint64_t> large_bits)
    : operation_capacity_(operation_capacity),
      words_per_cell_(operation_spatial_word_count(operation_capacity)),
      cell_bits_(cell_bits),
      large_bits_(large_bits) {
  clear();
}

bool OperationSpatialIndex::ready() const {
  return operation_capacity_ != 0U &&
         operation_capacity_ <= std::numeric_limits<std::uint16_t>::max() &&
         words_per_cell_ != 0U &&
         cell_bits_.size() >= operation_spatial_cell_word_count(operation_capacity_) &&
         large_bits_.size() >= words_per_cell_ &&
         !storage_overlaps(std::as_bytes(cell_bits_), std::as_bytes(large_bits_));
}

bool OperationSpatialIndex::workspace_overlaps_storage(std::span<const std::byte> workspace) const {
  return storage_overlaps(workspace, std::as_bytes(cell_bits_)) ||
         storage_overlaps(workspace, std::as_bytes(large_bits_));
}

void OperationSpatialIndex::clear() {
  if (cell_bits_.size() >= operation_spatial_cell_word_count(operation_capacity_)) {
    std::fill_n(cell_bits_.begin(), operation_spatial_cell_word_count(operation_capacity_),
                std::uint64_t{0});
  }
  if (large_bits_.size() >= words_per_cell_) {
    std::fill_n(large_bits_.begin(), words_per_cell_, std::uint64_t{0});
  }
  cell_population_.fill(0U);
  large_population_ = 0U;
  indexed_prefix_count_ = 0U;
}

std::span<std::uint64_t> OperationSpatialIndex::cell_words(std::size_t cell) {
  return cell_bits_.subspan(cell * words_per_cell_, words_per_cell_);
}

std::span<const std::uint64_t> OperationSpatialIndex::cell_words(std::size_t cell) const {
  return cell_bits_.subspan(cell * words_per_cell_, words_per_cell_);
}

void OperationSpatialIndex::clear_operation(std::size_t operation_index) {
  const std::size_t word = operation_index / 64U;
  const std::uint64_t keep = ~(std::uint64_t{1} << (operation_index & 63U));
  for (std::size_t cell = 0; cell < kOperationSpatialCellCount; ++cell) {
    std::uint64_t& stored = cell_words(cell)[word];
    if ((stored & ~keep) != 0U) {
      stored &= keep;
      --cell_population_[cell];
    }
  }
  if ((large_bits_[word] & ~keep) != 0U) {
    large_bits_[word] &= keep;
    --large_population_;
  }
}

bool OperationSpatialIndex::replace(std::size_t operation_index, PixelRect world_bounds) {
  const auto cells = covered_cells(world_bounds);
  if (!ready() || operation_index >= operation_capacity_ || !cells.has_value() ||
      operation_index > indexed_prefix_count_) {
    return false;
  }
  if (operation_index < indexed_prefix_count_) {
    clear_operation(operation_index);
  }
  const std::size_t word = operation_index / 64U;
  const std::uint64_t bit = std::uint64_t{1} << (operation_index & 63U);
  const std::size_t cells_covered = static_cast<std::size_t>(cells->x1 - cells->x0) *
                                    static_cast<std::size_t>(cells->y1 - cells->y0);
  if (cells_covered > kOperationSpatialLargeCellThreshold) {
    large_bits_[word] |= bit;
    ++large_population_;
  } else {
    for (int y = cells->y0; y < cells->y1; ++y) {
      for (int x = cells->x0; x < cells->x1; ++x) {
        const std::size_t cell = cell_index(x, y);
        cell_words(cell)[word] |= bit;
        ++cell_population_[cell];
      }
    }
  }
  // Retain the high-water mark across branch replacement. Stale redo-tail
  // bits above the active prefix must still be cleared as replacement appends
  // advance through those indices.
  indexed_prefix_count_ = std::max(indexed_prefix_count_, operation_index + 1U);
  return true;
}

std::optional<std::size_t> OperationSpatialIndex::query(
    PixelRect world_bounds, std::size_t first_operation, std::size_t operation_count,
    std::span<std::uint16_t> newest_first_candidates, OperationSpatialQueryStats* stats) const {
  const auto cells = covered_cells(world_bounds);
  if (!ready() || !cells.has_value() || first_operation > indexed_prefix_count_ ||
      operation_count > indexed_prefix_count_ - first_operation ||
      operation_count > newest_first_candidates.size()) {
    return std::nullopt;
  }
  if (first_operation == 0U && operation_count == indexed_prefix_count_) {
    const std::size_t useful_candidate_limit = operation_count - operation_count / 4U;
    std::size_t unavoidable_candidates = large_population_;
    for (int y = cells->y0; y < cells->y1; ++y) {
      for (int x = cells->x0; x < cells->x1; ++x) {
        unavoidable_candidates =
            std::max(unavoidable_candidates, static_cast<std::size_t>(large_population_) +
                                                 cell_population_[cell_index(x, y)]);
      }
    }
    if (unavoidable_candidates > useful_candidate_limit) {
      return std::nullopt;
    }
  }
  OperationSpatialQueryStats measured{.operations_in_authority = operation_count};
  std::size_t written = 0U;
  if (operation_count == 0U) {
    if (stats != nullptr) {
      *stats = measured;
    }
    return written;
  }
  const std::size_t last_operation = first_operation + operation_count;
  const std::size_t first_word = first_operation / 64U;
  const std::size_t last_word = (last_operation - 1U) / 64U;
  for (std::size_t word = last_word + 1U; word-- > first_word;) {
    std::uint64_t range_mask = ~std::uint64_t{0};
    if (word == first_word) {
      range_mask &= ~std::uint64_t{0} << (first_operation & 63U);
    }
    if (word == last_word && (last_operation & 63U) != 0U) {
      range_mask &= (std::uint64_t{1} << (last_operation & 63U)) - 1U;
    }
    std::uint64_t merged = large_bits_[word] & range_mask;
    measured.index_candidates += static_cast<std::size_t>(std::popcount(merged));
    for (int y = cells->y0; y < cells->y1; ++y) {
      for (int x = cells->x0; x < cells->x1; ++x) {
        const std::uint64_t source = cell_words(cell_index(x, y))[word] & range_mask;
        measured.index_candidates += static_cast<std::size_t>(std::popcount(source));
        merged |= source;
      }
    }
    measured.deduplicated_candidates += static_cast<std::size_t>(std::popcount(merged));
    while (merged != 0U) {
      const unsigned bit = static_cast<unsigned>(std::bit_width(merged) - 1);
      newest_first_candidates[written++] =
          static_cast<std::uint16_t>(word * 64U + static_cast<std::size_t>(bit));
      merged &= ~(std::uint64_t{1} << bit);
    }
  }
  if (stats != nullptr) {
    *stats = measured;
  }
  return written;
}

}  // namespace tinydraw::vector_v2
