#ifndef TINYDRAW_PRODUCTION_OPERATION_LOG_H
#define TINYDRAW_PRODUCTION_OPERATION_LOG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/operation.h"

namespace tinydraw::production {

struct StoredOperation {
  OperationIdentity identity{};
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  PixelRect world_bounds{};
  std::span<const CompactOperationSample> samples{};
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

// Fixed-capacity, ordered document authority. Storage is caller-owned and must
// outlive the log. Append validates all input and capacity before mutation.
// Callers must serialize reads, appends, and clear operations.
class OperationLog {
 public:
  OperationLog(std::span<OperationRecord> records, std::span<CompactOperationSample> samples);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] DocumentRevision current_revision() const;
  [[nodiscard]] std::size_t operation_count() const;
  [[nodiscard]] std::size_t sample_count() const;
  [[nodiscard]] std::size_t operation_capacity() const;
  [[nodiscard]] std::size_t sample_capacity() const;
  [[nodiscard]] bool workspace_overlaps_storage(std::span<const std::uint16_t> pixels) const;

  // Preparation copies into unused caller storage but does not advance document
  // authority. Exactly one append may be prepared. Publish is valid only for
  // that preparation; cancel leaves operation/sample counts and revision intact.
  [[nodiscard]] std::optional<PreparedAppend> prepare(const OperationAppend& append_request);
  [[nodiscard]] std::optional<OperationIdentity> append(const OperationAppend& append_request);
  [[nodiscard]] std::optional<StoredOperation> operation(std::size_t index) const;
  // Resets empty authority to a snapshot revision. No-op while a
  // PreparedAppend owns the pending slot. Existing operations are discarded.
  void reset(DocumentRevision revision = {});
  void clear();

 private:
  friend class PreparedAppend;
  [[nodiscard]] bool valid_append(const OperationAppend& append_request) const;
  void publish_prepared(const PreparedAppend& prepared);
  void cancel_prepared(const PreparedAppend& prepared);

  std::span<OperationRecord> records_;
  std::span<CompactOperationSample> samples_;
  std::size_t operation_count_ = 0;
  std::size_t sample_count_ = 0;
  DocumentRevision base_revision_{};
  DocumentRevision revision_{};
  std::uint32_t next_prepare_token_ = 1;
  std::uint32_t pending_token_ = 0;
  std::size_t pending_sample_count_ = 0;
  bool append_pending_ = false;
};

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_OPERATION_LOG_H
