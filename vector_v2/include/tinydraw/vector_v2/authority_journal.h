#ifndef TINYDRAW_VECTOR_V2_AUTHORITY_JOURNAL_H
#define TINYDRAW_VECTOR_V2_AUTHORITY_JOURNAL_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

enum class JournalChangeKind : std::uint16_t {
  kCheckpoint = 1,
  kAppendStroke = 2,
  kUpdate = 3,
};

struct JournalChange {
  JournalChangeKind kind = JournalChangeKind::kUpdate;
  // AppendStroke serializes [first_operation, active prefix). It declares
  // first_operation as the branch point, replacing any retained Redo tail.
  std::size_t first_operation = 0;
};

// Immutable description prepared once from serialized drawing authority and
// reused for allocation and encoding.
struct AuthorityJournalPlan {
  JournalChange change{};
  AuthorityReadView authority{};
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
  std::size_t payload_bytes = 0;
  std::size_t encoded_bytes = 0;
};

enum class AuthorityJournalStageResult : std::uint8_t {
  kProgress,
  kComplete,
  kStale,
  kError,
};

// Resumable staging state for one immutable transaction. resume() bounds
// operation metadata and sample copying even within one maximum-sized
// operation, and rejects any authority-view change before reading more bytes.
class AuthorityJournalStager {
 public:
  [[nodiscard]] bool start(const AuthorityJournalPlan& plan, const OperationLog& log,
                           std::uint64_t sequence, std::span<std::byte> output);
  [[nodiscard]] AuthorityJournalStageResult resume(const OperationLog& log,
                                                   std::size_t maximum_payload_bytes);
  void reset();

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool complete() const { return complete_; }

 private:
  [[nodiscard]] AuthorityJournalStageResult resume_operation(const StoredOperation& operation,
                                                             std::span<std::byte> payload,
                                                             std::size_t maximum_payload_bytes,
                                                             std::size_t& spent);

  AuthorityJournalPlan plan_{};
  std::span<std::byte> output_{};
  std::size_t operation_offset_ = 0;
  std::size_t sample_offset_ = 0;
  std::size_t payload_position_ = 0;
  bool operation_header_staged_ = false;
  bool active_ = false;
  bool complete_ = false;
};

class AuthorityJournalSource {
 public:
  virtual ~AuthorityJournalSource() = default;
  [[nodiscard]] virtual bool read(std::size_t offset, std::span<std::byte> output) const = 0;
};

enum class JournalRecoveryStatus : std::uint8_t {
  kEmpty,
  kRecovered,
  kCorrupt,
  kIoError,
  kInsufficientStorage,
};

struct JournalRecovery {
  JournalRecoveryStatus status = JournalRecoveryStatus::kEmpty;
  AuthorityReadView state{};
  std::size_t bytes_consumed = 0;
  std::size_t transaction_count = 0;
  std::uint64_t sequence = 0;
  bool discarded_tail = false;
};

// Prepares one immutable transaction. Null means the requested change does
// not describe the log's current serialized state.
[[nodiscard]] std::optional<AuthorityJournalPlan> prepare_authority_journal(
    JournalChange change, const OperationLog& log);

// Stages immutable header and payload bytes without spending the transaction
// CRC on the caller. Ownership of output must transfer to one serialized
// worker before seal_authority_journal() mutates its padding and marker.
[[nodiscard]] bool stage_authority_journal(const AuthorityJournalPlan& plan,
                                           const OperationLog& log, std::uint64_t sequence,
                                           std::span<std::byte> output);

// Completes one staged transaction without consulting live authority.
[[nodiscard]] bool seal_authority_journal(std::span<std::byte> output);

// Stages and seals synchronously. The final commit marker is part of the
// returned bytes; flash adapters still write that marker last.
[[nodiscard]] bool encode_authority_journal(const AuthorityJournalPlan& plan,
                                            const OperationLog& log, std::uint64_t sequence,
                                            std::span<std::byte> output);

// Scans consecutive transactions and returns the newest complete recovery
// point. records/samples are caller-owned restoration storage. A corrupt or
// incomplete tail after at least one valid transaction is discarded without
// replacing that prior recovery point.
[[nodiscard]] JournalRecovery recover_authority_journal(const AuthorityJournalSource& source,
                                                        std::size_t bytes,
                                                        std::span<OperationRecord> records,
                                                        std::span<CompactOperationSample> samples);

inline constexpr std::size_t kAuthorityJournalHeaderBytes = 176U;
inline constexpr std::size_t kAuthorityJournalCommitMarkerBytes = 16U;
inline constexpr std::size_t kAuthorityJournalMaximumOperationBytes =
    16U + static_cast<std::size_t>(UINT16_MAX) * sizeof(CompactOperationSample);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_AUTHORITY_JOURNAL_H
