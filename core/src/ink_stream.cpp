#include "tinydraw/ink/ink_stream.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace tinydraw {
namespace {

constexpr float kInitialPressure = 0.25F;
constexpr float kMinimumRadius = 0.01F;

bool valid(TouchPoint point) { return std::isfinite(point.x) && std::isfinite(point.y); }

}  // namespace

InkStream::InkStream(InkConfig config) : config_(config) {}

InkPoint InkStream::begin(TouchPoint point) {
  if (!valid(point)) {
    active_ = false;
    previous_ = {};
    return previous_;
  }
  active_ = true;
  previous_ = {
      .position = {.x = point.x, .y = point.y},
      .pressure = kInitialPressure,
      .radius = radius_for(kInitialPressure),
      .distance = 0.0F,
      .running_length = 0.0F,
      .timestamp_us = point.timestamp_us,
  };
  return previous_;
}

InkPoint InkStream::update(TouchPoint point) { return ingest(point, false); }

InkPoint InkStream::finish(TouchPoint point) {
  const InkPoint result = ingest(point, true);
  active_ = false;
  return result;
}

InkPoint InkStream::ingest(TouchPoint point, bool complete) {
  // Update/finish without an active stroke can be produced by anomalous
  // touch event streams, not only by caller bugs, so this is a runtime
  // guard rather than an assert: return the safe inactive point unchanged.
  if (!active_ || !valid(point)) {
    return previous_;
  }

  const std::uint32_t elapsed_us = point.timestamp_us - previous_.timestamp_us;
  const bool timestamp_moved_backward = elapsed_us > 0x7FFF'FFFFU;
  const float nominal_dt_ms = std::max(0.001F, config_.nominal_dt_ms);
  // Equal or backward timestamps cannot provide a useful interval. Treat them as
  // one nominal interval and retain the last valid timestamp. Unsigned subtraction
  // still handles the normal uint32_t wrap-around case.
  const float elapsed_ms = elapsed_us == 0U || timestamp_moved_backward
                               ? nominal_dt_ms
                               : static_cast<float>(elapsed_us) / 1'000.0F;
  const float interval_ratio = elapsed_ms / nominal_dt_ms;

  const float streamline = std::clamp(config_.streamline, 0.0F, 1.0F);
  const float nominal_alpha = 0.15F + (1.0F - streamline) * 0.85F;
  const float alpha = complete ? 1.0F : 1.0F - std::pow(1.0F - nominal_alpha, interval_ratio);

  const Point adjusted{
      .x = previous_.position.x + (point.x - previous_.position.x) * alpha,
      .y = previous_.position.y + (point.y - previous_.position.y) * alpha,
  };
  const float delta_x = adjusted.x - previous_.position.x;
  const float delta_y = adjusted.y - previous_.position.y;
  const float distance = std::hypot(delta_x, delta_y);

  float pressure = 0.5F;
  if (config_.simulate_pressure) {
    const float size = std::max(0.001F, config_.size);
    const float nominal_distance = distance / interval_ratio;
    const float speed = std::min(1.0F, nominal_distance / size);
    const float target_pressure = 1.0F - speed;
    const float nominal_mix = speed * std::clamp(config_.pressure_rate, 0.0F, 1.0F);
    const float pressure_mix = 1.0F - std::pow(1.0F - nominal_mix, interval_ratio);
    pressure = std::clamp(
        previous_.pressure + (target_pressure - previous_.pressure) * pressure_mix, 0.0F, 1.0F);
  }

  previous_ = {
      .position = adjusted,
      .pressure = pressure,
      .radius = radius_for(pressure),
      .distance = distance,
      .running_length = previous_.running_length + distance,
      .timestamp_us = timestamp_moved_backward ? previous_.timestamp_us : point.timestamp_us,
  };
  return previous_;
}

void InkStream::end() { active_ = false; }

float InkStream::radius_for(float pressure) const {
  const float size = std::max(0.0F, config_.size);
  const float pressure_scale = 0.5F - config_.thinning * (0.5F - pressure);
  return std::max(kMinimumRadius, size * pressure_scale);
}

}  // namespace tinydraw
