#include "tinydraw/vector_v2/navigation_state.h"

#include <doctest.h>

namespace vector_v2 = tinydraw::vector_v2;

namespace {

constexpr vector_v2::NavigationPoint kDrawingCenter{vector_v2::kOverviewWidth / 2, 372 / 2};

vector_v2::NavigationPoint panel_focus_world(const vector_v2::NavigationState& navigation,
                                             vector_v2::NavigationPoint panel_focus) {
  const int percent = vector_v2::zoom_percent(navigation.zoom());
  return {
      .x = (navigation.origin().x + panel_focus.x) * 400 / percent,
      .y = (navigation.origin().y + panel_focus.y) * 400 / percent,
  };
}

}  // namespace

TEST_CASE("committed zoom helpers visit every level and stop at the ends") {
  using vector_v2::ZoomLevel;
  CHECK(vector_v2::next_zoom(ZoomLevel::k25Percent) == ZoomLevel::k50Percent);
  CHECK(vector_v2::next_zoom(ZoomLevel::k50Percent) == ZoomLevel::k100Percent);
  CHECK(vector_v2::next_zoom(ZoomLevel::k100Percent) == ZoomLevel::k200Percent);
  CHECK(vector_v2::next_zoom(ZoomLevel::k200Percent) == ZoomLevel::k400Percent);
  CHECK(vector_v2::next_zoom(ZoomLevel::k400Percent) == ZoomLevel::k400Percent);
  CHECK(vector_v2::previous_zoom(ZoomLevel::k400Percent) == ZoomLevel::k200Percent);
  CHECK(vector_v2::previous_zoom(ZoomLevel::k200Percent) == ZoomLevel::k100Percent);
  CHECK(vector_v2::previous_zoom(ZoomLevel::k100Percent) == ZoomLevel::k50Percent);
  CHECK(vector_v2::previous_zoom(ZoomLevel::k50Percent) == ZoomLevel::k25Percent);
  CHECK(vector_v2::previous_zoom(ZoomLevel::k25Percent) == ZoomLevel::k25Percent);
}

TEST_CASE("initial overview leaves around the world center at every tiled zoom") {
  constexpr vector_v2::ZoomLevel zooms[]{
      vector_v2::ZoomLevel::k50Percent, vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent, vector_v2::ZoomLevel::k400Percent};
  for (const auto zoom : zooms) {
    vector_v2::NavigationState navigation;
    REQUIRE(navigation.set_zoom(zoom, kDrawingCenter));
    const auto focus = panel_focus_world(navigation, kDrawingCenter);
    CHECK(focus.x == vector_v2::kWorldWidth * 2);
    CHECK(focus.y == vector_v2::kWorldHeight * 2);
  }
}

TEST_CASE("adjacent zoom transitions preserve an unclamped world focus") {
  vector_v2::NavigationState navigation;
  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k100Percent, kDrawingCenter));
  REQUIRE(navigation.set_origin(500, 700, kDrawingCenter));
  const auto expected = navigation.focus_quarter_world();
  constexpr vector_v2::ZoomLevel route[]{
      vector_v2::ZoomLevel::k200Percent, vector_v2::ZoomLevel::k400Percent,
      vector_v2::ZoomLevel::k200Percent, vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k50Percent};
  for (const auto zoom : route) {
    REQUIRE(navigation.set_zoom(zoom, kDrawingCenter));
    CHECK(navigation.focus_quarter_world() == expected);
    const auto visible = panel_focus_world(navigation, kDrawingCenter);
    CHECK(std::abs(visible.x - expected.x) <= 4);
    CHECK(std::abs(visible.y - expected.y) <= 4);
  }
}

TEST_CASE("zoom centers the focused point after exploring another level") {
  vector_v2::NavigationState navigation;
  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k400Percent, kDrawingCenter));
  REQUIRE(navigation.set_origin(1000, 1200, kDrawingCenter));
  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k100Percent, kDrawingCenter));
  REQUIRE(navigation.set_origin(70, 120, kDrawingCenter));
  const auto focus = navigation.focus_quarter_world();

  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k400Percent, kDrawingCenter));
  CHECK(navigation.focus_quarter_world() == focus);
  const auto visible = panel_focus_world(navigation, kDrawingCenter);
  CHECK(std::abs(visible.x - focus.x) <= 4);
  CHECK(std::abs(visible.y - focus.y) <= 4);
}

