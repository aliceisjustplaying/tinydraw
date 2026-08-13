#ifndef TINYDRAW_PRODUCTION_FRAME_SCROLLER_H
#define TINYDRAW_PRODUCTION_FRAME_SCROLLER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/materialized_canvas.h"

namespace tinydraw::production {

struct FrameScrollResult {
  std::array<PixelRect, 2> exposed{};
  std::size_t exposed_count = 0;
};

// Reuses the overlap between two views by moving pixels in place. delta_x and
// delta_y are new view origin minus old view origin. The returned rectangles
// partition the newly exposed area without overlap. Pixels outside area are
// untouched.
[[nodiscard]] std::optional<FrameScrollResult> scroll_frame(std::span<std::uint16_t> frame,
                                                            int stride, PixelRect area, int delta_x,
                                                            int delta_y);

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_FRAME_SCROLLER_H
