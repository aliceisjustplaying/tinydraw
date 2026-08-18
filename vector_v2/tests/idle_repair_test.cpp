#include "tinydraw/vector_v2/idle_repair.h"

#include <doctest.h>

#include <algorithm>

#include "tinydraw/vector_v2/navigation_state.h"

namespace {

using tinydraw::vector_v2::IdleRepairPanDelta;
using tinydraw::vector_v2::IdleRepairPlan;
using tinydraw::vector_v2::kOverviewHeight;
using tinydraw::vector_v2::kOverviewWidth;
using tinydraw::vector_v2::PixelRect;
using tinydraw::vector_v2::plan_idle_repair;
using tinydraw::vector_v2::ViewFootprint;
using tinydraw::vector_v2::ViewRequest;
using tinydraw::vector_v2::ZoomLevel;

ViewRequest view_at(ZoomLevel zoom, int x, int y) {
  return {.zoom = zoom, .level_pixels = {x, y, x + kOverviewWidth, y + kOverviewHeight}};
}

bool contains(const IdleRepairPlan& plan, ZoomLevel zoom, int x, int y) {
  return std::any_of(
      plan.views.begin(), plan.views.begin() + plan.count, [&](const ViewRequest& view) {
        return view.zoom == zoom && view.level_pixels.x0 == x && view.level_pixels.y0 == y;
      });
}

bool all_views_valid(const IdleRepairPlan& plan) {
  for (std::size_t index = 0; index < plan.count; ++index) {
    const ViewRequest& view = plan.views[index];
    const int level_width = kOverviewWidth << static_cast<int>(view.zoom);
    const int level_height = kOverviewHeight << static_cast<int>(view.zoom);
    if (view.level_pixels.x1 - view.level_pixels.x0 != kOverviewWidth ||
        view.level_pixels.y1 - view.level_pixels.y0 != kOverviewHeight ||
        view.level_pixels.x0 < 0 || view.level_pixels.y0 < 0 ||
        view.level_pixels.x1 > level_width || view.level_pixels.y1 > level_height) {
      return false;
    }
  }
  return true;
}

bool all_views_unique(const IdleRepairPlan& plan) {
  for (std::size_t left = 0; left < plan.count; ++left) {
    for (std::size_t right = left + 1; right < plan.count; ++right) {
      const auto& a = plan.views[left];
      const auto& b = plan.views[right];
      if (a.zoom == b.zoom && a.level_pixels.x0 == b.level_pixels.x0 &&
          a.level_pixels.y0 == b.level_pixels.y0) {
        return false;
      }
    }
  }
  return true;
}

TEST_CASE("idle repair plans nothing at 25 percent") {
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k25Percent, 0, 0), {});
  CHECK(plan.count == 0);
}

TEST_CASE("idle repair at 400 percent covers the cardinal neighborhood") {
  // Center of the 5888x7168 level: all four neighbors are distinct.
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k400Percent, 2760, 3360), {});
  CHECK(plan.count == 4);
  CHECK(!contains(plan, ZoomLevel::k400Percent, 2760, 3360));
  CHECK(contains(plan, ZoomLevel::k400Percent, 2760 - kOverviewWidth, 3360));
  CHECK(contains(plan, ZoomLevel::k400Percent, 2760 + kOverviewWidth, 3360));
  CHECK(contains(plan, ZoomLevel::k400Percent, 2760, 3360 - kOverviewHeight));
  CHECK(contains(plan, ZoomLevel::k400Percent, 2760, 3360 + kOverviewHeight));
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair prioritizes continued horizontal travel and short runways") {
  const auto active = view_at(ZoomLevel::k400Percent, 2760, 3360);
  const auto plan = plan_idle_repair(active, {}, IdleRepairPanDelta{.x = 97, .y = 21});
  REQUIRE(plan.count == 4U);
  CHECK(plan.views[0] == view_at(ZoomLevel::k400Percent, 2760 + kOverviewWidth, 3360));
  CHECK(plan.views[1] == view_at(ZoomLevel::k400Percent, 2760 - 64, 3360));
  CHECK(plan.views[2] == view_at(ZoomLevel::k400Percent, 2760, 3360 - 64));
  CHECK(plan.views[3] == view_at(ZoomLevel::k400Percent, 2760, 3360 + 64));
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair prioritizes continued vertical travel and short runways") {
  const auto active = view_at(ZoomLevel::k400Percent, 2760, 3360);
  const auto plan = plan_idle_repair(active, {}, IdleRepairPanDelta{.x = 12, .y = -80});
  REQUIRE(plan.count == 4U);
  CHECK(plan.views[0] == view_at(ZoomLevel::k400Percent, 2760, 3360 - kOverviewHeight));
  CHECK(plan.views[1] == view_at(ZoomLevel::k400Percent, 2760, 3360 + 64));
  CHECK(plan.views[2] == view_at(ZoomLevel::k400Percent, 2760 - 64, 3360));
  CHECK(plan.views[3] == view_at(ZoomLevel::k400Percent, 2760 + 64, 3360));
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair zero pan delta preserves the symmetric plan") {
  const auto active = view_at(ZoomLevel::k400Percent, 2760, 3360);
  const auto implicit = plan_idle_repair(active, {});
  const auto explicit_zero = plan_idle_repair(active, {}, {});
  CHECK(explicit_zero.count == implicit.count);
  CHECK(explicit_zero.grid_start == implicit.grid_start);
  for (std::size_t index = 0; index < implicit.count; ++index) {
    CHECK(explicit_zero.views[index] == implicit.views[index]);
  }
}

