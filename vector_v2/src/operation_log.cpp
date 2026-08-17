#include "tinydraw/vector_v2/operation_log.h"

#include <algorithm>
#include <limits>

#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {
namespace {

constexpr std::uint16_t kMaximumXQuarter =
    static_cast<std::uint16_t>(kWorldWidth * kSampleUnitsPerWorldUnit);
constexpr std::uint16_t kMaximumYQuarter =
    static_cast<std::uint16_t>(kWorldHeight * kSampleUnitsPerWorldUnit);

bool valid_sample(CompactOperationSample sample) {
  return sample.x_quarter <= kMaximumXQuarter && sample.y_quarter <= kMaximumYQuarter &&
         sample.radius_256 > 0;
}

bool valid_samples(std::span<const CompactOperationSample> samples) {
  if (!std::all_of(samples.begin(), samples.end(), valid_sample)) {
    return false;
  }
  for (std::size_t index = 1; index < samples.size(); ++index) {
    if (samples[index].elapsed_ms < samples[index - 1U].elapsed_ms) {
      return false;
    }
  }
  return true;
}

}  // namespace

PreparedAppend::PreparedAppend(OperationLog& owner, StoredOperation operation, std::uint32_t token)
    : owner_(&owner), operation_(operation), token_(token) {}

PreparedAppend::~PreparedAppend() { cancel(); }

PreparedAppend::PreparedAppend(PreparedAppend&& other) noexcept
    : owner_(other.owner_), operation_(other.operation_), token_(other.token_) {
  other.owner_ = nullptr;
  other.operation_ = {};
  other.token_ = 0;
}

PreparedAppend& PreparedAppend::operator=(PreparedAppend&& other) noexcept {
  if (this != &other) {
    cancel();
    owner_ = other.owner_;
    operation_ = other.operation_;
    token_ = other.token_;
    other.owner_ = nullptr;
    other.operation_ = {};
    other.token_ = 0;
  }
  return *this;
}

const StoredOperation& PreparedAppend::operation() const { return operation_; }

void PreparedAppend::publish() {
  if (owner_ != nullptr) {
    owner_->publish_prepared(*this);
    owner_ = nullptr;
    operation_ = {};
    token_ = 0;
  }
}

void PreparedAppend::cancel() {
  if (owner_ != nullptr) {
    owner_->cancel_prepared(*this);
    owner_ = nullptr;
    operation_ = {};
    token_ = 0;
  }
}

PreparedHistoryChange::PreparedHistoryChange(OperationLog& owner, HistoryChange change,
                                             std::size_t active_sample_count,
                                             std::uint32_t token)
    : owner_(&owner),
      change_(change),
      active_sample_count_(active_sample_count),
      token_(token) {}

PreparedHistoryChange::~PreparedHistoryChange() { cancel(); }

PreparedHistoryChange::PreparedHistoryChange(PreparedHistoryChange&& other) noexcept
    : owner_(other.owner_),
      change_(other.change_),
      active_sample_count_(other.active_sample_count_),
      token_(other.token_) {
  other.owner_ = nullptr;
  other.change_ = {};
  other.active_sample_count_ = 0;
  other.token_ = 0;
}

PreparedHistoryChange& PreparedHistoryChange::operator=(PreparedHistoryChange&& other) noexcept {
  if (this != &other) {
    cancel();
    owner_ = other.owner_;
    change_ = other.change_;
    active_sample_count_ = other.active_sample_count_;
    token_ = other.token_;
    other.owner_ = nullptr;
    other.change_ = {};
    other.active_sample_count_ = 0;
    other.token_ = 0;
  }
  return *this;
}

const HistoryChange& PreparedHistoryChange::change() const { return change_; }

std::optional<StoredOperation> PreparedHistoryChange::target_operation(
    std::size_t active_index) const {
  return owner_ != nullptr ? owner_->history_operation(*this, active_index) : std::nullopt;
}

void PreparedHistoryChange::publish() {
  if (owner_ != nullptr) {
    owner_->publish_history(*this);
    owner_ = nullptr;
    change_ = {};
    active_sample_count_ = 0;
    token_ = 0;
  }
}

void PreparedHistoryChange::cancel() {
  if (owner_ != nullptr) {
    owner_->cancel_history(*this);
    owner_ = nullptr;
    change_ = {};
    active_sample_count_ = 0;
    token_ = 0;
  }
}

OperationLog::OperationLog(std::span<OperationRecord> records,
                           std::span<CompactOperationSample> samples)
    : records_(records), samples_(samples) {}

