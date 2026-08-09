#include "tinydraw/ink/perfect_freehand.h"

#include <cmath>

namespace tinydraw::perfect_freehand {
namespace {

constexpr Point kUnitOffset{.x = 1.0F, .y = 1.0F};
constexpr float kDefaultFirstPressure = 0.25F;
constexpr float kDefaultPressure = 0.5F;

Point add(Point left, Point right) { return {.x = left.x + right.x, .y = left.y + right.y}; }

Point subtract(Point left, Point right) { return {.x = left.x - right.x, .y = left.y - right.y}; }

Point interpolate(Point start, Point end, float amount) {
  return {.x = start.x + (end.x - start.x) * amount, .y = start.y + (end.y - start.y) * amount};
}

float distance(Point left, Point right) { return std::hypot(left.x - right.x, left.y - right.y); }

bool equal(Point left, Point right) { return left.x == right.x && left.y == right.y; }

Point unit(Point vector) {
  const float length = std::hypot(vector.x, vector.y);
  if (length == 0.0F) {
    return {.x = 0.0F, .y = 0.0F};
  }
  return {.x = vector.x / length, .y = vector.y / length};
}

}  // namespace

std::vector<StrokePoint> get_stroke_points(std::span<const Point> input, const InkConfig& config,
                                           bool complete) {
  if (input.empty()) {
    return {};
  }

  std::vector<Point> expanded(input.begin(), input.end());
  if (expanded.size() == 2U) {
    const Point first = expanded.front();
    const Point last = expanded.back();
    expanded.resize(1U);
    for (int index = 1; index < 5; ++index) {
      expanded.push_back(interpolate(first, last, static_cast<float>(index) / 4.0F));
    }
  } else if (expanded.size() == 1U) {
    expanded.push_back(add(expanded.front(), kUnitOffset));
  }

  const float interpolation = 0.15F + (1.0F - config.streamline) * 0.85F;
  std::vector<StrokePoint> result;
  result.reserve(expanded.size());
  result.push_back({
      .position = expanded.front(),
      .pressure = kDefaultFirstPressure,
      .vector = kUnitOffset,
      .distance = 0.0F,
      .running_length = 0.0F,
  });

  bool reached_minimum_length = false;
  float running_length = 0.0F;
  const std::size_t last_index = expanded.size() - 1U;
  for (std::size_t index = 1; index < expanded.size(); ++index) {
    const StrokePoint& previous = result.back();
    const Point point = complete && index == last_index
                            ? expanded[index]
                            : interpolate(previous.position, expanded[index], interpolation);
    if (equal(previous.position, point)) {
      continue;
    }

    const float point_distance = distance(point, previous.position);
    running_length += point_distance;
    if (index < last_index && !reached_minimum_length) {
      if (running_length < config.size) {
        continue;
      }
      reached_minimum_length = true;
    }

    result.push_back({
        .position = point,
        .pressure = kDefaultPressure,
        .vector = unit(subtract(previous.position, point)),
        .distance = point_distance,
        .running_length = running_length,
    });
  }

  result.front().vector = result.size() > 1U ? result[1].vector : Point{};
  return result;
}

std::vector<Point> get_stroke_outline(std::span<const StrokePoint>, const InkConfig&, bool) {
  return {};
}

std::vector<Point> get_stroke(std::span<const Point> input, const InkConfig& config,
                              bool complete) {
  const auto points = get_stroke_points(input, config, complete);
  return get_stroke_outline(points, config, complete);
}

}  // namespace tinydraw::perfect_freehand
