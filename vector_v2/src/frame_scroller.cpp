#include "tinydraw/vector_v2/frame_scroller.h"

#include <algorithm>
#include <cstdlib>

namespace tinydraw::vector_v2 {
namespace {

FrameScrollResult exposed_partition(PixelRect area, int delta_x, int delta_y) {
  FrameScrollResult result{};
  if (delta_x != 0) {
    result.exposed[result.exposed_count++] =
        delta_x > 0 ? PixelRect{area.x1 - delta_x, area.y0, area.x1, area.y1}
                    : PixelRect{area.x0, area.y0, area.x0 - delta_x, area.y1};
  }
  if (delta_y != 0) {
    const int overlap_x0 = area.x0 + std::max(-delta_x, 0);
    const int overlap_x1 = area.x1 - std::max(delta_x, 0);
    result.exposed[result.exposed_count++] =
        delta_y > 0 ? PixelRect{overlap_x0, area.y1 - delta_y, overlap_x1, area.y1}
                    : PixelRect{overlap_x0, area.y0, overlap_x1, area.y0 - delta_y};
  }
  return result;
}

}  // namespace

std::optional<FrameScrollResult> ring_scroll(RingFrame& ring, PixelRect area, int delta_x,
                                             int delta_y) {
  const int area_width = area.x1 - area.x0;
  const int area_height = area.y1 - area.y0;
  if (area_width <= 0 || area_height <= 0 || std::abs(delta_x) >= area_width ||
      std::abs(delta_y) >= area_height) {
    return std::nullopt;
  }
  ring.shift_x = ((ring.shift_x + delta_x) % area_width + area_width) % area_width;
  ring.shift_y = ((ring.shift_y + delta_y) % area_height + area_height) % area_height;
  return exposed_partition(area, delta_x, delta_y);
}

}  // namespace tinydraw::vector_v2
