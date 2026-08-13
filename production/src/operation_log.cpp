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

PixelRect operation_bounds(std::span<const CompactOperationSample> samples) {
  int minimum_x = kWorldWidth * 4;
  int minimum_y = kWorldHeight * 4;
  int maximum_x = 0;
  int maximum_y = 0;
  for (const CompactOperationSample sample : samples) {
    const int radius_quarter = (static_cast<int>(sample.radius_256) + 63) / 64;
    minimum_x = std::min(minimum_x, static_cast<int>(sample.x_quarter) - radius_quarter);
    minimum_y = std::min(minimum_y, static_cast<int>(sample.y_quarter) - radius_quarter);
    maximum_x = std::max(maximum_x, static_cast<int>(sample.x_quarter) + radius_quarter);
    maximum_y = std::max(maximum_y, static_cast<int>(sample.y_quarter) + radius_quarter);
  }
  return {
      .x0 = std::max(0, minimum_x) / 4,
      .y0 = std::max(0, minimum_y) / 4,
      .x1 = std::min(kWorldWidth, (maximum_x + 3) / 4),
      .y1 = std::min(kWorldHeight, (maximum_y + 3) / 4),
  };
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

std::optional<OperationIdentity> OperationLog::append(const OperationAppend& append_request) {
  if (!valid_append(append_request)) {
    return std::nullopt;
  }
  const std::size_t operation_index = operation_count_;
  const std::size_t first_sample = sample_count_;
  const PixelRect bounds = operation_bounds(append_request.samples);
  std::copy(append_request.samples.begin(), append_request.samples.end(),
            samples_.subspan(sample_count_).begin());
  sample_count_ += append_request.samples.size();
  records_[operation_count_] = {
      .first_sample = static_cast<std::uint32_t>(first_sample),
      .sample_count = static_cast<std::uint16_t>(append_request.samples.size()),
      .color = append_request.color,
      .bounds_x0 = static_cast<std::uint16_t>(bounds.x0),
      .bounds_y0 = static_cast<std::uint16_t>(bounds.y0),
      .bounds_x1 = static_cast<std::uint16_t>(bounds.x1),
      .bounds_y1 = static_cast<std::uint16_t>(bounds.y1),
      .tool = append_request.tool,
  };
  ++operation_count_;
  ++revision_.value;
  return OperationIdentity{.revision = revision_,
                           .operation_index = static_cast<std::uint32_t>(operation_index)};
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
}

bool OperationLog::valid_append(const OperationAppend& append_request) const {
  if (!ready() || append_request.samples.empty() ||
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
