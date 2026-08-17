#ifndef TINYDRAW_VECTOR_V2_OPERATION_LOG_H
#define TINYDRAW_VECTOR_V2_OPERATION_LOG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/operation.h"

namespace tinydraw::vector_v2 {

struct StoredOperation {
  OperationIdentity identity{};
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  std::uint16_t gesture_id = 0;
  PixelRect world_bounds{};
  std::span<const CompactOperationSample> samples{};
};

// A contiguous painter-ordered range available after an authoritative
// snapshot. first_operation is an index into this log, not a document-global
// operation identity.
struct OperationLogEpoch {
  std::uint64_t value = 0;
  bool operator==(const OperationLogEpoch&) const = default;
};

struct OperationReplayRange {
  OperationLogEpoch epoch{};
  DocumentRevision baseline_revision{};
  DocumentRevision destination_revision{};
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
  bool operator==(const OperationReplayRange&) const = default;
};

// One coherent drawing-authority observation. The active prefix is visible;
// the retained prefix also includes any future Redo tail.
struct AuthorityReadView {
  OperationLogEpoch epoch{};
  DocumentRevision generation{};
  std::size_t active_operation_count = 0;
  std::size_t retained_operation_count = 0;
  bool operator==(const AuthorityReadView&) const = default;
};

struct HistoryChange {
  DocumentRevision generation{};
  std::size_t previous_active_operation_count = 0;
  std::size_t active_operation_count = 0;
  PixelRect affected_world_bounds{};
  bool operator==(const HistoryChange&) const = default;
};

class OperationLog;

// Move-only preparation owned by one OperationLog. It must not outlive that
// log. Destruction cancels an unpublished preparation. publish() is infallible
// for a live preparation.
class PreparedAppend {
 public:
  ~PreparedAppend();
  PreparedAppend(const PreparedAppend&) = delete;
  PreparedAppend& operator=(const PreparedAppend&) = delete;
  PreparedAppend(PreparedAppend&& other) noexcept;
  PreparedAppend& operator=(PreparedAppend&& other) noexcept;

  [[nodiscard]] const StoredOperation& operation() const;
  void publish();
  void cancel();

 private:
  friend class OperationLog;
  PreparedAppend(OperationLog& owner, StoredOperation operation, std::uint32_t token);

  OperationLog* owner_ = nullptr;
  StoredOperation operation_{};
  std::uint32_t token_ = 0;
};

// Move-only whole-Stroke history transition. Destruction or cancel leaves
// authority unchanged; publish is infallible for a live preparation.
class PreparedHistoryChange {
 public:
  ~PreparedHistoryChange();
  PreparedHistoryChange(const PreparedHistoryChange&) = delete;
  PreparedHistoryChange& operator=(const PreparedHistoryChange&) = delete;
  PreparedHistoryChange(PreparedHistoryChange&& other) noexcept;
  PreparedHistoryChange& operator=(PreparedHistoryChange&& other) noexcept;

  [[nodiscard]] const HistoryChange& change() const;
  [[nodiscard]] std::optional<StoredOperation> target_operation(std::size_t active_index) const;
  void publish();
  void cancel();

 private:
  friend class OperationLog;
  PreparedHistoryChange(OperationLog& owner, HistoryChange change,
                        std::size_t active_sample_count, std::uint32_t token);

  OperationLog* owner_ = nullptr;
  HistoryChange change_{};
  std::size_t active_sample_count_ = 0;
  std::uint32_t token_ = 0;
};

// Fixed-capacity, ordered document authority. Storage is caller-owned and must
// outlive the log. Append validates all input and capacity before mutation.
// Callers must serialize reads, appends, and clear operations.
class OperationLog {
 public:
  OperationLog(std::span<OperationRecord> records, std::span<CompactOperationSample> samples);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] DocumentRevision current_revision() const;
  [[nodiscard]] OperationLogEpoch epoch() const;
  [[nodiscard]] std::size_t operation_count() const;
  [[nodiscard]] std::size_t sample_count() const;
  [[nodiscard]] std::size_t operation_capacity() const;
  [[nodiscard]] std::size_t sample_capacity() const;
  [[nodiscard]] bool can_reset() const;
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  [[nodiscard]] bool workspace_overlaps_storage(std::span<const std::byte> workspace) const;

  // Preparation validates the caller-owned samples but does not mutate storage
  // or authority. The samples must outlive publish/cancel. Publish copies them,
  // atomically replacing any Redo tail; cancel leaves that tail intact.
  [[nodiscard]] std::optional<PreparedAppend> prepare(const OperationAppend& append_request);
  [[nodiscard]] std::optional<OperationIdentity> append(const OperationAppend& append_request);
  [[nodiscard]] AuthorityReadView read_view() const;
  [[nodiscard]] bool unchanged(const AuthorityReadView& view) const;
  [[nodiscard]] std::optional<StoredOperation> operation(std::size_t index) const;
  [[nodiscard]] std::optional<StoredOperation> operation(const AuthorityReadView& view,
                                                         std::size_t active_index) const;
  [[nodiscard]] std::optional<StoredOperation> retained_operation(
      const AuthorityReadView& view, std::size_t retained_index) const;
  [[nodiscard]] std::optional<PreparedHistoryChange> prepare_undo();
  [[nodiscard]] std::optional<PreparedHistoryChange> prepare_redo();
  // Returns the exact contiguous operations needed to advance a caller-owned
  // baseline snapshot. Both revisions must still be represented by this log.
  [[nodiscard]] std::optional<OperationReplayRange> replay_range(
      OperationLogEpoch baseline_epoch, DocumentRevision baseline_revision,
      DocumentRevision destination_revision) const;
  // Returns the complete active painter-order prefix from its current uniform
  // baseline. Unlike a caller-held revision, this remains valid after history
  // rebases the represented range.
  [[nodiscard]] std::optional<OperationReplayRange> active_replay_range(
      OperationLogEpoch requested_epoch) const;
  // Resets empty authority to a snapshot revision. Fails while a
  // PreparedAppend owns the pending slot. Existing operations are discarded.
  [[nodiscard]] bool reset(DocumentRevision revision = {});
  [[nodiscard]] bool clear();

 private:
  friend class PreparedAppend;
  friend class PreparedHistoryChange;
  [[nodiscard]] bool valid_append(const OperationAppend& append_request) const;
  [[nodiscard]] std::size_t sample_count_for_prefix(std::size_t operation_count) const;
  [[nodiscard]] PixelRect bounds_for_range(std::size_t first, std::size_t last) const;
  void publish_prepared(const PreparedAppend& prepared);
  void cancel_prepared(const PreparedAppend& prepared);
  [[nodiscard]] std::optional<StoredOperation> history_operation(
      const PreparedHistoryChange& prepared, std::size_t active_index) const;
  void publish_history(const PreparedHistoryChange& prepared);
  void cancel_history(const PreparedHistoryChange& prepared);

  std::span<OperationRecord> records_;
  std::span<CompactOperationSample> samples_;
  std::size_t operation_count_ = 0;
  std::size_t sample_count_ = 0;
  std::size_t retained_operation_count_ = 0;
  std::size_t retained_sample_count_ = 0;
  DocumentRevision base_revision_{};
  DocumentRevision revision_{};
  OperationLogEpoch epoch_{};
  std::uint32_t next_prepare_token_ = 1;
  std::uint32_t pending_token_ = 0;
  std::size_t pending_sample_count_ = 0;
  bool append_pending_ = false;
  std::uint32_t next_history_token_ = 1;
  std::uint32_t pending_history_token_ = 0;
  bool history_pending_ = false;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_OPERATION_LOG_H
