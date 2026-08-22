#include "tinydraw/vector_v2/frame_scroller.h"

#include <algorithm>

namespace tinydraw::vector_v2 {
namespace {

// ring_scroll keeps shifts normalized and accepts only deltas strictly inside
// the extent, so advancing can cross at most one seam. Splitting by direction
// also keeps the addition defined for every supported int-sized extent.
int advance_ring_shift(int shift, int delta, int extent) {
  unsigned advanced = static_cast<unsigned>(shift) + static_cast<unsigned>(delta);
  if (delta < 0) {
    if (shift < -delta) {
      advanced += static_cast<unsigned>(extent);
    }
  } else if (advanced >= static_cast<unsigned>(extent)) {
    advanced -= static_cast<unsigned>(extent);
  }
  return static_cast<int>(advanced);
}

unsigned magnitude(int value) {
  const unsigned bits = static_cast<unsigned>(value);
  return value < 0 ? 0U - bits : bits;
}

std::optional<FrameScrollResult> exposed_partition(PixelRect area, int delta_x, int delta_y) {
  std::optional<FrameScrollResult> result{std::in_place};
  std::size_t exposed_count = 0;
  if (delta_x != 0) {
    result->exposed[exposed_count++] =
        delta_x > 0 ? PixelRect{area.x1 - delta_x, area.y0, area.x1, area.y1}
                    : PixelRect{area.x0, area.y0, area.x0 - delta_x, area.y1};
  }
  if (delta_y != 0) {
    const int overlap_x0 = area.x0 + std::max(-delta_x, 0);
    const int overlap_x1 = area.x1 - std::max(delta_x, 0);
    result->exposed[exposed_count++] =
        delta_y > 0 ? PixelRect{overlap_x0, area.y1 - delta_y, overlap_x1, area.y1}
                    : PixelRect{overlap_x0, area.y0, overlap_x1, area.y0 - delta_y};
  }
  result->exposed_count = exposed_count;
  return result;
}

}  // namespace

std::optional<FrameScrollResult> ring_scroll(RingFrame& ring, PixelRect area, int delta_x,
                                             int delta_y) {
  const int area_width = area.x1 - area.x0;
  const int area_height = area.y1 - area.y0;
  if (area_width <= 0 || area_height <= 0 ||
      magnitude(delta_x) >= static_cast<unsigned>(area_width) ||
      magnitude(delta_y) >= static_cast<unsigned>(area_height)) {
    return std::nullopt;
  }
  ring.shift_x = advance_ring_shift(ring.shift_x, delta_x, area_width);
  ring.shift_y = advance_ring_shift(ring.shift_y, delta_y, area_height);

  return exposed_partition(area, delta_x, delta_y);
}

}  // namespace tinydraw::vector_v2
