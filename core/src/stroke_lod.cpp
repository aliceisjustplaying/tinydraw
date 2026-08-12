#include "tinydraw/graphics/stroke_lod.h"

#include <algorithm>
#include <cmath>

namespace tinydraw {
namespace {

float point_segment_distance_squared(StrokeSample point, StrokeSample start, StrokeSample end,
                                     float& t) {
  const float delta_x = end.x - start.x;
  const float delta_y = end.y - start.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  if (length_squared <= 0.0F) {
    t = 0.0F;
    const float point_x = point.x - start.x;
    const float point_y = point.y - start.y;
    return point_x * point_x + point_y * point_y;
  }
  t = std::clamp(((point.x - start.x) * delta_x + (point.y - start.y) * delta_y) / length_squared,
                 0.0F, 1.0F);
  const float nearest_x = start.x + t * delta_x;
  const float nearest_y = start.y + t * delta_y;
  const float distance_x = point.x - nearest_x;
  const float distance_y = point.y - nearest_y;
  return distance_x * distance_x + distance_y * distance_y;
}

bool radius_extremum(std::span<const StrokeSample> input, std::size_t index) {
  if (index == 0U || index + 1U >= input.size()) {
    return false;
  }
  const float before = input[index - 1U].radius;
  const float radius = input[index].radius;
  const float after = input[index + 1U].radius;

  // Preserve both boundaries of a local pressure plateau, but not every sample
  // inside it. The previous implementation rescanned the whole plateau for
  // every candidate sample, turning a constant-radius stroke into cubic work.
  const bool maximum_boundary =
      (radius > before && radius >= after) || (radius >= before && radius > after);
  const bool minimum_boundary =
      (radius < before && radius <= after) || (radius <= before && radius < after);
  return maximum_boundary || minimum_boundary;
}

}  // namespace

std::span<StrokeSample> simplify_stroke_samples(std::span<const StrokeSample> input,
                                                std::span<StrokeSample> output,
                                                float maximum_center_error,
                                                float maximum_radius_error) {
  if (input.empty()) {
    return output.first(0U);
  }
  if (output.empty() || !std::isfinite(maximum_center_error) || maximum_center_error < 0.0F ||
      !std::isfinite(maximum_radius_error) || maximum_radius_error < 0.0F) {
    return {};
  }
  output[0] = input.front();
  if (input.size() == 1U) {
    return output.first(1U);
  }

  // Iterative bounded-error chord growth avoids recursion on the firmware's
  // main-task stack. A chord is extended only while every sample it replaces
  // remains inside both center and radius error limits.
  const float maximum_center_error_squared = maximum_center_error * maximum_center_error;
  std::size_t count = 1U;
  std::size_t anchor = 0U;
  while (anchor + 1U < input.size()) {
    std::size_t accepted = anchor + 1U;
    for (std::size_t candidate = anchor + 2U; candidate < input.size(); ++candidate) {
      bool within_error = true;
      for (std::size_t index = anchor + 1U; index < candidate; ++index) {
        // Preserve every sampled pressure/radius reversal exactly. The numeric
        // radius tolerance may smooth monotonic ramps, but it may not erase a
        // local pen or eraser extremum at high zoom.
        if (radius_extremum(input, index)) {
          within_error = false;
          break;
        }
        float t = 0.0F;
        const float center_error_squared =
            point_segment_distance_squared(input[index], input[anchor], input[candidate], t);
        const float interpolated_radius =
            input[anchor].radius + t * (input[candidate].radius - input[anchor].radius);
        if (center_error_squared > maximum_center_error_squared ||
            std::abs(input[index].radius - interpolated_radius) > maximum_radius_error) {
          within_error = false;
          break;
        }
      }
      if (!within_error) {
        break;
      }
      accepted = candidate;
    }
    if (count >= output.size()) {
      return {};
    }
    output[count++] = input[accepted];
    anchor = accepted;
  }
  return output.first(count);
}

}  // namespace tinydraw
