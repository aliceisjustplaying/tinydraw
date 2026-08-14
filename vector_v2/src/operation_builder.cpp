#include "tinydraw/vector_v2/operation_builder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tinydraw::vector_v2 {
namespace {

constexpr float kQuarterUnitsPerWorldUnit = 4.0F;
constexpr float kRadiusUnitsPerWorldUnit = 256.0F;
constexpr std::uint32_t kMaximumElapsedUs =
    static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max()) * 1'000U + 999U;

bool valid_point(OperationPoint point) {
  return std::isfinite(point.world_x) && std::isfinite(point.world_y) &&
         std::isfinite(point.radius) && point.world_x >= 0.0F &&
         point.world_x <= static_cast<float>(kWorldWidth) && point.world_y >= 0.0F &&
         point.world_y <= static_cast<float>(kWorldHeight) &&
         point.radius * kRadiusUnitsPerWorldUnit >= 0.5F &&
         point.radius * kRadiusUnitsPerWorldUnit <=
             static_cast<float>(std::numeric_limits<std::uint16_t>::max());
}

std::uint16_t quantize(float value, float scale) {
  return static_cast<std::uint16_t>(std::lround(value * scale));
}

}  // namespace

OperationBuilder::OperationBuilder(std::span<CompactOperationSample> storage) : storage_(storage) {}

bool OperationBuilder::ready() const { return !storage_.empty(); }

bool OperationBuilder::active() const { return active_; }

bool OperationBuilder::overflowed() const { return overflowed_; }

OperationBuilderReject OperationBuilder::last_reject() const { return last_reject_; }

std::optional<OperationAppend> OperationBuilder::collected() const {
  if (sample_count_ == 0U) {
    return std::nullopt;
  }
  return OperationAppend{.tool = tool_,
                         .color = color_,
                         .gesture_id = gesture_id_,
                         .samples = storage_.first(sample_count_)};
}

std::size_t OperationBuilder::sample_count() const { return sample_count_; }

bool OperationBuilder::begin(OperationTool tool, std::uint16_t color, OperationPoint point,
                             std::uint16_t gesture_id) {
  cancel();
  if (!ready() || (tool != OperationTool::kPen && tool != OperationTool::kEraser)) {
    last_reject_ = OperationBuilderReject::kNotActive;
    return false;
  }
  if (!valid_point(point)) {
    last_reject_ = OperationBuilderReject::kInvalidPoint;
    return false;
  }
  tool_ = tool;
  color_ = color;
  gesture_id_ = gesture_id;
  started_us_ = point.timestamp_us;
  previous_us_ = point.timestamp_us;
  active_ = true;
  return append_point(point, true);
}

bool OperationBuilder::add(OperationPoint point) {
  if (!active_ || overflowed_) {
    last_reject_ = overflowed_ ? OperationBuilderReject::kCapacityOverflow
                               : OperationBuilderReject::kNotActive;
    return false;
  }
  if (!append_point(point, false)) {
    active_ = false;
    return false;
  }
  return true;
}

std::optional<OperationAppend> OperationBuilder::finish(OperationPoint point) {
  if (!active_ || overflowed_) {
    return std::nullopt;
  }
  if (!append_point(point, false) && !overflowed_) {
    active_ = false;
    return std::nullopt;
  }
  active_ = false;
  return collected();
}

void OperationBuilder::cancel() {
  sample_count_ = 0;
  active_ = false;
  overflowed_ = false;
  gesture_id_ = 0;
  last_reject_ = OperationBuilderReject::kNone;
}

bool OperationBuilder::append_point(OperationPoint point, bool retain_duplicate) {
  if (!valid_point(point)) {
    last_reject_ = OperationBuilderReject::kInvalidPoint;
    return false;
  }
  const std::uint32_t since_previous = point.timestamp_us - previous_us_;
  const std::uint32_t elapsed_us = point.timestamp_us - started_us_;
  if (since_previous > 0x7FFF'FFFFU) {
    last_reject_ = OperationBuilderReject::kTimestampRegression;
    return false;
  }
  if (elapsed_us > kMaximumElapsedUs) {
    last_reject_ = OperationBuilderReject::kElapsedOverflow;
    return false;
  }
  const CompactOperationSample sample{
      .x_quarter = quantize(point.world_x, kQuarterUnitsPerWorldUnit),
      .y_quarter = quantize(point.world_y, kQuarterUnitsPerWorldUnit),
      .radius_256 = quantize(point.radius, kRadiusUnitsPerWorldUnit),
      .elapsed_ms = static_cast<std::uint16_t>(elapsed_us / 1'000U),
  };
  previous_us_ = point.timestamp_us;
  if (!retain_duplicate && sample_count_ != 0U &&
      storage_[sample_count_ - 1U].x_quarter == sample.x_quarter &&
      storage_[sample_count_ - 1U].y_quarter == sample.y_quarter &&
      storage_[sample_count_ - 1U].radius_256 == sample.radius_256) {
    return true;
  }
  if (sample_count_ == storage_.size()) {
    overflowed_ = true;
    last_reject_ = OperationBuilderReject::kCapacityOverflow;
    return false;
  }
  storage_[sample_count_++] = sample;
  return true;
}

}  // namespace tinydraw::vector_v2
