#include "tinydraw/vector_v2/chrome.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using tinydraw::vector_v2::ChromeAction;
using tinydraw::vector_v2::ChromeExportStatus;
using tinydraw::vector_v2::ChromePopup;
using tinydraw::vector_v2::ChromeState;
using tinydraw::vector_v2::ChromeTool;

TEST_CASE("PICO-8 palettes are locked to RGB565") {
  using tinydraw::vector_v2::kPico8Palettes;
  CHECK(kPico8Palettes[0][0] == 0x0000U);
  CHECK(kPico8Palettes[0][7] == 0xFF9DU);
  CHECK(kPico8Palettes[0][12] == 0x2D7FU);
  CHECK(kPico8Palettes[0][15] == 0xFE75U);
  CHECK(kPico8Palettes[1][0] == 0x28C2U);
  CHECK(kPico8Palettes[1][7] == 0xF76FU);
  CHECK(kPico8Palettes[1][12] == 0x02D6U);
  CHECK(kPico8Palettes[1][15] == 0xFCF0U);
}

TEST_CASE("bottom chrome maps six stable actions") {
  const ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_action_at({30.0F, 410.0F}, state) == ChromeAction::kUndo);
  CHECK(tinydraw::vector_v2::chrome_action_at({90.0F, 410.0F}, state) == ChromeAction::kRedo);
  CHECK(tinydraw::vector_v2::chrome_action_at({150.0F, 410.0F}, state) ==
        ChromeAction::kToggleTools);
  CHECK(tinydraw::vector_v2::chrome_action_at({210.0F, 410.0F}, state) ==
        ChromeAction::kToggleColors);
  CHECK(tinydraw::vector_v2::chrome_action_at({270.0F, 410.0F}, state) ==
        ChromeAction::kToggleSizes);
  CHECK(tinydraw::vector_v2::chrome_action_at({330.0F, 410.0F}, state) ==
        ChromeAction::kToggleDocument);
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({180.0F, 200.0F}, state));
}

TEST_CASE("tools popup contains draw erase and pan") {
  ChromeState state{.popup = ChromePopup::kTools};
  CHECK(tinydraw::vector_v2::chrome_action_at({60.0F, 331.0F}, state) == ChromeAction::kSelectDraw);
  CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 331.0F}, state) ==
        ChromeAction::kSelectErase);
  CHECK(tinydraw::vector_v2::chrome_action_at({306.0F, 331.0F}, state) == ChromeAction::kSelectPan);
  state.tool = ChromeTool::kErase;
  CHECK(tinydraw::vector_v2::chrome_contains({184.0F, 331.0F}, state));
}

TEST_CASE("rectangular bottom toolbar owns the curved lower display corners") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t untouched = 0x1234U;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), untouched);

  tinydraw::vector_v2::draw_chrome(pixels, width, height, {});

  CHECK(pixels[372U * width] == 0xBDF7U);
  CHECK(pixels[373U * width] == 0xDEDBU);
  CHECK(pixels[447U * width] == 0xFFFFU);
  CHECK(pixels[447U * width + 367U] == 0xFFFFU);
  CHECK(pixels[371U * width] == untouched);
}

TEST_CASE("color palette is a second-level popup above the persistent toolbar") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t untouched = 0x1234U;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), untouched);
  const ChromeState state{.popup = ChromePopup::kColors};

  tinydraw::vector_v2::draw_chrome(pixels, width, height, state);

  CHECK(pixels[0] == untouched);
  CHECK(pixels[365U * width + 46U] == 0xFFFFU);
  CHECK(pixels[410U * width + 214U] == tinydraw::vector_v2::selected_color(state));
  CHECK(tinydraw::vector_v2::chrome_action_at({210.0F, 410.0F}, state) ==
        ChromeAction::kToggleColors);
  CHECK(tinydraw::vector_v2::chrome_action_at({30.0F, 410.0F}, state) == ChromeAction::kNone);
}

