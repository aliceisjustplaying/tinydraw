#ifndef TINYDRAW_VECTOR_V2_AUTHORITY_JOURNAL_H
#define TINYDRAW_VECTOR_V2_AUTHORITY_JOURNAL_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

enum class JournalChangeKind : std::uint16_t {
  kCheckpoint = 1,
  kAppendStroke = 2,
  kHistory = 3,
  kReset = 4,
  kState = 5,
};

struct JournalChange {
  JournalChangeKind kind = JournalChangeKind::kState;
  // AppendStroke serializes [first_operation, active prefix). It declares
  // first_operation as the branch point, replacing any retained Redo tail.
  std::size_t first_operation = 0;
};

struct JournalState {
  NavigationSnapshot navigation{};
  ChromeTool tool = ChromeTool::kDraw;
  ChromeSize size = ChromeSize::kLarge;
  std::uint8_t palette_page = 0;
  std::uint8_t color_index = 12;
  std::uint16_t next_stroke_id = 1;
  bool operator==(const JournalState&) const = default;
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
  std::size_t retained_sample_count = 0;
  std::size_t bytes_consumed = 0;
  std::size_t transaction_count = 0;
  std::uint64_t sequence = 0;
  bool discarded_tail = false;
};

// Exact byte count for one immutable journal transaction. Null means the
// requested change does not describe the log's current coherent state.
[[nodiscard]] std::optional<std::size_t> authority_journal_encoded_size(
    JournalChange change, const OperationLog& log);

// Encodes one generation-checked transaction. output must be exactly the size
// returned above. The final commit marker is part of the returned bytes; flash
// adapters write that marker last.
[[nodiscard]] bool encode_authority_journal(JournalChange change, const OperationLog& log,
                                            const JournalState& state, std::uint64_t sequence,
                                            std::span<std::byte> output);

// Scans consecutive transactions and returns the newest complete recovery
// point. records/samples are caller-owned restoration storage. A corrupt or
// incomplete tail after at least one valid transaction is discarded without
// replacing that prior recovery point.
[[nodiscard]] JournalRecovery recover_authority_journal(
    const AuthorityJournalSource& source, std::size_t bytes,
    std::span<OperationRecord> records, std::span<CompactOperationSample> samples,
    JournalState& state);

inline constexpr std::size_t kAuthorityJournalHeaderBytes = 176U;
inline constexpr std::size_t kAuthorityJournalCommitMarkerBytes = 16U;
inline constexpr std::size_t kAuthorityJournalMaximumOperationBytes =
    16U + static_cast<std::size_t>(UINT16_MAX) * sizeof(CompactOperationSample);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_AUTHORITY_JOURNAL_H
