#ifndef TINYDRAW_VECTOR_V2_REPLAY_BLOCK_INDEX_H
#define TINYDRAW_VECTOR_V2_REPLAY_BLOCK_INDEX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

inline constexpr int kReplayCellWorldSize = 128;
inline constexpr int kReplayCellColumns =
    (kWorldWidth + kReplayCellWorldSize - 1) / kReplayCellWorldSize;
inline constexpr int kReplayCellRows =
    (kWorldHeight + kReplayCellWorldSize - 1) / kReplayCellWorldSize;
inline constexpr std::size_t kReplayCellCount =
    static_cast<std::size_t>(kReplayCellColumns) * kReplayCellRows;
inline constexpr std::size_t kReplayOperationsPerBlock = 16;
inline constexpr std::size_t kReplayBlockCount =
    (kOperationCapacity + kReplayOperationsPerBlock - 1U) / kReplayOperationsPerBlock;
inline constexpr std::size_t kReplayBlockWords = (kReplayBlockCount + 31U) / 32U;
inline constexpr std::size_t kReplayIndexWords = kReplayCellCount * kReplayBlockWords;
inline constexpr std::size_t kReplayIndexBytes = kReplayIndexWords * sizeof(std::uint32_t);

struct ReplayCandidateCursor {
  std::array<std::uint32_t, kReplayBlockWords> candidate_blocks{};
  std::size_t first_operation = 0;
  std::size_t next_operation = 0;
};

// Fixed-size conservative spatial index. Updates only append bits while an
// OperationLog epoch grows. A reset/truncation changes the epoch and rebuilds
// the caller-funded words. Query ranges are explicit so a future undo cursor
// can mask candidates to its active operation prefix without rebuilding.
class ReplayBlockIndex {
 public:
  explicit ReplayBlockIndex(std::span<std::uint32_t> cell_block_words = {})
      : words_(cell_block_words) {}

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool sync(const OperationLog& log);
  [[nodiscard]] ReplayCandidateCursor query(PixelRect world_bounds, std::size_t first_operation,
                                            std::size_t operation_count) const;
  [[nodiscard]] static std::optional<std::size_t> previous(ReplayCandidateCursor& cursor);
  void reset();

 private:
  void index_operation(std::size_t operation_index, PixelRect bounds);
  [[nodiscard]] std::span<std::uint32_t> cell_words(std::size_t cell);
  [[nodiscard]] std::span<const std::uint32_t> cell_words(std::size_t cell) const;

  std::span<std::uint32_t> words_{};
  OperationLogEpoch epoch_{};
  std::size_t indexed_operations_ = 0;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_REPLAY_BLOCK_INDEX_H
