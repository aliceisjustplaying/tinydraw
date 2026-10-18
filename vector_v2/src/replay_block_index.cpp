#include "tinydraw/vector_v2/replay_block_index.h"

#include <algorithm>

namespace tinydraw::vector_v2 {
namespace {

bool valid_world_bounds(PixelRect bounds) {
  return bounds.x0 >= 0 && bounds.y0 >= 0 && bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kWorldWidth && bounds.y1 <= kWorldHeight;
}

}  // namespace

bool ReplayBlockIndex::ready() const { return words_.size() >= kReplayIndexWords; }

std::span<std::uint32_t> ReplayBlockIndex::cell_words(std::size_t cell) {
  return words_.subspan(cell * kReplayBlockWords, kReplayBlockWords);
}

std::span<const std::uint32_t> ReplayBlockIndex::cell_words(std::size_t cell) const {
  return words_.subspan(cell * kReplayBlockWords, kReplayBlockWords);
}

void ReplayBlockIndex::reset() {
  if (ready()) {
    std::fill_n(words_.begin(), kReplayIndexWords, std::uint32_t{0});
  }
  epoch_ = {};
  indexed_operations_ = 0;
}

void ReplayBlockIndex::index_operation(std::size_t operation_index, PixelRect bounds) {
  if (!valid_world_bounds(bounds) || operation_index >= kOperationCapacity) {
    return;
  }
  const std::size_t block = operation_index / kReplayOperationsPerBlock;
  const std::size_t word = block / 32U;
  const std::uint32_t bit = std::uint32_t{1} << (block % 32U);
  const int first_x = bounds.x0 / kReplayCellWorldSize;
  const int first_y = bounds.y0 / kReplayCellWorldSize;
  const int last_x = (bounds.x1 - 1) / kReplayCellWorldSize;
  const int last_y = (bounds.y1 - 1) / kReplayCellWorldSize;
  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      const auto cell = static_cast<std::size_t>(y * kReplayCellColumns + x);
      cell_words(cell)[word] |= bit;
    }
  }
}

bool ReplayBlockIndex::sync(const OperationLog& log) {
  if (!ready() || !log.ready() || log.operation_count() > kOperationCapacity) {
    return false;
  }
  if (epoch_ != log.epoch() || indexed_operations_ > log.operation_count()) {
    reset();
    epoch_ = log.epoch();
  }
  while (indexed_operations_ < log.operation_count()) {
    const auto operation = log.operation(indexed_operations_);
    if (!operation.has_value()) {
      return false;
    }
    index_operation(indexed_operations_, operation->world_bounds);
    ++indexed_operations_;
  }
  return true;
}

ReplayCandidateCursor ReplayBlockIndex::query(PixelRect bounds, std::size_t first_operation,
                                              std::size_t operation_count) const {
  ReplayCandidateCursor cursor{
      .first_operation = first_operation,
      .next_operation = first_operation + operation_count,
  };
  if (operation_count == 0U) {
    return cursor;
  }
  if (!ready()) {
    const std::size_t first_block = first_operation / kReplayOperationsPerBlock;
    const std::size_t last_block =
        (first_operation + operation_count - 1U) / kReplayOperationsPerBlock;
    for (std::size_t block = first_block; block <= last_block; ++block) {
      cursor.candidate_blocks[block / 32U] |= std::uint32_t{1} << (block % 32U);
    }
    return cursor;
  }
  if (!valid_world_bounds(bounds)) {
    return cursor;
  }
  const int first_x = bounds.x0 / kReplayCellWorldSize;
  const int first_y = bounds.y0 / kReplayCellWorldSize;
  const int last_x = (bounds.x1 - 1) / kReplayCellWorldSize;
  const int last_y = (bounds.y1 - 1) / kReplayCellWorldSize;
  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      const auto source = cell_words(static_cast<std::size_t>(y * kReplayCellColumns + x));
      for (std::size_t word = 0; word < kReplayBlockWords; ++word) {
        cursor.candidate_blocks[word] |= source[word];
      }
    }
  }
  return cursor;
}

std::optional<std::size_t> ReplayBlockIndex::previous(ReplayCandidateCursor& cursor) {
  while (cursor.next_operation > cursor.first_operation) {
    const std::size_t candidate = cursor.next_operation - 1U;
    const std::size_t block = candidate / kReplayOperationsPerBlock;
    const std::uint32_t mask = std::uint32_t{1} << (block % 32U);
    if ((cursor.candidate_blocks[block / 32U] & mask) != 0U) {
      cursor.next_operation = candidate;
      return candidate;
    }
    cursor.next_operation = std::max(cursor.first_operation, block * kReplayOperationsPerBlock);
  }
  return std::nullopt;
}

}  // namespace tinydraw::vector_v2