TEST_CASE("overview round trip preserves the focused world point") {
  vector_v2::NavigationState navigation;
  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k400Percent, kDrawingCenter));
  REQUIRE(navigation.set_origin(2300, 3100, kDrawingCenter));
  const auto focus = navigation.focus_quarter_world();

  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k25Percent, kDrawingCenter));
  CHECK(navigation.origin() == vector_v2::NavigationPoint{});
  CHECK(navigation.focus_quarter_world() == focus);
  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k400Percent, kDrawingCenter));
  const auto visible = panel_focus_world(navigation, kDrawingCenter);
  CHECK(std::abs(visible.x - focus.x) <= 4);
  CHECK(std::abs(visible.y - focus.y) <= 4);
}

TEST_CASE("full button cycle preserves the explored world focus") {
  vector_v2::NavigationState navigation;
  REQUIRE(navigation.set_zoom(vector_v2::ZoomLevel::k400Percent, kDrawingCenter));
  REQUIRE(navigation.set_origin(2300, 3100, kDrawingCenter));
  const auto focus = navigation.focus_quarter_world();

  constexpr vector_v2::ZoomLevel cycle[]{
      vector_v2::ZoomLevel::k25Percent, vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent, vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent};
  for (const auto zoom : cycle) {
    REQUIRE(navigation.set_zoom(zoom, kDrawingCenter));
  }
  const auto visible = panel_focus_world(navigation, kDrawingCenter);
  CHECK(std::abs(visible.x - focus.x) <= 4);
  CHECK(std::abs(visible.y - focus.y) <= 4);
}

TEST_CASE("camera clamps and extent indicators agree at every edge") {
  constexpr vector_v2::ZoomLevel zooms[]{
      vector_v2::ZoomLevel::k50Percent, vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent, vector_v2::ZoomLevel::k400Percent};
  for (const auto zoom : zooms) {
    vector_v2::NavigationState navigation;
    REQUIRE(navigation.set_zoom(zoom, kDrawingCenter));
    REQUIRE(navigation.set_origin(-9999, -9999, kDrawingCenter));
    CHECK(navigation.origin() == vector_v2::NavigationPoint{});
    CHECK(navigation.extent() == vector_v2::NavigationExtent{.right = true, .bottom = true});

    REQUIRE(navigation.set_origin(99999, 99999, kDrawingCenter));
    const int maximum_x =
        vector_v2::kWorldWidth * vector_v2::zoom_percent(zoom) / 100 - vector_v2::kOverviewWidth;
    const int maximum_y =
        vector_v2::kWorldHeight * vector_v2::zoom_percent(zoom) / 100 - vector_v2::kOverviewHeight;
    CHECK(navigation.origin() == vector_v2::NavigationPoint{maximum_x, maximum_y});
    CHECK(navigation.extent() == vector_v2::NavigationExtent{.top = true, .left = true});
  }
}

TEST_CASE("overview is fixed and exposes no pan affordance") {
  vector_v2::NavigationState navigation;
  REQUIRE(navigation.set_origin(100, 100, kDrawingCenter));
  CHECK(navigation.zoom() == vector_v2::ZoomLevel::k25Percent);
  CHECK(navigation.origin() == vector_v2::NavigationPoint{});
  CHECK(navigation.extent() == vector_v2::NavigationExtent{});
  const auto view = navigation.view();
  CHECK(view.zoom == vector_v2::ZoomLevel::k25Percent);
  CHECK(view.level_pixels ==
        vector_v2::PixelRect{0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight});
}

TEST_CASE("navigation rejects panel focus outside the viewport") {
  vector_v2::NavigationState navigation;
  CHECK_FALSE(navigation.set_zoom(vector_v2::ZoomLevel::k100Percent, {-1, 0}));
  CHECK_FALSE(
      navigation.set_zoom(vector_v2::ZoomLevel::k100Percent, {vector_v2::kOverviewWidth, 0}));
  CHECK_FALSE(navigation.set_origin(10, 10, {0, vector_v2::kOverviewHeight}));
  CHECK(navigation.zoom() == vector_v2::ZoomLevel::k25Percent);
  CHECK(navigation.origin() == vector_v2::NavigationPoint{});
}