TEST_CASE("color popup maps all sixteen swatches and both page actions") {
  const ChromeState state{.popup = ChromePopup::kColors};
  std::array<bool, 16> seen{};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const tinydraw::vector_v2::ChromePoint point{46.0F + column * 92.0F, 103.0F + row * 75.0F};
      const auto color = tinydraw::vector_v2::chrome_color_at(point, state);
      REQUIRE(color.has_value());
      seen[*color] = true;
      CHECK(tinydraw::vector_v2::chrome_action_at(point, state) == ChromeAction::kSelectColor);
    }
  }
  for (const bool present : seen) {
    CHECK(present);
  }
  CHECK(tinydraw::vector_v2::chrome_action_at({30.0F, 55.0F}, state) ==
        ChromeAction::kPreviousPalette);
  CHECK(tinydraw::vector_v2::chrome_action_at({340.0F, 55.0F}, state) ==
        ChromeAction::kNextPalette);
  CHECK(tinydraw::vector_v2::chrome_contains({180.0F, 200.0F}, state));
}

TEST_CASE("right zoom rail exposes plus and minus without stealing its label") {
  const ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_contains({332.0F, 98.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 98.0F}, state) == ChromeAction::kZoomIn);
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 150.0F}, state) == ChromeAction::kNone);
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 200.0F}, state) == ChromeAction::kZoomOut);

  const ChromeState popup{.popup = ChromePopup::kTools};
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({332.0F, 98.0F}, popup));
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 98.0F}, popup) == ChromeAction::kNone);
}

TEST_CASE("overview mutations schedule a minimap refresh outside its panel bounds") {
  const ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_minimap_refresh_required(state, true, true));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required(state, false, true));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required(state, true, false));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required({.popup = ChromePopup::kTools},
                                                                   true, true));
}

TEST_CASE("canvas overlay regions disappear for modal and popup chrome") {
  CHECK(tinydraw::vector_v2::chrome_overlay_regions({}).count == 2U);
  CHECK(tinydraw::vector_v2::chrome_overlay_regions({.battery_percentage = 50}).count == 3U);
  CHECK(tinydraw::vector_v2::chrome_overlay_regions({.popup = ChromePopup::kTools}).count == 0U);
  CHECK(tinydraw::vector_v2::chrome_overlay_regions({.confirm_new = true}).count == 0U);
  CHECK(tinydraw::vector_v2::chrome_overlay_regions({.export_status = ChromeExportStatus::kSaving})
            .count == 0U);
}

TEST_CASE("live ink presentation regions exclude every fixed canvas overlay") {
  const ChromeState state{.battery_percentage = 50};
  const auto regions = tinydraw::vector_v2::chrome_unobscured_regions({0, 0, 368, 372}, state);
  const auto contains = [&](int x, int y) {
    for (std::size_t index = 0; index < regions.count; ++index) {
      const auto region = regions.regions[index];
      if (x >= region.x0 && x < region.x1 && y >= region.y0 && y < region.y1) {
        return true;
      }
    }
    return false;
  };

  CHECK(contains(100, 100));
  CHECK(contains(100, 300));
  CHECK_FALSE(contains(332, 98));
  CHECK_FALSE(contains(300, 300));
  CHECK_FALSE(contains(230, 28));
  for (std::size_t index = 0; index < regions.count; ++index) {
    const auto region = regions.regions[index];
    CHECK((region.x0 & 1) == 0);
    CHECK((region.y0 & 1) == 0);
    CHECK((region.x1 & 1) == 0);
    CHECK((region.y1 & 1) == 0);
  }

  const ChromeState popup{.popup = ChromePopup::kTools};
  const auto popup_regions =
      tinydraw::vector_v2::chrome_unobscured_regions({20, 40, 120, 160}, popup);
  REQUIRE(popup_regions.count == 1U);
  CHECK(popup_regions.regions[0] == tinydraw::vector_v2::ChromeRect{20, 40, 120, 160});
}

TEST_CASE("canvas overlay regions own every pixel their painters mutate") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t untouched = 0x1234U;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> pixels(pixel_count, untouched);
  std::vector<std::uint16_t> overview(pixel_count, 0x4567U);
  const ChromeState state{.battery_percentage = 50};
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 100,
      .level_x = 200,
      .level_y = 300,
      .level_width = 1472,
      .level_height = 1792,
      .can_pan_top = true,
      .can_pan_left = true,
      .can_pan_right = true,
      .can_pan_bottom = true,
      .overview_pixels = overview,
  };

  tinydraw::vector_v2::draw_chrome_canvas_overlays(pixels, width, height, state, navigation);
  const auto regions = tinydraw::vector_v2::chrome_overlay_regions(state);
  std::size_t mutated_outside_owned_regions = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (pixels[static_cast<std::size_t>(y * width + x)] == untouched) {
        continue;
      }
      bool owned = false;
      for (std::size_t index = 0; index < regions.count; ++index) {
        const auto region = regions.regions[index];
        owned = owned || (x >= region.x0 && x < region.x1 && y >= region.y0 && y < region.y1);
      }
      mutated_outside_owned_regions += owned ? 0U : 1U;
    }
  }
  CHECK(mutated_outside_owned_regions == 0U);
}

