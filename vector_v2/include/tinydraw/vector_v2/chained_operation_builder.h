#ifndef TINYDRAW_VECTOR_V2_CHAINED_OPERATION_BUILDER_H
#define TINYDRAW_VECTOR_V2_CHAINED_OPERATION_BUILDER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/operation_builder.h"

namespace tinydraw::vector_v2 {

enum class ChainedOperationStatus : std::uint8_t {
  kAccepted,
  kChunkReady,
  kFinalChunkReady,
  kComplete,
  kRejected,
};

// Collects one physical gesture into bounded persistent operations. A ready
// chunk remains valid until acknowledge_commit() or cancel(). The caller must
// commit it authoritatively before acknowledging; continuation then overlaps
// the prior chunk by one sample so no segment is lost. Every chunk carries the
// same nonzero gesture identity for future whole-gesture Undo.
class ChainedOperationBuilder {
 public:
  explicit ChainedOperationBuilder(std::span<CompactOperationSample> storage);
  ChainedOperationBuilder(std::span<CompactOperationSample> storage,
                          std::size_t maximum_chunk_samples);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] OperationBuilderReject last_reject() const;
  [[nodiscard]] std::size_t sample_count() const;
  [[nodiscard]] std::optional<OperationAppend> pending_append() const;

  [[nodiscard]] bool begin(OperationTool tool, std::uint16_t color, std::uint16_t gesture_id,
                           OperationPoint point);
  [[nodiscard]] ChainedOperationStatus add(OperationPoint point);
  [[nodiscard]] ChainedOperationStatus finish(OperationPoint point);
  [[nodiscard]] ChainedOperationStatus acknowledge_commit();
  void cancel();

 private:
  enum class State : std::uint8_t {
    kIdle,
    kCollecting,
    kChunkReady,
    kFinalChunkReady,
    kRejected,
  };

  [[nodiscard]] ChainedOperationStatus capture_boundary(OperationPoint rejected,
                                                        bool finish_after_commit);
  [[nodiscard]] ChainedOperationStatus capture_final(OperationPoint point);

  OperationBuilder builder_;
  std::size_t maximum_chunk_samples_ = 0;
  bool ready_ = false;
  OperationTool tool_ = OperationTool::kPen;
  std::uint16_t color_ = 0;
  std::uint16_t gesture_id_ = 0;
  OperationPoint last_accepted_{};
  OperationPoint rejected_{};
  State state_ = State::kIdle;
  bool finish_after_commit_ = false;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_CHAINED_OPERATION_BUILDER_H
