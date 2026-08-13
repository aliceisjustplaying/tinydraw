#include "tinydraw/production/operation_log.h"

#include <algorithm>
#include <limits>

namespace tinydraw::production {
namespace {

constexpr std::uint16_t kMaximumXQuarter = static_cast<std::uint16_t>(kWorldWidth * 4);
constexpr std::uint16_t kMaximumYQuarter = static_cast<std::uint16_t>(kWorldHeight * 4);

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

OperationLog::OperationLog(std::span<OperationRecord> records,
                           std::span<CompactOperationSample> samples)
    : records_(records), samples_(samples) {}

bool OperationLog::ready() const { return !records_.empty() && !samples_.empty(); }

DocumentRevision OperationLog::current_revision() const { return revision_; }

std::size_t OperationLog::operation_count() const { return operation_count_; }

std::size_t OperationLog::sample_count() const { return sample_count_; }

std::size_t OperationLog::operation_capacity() const { return records_.size(); }

std::size_t OperationLog::sample_capacity() const { return samples_.size(); }

std::optional<PreparedAppend> OperationLog::prepare(const OperationAppend& append_request) {
  if (!valid_append(append_request)) {
    return std::nullopt;
  }
  const auto calculated_bounds = operation_world_bounds(append_request.samples);
  if (!calculated_bounds.has_value()) {
    return std::nullopt;
  }
  std::copy(append_request.samples.begin(), append_request.samples.end(),
            samples_.subspan(sample_count_).begin());
  pending_sample_count_ = append_request.samples.size();
  pending_token_ = next_prepare_token_++;
  if (next_prepare_token_ == 0U) {
    next_prepare_token_ = 1U;
  }
  append_pending_ = true;
  return PreparedAppend(
      StoredOperation{
          .identity = {.revision = {revision_.value + 1U},
                       .operation_index = static_cast<std::uint32_t>(operation_count_)},
          .tool = append_request.tool,
          .color = append_request.color,
          .world_bounds = *calculated_bounds,
          .samples = samples_.subspan(sample_count_, pending_sample_count_),
      },
      pending_token_);
}

bool OperationLog::publish(const PreparedAppend& prepared) {
  if (!append_pending_ || prepared.token_ != pending_token_ ||
      prepared.operation_.identity.revision.value != revision_.value + 1U ||
      prepared.operation_.identity.operation_index != operation_count_ ||
      prepared.operation_.samples.data() != samples_.subspan(sample_count_).data() ||
      prepared.operation_.samples.size() != pending_sample_count_) {
    return false;
  }
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
  };
  sample_count_ += pending_sample_count_;
  ++operation_count_;
  revision_ = prepared.operation_.identity.revision;
  append_pending_ = false;
  pending_sample_count_ = 0;
  pending_token_ = 0;
  return true;
}

bool OperationLog::cancel(const PreparedAppend& prepared) {
  if (!append_pending_ || prepared.token_ != pending_token_) {
    return false;
  }
  append_pending_ = false;
  pending_sample_count_ = 0;
  pending_token_ = 0;
  return true;
}

std::optional<OperationIdentity> OperationLog::append(const OperationAppend& append_request) {
  const auto prepared = prepare(append_request);
  if (!prepared.has_value() || !publish(*prepared)) {
    return std::nullopt;
  }
  return prepared->operation().identity;
}

std::optional<StoredOperation> OperationLog::operation(std::size_t index) const {
  if (index >= operation_count_) {
    return std::nullopt;
  }
  const OperationRecord& record = records_[index];
  return StoredOperation{
      .identity = {.revision = {static_cast<std::uint32_t>(index + 1U)},
                   .operation_index = static_cast<std::uint32_t>(index)},
      .tool = record.tool,
      .color = record.color,
      .world_bounds = {.x0 = record.bounds_x0,
                       .y0 = record.bounds_y0,
                       .x1 = record.bounds_x1,
                       .y1 = record.bounds_y1},
      .samples = samples_.subspan(record.first_sample, record.sample_count),
  };
}

void OperationLog::clear() {
  operation_count_ = 0;
  sample_count_ = 0;
  revision_ = {};
  append_pending_ = false;
  pending_sample_count_ = 0;
  pending_token_ = 0;
}

bool OperationLog::valid_append(const OperationAppend& append_request) const {
  if (!ready() || append_pending_ || append_request.samples.empty() ||
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

}  // namespace tinydraw::production