TEST_CASE("idle repair clamps and deduplicates at the level corner") {
  // Top-left corner of the 400% level: left and up neighbors clamp onto the
  // active view and disappear.
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k400Percent, 0, 0), {});
  CHECK(plan.count == 2);
  CHECK(!contains(plan, ZoomLevel::k400Percent, 0, 0));
  CHECK(contains(plan, ZoomLevel::k400Percent, kOverviewWidth, 0));
  CHECK(contains(plan, ZoomLevel::k400Percent, 0, kOverviewHeight));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair includes remembered views at other zooms only") {
  const std::array<ViewFootprint, 3> remembered{{
      {.zoom = ZoomLevel::k400Percent, .level_pixels = {736, 896, 1104, 1344}, .valid = true},
      {.zoom = ZoomLevel::k200Percent, .level_pixels = {368, 448, 736, 896}, .valid = true},
      {.zoom = ZoomLevel::k50Percent, .level_pixels = {0, 0, 368, 448}, .valid = false},
  }};
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k400Percent, 2760, 3360), remembered);
  // The remembered 400% view matches the active zoom and is skipped; the
  // invalid 50% footprint is skipped.
  CHECK(plan.count == 5);
  CHECK(contains(plan, ZoomLevel::k200Percent, 368, 448));
  CHECK(!contains(plan, ZoomLevel::k400Percent, 736, 896));
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair at 100 percent sweeps the full level grid") {
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k100Percent, 500, 700), {});
  // Four neighbors (grid-unaligned, so no dedupe) + 16 grid cells.
  CHECK(plan.count == 20);
  for (int grid_y = 0; grid_y < kOverviewHeight << 2; grid_y += kOverviewHeight) {
    for (int grid_x = 0; grid_x < kOverviewWidth << 2; grid_x += kOverviewWidth) {
      CHECK(contains(plan, ZoomLevel::k100Percent, grid_x, grid_y));
    }
  }
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair at a grid-aligned 100 percent origin dedupes the grid") {
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k100Percent, 368, 448), {});
  // All four neighbors coincide with grid cells.
  CHECK(plan.count == 16);
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

TEST_CASE("idle repair never exceeds plan capacity") {
  const std::array<ViewFootprint, 4> remembered{{
      {.zoom = ZoomLevel::k50Percent, .level_pixels = {100, 100, 468, 548}, .valid = true},
      {.zoom = ZoomLevel::k200Percent, .level_pixels = {200, 200, 568, 648}, .valid = true},
      {.zoom = ZoomLevel::k400Percent, .level_pixels = {300, 300, 668, 748}, .valid = true},
      {.zoom = ZoomLevel::k100Percent, .level_pixels = {400, 400, 768, 848}, .valid = true},
  }};
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k100Percent, 501, 701), remembered);
  CHECK(plan.count <= plan.views.size());
  CHECK(all_views_valid(plan));
  CHECK(all_views_unique(plan));
}

}  // namespace

TEST_CASE("plan exposes the saturation contract through grid_start") {
  // The grid suffix is the optional full-level sweep a runner must stop at
  // pool saturation; everything before grid_start is always safe to produce.
  const std::array zooms{ZoomLevel::k50Percent, ZoomLevel::k200Percent, ZoomLevel::k400Percent};
  for (const auto zoom : zooms) {
    const auto plan = plan_idle_repair(view_at(zoom, 368, 448), {});
    CHECK(plan.grid_start <= plan.count);
    CHECK(plan.grid_start == plan.count);
  }
  const auto plan = plan_idle_repair(view_at(ZoomLevel::k100Percent, 500, 700), {});
  CHECK(plan.grid_start <= plan.count);
  CHECK(plan.grid_start < plan.count);
  for (std::size_t index = plan.grid_start; index < plan.count; ++index) {
    CHECK(plan.views[index].zoom == ZoomLevel::k100Percent);
    // Grid views are viewport-aligned, distinguishing the sweep suffix from
    // the unaligned active-neighborhood prefix.
    CHECK(plan.views[index].level_pixels.x0 % kOverviewWidth == 0);
    CHECK(plan.views[index].level_pixels.y0 % kOverviewHeight == 0);
  }
}
