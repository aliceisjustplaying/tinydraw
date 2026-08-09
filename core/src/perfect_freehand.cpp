#include "tinydraw/ink/perfect_freehand.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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

Point multiply(Point point, float scalar) { return {.x = point.x * scalar, .y = point.y * scalar}; }

Point perpendicular(Point point) { return {.x = point.y, .y = -point.x}; }

float dot(Point left, Point right) { return left.x * right.x + left.y * right.y; }

float distance_squared(Point left, Point right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  return x * x + y * y;
}

Point unit(Point vector) {
  const float length = std::hypot(vector.x, vector.y);
  if (length == 0.0F) {
    return {.x = 0.0F, .y = 0.0F};
  }
  return {.x = vector.x / length, .y = vector.y / length};
}

Point rotate_around(Point point, Point center, float radians) {
  const float sine = std::sin(radians);
  const float cosine = std::cos(radians);
  const float x = point.x - center.x;
  const float y = point.y - center.y;
  return {.x = x * cosine - y * sine + center.x, .y = x * sine + y * cosine + center.y};
}

float simulate_pressure(float previous, float point_distance, float size) {
  const float speed = std::min(1.0F, point_distance / size);
  const float target = std::min(1.0F, 1.0F - speed);
  return std::min(1.0F, previous + (target - previous) * (speed * 0.275F));
}

