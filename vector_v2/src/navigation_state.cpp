#include "tinydraw/vector_v2/navigation_state.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace tinydraw::vector_v2 {
namespace {

constexpr int kViewportWidth = kOverviewWidth;
constexpr int kViewportHeight = kOverviewHeight;
int level_extent(int world_extent, ZoomLevel zoom) {
  return world_extent * zoom_percent(zoom) / 100;
}

int rounded_divide(std::int64_t numerator, int denominator) {
  return static_cast<int>((numerator + denominator / 2) / denominator);
}

}  // namespace

NavigationState::NavigationState() = default;

ZoomLevel NavigationState::zoom() const { return zoom_; }

NavigationPoint NavigationState::origin() const { return origin_; }

NavigationPoint NavigationState::focus_quarter_world() const { return focus_quarter_world_; }

ViewRequest NavigationState::view() const {
  return {.zoom = zoom_,
          .level_pixels = {origin_.x, origin_.y, origin_.x + kViewportWidth,
                           origin_.y + kViewportHeight}};
}

NavigationExtent NavigationState::extent() const {
  if (zoom_ == ZoomLevel::k25Percent) {
    return {};
  }
  const int maximum_x = level_extent(kWorldWidth, zoom_) - kViewportWidth;
  const int maximum_y = level_extent(kWorldHeight, zoom_) - kViewportHeight;
  return {.top = origin_.y > 0,
          .left = origin_.x > 0,
          .right = origin_.x < maximum_x,
          .bottom = origin_.y < maximum_y};
}

bool NavigationState::valid_panel_focus(NavigationPoint point) {
  return point.x >= 0 && point.y >= 0 && point.x < kViewportWidth && point.y < kViewportHeight;
}

NavigationPoint NavigationState::clamp_origin(ZoomLevel zoom, int x, int y) {
  if (zoom == ZoomLevel::k25Percent) {
    return {};
  }
  return {
      .x = std::clamp(x, 0, level_extent(kWorldWidth, zoom) - kViewportWidth),
      .y = std::clamp(y, 0, level_extent(kWorldHeight, zoom) - kViewportHeight),
  };
}

NavigationPoint NavigationState::focus_for_view(ZoomLevel zoom, NavigationPoint origin,
                                                NavigationPoint panel_focus) {
  const int percent = zoom_percent(zoom);
  return {
      .x = rounded_divide(static_cast<std::int64_t>(origin.x + panel_focus.x) * 400, percent),
      .y = rounded_divide(static_cast<std::int64_t>(origin.y + panel_focus.y) * 400, percent),
  };
}

NavigationPoint NavigationState::centered_origin(ZoomLevel target_zoom,
                                                 NavigationPoint panel_focus) const {
  const int percent = zoom_percent(target_zoom);
  return clamp_origin(
      target_zoom,
      rounded_divide(static_cast<std::int64_t>(focus_quarter_world_.x) * percent, 400) -
          panel_focus.x,
      rounded_divide(static_cast<std::int64_t>(focus_quarter_world_.y) * percent, 400) -
          panel_focus.y);
}

bool NavigationState::set_zoom(ZoomLevel target_zoom, NavigationPoint panel_focus) {
  if (!valid_panel_focus(panel_focus)) {
    return false;
  }
  if (target_zoom == zoom_) {
    return true;
  }
  if (zoom_ != ZoomLevel::k25Percent) {
    focus_quarter_world_ = focus_for_view(zoom_, origin_, panel_focus);
  }
  zoom_ = target_zoom;
  if (zoom_ == ZoomLevel::k25Percent) {
    origin_ = {};
    return true;
  }
  // Zoom always centers the retained world focus at the panel focus. Exact
  // same-zoom round trips through 25% fall out of the focus math (the focus
  // derives from origin plus panel focus, so re-centering reproduces the
  // origin bit-for-bit away from level edges). A remembered-origin reuse
  // used to live here and made the zoom button land the focused point in a
  // stale view's corner instead of the center.
  origin_ = centered_origin(zoom_, panel_focus);
  return true;
}

bool NavigationState::set_origin(int x, int y, NavigationPoint panel_focus) {
  if (!valid_panel_focus(panel_focus)) {
    return false;
  }
  origin_ = clamp_origin(zoom_, x, y);
  if (zoom_ != ZoomLevel::k25Percent) {
    focus_quarter_world_ = focus_for_view(zoom_, origin_, panel_focus);
  }
  return true;
}

ZoomLevel next_zoom(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return ZoomLevel::k50Percent;
    case ZoomLevel::k50Percent:
      return ZoomLevel::k100Percent;
    case ZoomLevel::k100Percent:
      return ZoomLevel::k200Percent;
    case ZoomLevel::k200Percent:
    case ZoomLevel::k400Percent:
      return ZoomLevel::k400Percent;
  }
  return ZoomLevel::k400Percent;
}

ZoomLevel previous_zoom(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
    case ZoomLevel::k50Percent:
      return ZoomLevel::k25Percent;
    case ZoomLevel::k100Percent:
      return ZoomLevel::k50Percent;
    case ZoomLevel::k200Percent:
      return ZoomLevel::k100Percent;
    case ZoomLevel::k400Percent:
      return ZoomLevel::k200Percent;
  }
  return ZoomLevel::k25Percent;
}

}  // namespace tinydraw::vector_v2
