#include "tinydraw/ink/ribbon_geometry.h"

#include <cmath>

namespace tinydraw {
namespace {

Point subtract(Point left, Point right) { return {.x = left.x - right.x, .y = left.y - right.y}; }

Point add(Point left, Point right) { return {.x = left.x + right.x, .y = left.y + right.y}; }

Point multiply(Point point, float scalar) { return {.x = point.x * scalar, .y = point.y * scalar}; }

Point interpolate(Point start, Point end, float amount) {
  return {.x = start.x + (end.x - start.x) * amount, .y = start.y + (end.y - start.y) * amount};
}

Point perpendicular(Point point) { return {.x = point.y, .y = -point.x}; }

float dot(Point left, Point right) { return left.x * right.x + left.y * right.y; }

Point unit(Point point) {
  const float length = std::hypot(point.x, point.y);
  return length == 0.0F ? Point{} : Point{.x = point.x / length, .y = point.y / length};
}

bool equal(Point left, Point right) { return left.x == right.x && left.y == right.y; }

RibbonPrimitive circle(Point center, float radius) {
  return {.kind = RibbonPrimitiveKind::kCircle, .center = center, .radius = radius};
}

RibbonPrimitive triangle(Point first, Point second, Point third) {
  return {
      .kind = RibbonPrimitiveKind::kConvex,
      .points = {first, second, third, {}},
      .point_count = 3,
  };
}

struct Section {
  Point left;
  Point right;
};

}  // namespace

std::vector<RibbonPrimitive> build_pf_ribbon(std::span<const InkPoint> input) {
  if (input.empty()) {
    return {};
  }

  std::vector<InkPoint> points;
  points.reserve(input.size());
  for (const InkPoint& point : input) {
    if (points.empty() || !equal(points.back().position, point.position)) {
      points.push_back(point);
    }
  }
  if (points.size() == 1U) {
    return {circle(points.front().position, points.front().radius)};
  }

  std::vector<Point> vectors(points.size());
  for (std::size_t index = 1; index < points.size(); ++index) {
    vectors[index] = unit(subtract(points[index - 1U].position, points[index].position));
  }
  vectors.front() = vectors[1];

  std::vector<Section> sections;
  sections.reserve(points.size());
  for (std::size_t index = 0; index < points.size(); ++index) {
    const bool last = index + 1U == points.size();
    const Point next_vector = last ? vectors[index] : vectors[index + 1U];
    const float direction_dot = last ? 1.0F : dot(vectors[index], next_vector);
    const Point direction = direction_dot < 0.0F
                                ? vectors[index]
                                : interpolate(next_vector, vectors[index], direction_dot);
    const Point offset = multiply(perpendicular(direction), points[index].radius);
    sections.push_back({
        .left = subtract(points[index].position, offset),
        .right = add(points[index].position, offset),
    });
  }

  std::vector<RibbonPrimitive> primitives;
  primitives.reserve(points.size() * 3U);
  primitives.push_back(circle(points.front().position, points.front().radius));
  for (std::size_t index = 0; index + 1U < points.size(); ++index) {
    primitives.push_back(
        triangle(sections[index].left, sections[index + 1U].left, sections[index].right));
    primitives.push_back(
        triangle(sections[index].right, sections[index + 1U].left, sections[index + 1U].right));
    if (index + 2U < points.size() && dot(vectors[index + 1U], vectors[index + 2U]) < 0.0F) {
      primitives.push_back(circle(points[index + 1U].position, points[index + 1U].radius));
    }
  }
  primitives.push_back(circle(points.back().position, points.back().radius));
  return primitives;
}

}  // namespace tinydraw
