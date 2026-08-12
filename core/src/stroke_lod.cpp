#include "tinydraw/graphics/stroke_lod.h"

#include <cmath>

namespace tinydraw {

std::span<StrokeSample> simplify_stroke_samples(std::span<const StrokeSample> input,
                                                std::span<StrokeSample> output,
                                                float minimum_distance,
                                                float maximum_radius_delta) {
  if (input.empty()) {
    return output.first(0U);
  }
  if (output.empty() || !std::isfinite(minimum_distance) || minimum_distance < 0.0F ||
      !std::isfinite(maximum_radius_delta) || maximum_radius_delta < 0.0F) {
    return {};
  }

  output[0] = input.front();
  std::size_t count = 1U;
  const float minimum_distance_squared = minimum_distance * minimum_distance;
  for (std::size_t index = 1U; index + 1U < input.size(); ++index) {
    const StrokeSample& candidate = input[index];
    const StrokeSample& retained = output[count - 1U];
    const float delta_x = candidate.x - retained.x;
    const float delta_y = candidate.y - retained.y;
    if (delta_x * delta_x + delta_y * delta_y < minimum_distance_squared &&
        std::abs(candidate.radius - retained.radius) <= maximum_radius_delta) {
      continue;
    }
    if (count >= output.size()) {
      return {};
    }
    output[count++] = candidate;
  }

  if (input.size() > 1U) {
    if (count >= output.size()) {
      return {};
    }
    output[count++] = input.back();
  }
  return output.first(count);
}

}  // namespace tinydraw
