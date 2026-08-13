#ifndef TINYDRAW_PRODUCTION_OPERATION_LOG_H
#define TINYDRAW_PRODUCTION_OPERATION_LOG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/operation.h"

namespace tinydraw::production {

struct OperationAppend {
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  std::span<const CompactOperationSample> samples{};
};

struct StoredOperation {
  OperationIdentity identity{};
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  PixelRect world_bounds{};
  std::span<const CompactOperationSample> samples{};
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

  [[nodiscard]] std::optional<OperationIdentity> append(const OperationAppend& append_request);
  [[nodiscard]] std::optional<StoredOperation> operation(std::size_t index) const;
  void clear();

 private:
  [[nodiscard]] bool valid_append(const OperationAppend& append_request) const;

  std::span<OperationRecord> records_;
  std::span<CompactOperationSample> samples_;
  std::size_t operation_count_ = 0;
  std::size_t sample_count_ = 0;
  DocumentRevision revision_{};
};

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_OPERATION_LOG_H