bool OperationLog::ready() const {
  // Nonempty is not enough: overlapping caller-owned storage would let an
  // append corrupt records through the sample span (and vice versa), and
  // OperationRecord stores sample offsets/operation indices as uint32, so
  // larger spans could silently truncate.
  if (records_.empty() || samples_.empty()) {
    return false;
  }
  if (storage_overlaps(std::as_bytes(records_), std::as_bytes(samples_))) {
    return false;
  }
  constexpr std::size_t kMaximumIndex = std::numeric_limits<std::uint32_t>::max();
  return records_.size() <= kMaximumIndex && samples_.size() <= kMaximumIndex;
}

DocumentRevision OperationLog::current_revision() const { return revision_; }

OperationLogEpoch OperationLog::epoch() const { return epoch_; }

std::size_t OperationLog::operation_count() const { return operation_count_; }

std::size_t OperationLog::sample_count() const { return sample_count_; }

std::size_t OperationLog::operation_capacity() const { return records_.size(); }

std::size_t OperationLog::sample_capacity() const { return samples_.size(); }

bool OperationLog::can_reset() const { return !append_pending_ && !history_pending_; }

bool OperationLog::can_undo() const {
  return !append_pending_ && !history_pending_ && operation_count_ != 0U &&
         revision_.value != std::numeric_limits<std::uint32_t>::max();
}

bool OperationLog::can_redo() const {
  return !append_pending_ && !history_pending_ && operation_count_ < retained_operation_count_ &&
         revision_.value != std::numeric_limits<std::uint32_t>::max();
}

bool OperationLog::workspace_overlaps_storage(std::span<const std::byte> workspace) const {
  return storage_overlaps(workspace, std::as_bytes(std::span(records_))) ||
         storage_overlaps(workspace, std::as_bytes(std::span(samples_)));
}

std::optional<PreparedAppend> OperationLog::prepare(const OperationAppend& append_request) {
  if (!valid_append(append_request)) {
    return std::nullopt;
  }
  const auto calculated_bounds = operation_world_bounds(append_request.samples);
  if (!calculated_bounds.has_value()) {
    return std::nullopt;
  }
  pending_sample_count_ = append_request.samples.size();
  pending_token_ = next_prepare_token_++;
  if (next_prepare_token_ == 0U) {
    next_prepare_token_ = 1U;
  }
  append_pending_ = true;
  return PreparedAppend(
      *this,
      StoredOperation{
          .identity = {.revision = {revision_.value + 1U},
                       .operation_index = static_cast<std::uint32_t>(operation_count_)},
          .tool = append_request.tool,
          .color = append_request.color,
          .gesture_id = append_request.gesture_id,
          .world_bounds = *calculated_bounds,
          .samples = append_request.samples,
      },
      pending_token_);
}

void OperationLog::publish_prepared(const PreparedAppend& prepared) {
  if (!append_pending_ || prepared.token_ != pending_token_) {
    return;
  }
  const bool replaces_redo = operation_count_ != retained_operation_count_;
  std::copy(prepared.operation_.samples.begin(), prepared.operation_.samples.end(),
            samples_.subspan(sample_count_, pending_sample_count_).begin());
  const PixelRect bounds = prepared.operation_.world_bounds;
  records_[operation_count_] = {
      .first_sample = static_cast<std::uint32_t>(sample_count_),
      .sample_count = static_cast<std::uint16_t>(pending_sample_count_),
      .color = prepared.operation_.color,
      .bounds_x0 = static_cast<std::uint16_t>(bounds.x0),
      .bounds_y0 = static_cast<std::uint16_t>(bounds.y0),
      .bounds_x1 = static_cast<std::uint16_t>(bounds.x1),
      .bounds_y1 = static_cast<std::uint16_t>(bounds.y1),
      .tool = prepared.operation_.tool,
      .gesture_id = prepared.operation_.gesture_id,
  };
  sample_count_ += pending_sample_count_;
  ++operation_count_;
  retained_sample_count_ = sample_count_;
  retained_operation_count_ = operation_count_;
  revision_ = prepared.operation_.identity.revision;
  if (replaces_redo) {
    base_revision_ = {revision_.value - static_cast<std::uint32_t>(operation_count_)};
    ++epoch_.value;
    if (epoch_.value == 0U) {
      ++epoch_.value;
    }
  }
  append_pending_ = false;
  pending_sample_count_ = 0;
  pending_token_ = 0;
}

void OperationLog::cancel_prepared(const PreparedAppend& prepared) {
  if (!append_pending_ || prepared.token_ != pending_token_) {
    return;
  }
  append_pending_ = false;
  pending_sample_count_ = 0;
  pending_token_ = 0;
}

std::optional<OperationIdentity> OperationLog::append(const OperationAppend& append_request) {
  auto prepared = prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const OperationIdentity identity = prepared->operation().identity;
  prepared->publish();
  return identity;
}

