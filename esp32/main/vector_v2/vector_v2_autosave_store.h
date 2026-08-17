#pragma once

#include <cstdint>

#include "tinydraw/vector_v2/authority_journal.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::esp32 {

enum class VectorV2AutosaveRestoreStatus : std::uint8_t {
  kUnavailable,
  kBlank,
  kRestored,
  kRecoveredTail,
  kError,
};

enum class VectorV2AutosaveStatus : std::uint8_t {
  kIdle,
  kSaving,
  kNeedsCheckpoint,
  kFull,
  kError,
};

// Asynchronous ESP flash adapter for the portable Vector V2 authority journal.
// submit() copies one coherent transaction; a low-priority worker owns every
// erase, write, and readback after that copy. Transactions start and end on
// 4 KiB boundaries so an interrupted tail can be erased without touching the
// previous Recovery point.
class VectorV2AutosaveStore {
 public:
  VectorV2AutosaveStore();
  ~VectorV2AutosaveStore();

  VectorV2AutosaveStore(const VectorV2AutosaveStore&) = delete;
  VectorV2AutosaveStore& operator=(const VectorV2AutosaveStore&) = delete;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] VectorV2AutosaveRestoreStatus restore(
      vector_v2::OperationLog& log, vector_v2::JournalState& state);
  [[nodiscard]] bool submit(vector_v2::JournalChange change,
                            const vector_v2::OperationLog& log,
                            const vector_v2::JournalState& state);
  [[nodiscard]] bool submit_checkpoint(const vector_v2::OperationLog& log,
                                       const vector_v2::JournalState& state);
  [[nodiscard]] bool checkpoint_required() const;
  [[nodiscard]] bool flush(std::uint32_t timeout_ms);
  [[nodiscard]] VectorV2AutosaveStatus status() const;
  [[nodiscard]] std::uint64_t committed_sequence() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace tinydraw::esp32
