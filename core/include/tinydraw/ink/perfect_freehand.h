#pragma once

#include <span>
#include <vector>

#include "tinydraw/geometry.h"
#include "tinydraw/ink_config.h"

namespace tinydraw::perfect_freehand {

struct StrokePoint {
  Point position;
  float pressure;
  Point vector;
  float distance;
  float running_length;
};

// Behavioral baseline matching the pinned perfect-freehand implementation.
// This batch interface is suitable for reference tests and stroke finalization,
// not for recomputing an active stroke after every input sample.
[[nodiscard]] std::vector<StrokePoint> get_stroke_points(std::span<const Point> input,
                                                         const InkConfig& config, bool complete);

[[nodiscard]] std::vector<Point> get_stroke_outline(std::span<const StrokePoint> points,
                                                    const InkConfig& config, bool complete);

[[nodiscard]] std::vector<Point> get_stroke(std::span<const Point> input, const InkConfig& config,
                                            bool complete);

}  // namespace tinydraw::perfect_freehand
