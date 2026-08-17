#include "tinydraw/vector_v2/navigation_state.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

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

std::size_t tiled_zoom_index(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k50Percent:
      return 0U;
    case ZoomLevel::k100Percent:
      return 1U;
    case ZoomLevel::k200Percent:
      return 2U;
    case ZoomLevel::k400Percent:
      return 3U;
    case ZoomLevel::k25Percent:
      // Callers exclude the overview; retain a bounds-safe fallback.
      return 0U;
  }
  return 0U;
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
    const std::size_t current_index = tiled_zoom_index(zoom_);
    remembered_origins_[current_index] = origin_;
    remembered_focuses_[current_index] = focus_quarter_world_;
    remembered_valid_[current_index] = true;
  }
  zoom_ = target_zoom;
  if (zoom_ == ZoomLevel::k25Percent) {
    origin_ = {};
    return true;
  }

  const NavigationPoint centered = centered_origin(zoom_, panel_focus);
  const std::size_t target_index = tiled_zoom_index(zoom_);
  const NavigationPoint remembered = remembered_origins_[target_index];
  const NavigationPoint remembered_focus = remembered_focuses_[target_index];
  // A full 25→50→100→200→400 cycle can move the retained focus by up to four
  // quarter-world units through integer zoom conversion. Treat that bounded
  // quantization as the same focus, but reject a stale remembered view whose
  // old focus merely happens to contain the new one near a viewport corner.
  constexpr int kFocusCompatibilityQuarterWorld = 4;
  const bool focus_matches =
      std::abs(remembered_focus.x - focus_quarter_world_.x) <= kFocusCompatibilityQuarterWorld &&
      std::abs(remembered_focus.y - focus_quarter_world_.y) <= kFocusCompatibilityQuarterWorld;
  const int percent = zoom_percent(zoom_);
  const NavigationPoint focus_level{
      .x = rounded_divide(static_cast<std::int64_t>(focus_quarter_world_.x) * percent, 400),
      .y = rounded_divide(static_cast<std::int64_t>(focus_quarter_world_.y) * percent, 400),
  };
  const bool remembered_contains_focus =
      focus_level.x >= remembered.x && focus_level.x < remembered.x + kViewportWidth &&
      focus_level.y >= remembered.y && focus_level.y < remembered.y + kViewportHeight;
  origin_ = remembered_valid_[target_index] && focus_matches && remembered_contains_focus
                ? remembered
                : centered;
  return true;
}

bool NavigationState::set_origin(int x, int y, NavigationPoint panel_focus) {
  if (!valid_panel_focus(panel_focus)) {
    return false;
  }
  origin_ = clamp_origin(zoom_, x, y);
  if (zoom_ != ZoomLevel::k25Percent) {
    focus_quarter_world_ = focus_for_view(zoom_, origin_, panel_focus);
    const std::size_t index = tiled_zoom_index(zoom_);
    remembered_origins_[index] = origin_;
    remembered_focuses_[index] = focus_quarter_world_;
    remembered_valid_[index] = true;
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