TEST_CASE("navigation overlays render overview viewport zoom and battery") {
  constexpr int width = 368;
  constexpr int height = 448;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> pixels(pixel_count, 0x1234U);
  std::vector<std::uint16_t> overview(pixel_count, 0x4567U);
  const ChromeState state{.battery_percentage = 50};
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 100,
      .level_x = 200,
      .level_y = 300,
      .level_width = 1472,
      .level_height = 1792,
      .can_pan_top = true,
      .can_pan_left = true,
      .can_pan_right = true,
      .can_pan_bottom = true,
      .overview_pixels = overview,
  };

  tinydraw::vector_v2::draw_chrome_canvas_overlays(pixels, width, height, state, navigation);

  CHECK(pixels[98U * width + 332U] == 0x2104U);
  CHECK(pixels[150U * width + 332U] != 0x1234U);
  CHECK(pixels[300U * width + 300U] == 0x4567U);
  CHECK(pixels[28U * width + 230U] == 0x2104U);
  CHECK(pixels[100U * width + 100U] == 0x1234U);
}

TEST_CASE("chrome canvas clipping follows the visible overlay") {
  ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 372);
  // With no popup open, committed ink continues under the dock: segments
  // crossing the canvas bottom keep their true coordinates instead of
  // clamping at the dock edge.
  CHECK(tinydraw::vector_v2::chrome_ink_bottom(state) == 448);
  auto clipped =
      tinydraw::vector_v2::clip_canvas_segment({100.0F, 280.0F}, {120.0F, 400.0F}, state);
  REQUIRE(clipped.has_value());
  CHECK(clipped->y == doctest::Approx(400.0F));
  clipped = tinydraw::vector_v2::clip_canvas_segment({120.0F, 400.0F}, {140.0F, 440.0F}, state);
  REQUIRE(clipped.has_value());
  CHECK(clipped->y == doctest::Approx(440.0F));

  state.popup = ChromePopup::kTools;
  CHECK(tinydraw::vector_v2::chrome_ink_bottom(state) ==
        tinydraw::vector_v2::chrome_input_bottom(state));
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 294);
  CHECK(tinydraw::vector_v2::chrome_input_bottom(state) == 288);
  clipped = tinydraw::vector_v2::clip_canvas_segment({100.0F, 280.0F}, {120.0F, 331.0F}, state);
  REQUIRE(clipped.has_value());
  CHECK(clipped->y == doctest::Approx(287.0F));
  CHECK_FALSE(tinydraw::vector_v2::clip_canvas_segment({120.0F, 331.0F}, {140.0F, 340.0F}, state)
                  .has_value());

  state.popup = ChromePopup::kColors;
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 0);
  CHECK_FALSE(tinydraw::vector_v2::clip_canvas_segment({100.0F, 100.0F}, {120.0F, 120.0F}, state)
                  .has_value());
}

TEST_CASE("new drawing confirmation is modal and exposes large Raster V1 choices") {
  const ChromeState state{.confirm_new = true};

  CHECK(tinydraw::vector_v2::chrome_contains({10.0F, 10.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_input_bottom(state) == 0);
  CHECK(tinydraw::vector_v2::chrome_action_at({100.0F, 230.0F}, state) ==
        ChromeAction::kCancelNewDrawing);
  CHECK(tinydraw::vector_v2::chrome_action_at({260.0F, 230.0F}, state) ==
        ChromeAction::kConfirmNewDrawing);
  CHECK(tinydraw::vector_v2::chrome_action_at({180.0F, 160.0F}, state) == ChromeAction::kNone);
}

TEST_CASE("saved export label sits one pixel below the old baseline") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0xFFFFU);
  const ChromeState state{.export_status = ChromeExportStatus::kSaved};

  tinydraw::vector_v2::draw_chrome(pixels, width, height, state);

  CHECK(pixels[90U * width + 142U] == 0xFFFFU);
  CHECK(pixels[91U * width + 142U] == 0x2104U);
}