AuthorityReadView OperationLog::read_view() const {
  return {
      .epoch = epoch_,
      .generation = revision_,
      .active_operation_count = operation_count_,
      .retained_operation_count = retained_operation_count_,
  };
}

bool OperationLog::unchanged(const AuthorityReadView& view) const {
  return !append_pending_ && !history_pending_ && view == read_view();
}

std::optional<StoredOperation> OperationLog::operation(std::size_t index) const {
  if (index >= operation_count_) {
    return std::nullopt;
  }
  const OperationRecord& record = records_[index];
  return StoredOperation{
      .identity = {.revision = {base_revision_.value + static_cast<std::uint32_t>(index) + 1U},
                   .operation_index = static_cast<std::uint32_t>(index)},
      .tool = record.tool,
      .color = record.color,
      .gesture_id = record.gesture_id,
      .world_bounds = {.x0 = record.bounds_x0,
                       .y0 = record.bounds_y0,
                       .x1 = record.bounds_x1,
                       .y1 = record.bounds_y1},
      .samples = samples_.subspan(record.first_sample, record.sample_count),
  };
}

std::optional<StoredOperation> OperationLog::operation(const AuthorityReadView& view,
                                                       std::size_t active_index) const {
  if (!unchanged(view) || active_index >= view.active_operation_count) {
    return std::nullopt;
  }
  return operation(active_index);
}

std::optional<StoredOperation> OperationLog::retained_operation(
    const AuthorityReadView& view, std::size_t retained_index) const {
  if (!unchanged(view) || retained_index >= view.retained_operation_count) {
    return std::nullopt;
  }
  const OperationRecord& record = records_[retained_index];
  return StoredOperation{
      .identity = {
          .revision = {base_revision_.value + static_cast<std::uint32_t>(retained_index) + 1U},
          .operation_index = static_cast<std::uint32_t>(retained_index),
      },
      .tool = record.tool,
      .color = record.color,
      .gesture_id = record.gesture_id,
      .world_bounds = {.x0 = record.bounds_x0,
                       .y0 = record.bounds_y0,
                       .x1 = record.bounds_x1,
                       .y1 = record.bounds_y1},
      .samples = samples_.subspan(record.first_sample, record.sample_count),
  };
}

std::optional<PreparedHistoryChange> OperationLog::prepare_undo() {
  if (!can_undo()) {
    return std::nullopt;
  }
  const std::size_t previous_count = operation_count_;
  std::size_t target_count = previous_count - 1U;
  const std::uint16_t gesture_id = records_[target_count].gesture_id;
  if (gesture_id != 0U) {
    while (target_count != 0U && records_[target_count - 1U].gesture_id == gesture_id) {
      --target_count;
    }
  }
  const HistoryChange change{
      .generation = {revision_.value + 1U},
      .previous_active_operation_count = previous_count,
      .active_operation_count = target_count,
      .affected_world_bounds = bounds_for_range(target_count, previous_count),
  };
  pending_history_token_ = next_history_token_++;
  if (next_history_token_ == 0U) {
    next_history_token_ = 1U;
  }
  history_pending_ = true;
  return PreparedHistoryChange(*this, change, sample_count_for_prefix(target_count),
                               pending_history_token_);
}

std::optional<PreparedHistoryChange> OperationLog::prepare_redo() {
  if (!can_redo()) {
    return std::nullopt;
  }
  const std::size_t previous_count = operation_count_;
  std::size_t target_count = previous_count + 1U;
  const std::uint16_t gesture_id = records_[previous_count].gesture_id;
  if (gesture_id != 0U) {
    while (target_count < retained_operation_count_ &&
           records_[target_count].gesture_id == gesture_id) {
      ++target_count;
    }
  }
  const HistoryChange change{
      .generation = {revision_.value + 1U},
      .previous_active_operation_count = previous_count,
      .active_operation_count = target_count,
      .affected_world_bounds = bounds_for_range(previous_count, target_count),
  };
  pending_history_token_ = next_history_token_++;
  if (next_history_token_ == 0U) {
    next_history_token_ = 1U;
  }
  history_pending_ = true;
  return PreparedHistoryChange(*this, change, sample_count_for_prefix(target_count),
                               pending_history_token_);
}

std::optional<OperationReplayRange> OperationLog::replay_range(
    OperationLogEpoch baseline_epoch, DocumentRevision baseline_revision,
    DocumentRevision destination_revision) const {
  if (append_pending_ || history_pending_ || baseline_epoch != epoch_ ||
      baseline_revision.value < base_revision_.value ||
      destination_revision.value < baseline_revision.value ||
      destination_revision.value > revision_.value) {
    return std::nullopt;
  }
  const std::size_t first_operation = baseline_revision.value - base_revision_.value;
  const std::size_t replay_count = destination_revision.value - baseline_revision.value;
  if (first_operation > operation_count_ || replay_count > operation_count_ - first_operation) {
    return std::nullopt;
  }
  return OperationReplayRange{
      .epoch = epoch_,
      .baseline_revision = baseline_revision,
      .destination_revision = destination_revision,
      .first_operation = first_operation,
      .operation_count = replay_count,
  };
}

