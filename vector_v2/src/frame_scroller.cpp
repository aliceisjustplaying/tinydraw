#include "tinydraw/vector_v2/frame_scroller.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

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

std::optional<FrameScrollResult> scroll_frame(std::span<std::uint16_t> frame, int stride,
                                              PixelRect area, int delta_x, int delta_y) {
  const int area_width = area.x1 - area.x0;
  const int area_height = area.y1 - area.y0;
  const std::size_t required =
      area.x0 >= 0 && area.y0 >= 0 && area_width > 0 && area_height > 0 && stride >= area.x1
          ? static_cast<std::size_t>(area.y1 - 1) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(area.x1)
          : 0U;
  if (required == 0U || frame.size() < required || std::abs(delta_x) >= area_width ||
      std::abs(delta_y) >= area_height) {
    return std::nullopt;
  }

  const int copy_width = area_width - std::abs(delta_x);
  const int copy_height = area_height - std::abs(delta_y);
  const int source_x = area.x0 + std::max(delta_x, 0);
  const int source_y = area.y0 + std::max(delta_y, 0);
  const int destination_x = area.x0 + std::max(-delta_x, 0);
  const int destination_y = area.y0 + std::max(-delta_y, 0);
  const int first_row = delta_y > 0 ? 0 : copy_height - 1;
  const int end_row = delta_y > 0 ? copy_height : -1;
  const int row_step = delta_y > 0 ? 1 : -1;
  for (int row = first_row; row != end_row; row += row_step) {
    const auto source =
        frame.begin() + static_cast<std::ptrdiff_t>(source_y + row) * stride + source_x;
    auto destination =
        frame.begin() + static_cast<std::ptrdiff_t>(destination_y + row) * stride + destination_x;
    std::memmove(std::to_address(destination), std::to_address(source),
                 static_cast<std::size_t>(copy_width) * sizeof(std::uint16_t));
  }

  return exposed_partition(area, delta_x, delta_y);
}

}  // namespace tinydraw::vector_v2