TEST_CASE("saving toast is modal and renders determinate progress") {
  constexpr int width = 368;
  constexpr int height = 448;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> empty(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> half = empty;

  const ChromeState empty_state{.export_status = ChromeExportStatus::kSaving, .export_progress = 0};
  const ChromeState half_state{.export_status = ChromeExportStatus::kSaving, .export_progress = 50};
  CHECK(tinydraw::vector_v2::chrome_contains({10.0F, 10.0F}, empty_state));
  CHECK(tinydraw::vector_v2::chrome_input_bottom(empty_state) == 0);
  CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 100.0F}, empty_state) ==
        ChromeAction::kNone);

  tinydraw::vector_v2::draw_chrome(empty, width, height, empty_state);
  tinydraw::vector_v2::draw_chrome(half, width, height, half_state);
  CHECK(empty[116U * width + 150U] == 0xFFFFU);
  CHECK(half[116U * width + 150U] == 0x349FU);
  CHECK(empty != half);
}

TEST_CASE("palette hit testing gives large cells to the circular swatches") {
  const ChromeState state{.popup = ChromePopup::kColors};
  CHECK_FALSE(tinydraw::vector_v2::chrome_color_at({46.0F, 63.0F}, state).has_value());
  CHECK(tinydraw::vector_v2::chrome_color_at({91.0F, 103.0F}, state) == 0U);
  CHECK(tinydraw::vector_v2::chrome_color_at({92.0F, 103.0F}, state) == 1U);
  CHECK(tinydraw::vector_v2::chrome_color_at({322.0F, 138.0F}, state) == 3U);
  CHECK(tinydraw::vector_v2::chrome_color_at({46.0F, 139.0F}, state) == 4U);
}

TEST_CASE("chrome rendering stays within the framebuffer") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0xFFFFU);
  const auto untouched = pixels[100U * width + 100U];
  tinydraw::vector_v2::draw_chrome(pixels, width, height, {});
  CHECK(pixels[100U * width + 100U] == untouched);
  CHECK(pixels[410U * width + 214U] != 0xFFFFU);

  pixels.assign(pixels.size(), 0x1234U);
  tinydraw::vector_v2::draw_chrome(pixels, width, height, {});
  for (int y = tinydraw::vector_v2::kChromeCanvasBottom; y < 374; ++y) {
    for (int x = 0; x < width; ++x) {
      CHECK(pixels[static_cast<std::size_t>(y * width + x)] != 0x1234U);
    }
  }

  ChromeState popup{.popup = ChromePopup::kTools};
  pixels.assign(pixels.size(), 0x1234U);
  tinydraw::vector_v2::draw_chrome(pixels, width, height, popup);
  for (int y = tinydraw::vector_v2::kChromePopupCanvasBottom; y < 296; ++y) {
    for (int x = 0; x < width; ++x) {
      CHECK(pixels[static_cast<std::size_t>(y * width + x)] != 0x1234U);
    }
  }

  const ChromeState palette{.popup = ChromePopup::kColors};
  tinydraw::vector_v2::draw_chrome(pixels, width, height, palette);
  CHECK(pixels[108U * width + 46U] == tinydraw::vector_v2::kPico8Palettes[0][0]);
  CHECK(pixels[32U * width + 184U] == 0xFFFFU);
}

TEST_CASE("main tool button renders the selected Raster V1 glyph") {
  constexpr int width = 368;
  constexpr int height = 448;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> pen(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> eraser = pen;
  std::vector<std::uint16_t> hand = pen;

  tinydraw::vector_v2::draw_chrome(pen, width, height, {.tool = ChromeTool::kDraw});
  tinydraw::vector_v2::draw_chrome(eraser, width, height, {.tool = ChromeTool::kErase});
  tinydraw::vector_v2::draw_chrome(hand, width, height, {.tool = ChromeTool::kPan});

  CHECK(pen != eraser);
  CHECK(pen != hand);
  CHECK(eraser != hand);
  CHECK(pen[428U * width + 140U] == 0x2104U);
  CHECK(hand[407U * width + 135U] == 0x2104U);
}

TEST_CASE("new drawing dialog preserves canvas outside its bounds") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0x1234U);

  tinydraw::vector_v2::draw_chrome(pixels, width, height, {.confirm_new = true});

  CHECK(pixels[20U * width + 20U] == 0x1234U);
  CHECK(pixels[140U * width + 180U] == 0xFFFFU);
}

}  // namespace

