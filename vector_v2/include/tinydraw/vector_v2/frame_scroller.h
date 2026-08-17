#ifndef TINYDRAW_VECTOR_V2_FRAME_SCROLLER_H
#define TINYDRAW_VECTOR_V2_FRAME_SCROLLER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

struct FrameScrollResult {
  std::array<PixelRect, 2> exposed{};
  std::size_t exposed_count = 0;
};

// Toroidal frame addressing: pan advances the ring origin instead of moving
// pixels, and consumers de-rotate while they copy (the panel byte-swap
// already touches every pixel, so de-rotation there is free). Panel
// coordinate (x, y) inside the ring area lives at buffer coordinate
// (ring_column(x), ring_row(y)).
struct RingFrame {
  int shift_x = 0;
  int shift_y = 0;

  [[nodiscard]] bool active() const { return shift_x != 0 || shift_y != 0; }
};

// Advances the ring origin by the pan delta and returns the same exposed
// panel-rectangle partition without touching pixels. Fails for deltas at or
// beyond the area extent.
[[nodiscard]] std::optional<FrameScrollResult> ring_scroll(RingFrame& ring, PixelRect area,
                                                           int delta_x, int delta_y);

[[nodiscard]] constexpr int ring_row(const RingFrame& ring, PixelRect area, int panel_y) {
  const int height = area.y1 - area.y0;
  const int offset = (panel_y - area.y0 + ring.shift_y) % height;
  return area.y0 + (offset < 0 ? offset + height : offset);
}

[[nodiscard]] constexpr int ring_column(const RingFrame& ring, PixelRect area, int panel_x) {
  const int width = area.x1 - area.x0;
  const int offset = (panel_x - area.x0 + ring.shift_x) % width;
  return area.x0 + (offset < 0 ? offset + width : offset);
}

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_FRAME_SCROLLER_H