bool OperationLog::reset(DocumentRevision revision) {
  if (append_pending_ || history_pending_) {
    return false;
  }
  operation_count_ = 0;
  sample_count_ = 0;
  retained_operation_count_ = 0;
  retained_sample_count_ = 0;
  base_revision_ = revision;
  revision_ = revision;
  ++epoch_.value;
  if (epoch_.value == 0U) {
    ++epoch_.value;
  }
  pending_sample_count_ = 0;
  pending_token_ = 0;
  pending_history_token_ = 0;
  return true;
}

bool OperationLog::clear() { return reset(); }

std::size_t OperationLog::sample_count_for_prefix(std::size_t operation_count) const {
  if (operation_count == 0U) {
    return 0U;
  }
  const OperationRecord& final_record = records_[operation_count - 1U];
  return static_cast<std::size_t>(final_record.first_sample) + final_record.sample_count;
}

PixelRect OperationLog::bounds_for_range(std::size_t first, std::size_t last) const {
  const OperationRecord& initial = records_[first];
  PixelRect bounds{initial.bounds_x0, initial.bounds_y0, initial.bounds_x1, initial.bounds_y1};
  for (std::size_t index = first + 1U; index < last; ++index) {
    const OperationRecord& record = records_[index];
    bounds.x0 = std::min(bounds.x0, static_cast<int>(record.bounds_x0));
    bounds.y0 = std::min(bounds.y0, static_cast<int>(record.bounds_y0));
    bounds.x1 = std::max(bounds.x1, static_cast<int>(record.bounds_x1));
    bounds.y1 = std::max(bounds.y1, static_cast<int>(record.bounds_y1));
  }
  return bounds;
}

std::optional<StoredOperation> OperationLog::history_operation(
    const PreparedHistoryChange& prepared, std::size_t active_index) const {
  if (!history_pending_ || prepared.token_ != pending_history_token_ ||
      active_index >= prepared.change_.active_operation_count ||
      active_index >= retained_operation_count_) {
    return std::nullopt;
  }
  const OperationRecord& record = records_[active_index];
  const std::uint32_t base_revision =
      prepared.change_.generation.value -
      static_cast<std::uint32_t>(prepared.change_.active_operation_count);
  return StoredOperation{
      .identity =
          {
              .revision = {base_revision + static_cast<std::uint32_t>(active_index) + 1U},
              .operation_index = static_cast<std::uint32_t>(active_index),
          },
      .tool = record.tool,
      .color = record.color,
      .gesture_id = record.gesture_id,
      .world_bounds = {.x0 = record.bounds_x0,
                       .y0 = record.bounds_y0,
                       .x1 = record.bounds_x1,
                       .y1 = record.bounds_y1},
      .samples = samples_.subspan(record.first_sample, record.sample_count),
  };
}

void OperationLog::publish_history(const PreparedHistoryChange& prepared) {
  if (!history_pending_ || prepared.token_ != pending_history_token_) {
    return;
  }
  operation_count_ = prepared.change_.active_operation_count;
  sample_count_ = prepared.active_sample_count_;
  revision_ = prepared.change_.generation;
  base_revision_ = {revision_.value - static_cast<std::uint32_t>(operation_count_)};
  ++epoch_.value;
  if (epoch_.value == 0U) {
    ++epoch_.value;
  }
  history_pending_ = false;
  pending_history_token_ = 0;
}

void OperationLog::cancel_history(const PreparedHistoryChange& prepared) {
  if (!history_pending_ || prepared.token_ != pending_history_token_) {
    return;
  }
  history_pending_ = false;
  pending_history_token_ = 0;
}

bool OperationLog::valid_append(const OperationAppend& append_request) const {
  if (!ready() || append_pending_ || history_pending_ || append_request.samples.empty() ||
      append_request.samples.size() > std::numeric_limits<std::uint16_t>::max() ||
      operation_count_ >= records_.size() ||
      revision_.value == std::numeric_limits<std::uint32_t>::max() ||
      append_request.samples.size() > samples_.size() - sample_count_ ||
      (append_request.tool != OperationTool::kPen &&
       append_request.tool != OperationTool::kEraser)) {
    return false;
  }
  return valid_samples(append_request.samples);
}

}  // namespace tinydraw::vector_v2
