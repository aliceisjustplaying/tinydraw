#include "tinydraw/ink/ribbon_geometry.h"

#include <cassert>
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

RibbonPrimitive convex(std::array<Point, 4> points, std::uint8_t point_count) {
  return {
      .kind = RibbonPrimitiveKind::kConvex,
      .points = points,
      .point_count = point_count,
  };
}

float cross(Point first, Point second, Point third) {
  return (second.x - first.x) * (third.y - second.y) - (second.y - first.y) * (third.x - second.x);
}

bool is_convex_quad(const std::array<Point, 4>& points) {
  float sign = 0.0F;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const float value = cross(points[index], points[(index + 1U) % points.size()],
                              points[(index + 2U) % points.size()]);
    if (value == 0.0F) {
      continue;
    }
    if (sign == 0.0F) {
      sign = value;
    } else if ((value > 0.0F) != (sign > 0.0F)) {
      return false;
    }
  }
  return true;
}

struct Section {
  Point left;
  Point right;
};

Section section(Point position, float radius, Point direction) {
  const Point offset = multiply(perpendicular(direction), radius);
  return {
      .left = subtract(position, offset),
      .right = add(position, offset),
  };
}

template <typename Emit>
void emit_span(Section start, Section end, Emit emit) {
  const std::array quad{start.left, end.left, end.right, start.right};
  if (is_convex_quad(quad)) {
    emit(convex(quad, 4));
    return;
  }
  emit(convex({start.left, end.left, start.right, {}}, 3));
  emit(convex({start.right, end.left, end.right, {}}, 3));
}

Point blended_direction(Point current, Point next) {
  const float direction_dot = dot(current, next);
  return direction_dot < 0.0F ? current : interpolate(next, current, direction_dot);
}

InkPoint midpoint(InkPoint start, InkPoint end) {
  InkPoint result = end;
  result.position = interpolate(start.position, end.position, 0.5F);
  result.radius = (start.radius + end.radius) * 0.5F;
  return result;
}

InkPoint quadratic_middle(InkPoint start, InkPoint control, InkPoint end) {
  InkPoint result = control;
  result.position = add(multiply(start.position, 0.25F),
                        add(multiply(control.position, 0.5F), multiply(end.position, 0.25F)));
  result.radius = start.radius * 0.25F + control.radius * 0.5F + end.radius * 0.25F;
  return result;
}

Point direction_or(Point start, Point end, Point fallback_start, Point fallback_end) {
  const Point direction = unit(subtract(start, end));
  return equal(direction, {}) ? unit(subtract(fallback_start, fallback_end)) : direction;
}

template <typename Emit>
void emit_quadratic(InkPoint start, InkPoint control, InkPoint end, Emit emit) {
  constexpr float overlap = 0.75F;
  const InkPoint middle = quadratic_middle(start, control, end);
  const Point chord = unit(subtract(start.position, end.position));
  const Point start_direction =
      direction_or(start.position, control.position, start.position, end.position);
  const Point middle_direction = equal(chord, {}) ? start_direction : chord;
  const Point end_direction =
      direction_or(control.position, end.position, start.position, end.position);
  const Section start_section = section(start.position, start.radius, start_direction);
  const Section middle_before =
      section(add(middle.position, multiply(middle_direction, overlap)), middle.radius,
              middle_direction);
  const Section middle_after =
      section(subtract(middle.position, multiply(middle_direction, overlap)), middle.radius,
              middle_direction);
  const Section end_section =
      section(subtract(end.position, multiply(end_direction, overlap)), end.radius, end_direction);
  emit_span(start_section, middle_after, emit);
  emit_span(middle_before, end_section, emit);
}

template <typename Emit>
void emit_tail(InkPoint start, InkPoint end, Emit emit) {
  const Point direction = unit(subtract(start.position, end.position));
  emit_span(section(start.position, start.radius, direction),
            section(end.position, end.radius, direction), emit);
}

}  // namespace

void RibbonPrimitiveBatch::push_back(RibbonPrimitive primitive) {
  assert(count_ < primitives_.size());
  primitives_[count_++] = primitive;
}