TEST_CASE("minimap surface draw matches the full overlay draw") {
  constexpr int width = 368;
  constexpr int height = 448;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> frame(pixel_count);
  std::vector<std::uint16_t> overview(pixel_count);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    frame[index] = static_cast<std::uint16_t>(index * 7U);
    overview[index] = static_cast<std::uint16_t>(index * 13U);
  }
  const ChromeState state{.battery_percentage = 50};
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 200,
      .level_x = 511,
      .level_y = 833,
      .level_width = 2944,
      .level_height = 3584,
      .can_pan_top = true,
      .can_pan_left = true,
      .can_pan_right = true,
      .can_pan_bottom = true,
      .overview_pixels = overview,
  };
  const auto region = tinydraw::vector_v2::chrome_minimap_region(state);
  REQUIRE(region.has_value());
  const int region_width = region->x1 - region->x0;
  const int region_height = region->y1 - region->y0;
  // The surface starts as a copy of the frame's backdrop under the overlay.
  std::vector<std::uint16_t> surface(static_cast<std::size_t>(region_width) *
                                     static_cast<std::size_t>(region_height));
  for (int y = 0; y < region_height; ++y) {
    for (int x = 0; x < region_width; ++x) {
      surface[static_cast<std::size_t>(y) * static_cast<std::size_t>(region_width) +
              static_cast<std::size_t>(x)] =
          frame[static_cast<std::size_t>(region->y0 + y) * width +
                static_cast<std::size_t>(region->x0 + x)];
    }
  }
  REQUIRE(
      tinydraw::vector_v2::draw_chrome_minimap_overlay(frame, width, height, state, navigation));
  REQUIRE(tinydraw::vector_v2::draw_chrome_minimap_surface(
      {surface, region_width, region_height, region->x0, region->y0}, state, navigation));
  for (int y = 0; y < region_height; ++y) {
    for (int x = 0; x < region_width; ++x) {
      CHECK(surface[static_cast<std::size_t>(y) * static_cast<std::size_t>(region_width) +
                    static_cast<std::size_t>(x)] ==
            frame[static_cast<std::size_t>(region->y0 + y) * width +
                  static_cast<std::size_t>(region->x0 + x)]);
    }
  }
}

TEST_CASE("full-width strip overlay drawing matches the full overlay draw") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr int canvas_bottom = 372;
  constexpr int rows_per_strip = 44;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> canvas(pixel_count);
  std::vector<std::uint16_t> overview(pixel_count);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    canvas[index] = static_cast<std::uint16_t>(index * 7U);
    overview[index] = static_cast<std::uint16_t>(index * 13U);
  }
  const ChromeState state{.battery_percentage = 50};
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 200,
      .level_x = 511,
      .level_y = 833,
      .level_width = 2944,
      .level_height = 3584,
      .can_pan_top = true,
      .can_pan_left = true,
      .can_pan_right = true,
      .can_pan_bottom = true,
      .overview_pixels = overview,
  };
  // Reference: overlays drawn over the canvas in one full-frame pass.
  std::vector<std::uint16_t> reference = canvas;
  tinydraw::vector_v2::draw_chrome_canvas_overlays(reference, width, height, state, navigation);
  // The pan sweep path: each full-width strip starts as backdrop and draws
  // just its intersecting overlay shares.
  std::size_t mismatches = 0;
  for (int y = 0; y < canvas_bottom; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, canvas_bottom - y);
    std::vector<std::uint16_t> strip(
        canvas.begin() + static_cast<std::ptrdiff_t>(y) * width,
        canvas.begin() + static_cast<std::ptrdiff_t>(y + rows) * width);
    REQUIRE(tinydraw::vector_v2::draw_chrome_strip_overlays({strip, width, rows, 0, y}, state,
                                                            navigation));
    for (int row = 0; row < rows; ++row) {
      for (int x = 0; x < width; ++x) {
        mismatches +=
            strip[static_cast<std::size_t>(row) * width + static_cast<std::size_t>(x)] !=
            reference[static_cast<std::size_t>(y + row) * width + static_cast<std::size_t>(x)];
      }
    }
  }
  CHECK(mismatches == 0);
}