float stroke_radius(float size, float thinning, float pressure) {
  return size * (0.5F - thinning * (0.5F - pressure));
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

std::vector<Point> get_stroke_outline(std::span<const StrokePoint> points, const InkConfig& config,
                                      bool /*complete*/) {
  if (points.empty() || config.size <= 0.0F) {
    return {};
  }

  constexpr int kStartCapSegments = 13;
  constexpr int kEndCapSegments = 29;
  constexpr int kCornerCapSegments = 13;
  constexpr float kFixedPi = std::numbers::pi_v<float> + 0.0001F;
  constexpr float kMinimumRadius = 0.01F;

  const float total_length = points.back().running_length;
  const float minimum_distance_squared = std::pow(config.size * config.smoothing, 2.0F);
  std::vector<Point> left_points;
  std::vector<Point> right_points;
  left_points.reserve(points.size() + static_cast<std::size_t>(kCornerCapSegments));
  right_points.reserve(points.size() + static_cast<std::size_t>(kCornerCapSegments));

  float previous_pressure = points.front().pressure;
  const std::size_t initial_count = std::min<std::size_t>(10U, points.size());
  for (std::size_t index = 0; index < initial_count; ++index) {
    float pressure = points[index].pressure;
    if (config.simulate_pressure) {
      pressure = simulate_pressure(previous_pressure, points[index].distance, config.size);
    }
    previous_pressure = (previous_pressure + pressure) * 0.5F;
  }

  float radius = stroke_radius(config.size, config.thinning, points.back().pressure);
  float first_radius = 0.0F;
  bool has_first_radius = false;
  Point previous_vector = points.front().vector;
  Point previous_left = points.front().position;
  Point previous_right = previous_left;
  bool previous_was_sharp = false;

  for (std::size_t index = 0; index < points.size(); ++index) {
    const StrokePoint& current = points[index];
    const bool is_last = index == points.size() - 1U;
    if (!is_last && total_length - current.running_length < 3.0F) {
      continue;
    }

    float pressure = current.pressure;
    if (config.thinning != 0.0F) {
      if (config.simulate_pressure) {
        pressure = simulate_pressure(previous_pressure, current.distance, config.size);
      }
      radius = stroke_radius(config.size, config.thinning, pressure);
    } else {
      radius = config.size * 0.5F;
    }
    if (!has_first_radius) {
      first_radius = radius;
      has_first_radius = true;
    }
    radius = std::max(kMinimumRadius, radius);

    const Point next_vector = is_last ? current.vector : points[index + 1U].vector;
    const float next_dot = is_last ? 1.0F : dot(current.vector, next_vector);
    const float previous_dot = dot(current.vector, previous_vector);
    const bool is_sharp = previous_dot < 0.0F && !previous_was_sharp;
    const bool next_is_sharp = next_dot < 0.0F;

    if (is_sharp || next_is_sharp) {
      const Point offset = multiply(perpendicular(previous_vector), radius);
      Point temporary_left{};
      Point temporary_right{};
      for (int segment = 0; segment <= kCornerCapSegments; ++segment) {
        const float amount = static_cast<float>(segment) / static_cast<float>(kCornerCapSegments);
        temporary_left =
            rotate_around(subtract(current.position, offset), current.position, kFixedPi * amount);
        temporary_right =
            rotate_around(add(current.position, offset), current.position, -kFixedPi * amount);
        left_points.push_back(temporary_left);
        right_points.push_back(temporary_right);
      }
      previous_left = temporary_left;
      previous_right = temporary_right;
      if (next_is_sharp) {
        previous_was_sharp = true;
      }
      continue;
    }

    previous_was_sharp = false;
    if (is_last) {
      const Point offset = multiply(perpendicular(current.vector), radius);
      left_points.push_back(subtract(current.position, offset));
      right_points.push_back(add(current.position, offset));
      continue;
    }

    const Point offset =
        multiply(perpendicular(interpolate(next_vector, current.vector, next_dot)), radius);
    const Point temporary_left = subtract(current.position, offset);
    if (index <= 1U || distance_squared(previous_left, temporary_left) > minimum_distance_squared) {
      left_points.push_back(temporary_left);
      previous_left = temporary_left;
    }
    const Point temporary_right = add(current.position, offset);
    if (index <= 1U ||
        distance_squared(previous_right, temporary_right) > minimum_distance_squared) {
      right_points.push_back(temporary_right);
      previous_right = temporary_right;
    }

    previous_pressure = pressure;
    previous_vector = current.vector;
  }

  if (points.size() == 1U) {
    const Point center = points.front().position;
    const Point offset_point = add(center, kUnitOffset);
    const Point start =
        add(center, multiply(unit(perpendicular(subtract(center, offset_point))), -first_radius));
    std::vector<Point> dot_points;
    dot_points.reserve(kStartCapSegments);
    for (int segment = 1; segment <= kStartCapSegments; ++segment) {
      const float amount = static_cast<float>(segment) / static_cast<float>(kStartCapSegments);
      dot_points.push_back(rotate_around(start, center, kFixedPi * 2.0F * amount));
    }
    return dot_points;
  }

  std::vector<Point> outline;
  outline.reserve(left_points.size() + right_points.size() +
                  static_cast<std::size_t>(kStartCapSegments + kEndCapSegments));
  outline.insert(outline.end(), left_points.begin(), left_points.end());

  const Point last = points.back().position;
  const Point direction = perpendicular(multiply(points.back().vector, -1.0F));
  const Point end_start = add(last, multiply(direction, radius));
  // The upstream floating-point loop includes an amount extremely close to 1.0.
  for (int segment = 1; segment <= kEndCapSegments; ++segment) {
    const float amount = static_cast<float>(segment) / static_cast<float>(kEndCapSegments);
    outline.push_back(rotate_around(end_start, last, kFixedPi * 3.0F * amount));
  }

  for (auto iterator = right_points.rbegin(); iterator != right_points.rend(); ++iterator) {
    outline.push_back(*iterator);
  }

  const Point first = points.front().position;
  const Point right = right_points.front();
  for (int segment = 1; segment <= kStartCapSegments; ++segment) {
    const float amount = static_cast<float>(segment) / static_cast<float>(kStartCapSegments);
    outline.push_back(rotate_around(right, first, kFixedPi * amount));
  }
  return outline;
}

std::vector<Point> get_stroke(std::span<const Point> input, const InkConfig& config,
                              bool complete) {
  const auto points = get_stroke_points(input, config, complete);
  return get_stroke_outline(points, config, complete);
}

}  // namespace tinydraw::perfect_freehand
