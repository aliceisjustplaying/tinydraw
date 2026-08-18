#ifndef TINYDRAW_VECTOR_V2_OPERATION_SPATIAL_INDEX_H
#define TINYDRAW_VECTOR_V2_OPERATION_SPATIAL_INDEX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/operation.h"

namespace tinydraw::vector_v2 {

inline constexpr int kOperationSpatialCellSize = 128;
inline constexpr int kOperationSpatialColumns =
    (kWorldWidth + kOperationSpatialCellSize - 1) / kOperationSpatialCellSize;
inline constexpr int kOperationSpatialRows =
    (kWorldHeight + kOperationSpatialCellSize - 1) / kOperationSpatialCellSize;
inline constexpr std::size_t kOperationSpatialCellCount =
    static_cast<std::size_t>(kOperationSpatialColumns) * kOperationSpatialRows;
inline constexpr std::size_t kOperationSpatialLargeCellThreshold = 16;

[[nodiscard]] constexpr std::size_t operation_spatial_word_count(std::size_t operation_capacity) {
  return (operation_capacity + 63U) / 64U;
}

[[nodiscard]] constexpr std::size_t operation_spatial_cell_word_count(
    std::size_t operation_capacity) {
  return kOperationSpatialCellCount * operation_spatial_word_count(operation_capacity);
}

struct OperationSpatialQueryStats {
  std::size_t operations_in_authority = 0;
  std::size_t index_candidates = 0;
  std::size_t deduplicated_candidates = 0;
};

// Append-maintained acceleration metadata. The grid is never drawing
// authority: queries return conservative candidates and callers retain the
// active-prefix, epoch, exact-bounds, and raster gates. All storage is
// caller-owned and queries emit newest-first painter order without allocation.
class OperationSpatialIndex {
 public:
  OperationSpatialIndex(std::size_t operation_capacity, std::span<std::uint64_t> cell_bits,
                        std::span<std::uint64_t> large_bits);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::size_t operation_capacity() const { return operation_capacity_; }
  [[nodiscard]] std::size_t indexed_prefix_count() const { return indexed_prefix_count_; }
  [[nodiscard]] bool workspace_overlaps_storage(std::span<const std::byte> workspace) const;

  void clear();
  [[nodiscard]] bool replace(std::size_t operation_index, PixelRect world_bounds);
  [[nodiscard]] std::optional<std::size_t> query(PixelRect world_bounds,
                                                 std::size_t first_operation,
                                                 std::size_t operation_count,
                                                 std::span<std::uint16_t> newest_first_candidates,
                                                 OperationSpatialQueryStats* stats = nullptr) const;

 private:
  [[nodiscard]] std::span<std::uint64_t> cell_words(std::size_t cell);
  [[nodiscard]] std::span<const std::uint64_t> cell_words(std::size_t cell) const;
  void clear_operation(std::size_t operation_index);

  std::size_t operation_capacity_ = 0;
  std::size_t words_per_cell_ = 0;
  std::span<std::uint64_t> cell_bits_{};
  std::span<std::uint64_t> large_bits_{};
  // Internal hot metadata avoids touching PSRAM bitsets/candidate output when
  // a complete active prefix is provably too dense to save 25% of fetches.
  std::array<std::uint16_t, kOperationSpatialCellCount> cell_population_{};
  std::uint16_t large_population_ = 0;
  std::size_t indexed_prefix_count_ = 0;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_OPERATION_SPATIAL_INDEX_H