TEST_CASE("staging strips reproduce all fixed chrome without mutating canvas source") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr int rows_per_strip = 44;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> canvas(pixel_count);
  std::vector<std::uint16_t> overview(pixel_count);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    canvas[index] = static_cast<std::uint16_t>(index * 11U);
    overview[index] = static_cast<std::uint16_t>(index * 17U);
  }
  const std::vector<std::uint16_t> original = canvas;
  const ChromeState state{.battery_percentage = 73, .battery_charging = true};
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 200,
      .level_x = 511,
      .level_y = 833,
      .level_width = 2944,
      .level_height = 3584,
      .can_pan_top = true,
      .can_pan_left = true,
      .can_pan_right = true,
      .can_pan_bottom = true,
      .overview_pixels = overview,
  };

  std::vector<std::uint16_t> reference = canvas;
  tinydraw::vector_v2::draw_chrome(reference, width, height, state);
  tinydraw::vector_v2::draw_chrome_canvas_overlays(reference, width, height, state, navigation);
  std::vector<std::uint16_t> staged = canvas;
  for (int y = 0; y < height; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, height - y);
    auto strip = std::span(staged).subspan(static_cast<std::size_t>(y) * width,
                                           static_cast<std::size_t>(rows) * width);
    REQUIRE(tinydraw::vector_v2::draw_chrome_staging_surface({strip, width, rows, 0, y}, state,
                                                             navigation));
  }
  CHECK(staged == reference);

  std::vector<std::uint16_t> cache_pixels(tinydraw::vector_v2::kChromeStagingCachePixels);
  tinydraw::vector_v2::ChromeStagingCache cache(cache_pixels);
  REQUIRE(cache.prepare(state, navigation, 7U));
  const std::vector<std::uint16_t> prepared_cache = cache_pixels;
  const auto initial_stats = cache.stats();
  CHECK(initial_stats.bottom_redraws == 1U);
  CHECK(initial_stats.battery_redraws == 1U);
  CHECK(initial_stats.zoom_redraws == 1U);
  CHECK(initial_stats.minimap_base_redraws == 1U);
  REQUIRE(cache.prepare(state, navigation, 7U));
  CHECK(cache_pixels == prepared_cache);
  CHECK(cache.stats().bottom_redraws == initial_stats.bottom_redraws);
  CHECK(cache.stats().battery_redraws == initial_stats.battery_redraws);
  CHECK(cache.stats().zoom_redraws == initial_stats.zoom_redraws);
  CHECK(cache.stats().minimap_base_redraws == initial_stats.minimap_base_redraws);

  // A document revision outside the submitted bounds must not regenerate the
  // minimap, and staged painting must never perform cache work itself.
  std::vector<std::uint16_t> scoped_cache_pixels(tinydraw::vector_v2::kChromeStagingCachePixels);
  tinydraw::vector_v2::ChromeStagingCache scoped_cache(scoped_cache_pixels);
  REQUIRE(scoped_cache.prepare(state, navigation, 7U));
  const auto scoped_stats = scoped_cache.stats();
  const tinydraw::vector_v2::ChromeRect ink_bounds{120, 220, 180, 280};
  REQUIRE(scoped_cache.prepare_for(ink_bounds, state, navigation, 8U));
  CHECK(scoped_cache.stats().minimap_base_redraws == scoped_stats.minimap_base_redraws);
  std::vector<std::uint16_t> ink_surface(60U * 60U, 0xFFFFU);
  REQUIRE(scoped_cache.paint_prepared({ink_surface, 60, 60, 120, 220}, state, navigation, 8U));
  std::vector<std::uint16_t> stale_minimap(118U * 140U, 0xFFFFU);
  CHECK_FALSE(
      scoped_cache.paint_prepared({stale_minimap, 118, 140, 250, 240}, state, navigation, 8U));
  CHECK(scoped_cache.stats().minimap_base_redraws == scoped_stats.minimap_base_redraws);
  REQUIRE(scoped_cache.prepare_for({250, 240, 368, 380}, state, navigation, 8U));
  CHECK(scoped_cache.stats().minimap_base_redraws == scoped_stats.minimap_base_redraws + 1U);
  REQUIRE(scoped_cache.paint_prepared({stale_minimap, 118, 140, 250, 240}, state, navigation, 8U));
  std::vector<std::uint16_t> cached = canvas;
  for (int y = 0; y < height; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, height - y);
    auto strip = std::span(cached).subspan(static_cast<std::size_t>(y) * width,
                                           static_cast<std::size_t>(rows) * width);
    REQUIRE(cache.paint({strip, width, rows, 0, y}, state, navigation, 7U));
  }
  CHECK(cached == reference);

  const auto byte_swap = [](std::uint16_t pixel) {
    return static_cast<std::uint16_t>((pixel >> 8U) | (pixel << 8U));
  };
  std::vector<std::uint16_t> swapped_cached = canvas;
  std::vector<std::uint16_t> swapped_reference = reference;
  for (std::uint16_t& pixel : swapped_cached) {
    pixel = byte_swap(pixel);
  }
  for (std::uint16_t& pixel : swapped_reference) {
    pixel = byte_swap(pixel);
  }
  for (int y = 0; y < height; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, height - y);
    auto strip =
        std::span(swapped_cached)
            .subspan(static_cast<std::size_t>(y) * width, static_cast<std::size_t>(rows) * width);
    REQUIRE(cache.paint({strip, width, rows, 0, y, true}, state, navigation, 7U));
  }
  CHECK(swapped_cached == swapped_reference);

  const ChromeState modal_state{.popup = tinydraw::vector_v2::ChromePopup::kColors};
  std::vector<std::uint16_t> modal_reference = canvas;
  tinydraw::vector_v2::draw_chrome(modal_reference, width, height, modal_state);
  tinydraw::vector_v2::draw_chrome_canvas_overlays(modal_reference, width, height, modal_state,
                                                   navigation);
  std::vector<std::uint16_t> modal_cache_pixels(tinydraw::vector_v2::kChromeStagingCachePixels);
  tinydraw::vector_v2::ChromeStagingCache modal_cache(modal_cache_pixels);
  REQUIRE(modal_cache.prepare(modal_state, navigation, 7U));
  const auto modal_stats = modal_cache.stats();
  CHECK(modal_stats.modal_redraws == 1U);
  std::vector<std::uint16_t> modal_staged = canvas;
  for (int y = 0; y < height; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, height - y);
    auto strip =
        std::span(modal_staged)
            .subspan(static_cast<std::size_t>(y) * width, static_cast<std::size_t>(rows) * width);
    REQUIRE(modal_cache.paint_prepared({strip, width, rows, 0, y}, modal_state, navigation, 7U));
  }
  CHECK(modal_staged == modal_reference);
  CHECK(modal_cache.stats().modal_redraws == modal_stats.modal_redraws);

  auto moved = navigation;
  moved.level_x += 137;
  moved.level_y += 211;
  std::vector<std::uint16_t> moved_reference = canvas;
  tinydraw::vector_v2::draw_chrome(moved_reference, width, height, state);
  tinydraw::vector_v2::draw_chrome_canvas_overlays(moved_reference, width, height, state, moved);
  REQUIRE(cache.prepare(state, moved, 7U));
  CHECK(cache_pixels == prepared_cache);
  CHECK(cache.stats().bottom_redraws == initial_stats.bottom_redraws);
  CHECK(cache.stats().battery_redraws == initial_stats.battery_redraws);
  CHECK(cache.stats().zoom_redraws == initial_stats.zoom_redraws);
  CHECK(cache.stats().minimap_base_redraws == initial_stats.minimap_base_redraws);
  std::vector<std::uint16_t> moved_cached = canvas;
  for (int y = 0; y < height; y += rows_per_strip) {
    const int rows = std::min(rows_per_strip, height - y);
    auto strip =
        std::span(moved_cached)
            .subspan(static_cast<std::size_t>(y) * width, static_cast<std::size_t>(rows) * width);
    REQUIRE(cache.paint({strip, width, rows, 0, y}, state, moved, 7U));
  }
  CHECK(moved_cached == moved_reference);

  auto zoomed = moved;
  zoomed.zoom_percent = 400;
  REQUIRE(cache.prepare(state, zoomed, 7U));
  CHECK(cache.stats().zoom_redraws == initial_stats.zoom_redraws + 1U);
  CHECK(cache.stats().bottom_redraws == initial_stats.bottom_redraws);
  CHECK(cache.stats().battery_redraws == initial_stats.battery_redraws);
  CHECK(cache.stats().minimap_base_redraws == initial_stats.minimap_base_redraws);

  auto battery_changed = state;
  battery_changed.battery_percentage = 74;
  REQUIRE(cache.prepare(battery_changed, zoomed, 7U));
  CHECK(cache.stats().battery_redraws == initial_stats.battery_redraws + 1U);
  CHECK(cache.stats().bottom_redraws == initial_stats.bottom_redraws);

  auto bottom_changed = battery_changed;
  bottom_changed.can_undo = true;
  REQUIRE(cache.prepare(bottom_changed, zoomed, 7U));
  CHECK(cache.stats().bottom_redraws == initial_stats.bottom_redraws + 1U);
  CHECK(cache.stats().battery_redraws == initial_stats.battery_redraws + 1U);

  overview[0] ^= 0xFFFFU;
  REQUIRE(cache.prepare(bottom_changed, zoomed, 8U));
  CHECK(cache.stats().minimap_base_redraws == initial_stats.minimap_base_redraws + 1U);
  CHECK(canvas == original);
}

