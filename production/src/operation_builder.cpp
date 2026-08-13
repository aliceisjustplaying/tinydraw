#include "tinydraw/production/operation_builder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tinydraw::production {
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

std::size_t OperationBuilder::sample_count() const { return sample_count_; }

bool OperationBuilder::begin(OperationTool tool, std::uint16_t color, OperationPoint point) {
  cancel();
  if (!ready() || (tool != OperationTool::kPen && tool != OperationTool::kEraser) ||
      !valid_point(point)) {
    return false;
  }
  tool_ = tool;
  color_ = color;
  started_us_ = point.timestamp_us;
  previous_us_ = point.timestamp_us;
  active_ = true;
  return append_point(point, true);
}

bool OperationBuilder::add(OperationPoint point) {
  if (!active_ || overflowed_) {
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
  return OperationAppend{.tool = tool_, .color = color_, .samples = storage_.first(sample_count_)};
}

void OperationBuilder::cancel() {
  sample_count_ = 0;
  active_ = false;
  overflowed_ = false;
}

bool OperationBuilder::append_point(OperationPoint point, bool retain_duplicate) {
  if (!valid_point(point)) {
    return false;
  }
  const std::uint32_t since_previous = point.timestamp_us - previous_us_;
  const std::uint32_t elapsed_us = point.timestamp_us - started_us_;
  if (since_previous > 0x7FFF'FFFFU || elapsed_us > kMaximumElapsedUs) {
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
    return false;
  }
  storage_[sample_count_++] = sample;
  return true;
}

}  // namespace tinydraw::production