RibbonUpdate RibbonStream::append(InkPoint point) {
  RibbonUpdate update;
  if (point_count_ == 0U) {
    first_ = point;
    last_ = point;
    point_count_ = 1U;
    update.provisional.push_back(circle(point.position, point.radius));
    return update;
  }

  if (equal(last_.position, point.position)) {
    if (point_count_ == 1U) {
      update.provisional.push_back(circle(last_.position, last_.radius));
      return update;
    }
    if (first_cap_pending_) {
      update.provisional.push_back(circle(first_.position, first_.radius));
    }
    const Point direction = unit(subtract(prior_.position, last_.position));
    emit_span({.left = tail_left_, .right = tail_right_},
              section(last_.position, last_.radius, direction),
              [&](RibbonPrimitive primitive) { update.provisional.push_back(primitive); });
    update.provisional.push_back(circle(last_.position, last_.radius));
    return update;
  }

  if (point_count_ == 1U) {
    prior_ = last_;
    last_ = point;
    const Point direction = unit(subtract(prior_.position, last_.position));
    const Section start = section(prior_.position, prior_.radius, direction);
    tail_left_ = start.left;
    tail_right_ = start.right;
    point_count_ = 2U;
    first_cap_pending_ = true;

    update.provisional.push_back(circle(first_.position, first_.radius));
    emit_span(start, section(last_.position, last_.radius, direction),
              [&](RibbonPrimitive primitive) { update.provisional.push_back(primitive); });
    update.provisional.push_back(circle(last_.position, last_.radius));
    return update;
  }

  const Point current = unit(subtract(prior_.position, last_.position));
  const Point next = unit(subtract(last_.position, point.position));
  const Section stable_end =
      section(last_.position, last_.radius, blended_direction(current, next));
  if (first_cap_pending_) {
    update.committed.push_back(circle(first_.position, first_.radius));
    first_cap_pending_ = false;
  }
  emit_span({.left = tail_left_, .right = tail_right_}, stable_end,
            [&](RibbonPrimitive primitive) { update.committed.push_back(primitive); });
  update.committed.push_back(circle(last_.position, last_.radius));

  prior_ = last_;
  last_ = point;
  tail_left_ = stable_end.left;
  tail_right_ = stable_end.right;
  ++point_count_;

  const Section provisional_end = section(last_.position, last_.radius, next);
  emit_span(stable_end, provisional_end,
            [&](RibbonPrimitive primitive) { update.provisional.push_back(primitive); });
  update.provisional.push_back(circle(last_.position, last_.radius));
  return update;
}

RibbonUpdate RibbonStream::finish(InkPoint point) {
  RibbonUpdate update = append(point);
  for (const RibbonPrimitive& primitive : update.provisional) {
    update.committed.push_back(primitive);
  }
  update.provisional = {};
  reset();
  return update;
}

void RibbonStream::reset() {
  point_count_ = 0U;
  first_cap_pending_ = false;
}

RibbonUpdate CurvedRibbonStream::append(InkPoint point) {
  RibbonUpdate update;
  if (point_count_ == 0U) {
    first_ = point;
    stable_ = point;
    last_ = point;
    point_count_ = 1U;
    update.provisional.push_back(circle(point.position, point.radius));
    return update;
  }

  if (equal(last_.position, point.position)) {
    if (point_count_ == 1U) {
      update.provisional.push_back(circle(last_.position, last_.radius));
      return update;
    }
    if (first_cap_pending_) {
      update.provisional.push_back(circle(first_.position, first_.radius));
    }
    emit_tail(stable_, last_, [&](RibbonPrimitive primitive) {
      update.provisional.push_back(primitive);
    });
    update.provisional.push_back(circle(last_.position, last_.radius));
    return update;
  }

  if (point_count_ == 1U) {
    last_ = point;
    point_count_ = 2U;
    first_cap_pending_ = true;
    update.provisional.push_back(circle(first_.position, first_.radius));
    emit_tail(stable_, last_, [&](RibbonPrimitive primitive) {
      update.provisional.push_back(primitive);
    });
    update.provisional.push_back(circle(last_.position, last_.radius));
    return update;
  }

  const InkPoint stable_end = midpoint(last_, point);
  if (first_cap_pending_) {
    update.committed.push_back(circle(first_.position, first_.radius));
    first_cap_pending_ = false;
  }
  emit_quadratic(stable_, last_, stable_end, [&](RibbonPrimitive primitive) {
    update.committed.push_back(primitive);
  });

  stable_ = stable_end;
  last_ = point;
  ++point_count_;
  emit_tail(stable_, last_,
            [&](RibbonPrimitive primitive) { update.provisional.push_back(primitive); });
  update.provisional.push_back(circle(last_.position, last_.radius));
  return update;
}

RibbonUpdate CurvedRibbonStream::finish(InkPoint point) {
  RibbonUpdate update = append(point);
  for (const RibbonPrimitive& primitive : update.provisional) {
    update.committed.push_back(primitive);
  }
  update.provisional = {};
  reset();
  return update;
}

void CurvedRibbonStream::reset() {
  point_count_ = 0U;
  first_cap_pending_ = false;
}

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
    const std::array quad{sections[index].left, sections[index + 1U].left,
                          sections[index + 1U].right, sections[index].right};
    if (is_convex_quad(quad)) {
      primitives.push_back(convex(quad, 4));
    } else {
      primitives.push_back(
          convex({sections[index].left, sections[index + 1U].left, sections[index].right, {}}, 3));
      primitives.push_back(convex(
          {sections[index].right, sections[index + 1U].left, sections[index + 1U].right, {}}, 3));
    }
    if (index + 2U < points.size()) {
      primitives.push_back(circle(points[index + 1U].position, points[index + 1U].radius));
    }
  }
  primitives.push_back(circle(points.back().position, points.back().radius));
  return primitives;
}

}  // namespace tinydraw