TEST_CASE("dock hit region starts at the dock face, not the last canvas rows") {
  const ChromeState state;
  // Rows 366-371 are visible canvas: strokes must be able to start there now
  // that committed ink continues under the dock.
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({100.0F, 368.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_action_at({100.0F, 368.0F}, state) ==
        tinydraw::vector_v2::ChromeAction::kNone);
  // The dock face (shadow band and buttons) still hit-tests as chrome.
  CHECK(tinydraw::vector_v2::chrome_contains({100.0F, 373.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_contains({100.0F, 400.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_action_at({100.0F, 400.0F}, state) !=
        tinydraw::vector_v2::ChromeAction::kNone);
}

TEST_CASE("unobscured regions cover the bounds exactly and never overflow") {
  const ChromeState state{.battery_percentage = 50};
  const auto overlays = tinydraw::vector_v2::chrome_overlay_regions(state);
  REQUIRE(overlays.count == 3);
  const auto aligned = [](tinydraw::vector_v2::ChromeRect rect) {
    rect.x0 = std::clamp(rect.x0 & ~1, 0, 368);
    rect.y0 = std::clamp(rect.y0 & ~1, 0, 448);
    rect.x1 = std::clamp((rect.x1 + 1) & ~1, rect.x0, 368);
    rect.y1 = std::clamp((rect.y1 + 1) & ~1, rect.y0, 448);
    return rect;
  };
  const std::array bounds_cases{
      tinydraw::vector_v2::ChromeRect{0, 0, 368, 372},
      tinydraw::vector_v2::ChromeRect{0, 200, 368, 372},
      tinydraw::vector_v2::ChromeRect{0, 0, 2, 372},
  };
  for (const auto& bounds : bounds_cases) {
    const auto regions = tinydraw::vector_v2::chrome_unobscured_regions(bounds, state);
    CHECK_FALSE(regions.overflowed);
    // Every pixel of the aligned bounds is covered exactly once by a
    // returned region or sits under an aligned overlay: no dropped strips,
    // no even-aligned holes at x=0 or x=366, no double pushes.
    const auto aligned_bounds = aligned(bounds);
    std::vector<std::uint8_t> coverage(368U * 448U, 0U);
    for (std::size_t index = 0; index < regions.count; ++index) {
      const auto& region = regions.regions[index];
      for (int y = region.y0; y < region.y1; ++y) {
        for (int x = region.x0; x < region.x1; ++x) {
          ++coverage[static_cast<std::size_t>(y) * 368U + static_cast<std::size_t>(x)];
        }
      }
    }
    for (std::size_t index = 0; index < overlays.count; ++index) {
      const auto overlay = aligned(overlays.regions[index]);
      for (int y = overlay.y0; y < overlay.y1; ++y) {
        for (int x = overlay.x0; x < overlay.x1; ++x) {
          ++coverage[static_cast<std::size_t>(y) * 368U + static_cast<std::size_t>(x)];
        }
      }
    }
    std::size_t holes = 0;
    std::size_t doubles = 0;
    for (int y = aligned_bounds.y0; y < aligned_bounds.y1; ++y) {
      for (int x = aligned_bounds.x0; x < aligned_bounds.x1; ++x) {
        const auto value =
            coverage[static_cast<std::size_t>(y) * 368U + static_cast<std::size_t>(x)];
        holes += value == 0U;
        doubles += value > 1U && x >= aligned_bounds.x0 && x < aligned_bounds.x1;
      }
    }
    CHECK(holes == 0U);
    CHECK(doubles == 0U);
  }
}
