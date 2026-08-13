#include "tinydraw/vector_v2/navigation_state.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace tinydraw::vector_v2 {
namespace {

constexpr int kViewportWidth = kOverviewWidth;
constexpr int kViewportHeight = kOverviewHeight;
constexpr std::array kTiledZooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent,
                                 ZoomLevel::k200Percent, ZoomLevel::k400Percent};

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

std::size_t NavigationState::tiled_index(ZoomLevel zoom) {
  const auto found = std::find(kTiledZooms.begin(), kTiledZooms.end(), zoom);
  return static_cast<std::size_t>(found - kTiledZooms.begin());
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

bool NavigationState::contains_focus(ZoomLevel zoom, NavigationPoint origin,
                                     NavigationPoint focus_quarter_world) {
  const int percent = zoom_percent(zoom);
  const int focus_x =
      rounded_divide(static_cast<std::int64_t>(focus_quarter_world.x) * percent, 400);
  const int focus_y =
      rounded_divide(static_cast<std::int64_t>(focus_quarter_world.y) * percent, 400);
  return focus_x >= origin.x && focus_y >= origin.y && focus_x < origin.x + kViewportWidth &&
         focus_y < origin.y + kViewportHeight;
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

void NavigationState::remember_current() {
  if (zoom_ != ZoomLevel::k25Percent) {
    remembered_[tiled_index(zoom_)] = {.origin = origin_, .valid = true};
  }
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
  remember_current();
  zoom_ = target_zoom;
  if (zoom_ == ZoomLevel::k25Percent) {
    origin_ = {};
    return true;
  }
  const RememberedOrigin remembered = remembered_[tiled_index(zoom_)];
  origin_ = remembered.valid && contains_focus(zoom_, remembered.origin, focus_quarter_world_)
                ? remembered.origin
                : centered_origin(zoom_, panel_focus);
  remember_current();
  return true;
}

bool NavigationState::set_origin(int x, int y, NavigationPoint panel_focus) {
  if (!valid_panel_focus(panel_focus)) {
    return false;
  }
  origin_ = clamp_origin(zoom_, x, y);
  if (zoom_ != ZoomLevel::k25Percent) {
    focus_quarter_world_ = focus_for_view(zoom_, origin_, panel_focus);
    remember_current();
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
