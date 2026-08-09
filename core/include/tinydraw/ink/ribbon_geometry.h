#pragma once

#include <array>
#include <span>
#include <vector>

#include "tinydraw/ink/ink_stream.h"

namespace tinydraw {

enum class RibbonPrimitiveKind { kConvex, kCircle };

struct RibbonPrimitive {
  RibbonPrimitiveKind kind = RibbonPrimitiveKind::kConvex;
  std::array<Point, 4> points{};
  int point_count = 0;
  Point center{};
  float radius = 0.0F;
};

// Build simple unionable pieces rather than one self-intersecting outline.
// The stream has already supplied dt-adapted positions, pressure, and radii.
[[nodiscard]] std::vector<RibbonPrimitive> build_pf_ribbon(std::span<const InkPoint> points);

}  // namespace tinydraw
