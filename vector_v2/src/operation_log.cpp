#include "tinydraw/vector_v2/operation_log.h"

#include <algorithm>
#include <limits>

#include "tinydraw/vector_v2/operation_builder.h"
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

bool continues_stroke(const OperationRecord& previous, const OperationAppend& next) {
  return same_stroke(stroke_identity(previous), stroke_identity(next));
}

bool has_shared_boundary(std::span<const CompactOperationSample> previous,
                         std::span<const CompactOperationSample> next) {
  return !previous.empty() && !next.empty() && same_sample_geometry(previous.back(), next.front());
}

}  // namespace

PreparedHistoryChange::PreparedHistoryChange(OperationLog& owner, HistoryChange change,
                                             std::size_t active_sample_count)
    : owner_(&owner), change_(change), active_sample_count_(active_sample_count) {}

PreparedHistoryChange::~PreparedHistoryChange() { cancel(); }

PreparedHistoryChange::PreparedHistoryChange(PreparedHistoryChange&& other) noexcept
    : owner_(other.owner_),
      change_(other.change_),
      active_sample_count_(other.active_sample_count_) {
  other.owner_ = nullptr;
  other.change_ = {};
  other.active_sample_count_ = 0;
}

PreparedHistoryChange& PreparedHistoryChange::operator=(PreparedHistoryChange&& other) noexcept {
  if (this != &other) {
    cancel();
    owner_ = other.owner_;
    change_ = other.change_;
    active_sample_count_ = other.active_sample_count_;
    other.owner_ = nullptr;
    other.change_ = {};
    other.active_sample_count_ = 0;
  }
  return *this;
}

const HistoryChange& PreparedHistoryChange::change() const { return change_; }

std::optional<std::size_t> PreparedHistoryChange::query_target_spatial(
    PixelRect world_bounds, std::span<std::uint16_t> newest_first_candidates,
    OperationSpatialQueryStats* stats) const {
  return owner_ != nullptr
             ? owner_->query_history_spatial(*this, world_bounds, newest_first_candidates, stats)
             : std::nullopt;
}

void PreparedHistoryChange::publish() {
  if (owner_ != nullptr) {
    owner_->publish_history(*this);
    owner_ = nullptr;
    change_ = {};
    active_sample_count_ = 0;
  }
}

void PreparedHistoryChange::cancel() {
  if (owner_ != nullptr) {
    owner_->cancel_history(*this);
    owner_ = nullptr;
    change_ = {};
    active_sample_count_ = 0;
  }
}

OperationLog::OperationLog(std::span<OperationRecord> records,
                           std::span<CompactOperationSample> samples,
                           OperationSpatialIndex* spatial_index)
    : records_(records), samples_(samples), spatial_index_(spatial_index) {
  rebuild_spatial_index();
}

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

bool OperationLog::can_reset() const { return !history_pending_; }

bool OperationLog::can_undo() const {
  return !history_pending_ && operation_count_ != 0U &&
         revision_.value != std::numeric_limits<std::uint32_t>::max();
}

bool OperationLog::can_redo() const {
  return !history_pending_ && operation_count_ < retained_operation_count_ &&
         revision_.value != std::numeric_limits<std::uint32_t>::max();
}

bool OperationLog::workspace_overlaps_storage(std::span<const std::byte> workspace) const {
  return storage_overlaps(workspace, std::as_bytes(std::span(records_))) ||
         storage_overlaps(workspace, std::as_bytes(std::span(samples_))) ||
         (spatial_index_ != nullptr && spatial_index_->workspace_overlaps_storage(workspace));
}

std::optional<OperationIdentity> OperationLog::append(const OperationAppend& append_request) {
  if (!valid_append(append_request)) {
    return std::nullopt;
  }
  const auto calculated_bounds = operation_world_bounds(append_request.samples);
  if (!calculated_bounds.has_value()) {
    return std::nullopt;
  }
  return append_validated(append_request, *calculated_bounds);
}

