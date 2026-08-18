#include "tinydraw/vector_v2/idle_repair.h"

#include <algorithm>
#include <cstdlib>

#include "tinydraw/vector_v2/navigation_state.h"

namespace tinydraw::vector_v2 {

namespace {

// Level dimensions double per tiled zoom step: 25% shows the whole world in
// one panel, 400% is 16x.
constexpr int level_shift(ZoomLevel zoom) { return static_cast<int>(zoom); }

void append_view(IdleRepairPlan& plan, ZoomLevel zoom, int x, int y,
                 const ViewRequest* excluded = nullptr) {
  const auto origin = NavigationState::clamp_origin(zoom, x, y);
  const ViewRequest view{
      .zoom = zoom,
      .level_pixels = {origin.x, origin.y, origin.x + kOverviewWidth, origin.y + kOverviewHeight},
  };
  const auto begin = plan.views.begin();
  const auto end = begin + static_cast<std::ptrdiff_t>(plan.count);
  if (excluded != nullptr && excluded->zoom == view.zoom &&
      excluded->level_pixels.x0 == view.level_pixels.x0 &&
      excluded->level_pixels.y0 == view.level_pixels.y0) {
    return;
  }
  const bool duplicate = std::any_of(begin, end, [&](const ViewRequest& existing) {
    return existing.zoom == view.zoom && existing.level_pixels.x0 == view.level_pixels.x0 &&
           existing.level_pixels.y0 == view.level_pixels.y0;
  });
  if (!duplicate && plan.count < plan.views.size()) {
    plan.views[plan.count++] = view;
  }
}

}  // namespace

IdleRepairPlan plan_idle_repair(const ViewRequest& active_view,
                                std::span<const ViewFootprint> remembered,
                                IdleRepairPanDelta last_pan) {
  IdleRepairPlan plan{};
  if (active_view.zoom == ZoomLevel::k25Percent) {
    // The 25% view composes from overview authority and has no tiles to
    // repair; other zooms are covered when the user is actually in them.
    return plan;
  }
  const int x = active_view.level_pixels.x0;
  const int y = active_view.level_pixels.y0;
  if (last_pan.x == 0 && last_pan.y == 0) {
    append_view(plan, active_view.zoom, x - kOverviewWidth, y, &active_view);
    append_view(plan, active_view.zoom, x + kOverviewWidth, y, &active_view);
    append_view(plan, active_view.zoom, x, y - kOverviewHeight, &active_view);
    append_view(plan, active_view.zoom, x, y + kOverviewHeight, &active_view);
  } else if (std::abs(static_cast<std::int64_t>(last_pan.x)) >=
             std::abs(static_cast<std::int64_t>(last_pan.y))) {
    const int direction = last_pan.x > 0 ? 1 : -1;
    append_view(plan, active_view.zoom, x + direction * kOverviewWidth, y, &active_view);
    append_view(plan, active_view.zoom, x - direction * kTileWidth, y, &active_view);
    append_view(plan, active_view.zoom, x, y - kTileHeight, &active_view);
    append_view(plan, active_view.zoom, x, y + kTileHeight, &active_view);
  } else {
    const int direction = last_pan.y > 0 ? 1 : -1;
    append_view(plan, active_view.zoom, x, y + direction * kOverviewHeight, &active_view);
    append_view(plan, active_view.zoom, x, y - direction * kTileHeight, &active_view);
    append_view(plan, active_view.zoom, x - kTileWidth, y, &active_view);
    append_view(plan, active_view.zoom, x + kTileWidth, y, &active_view);
  }
  for (const ViewFootprint& footprint : remembered) {
    if (footprint.valid && footprint.zoom != active_view.zoom) {
      append_view(plan, footprint.zoom, footprint.level_pixels.x0, footprint.level_pixels.y0);
    }
  }
  plan.grid_start = plan.count;
  if (active_view.zoom == ZoomLevel::k100Percent) {
    const int level_width = kOverviewWidth << level_shift(ZoomLevel::k100Percent);
    const int level_height = kOverviewHeight << level_shift(ZoomLevel::k100Percent);
    for (int grid_y = 0; grid_y < level_height; grid_y += kOverviewHeight) {
      for (int grid_x = 0; grid_x < level_width; grid_x += kOverviewWidth) {
        append_view(plan, ZoomLevel::k100Percent, grid_x, grid_y);
      }
    }
  }
  return plan;
}

}  // namespace tinydraw::vector_v2
