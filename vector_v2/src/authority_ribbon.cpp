#include "tinydraw/vector_v2/authority_ribbon.h"

namespace tinydraw::vector_v2 {
namespace {

bool same_geometry(InkPoint first, InkPoint second) {
  return first.position.x == second.position.x && first.position.y == second.position.y &&
         first.radius == second.radius;
}

InkPoint midpoint(InkPoint first, InkPoint second) {
  InkPoint result = second;
  result.position = {
      .x = (first.position.x + second.position.x) * 0.5F,
      .y = (first.position.y + second.position.y) * 0.5F,
  };
  result.radius = (first.radius + second.radius) * 0.5F;
  return result;
}

InkPoint quadratic_middle(InkPoint start, InkPoint control, InkPoint end) {
  InkPoint result = control;
  result.position = {
      .x = start.position.x * 0.25F + control.position.x * 0.5F + end.position.x * 0.25F,
      .y = start.position.y * 0.25F + control.position.y * 0.5F + end.position.y * 0.25F,
  };
  result.radius = start.radius * 0.25F + control.radius * 0.5F + end.radius * 0.25F;
  return result;
}

}  // namespace

RibbonPrimitive AuthorityRibbonStream::provisional_segment() const {
  if (point_count_ == 1U) {
    return tapered_ribbon_segment(first_, first_);
  }
  return tapered_ribbon_segment(stable_, last_);
}

RibbonUpdate AuthorityRibbonStream::append(InkPoint point, bool provisional_needed) {
  RibbonUpdate update;
  if (point_count_ == 0U) {
    first_ = point;
    stable_ = point;
    last_ = point;
    point_count_ = 1U;
    if (provisional_needed) {
      update.provisional.push_back(provisional_segment());
    }
    return update;
  }

  if (same_geometry(last_, point)) {
    if (provisional_needed) {
      update.provisional.push_back(provisional_segment());
    }
    return update;
  }

  if (point_count_ == 1U) {
    last_ = point;
    point_count_ = 2U;
    if (provisional_needed) {
      update.provisional.push_back(provisional_segment());
    }
    return update;
  }

  const InkPoint stable_end = midpoint(last_, point);
  const InkPoint curve_midpoint = quadratic_middle(stable_, last_, stable_end);
  update.committed.push_back(tapered_ribbon_segment(stable_, curve_midpoint));
  update.committed.push_back(tapered_ribbon_segment(curve_midpoint, stable_end));
  stable_ = stable_end;
  last_ = point;
  ++point_count_;
  if (provisional_needed) {
    update.provisional.push_back(provisional_segment());
  }
  return update;
}

RibbonUpdate AuthorityRibbonStream::finish(InkPoint point) {
  RibbonUpdate update = append(point, true);
  for (const RibbonPrimitive& primitive : update.provisional) {
    update.committed.push_back(primitive);
  }
  update.provisional = {};
  reset();
  return update;
}

void AuthorityRibbonStream::reset() { point_count_ = 0U; }

}  // namespace tinydraw::vector_v2
