#pragma once

#include "tinydraw/geometry.h"

namespace tinydraw {

// Transformation order is swap, mirror, then scale into logical canvas space.
// Raw dimensions describe inclusive coordinates [0, width - 1] × [0, height - 1].
struct TouchTransform {
  bool swap_xy = false;
  bool mirror_x = false;
  bool mirror_y = false;
};

[[nodiscard]] Point touch_to_logical(Point raw, int raw_width, int raw_height,
                                     TouchTransform transform);

[[nodiscard]] constexpr Rect logical_to_panel(Rect logical, PanelGeometry panel) {
  return {
      .x0 = logical.x0 + panel.x_offset,
      .y0 = logical.y0 + panel.y_offset,
      .x1 = logical.x1 + panel.x_offset,
      .y1 = logical.y1 + panel.y_offset,
  };
}

}  // namespace tinydraw