std::optional<OperationIdentity> OperationLog::append(const BuiltOperation& built) {
  const OperationAppend& operation = built.operation();
  if (!accepts_append(operation)) {
    return std::nullopt;
  }
  return append_validated(operation, built.world_bounds());
}

OperationIdentity OperationLog::append_validated(const OperationAppend& append_request,
                                                 PixelRect bounds) {
  const OperationIdentity identity{
      .revision = {revision_.value + 1U},
      .operation_index = static_cast<std::uint32_t>(operation_count_),
  };
  const bool replaces_redo = operation_count_ != retained_operation_count_;
  std::copy(append_request.samples.begin(), append_request.samples.end(),
            samples_.subspan(sample_count_, append_request.samples.size()).begin());
  records_[operation_count_] = {
      .first_sample = static_cast<std::uint32_t>(sample_count_),
      .sample_count = static_cast<std::uint16_t>(append_request.samples.size()),
      .color = append_request.color,
      .bounds_x0 = static_cast<std::uint16_t>(bounds.x0),
      .bounds_y0 = static_cast<std::uint16_t>(bounds.y0),
      .bounds_x1 = static_cast<std::uint16_t>(bounds.x1),
      .bounds_y1 = static_cast<std::uint16_t>(bounds.y1),
      .tool = append_request.tool,
      .gesture_id = append_request.gesture_id,
  };
  sample_count_ += append_request.samples.size();
  ++operation_count_;
  retained_sample_count_ = sample_count_;
  retained_operation_count_ = operation_count_;
  revision_ = identity.revision;
  if (replaces_redo) {
    base_revision_ = {revision_.value - static_cast<std::uint32_t>(operation_count_)};
    ++epoch_.value;
    if (epoch_.value == 0U) {
      ++epoch_.value;
    }
    ++history_timeline_;
  }
  if (spatial_index_usable_ && !spatial_index_->replace(operation_count_ - 1U, bounds)) {
    spatial_index_usable_ = false;
  }
  return identity;
}

AuthorityReadView OperationLog::read_view() const {
  return {
      .epoch = epoch_,
      .generation = revision_,
      .active_operation_count = operation_count_,
      .retained_operation_count = retained_operation_count_,
      .retained_sample_count = retained_sample_count_,
  };
}

