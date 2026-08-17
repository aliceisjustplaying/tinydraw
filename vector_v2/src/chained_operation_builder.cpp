#include "tinydraw/vector_v2/chained_operation_builder.h"

namespace tinydraw::vector_v2 {

ChainedOperationBuilder::ChainedOperationBuilder(std::span<CompactOperationSample> storage)
    : ChainedOperationBuilder(storage, storage.size()) {}

ChainedOperationBuilder::ChainedOperationBuilder(std::span<CompactOperationSample> storage,
                                                 std::size_t maximum_chunk_samples)
    : builder_(storage),
      maximum_chunk_samples_(maximum_chunk_samples),
      ready_(maximum_chunk_samples >= 2U && maximum_chunk_samples <= storage.size()) {}

bool ChainedOperationBuilder::ready() const { return ready_; }

bool ChainedOperationBuilder::active() const {
  return state_ == State::kCollecting || state_ == State::kChunkReady ||
         state_ == State::kFinalChunkReady;
}

OperationBuilderReject ChainedOperationBuilder::last_reject() const {
  return builder_.last_reject();
}

std::size_t ChainedOperationBuilder::sample_count() const { return builder_.sample_count(); }

std::optional<BuiltOperation> ChainedOperationBuilder::pending_append() const {
  if (state_ != State::kChunkReady && state_ != State::kFinalChunkReady) {
    return std::nullopt;
  }
  return builder_.collected();
}

bool ChainedOperationBuilder::begin(OperationTool tool, std::uint16_t color,
                                    std::uint16_t gesture_id, OperationPoint point) {
  cancel();
  if (!ready() || gesture_id == 0U || !builder_.begin(tool, color, point, gesture_id)) {
    state_ = State::kRejected;
    return false;
  }
  tool_ = tool;
  color_ = color;
  gesture_id_ = gesture_id;
  last_accepted_ = point;
  state_ = State::kCollecting;
  return true;
}

ChainedOperationStatus ChainedOperationBuilder::add(OperationPoint point) {
  if (state_ != State::kCollecting) {
    return ChainedOperationStatus::kRejected;
  }
  if (builder_.sample_count() >= maximum_chunk_samples_) {
    return capture_boundary(point, false);
  }
  if (builder_.add(point)) {
    last_accepted_ = point;
    return ChainedOperationStatus::kAccepted;
  }
  const OperationBuilderReject reject = builder_.last_reject();
  if (reject == OperationBuilderReject::kCapacityOverflow ||
      reject == OperationBuilderReject::kElapsedOverflow) {
    return capture_boundary(point, false);
  }
  state_ = State::kRejected;
  return ChainedOperationStatus::kRejected;
}

ChainedOperationStatus ChainedOperationBuilder::finish(OperationPoint point) {
  if (state_ != State::kCollecting) {
    return ChainedOperationStatus::kRejected;
  }
  if (builder_.sample_count() >= maximum_chunk_samples_) {
    return capture_boundary(point, true);
  }
  if (!builder_.add(point)) {
    const OperationBuilderReject reject = builder_.last_reject();
    if (reject == OperationBuilderReject::kCapacityOverflow ||
        reject == OperationBuilderReject::kElapsedOverflow) {
      return capture_boundary(point, true);
    }
    state_ = State::kRejected;
    return ChainedOperationStatus::kRejected;
  }
  last_accepted_ = point;
  return capture_final();
}

ChainedOperationStatus ChainedOperationBuilder::acknowledge_commit() {
  if (state_ == State::kFinalChunkReady) {
    cancel();
    return ChainedOperationStatus::kComplete;
  }
  if (state_ != State::kChunkReady) {
    return ChainedOperationStatus::kRejected;
  }

  builder_.cancel();
  if (!builder_.begin(tool_, color_, last_accepted_, gesture_id_) || !builder_.add(rejected_)) {
    state_ = State::kRejected;
    return ChainedOperationStatus::kRejected;
  }
  last_accepted_ = rejected_;
  state_ = State::kCollecting;
  if (finish_after_commit_) {
    finish_after_commit_ = false;
    return capture_final();
  }
  return ChainedOperationStatus::kAccepted;
}

void ChainedOperationBuilder::cancel() {
  builder_.cancel();
  tool_ = OperationTool::kPen;
  color_ = 0;
  gesture_id_ = 0;
  last_accepted_ = {};
  rejected_ = {};
  state_ = State::kIdle;
  finish_after_commit_ = false;
}

ChainedOperationStatus ChainedOperationBuilder::capture_boundary(OperationPoint rejected,
                                                                 bool finish_after_commit) {
  if (!builder_.collected().has_value()) {
    state_ = State::kRejected;
    return ChainedOperationStatus::kRejected;
  }
  rejected_ = rejected;
  finish_after_commit_ = finish_after_commit;
  state_ = State::kChunkReady;
  return ChainedOperationStatus::kChunkReady;
}

ChainedOperationStatus ChainedOperationBuilder::capture_final() {
  const auto append = builder_.finish();
  if (!append.has_value()) {
    state_ = State::kRejected;
    return ChainedOperationStatus::kRejected;
  }
  state_ = State::kFinalChunkReady;
  return ChainedOperationStatus::kFinalChunkReady;
}

}  // namespace tinydraw::vector_v2