std::optional<StoredOperation> OperationLog::retained_operation(std::size_t index) const {
  if (index >= retained_operation_count_) {
    return std::nullopt;
  }
  const OperationRecord& record = records_[index];
  return StoredOperation{
      .identity =
          {
              .revision = {base_revision_.value + static_cast<std::uint32_t>(index) + 1U},
              .operation_index = static_cast<std::uint32_t>(index),
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

std::optional<std::size_t> OperationLog::query_spatial(
    PixelRect world_bounds, std::size_t first_operation, std::size_t requested_operation_count,
    std::span<std::uint16_t> newest_first_candidates, OperationSpatialQueryStats* stats) const {
  if (!spatial_index_usable_ || history_pending_ || first_operation > operation_count_ ||
      requested_operation_count > operation_count_ - first_operation ||
      workspace_overlaps_storage(std::as_bytes(newest_first_candidates))) {
    return std::nullopt;
  }
  return spatial_index_->query(world_bounds, first_operation, requested_operation_count,
                               newest_first_candidates, stats);
}

std::optional<PreparedHistoryChange> OperationLog::prepare_undo() {
  if (!can_undo()) {
    return std::nullopt;
  }
  const std::size_t previous_count = operation_count_;
  std::size_t target_count = previous_count - 1U;
  const StrokeIdentity stroke = stroke_identity(records_[target_count]);
  while (target_count != 0U && same_stroke(stroke_identity(records_[target_count - 1U]), stroke)) {
    --target_count;
  }
  const HistoryChange change{
      .generation = {revision_.value + 1U},
      .previous_active_operation_count = previous_count,
      .active_operation_count = target_count,
      .affected_world_bounds = bounds_for_range(target_count, previous_count),
  };
  history_pending_ = true;
  return PreparedHistoryChange(*this, change, sample_count_for_prefix(target_count));
}

std::optional<PreparedHistoryChange> OperationLog::prepare_redo() {
  if (!can_redo()) {
    return std::nullopt;
  }
  const std::size_t previous_count = operation_count_;
  std::size_t target_count = previous_count + 1U;
  const StrokeIdentity stroke = stroke_identity(records_[previous_count]);
  while (target_count < retained_operation_count_ &&
         same_stroke(stroke, stroke_identity(records_[target_count]))) {
    ++target_count;
  }
  const HistoryChange change{
      .generation = {revision_.value + 1U},
      .previous_active_operation_count = previous_count,
      .active_operation_count = target_count,
      .affected_world_bounds = bounds_for_range(previous_count, target_count),
  };
  history_pending_ = true;
  return PreparedHistoryChange(*this, change, sample_count_for_prefix(target_count));
}

std::optional<OperationReplayRange> OperationLog::replay_range(
    OperationLogEpoch baseline_epoch, DocumentRevision baseline_revision,
    DocumentRevision destination_revision) const {
  if (history_pending_ || baseline_epoch != epoch_ ||
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

std::optional<OperationReplayRange> OperationLog::active_replay_range(
    OperationLogEpoch requested_epoch) const {
  if (history_pending_ || requested_epoch != epoch_) {
    return std::nullopt;
  }
  return OperationReplayRange{
      .epoch = epoch_,
      .baseline_revision = base_revision_,
      .destination_revision = revision_,
      .first_operation = 0U,
      .operation_count = operation_count_,
  };
}

bool OperationLog::restore(const AuthorityRestore& restore) {
  if (!ready() || !can_reset() || restore.active_operation_count > restore.records.size() ||
      restore.records.size() > records_.size() || restore.samples.size() > samples_.size() ||
      restore.generation.value < restore.active_operation_count ||
      workspace_overlaps_storage(std::as_bytes(restore.records)) ||
      workspace_overlaps_storage(std::as_bytes(restore.samples))) {
    return false;
  }
  const std::uint64_t base_revision =
      static_cast<std::uint64_t>(restore.generation.value) - restore.active_operation_count;
  if (base_revision + restore.records.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  std::size_t expected_sample = 0;
  const OperationRecord* previous_record = nullptr;
  std::span<const CompactOperationSample> previous_samples;
  for (const OperationRecord& record : restore.records) {
    if (record.first_sample != expected_sample || record.sample_count == 0U || record.flags != 0U ||
        (record.tool != OperationTool::kPen && record.tool != OperationTool::kEraser) ||
        record.sample_count > restore.samples.size() - expected_sample) {
      return false;
    }
    const auto operation_samples = restore.samples.subspan(expected_sample, record.sample_count);
    if (!valid_samples(operation_samples)) {
      return false;
    }
    const auto bounds = operation_world_bounds(operation_samples);
    if (!bounds.has_value() || *bounds != PixelRect{record.bounds_x0, record.bounds_y0,
                                                    record.bounds_x1, record.bounds_y1}) {
      return false;
    }
    if (previous_record != nullptr &&
        same_stroke(stroke_identity(*previous_record), stroke_identity(record)) &&
        !has_shared_boundary(previous_samples, operation_samples)) {
      return false;
    }
    previous_record = &record;
    previous_samples = operation_samples;
    expected_sample += record.sample_count;
  }
  if (expected_sample != restore.samples.size()) {
    return false;
  }
  if (restore.active_operation_count != 0U &&
      restore.active_operation_count < restore.records.size()) {
    const StrokeIdentity previous =
        stroke_identity(restore.records[restore.active_operation_count - 1U]);
    const StrokeIdentity next = stroke_identity(restore.records[restore.active_operation_count]);
    if (same_stroke(previous, next)) {
      return false;
    }
  }

  std::copy(restore.records.begin(), restore.records.end(), records_.begin());
  std::copy(restore.samples.begin(), restore.samples.end(), samples_.begin());
  retained_operation_count_ = restore.records.size();
  retained_sample_count_ = restore.samples.size();
  operation_count_ = restore.active_operation_count;
  sample_count_ = sample_count_for_prefix(operation_count_);
  base_revision_ = {static_cast<std::uint32_t>(base_revision)};
  revision_ = restore.generation;
  epoch_ = restore.epoch;
  ++history_timeline_;
  history_pending_ = false;
  rebuild_spatial_index();
  return true;
}

bool OperationLog::reset(DocumentRevision revision) {
  if (history_pending_) {
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
  ++history_timeline_;
  rebuild_spatial_index();
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

std::optional<std::size_t> OperationLog::query_history_spatial(
    const PreparedHistoryChange& prepared, PixelRect world_bounds,
    std::span<std::uint16_t> newest_first_candidates, OperationSpatialQueryStats* stats) const {
  if (!spatial_index_usable_ || !history_pending_ || prepared.owner_ != this ||
      prepared.change_.active_operation_count > retained_operation_count_ ||
      workspace_overlaps_storage(std::as_bytes(newest_first_candidates))) {
    return std::nullopt;
  }
  return spatial_index_->query(world_bounds, 0U, prepared.change_.active_operation_count,
                               newest_first_candidates, stats);
}

bool OperationLog::can_use_spatial_index() const {
  return spatial_index_ != nullptr && spatial_index_->ready() &&
         spatial_index_->operation_capacity() >= records_.size() &&
         !spatial_index_->workspace_overlaps_storage(std::as_bytes(std::span(records_))) &&
         !spatial_index_->workspace_overlaps_storage(std::as_bytes(std::span(samples_)));
}

void OperationLog::rebuild_spatial_index() {
  spatial_index_usable_ = can_use_spatial_index();
  if (!spatial_index_usable_) {
    return;
  }
  spatial_index_->clear();
  for (std::size_t index = 0; index < retained_operation_count_; ++index) {
    const OperationRecord& record = records_[index];
    if (!spatial_index_->replace(
            index, {record.bounds_x0, record.bounds_y0, record.bounds_x1, record.bounds_y1})) {
      spatial_index_usable_ = false;
      return;
    }
  }
}

void OperationLog::publish_history(const PreparedHistoryChange& prepared) {
  if (!history_pending_) {
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
}

void OperationLog::cancel_history(const PreparedHistoryChange&) {
  if (!history_pending_) {
    return;
  }
  history_pending_ = false;
}

bool OperationLog::valid_append(const OperationAppend& append_request) const {
  return accepts_append(append_request) && valid_samples(append_request.samples);
}

bool OperationLog::accepts_append(const OperationAppend& append_request) const {
  if (!ready() || history_pending_ || append_request.samples.empty() ||
      append_request.samples.size() > std::numeric_limits<std::uint16_t>::max() ||
      operation_count_ >= records_.size() ||
      revision_.value == std::numeric_limits<std::uint32_t>::max() ||
      append_request.samples.size() > samples_.size() - sample_count_ ||
      (append_request.tool != OperationTool::kPen &&
       append_request.tool != OperationTool::kEraser)) {
    return false;
  }
  if (operation_count_ == 0U ||
      !continues_stroke(records_[operation_count_ - 1U], append_request)) {
    return true;
  }
  const OperationRecord& previous = records_[operation_count_ - 1U];
  return has_shared_boundary(samples_.subspan(previous.first_sample, previous.sample_count),
                             append_request.samples);
}

}  // namespace tinydraw::vector_v2
