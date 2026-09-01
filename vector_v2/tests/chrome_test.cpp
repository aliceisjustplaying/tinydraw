#include "tinydraw/vector_v2/chrome.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using tinydraw::vector_v2::ChromeAction;
using tinydraw::vector_v2::ChromeExportStatus;
using tinydraw::vector_v2::ChromePoint;
using tinydraw::vector_v2::ChromePopup;
using tinydraw::vector_v2::ChromeSize;
using tinydraw::vector_v2::ChromeState;
using tinydraw::vector_v2::ChromeTimeSyncStatus;
using tinydraw::vector_v2::ChromeTool;

void paint_chrome(std::span<std::uint16_t> pixels, int width, int height, const ChromeState& state,
                  const tinydraw::vector_v2::ChromeNavigation& navigation = {}) {
  std::vector<std::uint16_t> cache_pixels(tinydraw::vector_v2::kChromeStagingCachePixels);
  tinydraw::vector_v2::ChromeStagingCache cache(cache_pixels);
  REQUIRE(cache.prepare(state, navigation, 0U));
  REQUIRE(cache.paint_prepared({pixels, width, height, 0, 0}, state, navigation, 0U));
}

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

  const ChromeState hidden_hud{.hud_visible = false};
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({150.0F, 410.0F}, hidden_hud));
  CHECK(tinydraw::vector_v2::chrome_action_at({150.0F, 410.0F}, hidden_hud) == ChromeAction::kNone);
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

TEST_CASE("sizes popup maps six brushes in a three by two grid") {
  const ChromeState state{.popup = ChromePopup::kSizes};
  CHECK(tinydraw::vector_v2::chrome_action_at({62.0F, 253.0F}, state) ==
        ChromeAction::kSelectSmall);
  CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 253.0F}, state) ==
        ChromeAction::kSelectMedium);
  CHECK(tinydraw::vector_v2::chrome_action_at({306.0F, 253.0F}, state) ==
        ChromeAction::kSelectLarge);
  CHECK(tinydraw::vector_v2::chrome_action_at({62.0F, 329.0F}, state) ==
        ChromeAction::kSelectExtraLarge);
  CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 329.0F}, state) ==
        ChromeAction::kSelectDoubleExtraLarge);
  CHECK(tinydraw::vector_v2::chrome_action_at({306.0F, 329.0F}, state) ==
        ChromeAction::kSelectTripleExtraLarge);
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) ==
        tinydraw::vector_v2::kChromeSizePopupCanvasBottom);
  CHECK(tinydraw::vector_v2::chrome_input_bottom(state) ==
        tinydraw::vector_v2::kChromeSizePopupInputBottom);

  CHECK(tinydraw::vector_v2::brush_size(ChromeSize::kSmall) == 5.0F);
  CHECK(tinydraw::vector_v2::brush_size(ChromeSize::kMedium) == 8.0F);
  CHECK(tinydraw::vector_v2::brush_size(ChromeSize::kLarge) == 13.0F);
  CHECK(tinydraw::vector_v2::brush_size(ChromeSize::kExtraLarge) == 20.0F);
  CHECK(tinydraw::vector_v2::brush_size(ChromeSize::kDoubleExtraLarge) == 30.0F);
  CHECK(tinydraw::vector_v2::brush_size(ChromeSize::kTripleExtraLarge) == 45.0F);
}

TEST_CASE("document popup gives new export and time sync equal touch targets") {
  const ChromeState state{.popup = ChromePopup::kDocument};
  CHECK(tinydraw::vector_v2::chrome_action_at({60.0F, 331.0F}, state) == ChromeAction::kNewDrawing);
  CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 331.0F}, state) == ChromeAction::kExport);
  CHECK(tinydraw::vector_v2::chrome_action_at({306.0F, 331.0F}, state) == ChromeAction::kSyncTime);
  CHECK(tinydraw::vector_v2::chrome_action_at({154.0F, 410.0F}, state) == ChromeAction::kNone);
  CHECK(tinydraw::vector_v2::chrome_action_at({334.0F, 410.0F}, state) ==
        ChromeAction::kToggleDocument);
}

TEST_CASE("terminal time sync feedback expires but active work does not") {
  constexpr std::uint32_t timeout = tinydraw::vector_v2::kChromeTimeSyncToastDurationUs;
  CHECK(tinydraw::vector_v2::chrome_time_sync_status_after(
            ChromeTimeSyncStatus::kSaved, timeout - 1U) == ChromeTimeSyncStatus::kSaved);
  CHECK(tinydraw::vector_v2::chrome_time_sync_status_after(ChromeTimeSyncStatus::kSaved, timeout) ==
        ChromeTimeSyncStatus::kIdle);
  CHECK(tinydraw::vector_v2::chrome_time_sync_status_after(ChromeTimeSyncStatus::kError, timeout) ==
        ChromeTimeSyncStatus::kIdle);
  CHECK(tinydraw::vector_v2::chrome_time_sync_status_after(ChromeTimeSyncStatus::kSynchronizing,
                                                           timeout * 2U) ==
        ChromeTimeSyncStatus::kSynchronizing);
}

TEST_CASE("time sync labels match EXPORTING size and stay centered") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> saving(static_cast<std::size_t>(width * height), 0xFFFFU);
  paint_chrome(saving, width, height, {.export_status = ChromeExportStatus::kSaving});
  int saving_ink_top = height;
  int saving_ink_bottom = 0;
  for (int y = 70; y < 108; ++y) {
    for (int x = 0; x < width; ++x) {
      if (saving[static_cast<std::size_t>(y * width + x)] == 0x2104U) {
        saving_ink_top = std::min(saving_ink_top, y);
        saving_ink_bottom = std::max(saving_ink_bottom, y);
      }
    }
  }
  REQUIRE(saving_ink_top < saving_ink_bottom);

  constexpr std::array statuses{ChromeTimeSyncStatus::kConnecting,
                                ChromeTimeSyncStatus::kSynchronizing, ChromeTimeSyncStatus::kSaved};
  for (const auto status : statuses) {
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0xFFFFU);
    paint_chrome(pixels, width, height, {.time_sync_status = status});

    int ink_left = width;
    int ink_right = 0;
    int ink_top = height;
    int ink_bottom = 0;
    for (int y = 70; y < 132; ++y) {
      for (int x = 0; x < width; ++x) {
        if (pixels[static_cast<std::size_t>(y * width + x)] == 0x2104U) {
          ink_left = std::min(ink_left, x);
          ink_right = std::max(ink_right, x);
          ink_top = std::min(ink_top, y);
          ink_bottom = std::max(ink_bottom, y);
        }
      }
    }
    REQUIRE(ink_left < ink_right);
    CHECK(ink_bottom - ink_top == saving_ink_bottom - saving_ink_top);
    CHECK(ink_left >= 80);
    CHECK(ink_right < 288);
    CHECK(std::abs((ink_left + ink_right) - (80 + 288)) <= 4);
  }
}

TEST_CASE("active time sync is modal and hides navigation overlays") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> connecting(static_cast<std::size_t>(width * height), 0xFFFFU);
  auto synchronizing = connecting;
  const ChromeState connecting_state{.time_sync_status = ChromeTimeSyncStatus::kConnecting};
  const ChromeState synchronizing_state{.time_sync_status = ChromeTimeSyncStatus::kSynchronizing};

  CHECK(tinydraw::vector_v2::chrome_contains({10.0F, 10.0F}, connecting_state));
  CHECK(tinydraw::vector_v2::chrome_input_bottom(connecting_state) == 0);
  CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 100.0F}, connecting_state) ==
        ChromeAction::kNone);

  paint_chrome(connecting, width, height, connecting_state);
  paint_chrome(synchronizing, width, height, synchronizing_state);
  CHECK(connecting != synchronizing);
}

TEST_CASE("rectangular bottom toolbar owns the curved lower display corners") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t untouched = 0x1234U;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), untouched);

  paint_chrome(pixels, width, height, {});

  CHECK(pixels[372U * width] == 0xBDF7U);
  CHECK(pixels[373U * width] == 0xDEDBU);
  CHECK(pixels[447U * width] == 0xFFFFU);
  CHECK(pixels[447U * width + 367U] == 0xFFFFU);
  CHECK(pixels[371U * width] == untouched);
}

TEST_CASE("recording indicator appears only while a V2 demo is recording") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> idle(static_cast<std::size_t>(width * height), 0xFFFFU);
  auto recording = idle;

  paint_chrome(idle, width, height, {});
  paint_chrome(recording, width, height, {.recording = true});

  const auto indicator = static_cast<std::size_t>(382 * width + 184);
  CHECK(idle[indicator] != 0xE186U);
  CHECK(recording[indicator] == 0xE186U);
  CHECK(tinydraw::vector_v2::chrome_recording_region() ==
        tinydraw::vector_v2::ChromeRect{178, 376, 191, 389});
}

TEST_CASE("color palette is a second-level popup above the persistent toolbar") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t untouched = 0x1234U;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), untouched);
  const ChromeState state{.popup = ChromePopup::kColors};

  paint_chrome(pixels, width, height, state);

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
  CHECK(tinydraw::vector_v2::chrome_contains({336.0F, 57.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_action_at({336.0F, 57.0F}, state) == ChromeAction::kZoomIn);
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 150.0F}, state) == ChromeAction::kNone);
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 200.0F}, state) == ChromeAction::kZoomOut);
  CHECK(tinydraw::vector_v2::chrome_contains({300.0F, 235.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_action_at({300.0F, 235.0F}, state) == ChromeAction::kZoomOut);

  // The complete navigation gutter consumes imprecise taps. The region above
  // the enlarged Plus target has no action, but it must not begin a Stroke.
  CHECK(tinydraw::vector_v2::chrome_contains({354.0F, 42.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_action_at({354.0F, 42.0F}, state) == ChromeAction::kNone);
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({287.0F, 42.0F}, state));

  const ChromeState popup{.popup = ChromePopup::kTools};
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({332.0F, 98.0F}, popup));
  CHECK(tinydraw::vector_v2::chrome_action_at({332.0F, 98.0F}, popup) == ChromeAction::kNone);
  const ChromeState hidden{.hud_visible = false};
  CHECK_FALSE(tinydraw::vector_v2::chrome_contains({354.0F, 42.0F}, hidden));
}

TEST_CASE("zoom rail presents internal render scales as user-facing multipliers") {
  CHECK(tinydraw::vector_v2::chrome_zoom_display_multiplier(25) == 1);
  CHECK(tinydraw::vector_v2::chrome_zoom_display_multiplier(50) == 2);
  CHECK(tinydraw::vector_v2::chrome_zoom_display_multiplier(100) == 4);
  CHECK(tinydraw::vector_v2::chrome_zoom_display_multiplier(200) == 8);
  CHECK(tinydraw::vector_v2::chrome_zoom_display_multiplier(400) == 16);
}

TEST_CASE("pan drags promote through the zoom rail while taps stay controls") {
  const ChromeState pan{.tool = tinydraw::vector_v2::ChromeTool::kPan};
  CHECK_FALSE(
      tinydraw::vector_v2::chrome_promotes_pan_drag({332.0F, 98.0F}, {337.0F, 103.0F}, pan));
  CHECK(tinydraw::vector_v2::chrome_promotes_pan_drag({332.0F, 98.0F}, {340.0F, 98.0F}, pan));
  CHECK(tinydraw::vector_v2::chrome_promotes_pan_drag({332.0F, 150.0F}, {332.0F, 142.0F}, pan));

  const ChromeState draw;
  CHECK_FALSE(
      tinydraw::vector_v2::chrome_promotes_pan_drag({332.0F, 98.0F}, {350.0F, 98.0F}, draw));
  const ChromeState popup{.tool = tinydraw::vector_v2::ChromeTool::kPan,
                          .popup = ChromePopup::kTools};
  CHECK_FALSE(
      tinydraw::vector_v2::chrome_promotes_pan_drag({332.0F, 98.0F}, {350.0F, 98.0F}, popup));
}

TEST_CASE("minimap hit guard absorbs imprecise touches around the rendered frame") {
  const ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_minimap_contains({250.0F, 236.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_minimap_contains({266.0F, 252.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_minimap_contains({367.0F, 371.0F}, state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_contains({249.0F, 235.0F}, state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_contains({249.0F, 372.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_contains({310.0F, 310.0F}, state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_contains({310.0F, 310.0F},
                                                           {.popup = ChromePopup::kTools}));
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 200,
      .level_width = 2944,
      .level_height = 3584,
  };
  CHECK(tinydraw::vector_v2::chrome_minimap_level_point({272.0F, 258.0F}, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{});
  CHECK(tinydraw::vector_v2::chrome_minimap_level_point({312.0F, 307.0F}, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{1472, 1792});
  CHECK(tinydraw::vector_v2::chrome_minimap_level_point({352.0F, 356.0F}, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{2944, 3584});
  // A captured drag keeps navigating after leaving the visible frame.
  CHECK(tinydraw::vector_v2::chrome_minimap_level_point({100.0F, 500.0F}, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{0, 3584});
}

TEST_CASE("ambiguous minimap dock presses preserve taps and promote deliberate drags") {
  const ChromeState state;
  // Both affected toolbar buttons remain truthful stationary taps.
  CHECK(tinydraw::vector_v2::chrome_action_at({274.0F, 400.0F}, state) ==
        ChromeAction::kToggleSizes);
  CHECK(tinydraw::vector_v2::chrome_action_at({334.0F, 400.0F}, state) ==
        ChromeAction::kToggleDocument);
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_contains({274.0F, 400.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_minimap_dock_drag_candidate({274.0F, 400.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_minimap_dock_drag_candidate({334.0F, 400.0F}, state));
  // The owner's captured misses landed at y=435..443: the drag candidate must
  // span the full dock, while a stationary release remains the same button.
  CHECK(tinydraw::vector_v2::chrome_action_at({334.0F, 442.0F}, state) ==
        ChromeAction::kToggleDocument);
  CHECK(tinydraw::vector_v2::chrome_minimap_dock_drag_candidate({274.0F, 442.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_minimap_dock_drag_candidate({334.0F, 442.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_promotes_minimap_dock_drag({334.0F, 442.0F}, {334.0F, 440.0F},
                                                               state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_promotes_minimap_dock_drag({274.0F, 400.0F},
                                                                     {281.0F, 400.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_promotes_minimap_dock_drag({274.0F, 400.0F}, {282.0F, 400.0F},
                                                               state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_promotes_minimap_dock_drag({274.0F, 400.0F},
                                                                     {274.0F, 399.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_promotes_minimap_dock_drag({274.0F, 400.0F}, {274.0F, 398.0F},
                                                               state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_dock_drag_candidate(
      {274.0F, 400.0F}, {.popup = ChromePopup::kSizes}));
}

TEST_CASE("minimap release keeps ownership across a split contact over chrome") {
  const ChromeState state;
  const ChromePoint released{332.0F, 150.0F};
  CHECK(tinydraw::vector_v2::chrome_suppresses_minimap_recontact(released, {334.0F, 153.0F}, 8'000U,
                                                                 state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_suppresses_minimap_recontact(released, {334.0F, 153.0F},
                                                                       120'001U, state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_suppresses_minimap_recontact(released, {280.0F, 150.0F},
                                                                       8'000U, state));
  CHECK_FALSE(tinydraw::vector_v2::chrome_suppresses_minimap_recontact(released, {280.0F, 200.0F},
                                                                       8'000U, state));
}

TEST_CASE("minimap pointer absolutely centers the viewport at every zoom") {
  constexpr tinydraw::vector_v2::ChromeLevelPoint focus{184, 186};
  const tinydraw::vector_v2::ChromeNavigation navigation{
      .zoom_percent = 400,
      .level_x = 5'520,
      .level_y = 6'796,
      .level_width = 5'888,
      .level_height = 7'168,
  };

  // A touch anywhere on the minimap acquires that absolute world position;
  // there is no requirement to grab the tiny viewport indicator first.
  CHECK(tinydraw::vector_v2::chrome_minimap_drag_origin({352.0F, 356.0F}, focus, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{5'520, 6'796});
  // Moving from the bottom-right to the map center must reach center within
  // the available finger travel, even at 400%.
  CHECK(tinydraw::vector_v2::chrome_minimap_drag_origin({312.0F, 307.0F}, focus, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{2'760, 3'398});
  // Projection clamps continuously at every world edge.
  CHECK(tinydraw::vector_v2::chrome_minimap_drag_origin({272.0F, 258.0F}, focus, navigation) ==
        tinydraw::vector_v2::ChromeLevelPoint{});
}

TEST_CASE("overview mutations schedule a minimap refresh outside its panel bounds") {
  const ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_minimap_refresh_required(state, true, true));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required(state, false, true));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required(state, true, false));
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required({.popup = ChromePopup::kTools},
                                                                   true, true));
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

  paint_chrome(pixels, width, height, state, navigation);

  CHECK(pixels[98U * width + 332U] == 0x2104U);
  CHECK(pixels[150U * width + 332U] != 0x1234U);
  CHECK(pixels[300U * width + 300U] == 0x4567U);
  CHECK(pixels[28U * width + 230U] == 0x2104U);
  CHECK(pixels[100U * width + 100U] == 0x1234U);
}

TEST_CASE("minimap viewport appears only above the whole-canvas zoom") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t white = 0xFFFFU;
  constexpr std::uint16_t selected = 0x349FU;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::vector<std::uint16_t> overview(pixel_count, white);

  const auto viewport_pixel_count = [&](int zoom_percent, int level_width, int level_height) {
    std::vector<std::uint16_t> pixels(pixel_count, white);
    paint_chrome(pixels, width, height, {},
                 {.zoom_percent = zoom_percent,
                  .level_width = level_width,
                  .level_height = level_height,
                  .overview_pixels = overview});
    std::size_t count = 0;
    for (int y = 251; y < 369; ++y) {
      for (int x = 265; x < 360; ++x) {
        count += pixels[static_cast<std::size_t>(y * width + x)] == selected ? 1U : 0U;
      }
    }
    return count;
  };

  CHECK(viewport_pixel_count(25, 368, 448) == 0U);
  CHECK(viewport_pixel_count(50, 736, 896) > 0U);
}

TEST_CASE("demo pointer blends a clipped translucent disk with an opaque rim") {
  constexpr int width = 64;
  constexpr int height = 64;
  constexpr std::uint16_t background = 0x001FU;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), background);

  const ChromePoint center{32.0F, 32.0F};
  CHECK(tinydraw::vector_v2::chrome_demo_pointer_region(center) ==
        tinydraw::vector_v2::ChromeRect{11, 11, 54, 54});
  REQUIRE(tinydraw::vector_v2::paint_demo_pointer({pixels, width, height, 0, 0}, center));

  CHECK(pixels[32U * width + 32U] != background);
  CHECK(pixels[32U * width + 32U] != 0xFFFFU);
  CHECK(pixels[32U * width + 52U] == 0x349FU);
  CHECK(pixels[32U * width + 53U] == background);

  std::vector<std::uint16_t> faded(static_cast<std::size_t>(width * height), background);
  REQUIRE(tinydraw::vector_v2::paint_demo_pointer({faded, width, height, 0, 0}, center, 160U));
  CHECK(faded[32U * width + 52U] != background);
  CHECK(faded[32U * width + 52U] != pixels[32U * width + 52U]);
  CHECK(faded[32U * width + 32U] != background);
  CHECK(faded[32U * width + 32U] != pixels[32U * width + 32U]);

  const auto before_swapped = pixels;
  CHECK_FALSE(tinydraw::vector_v2::paint_demo_pointer({pixels, width, height, 0, 0, true}, center));
  CHECK(pixels == before_swapped);
  CHECK(tinydraw::vector_v2::chrome_demo_pointer_region({0.0F, 0.0F}) ==
        tinydraw::vector_v2::ChromeRect{0, 0, 22, 22});
}

TEST_CASE("demo side button press paints a clipped right-edge tab") {
  constexpr int width = 40;
  constexpr int height = 68;
  constexpr int origin_x = 328;
  constexpr int origin_y = 36;
  constexpr std::uint16_t background = 0xFFFFU;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), background);

  CHECK(tinydraw::vector_v2::chrome_demo_side_button_region() ==
        tinydraw::vector_v2::ChromeRect{338, 40, 368, 100});
  REQUIRE(tinydraw::vector_v2::paint_demo_side_button({pixels, width, height, origin_x, origin_y}));
  CHECK(pixels[static_cast<std::size_t>(70 - origin_y) * width + (344 - origin_x)] != background);
  CHECK(pixels[static_cast<std::size_t>(70 - origin_y) * width + (350 - origin_x)] == background);

  const auto painted = pixels;
  CHECK_FALSE(tinydraw::vector_v2::paint_demo_side_button(
      {pixels, width, height, origin_x, origin_y, true}));
  CHECK(pixels == painted);
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

  state.popup = ChromePopup::kNone;
  state.hud_visible = false;
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 448);
  CHECK(tinydraw::vector_v2::chrome_input_bottom(state) == 448);
  CHECK(tinydraw::vector_v2::chrome_ink_bottom(state) == 448);
}

TEST_CASE("hidden chrome gives the complete panel to canvas input") {
  const ChromeState state{.hud_visible = false};
  for (const auto point : std::array{ChromePoint{230.0F, 28.0F}, ChromePoint{332.0F, 98.0F},
                                     ChromePoint{300.0F, 300.0F}, ChromePoint{180.0F, 410.0F}}) {
    CHECK_FALSE(tinydraw::vector_v2::chrome_contains(point, state));
    CHECK(tinydraw::vector_v2::chrome_action_at(point, state) == ChromeAction::kNone);
  }
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 448);
  CHECK(tinydraw::vector_v2::chrome_input_bottom(state) == 448);
  CHECK(tinydraw::vector_v2::chrome_ink_bottom(state) == 448);
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_region(state).has_value());
  CHECK_FALSE(tinydraw::vector_v2::chrome_minimap_refresh_required(state, true, true));
}

TEST_CASE("hidden chrome leaves every canvas pixel untouched") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height));
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    pixels[index] = static_cast<std::uint16_t>((index * 73U + 19U) & 0xFFFFU);
  }
  const auto before = pixels;

  paint_chrome(pixels, width, height, {.hud_visible = false});

  CHECK(pixels == before);
}

TEST_CASE("hiding the HUD dismisses transient popups and critical screens block the toggle") {
  for (const ChromePopup popup : std::array{ChromePopup::kTools, ChromePopup::kColors,
                                            ChromePopup::kSizes, ChromePopup::kDocument}) {
    ChromeState state{.popup = popup};
    CHECK(tinydraw::vector_v2::toggle_hud_visibility(state));
    CHECK_FALSE(state.hud_visible);
    CHECK(state.popup == ChromePopup::kNone);
  }

  for (const ChromeState blocked :
       std::array{ChromeState{.confirm_new = true},
                  ChromeState{.export_status = ChromeExportStatus::kSaving},
                  ChromeState{.time_sync_status = ChromeTimeSyncStatus::kConnecting}}) {
    auto candidate = blocked;
    CHECK_FALSE(tinydraw::vector_v2::chrome_can_toggle_hud(candidate));
    CHECK_FALSE(tinydraw::vector_v2::toggle_hud_visibility(candidate));
    CHECK(candidate == blocked);
  }
}

TEST_CASE("critical dialogs remain visible when the HUD is hidden") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0x1234U);
  const ChromeState state{.hud_visible = false, .confirm_new = true};

  CHECK(tinydraw::vector_v2::chrome_contains({10.0F, 10.0F}, state));
  CHECK(tinydraw::vector_v2::chrome_input_bottom(state) == 0);
  paint_chrome(pixels, width, height, state);
  CHECK(pixels[140U * width + 180U] == 0xFFFFU);
  CHECK(pixels[410U * width + 180U] == 0x1234U);
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

  paint_chrome(pixels, width, height, state);

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

  paint_chrome(empty, width, height, empty_state);
  paint_chrome(half, width, height, half_state);
  CHECK(empty[116U * width + 150U] == 0xFFFFU);
  CHECK(half[116U * width + 150U] == 0x349FU);
  CHECK(empty != half);
}

TEST_CASE("export UI names its work and keeps exit inside the dialog") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t white = 0xFFFFU;
  constexpr std::uint16_t ink = 0x2104U;

  const auto label_is_rendered = [&](const std::vector<std::uint16_t>& pixels, int text_x,
                                     int text_y, std::string_view text, std::uint16_t color,
                                     int scale) {
    int glyph_x = text_x;
    for (const char character : text) {
      if (character != ' ') {
        bool rendered = false;
        for (int y = text_y; y < text_y + 7 * scale; ++y) {
          for (int x = glyph_x; x < glyph_x + 5 * scale; ++x) {
            rendered = rendered || pixels[static_cast<std::size_t>(y * width + x)] == color;
          }
        }
        CHECK_MESSAGE(rendered, "missing export UI glyph: ", character);
      }
      glyph_x += 6 * scale;
    }
  };

  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), white);
  paint_chrome(pixels, width, height, {.export_status = ChromeExportStatus::kSaving});
  label_is_rendered(pixels, 105, 82, "EXPORTING", ink, 3);
  CHECK(pixels[100U * width + 79U] == 0xDEDBU);

  std::array exporting_bounds{width, -1};
  for (int y = 82; y < 103; ++y) {
    for (int x = 0; x < width; ++x) {
      if (pixels[static_cast<std::size_t>(y * width + x)] == ink) {
        exporting_bounds[0] = std::min(exporting_bounds[0], x);
        exporting_bounds[1] = std::max(exporting_bounds[1], x);
      }
    }
  }
  REQUIRE(exporting_bounds[1] >= exporting_bounds[0]);
  CHECK(exporting_bounds[0] + exporting_bounds[1] == doctest::Approx(367).epsilon(0.01));
  CHECK(exporting_bounds[0] - 80 >= 20);
  CHECK(287 - exporting_bounds[1] >= 20);

  pixels.assign(pixels.size(), white);
  paint_chrome(pixels, width, height, {.export_status = ChromeExportStatus::kPresented});
  CHECK(tinydraw::vector_v2::chrome_action_at({56.0F, 268.0F},
                                              {.export_status = ChromeExportStatus::kPresented}) ==
        ChromeAction::kExitExport);
  CHECK(tinydraw::vector_v2::chrome_action_at({48.0F, 268.0F},
                                              {.export_status = ChromeExportStatus::kPresented}) ==
        ChromeAction::kNone);
  CHECK(pixels[268U * width + 20U] == white);
  label_is_rendered(pixels, 78, 257, "EJECT & EXIT", white, 3);

  std::array return_bounds{width, -1};
  for (int y = 257; y < 278; ++y) {
    for (int x = 52; x < 316; ++x) {
      if (pixels[static_cast<std::size_t>(y * width + x)] == white) {
        return_bounds[0] = std::min(return_bounds[0], x);
        return_bounds[1] = std::max(return_bounds[1], x);
      }
    }
  }
  REQUIRE(return_bounds[1] >= return_bounds[0]);
  CHECK(return_bounds[0] + return_bounds[1] == doctest::Approx(367).epsilon(0.01));
  CHECK(return_bounds[1] - return_bounds[0] < 220);
  CHECK(return_bounds[0] - 52 >= 20);
  CHECK(315 - return_bounds[1] >= 20);
}

TEST_CASE("USB export mode blocks drawing and offers an explicit return action") {
  constexpr int width = 368;
  constexpr int height = 448;
  const std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::array<std::vector<std::uint16_t>, 3> renderings;
  std::size_t rendering_index = 0;

  for (const auto status : {ChromeExportStatus::kPresented, ChromeExportStatus::kHostEjected,
                            ChromeExportStatus::kExitError}) {
    const ChromeState state{.export_status = status};
    CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 0);
    CHECK(tinydraw::vector_v2::chrome_input_bottom(state) == 0);
    CHECK(tinydraw::vector_v2::chrome_ink_bottom(state) == 0);
    CHECK(tinydraw::vector_v2::chrome_contains({10.0F, 10.0F}, state));
    CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 268.0F}, state) ==
          ChromeAction::kExitExport);
    CHECK(tinydraw::vector_v2::chrome_action_at({184.0F, 180.0F}, state) == ChromeAction::kNone);

    auto& pixels = renderings[rendering_index++];
    pixels.assign(pixel_count, 0x1234U);
    paint_chrome(pixels, width, height, state);
    CHECK(pixels[0] == 0xFFFFU);
    CHECK(pixels[245U * width + 60U] == 0x349FU);
  }
  CHECK(renderings[0] != renderings[1]);
  CHECK(renderings[1] != renderings[2]);
}

TEST_CASE("USB export screen font renders every displayed character") {
  constexpr int width = 368;
  constexpr int height = 448;
  constexpr std::uint16_t white = 0xFFFFU;
  constexpr std::uint16_t ink = 0x2104U;
  constexpr std::uint16_t muted = 0x8410U;
  constexpr std::uint16_t selected = 0x349FU;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), white);
  paint_chrome(pixels, width, height, {.export_status = ChromeExportStatus::kPresented});

  const auto check_label = [&](int text_x, int text_y, std::string_view text, std::uint16_t color,
                               int scale) {
    int glyph_x = text_x;
    for (const char character : text) {
      if (character != ' ') {
        bool rendered = false;
        for (int y = text_y; y < text_y + 7 * scale; ++y) {
          for (int x = glyph_x; x < glyph_x + 5 * scale; ++x) {
            rendered = rendered || pixels[static_cast<std::size_t>(y * width + x)] == color;
          }
        }
        CHECK_MESSAGE(rendered, "missing export-screen glyph: ", character);
      }
      glyph_x += 6 * scale;
    }
  };

  check_label(96, 122, "USB EXPORT", ink, 3);
  check_label(95, 174, "READ-ONLY DRIVE", ink, 2);
  check_label(95, 205, "COPY YOUR FILES", muted, 2);
  check_label(78, 257, "EJECT & EXIT", white, 3);
  CHECK(pixels[245U * width + 60U] == selected);

  const auto horizontal_bounds = [&](int y0, int y1, std::uint16_t color) {
    std::array bounds{width, -1};
    for (int y = y0; y < y1; ++y) {
      for (int x = 0; x < width; ++x) {
        if (pixels[static_cast<std::size_t>(y * width + x)] == color) {
          bounds[0] = std::min(bounds[0], x);
          bounds[1] = std::max(bounds[1], x);
        }
      }
    }
    return bounds;
  };
  const auto presented_subtitle_bounds = horizontal_bounds(205, 219, muted);
  REQUIRE(presented_subtitle_bounds[1] >= presented_subtitle_bounds[0]);
  CHECK(presented_subtitle_bounds[0] + presented_subtitle_bounds[1] ==
        doctest::Approx(367).epsilon(0.01));

  pixels.assign(pixels.size(), white);
  paint_chrome(pixels, width, height, {.export_status = ChromeExportStatus::kHostEjected});
  check_label(107, 174, "DRIVE EJECTED", selected, 2);
  check_label(101, 205, "SAFE TO RETURN", muted, 2);

  const auto title_bounds = horizontal_bounds(174, 188, selected);
  const auto subtitle_bounds = horizontal_bounds(205, 219, muted);
  REQUIRE(title_bounds[1] >= title_bounds[0]);
  REQUIRE(subtitle_bounds[1] >= subtitle_bounds[0]);
  CHECK(title_bounds[0] + title_bounds[1] == doctest::Approx(367).epsilon(0.01));
  CHECK(subtitle_bounds[0] + subtitle_bounds[1] == doctest::Approx(367).epsilon(0.01));
}

TEST_CASE("palette hit testing gives large cells to the circular swatches") {
  const ChromeState state{.popup = ChromePopup::kColors};
  CHECK_FALSE(tinydraw::vector_v2::chrome_color_at({46.0F, 63.0F}, state).has_value());
  CHECK(tinydraw::vector_v2::chrome_color_at({91.0F, 103.0F}, state) == 0U);
  CHECK(tinydraw::vector_v2::chrome_color_at({92.0F, 103.0F}, state) == 1U);
  CHECK(tinydraw::vector_v2::chrome_color_at({322.0F, 138.0F}, state) == 3U);
  CHECK(tinydraw::vector_v2::chrome_color_at({46.0F, 139.0F}, state) == 4U);
}

TEST_CASE("canvas edge attraction supports full-bleed ink without a discontinuity") {
  const ChromeState shown;
  const ChromeState hidden{.hud_visible = false};
  const auto snapped_low = tinydraw::vector_v2::attract_canvas_edges({18.0F, 18.0F}, shown);
  CHECK(snapped_low.x == 0.0F);
  CHECK(snapped_low.y == 0.0F);
  const auto transition = tinydraw::vector_v2::attract_canvas_edges({27.0F, 27.0F}, shown);
  CHECK(transition.x == 18.0F);
  CHECK(transition.y == 18.0F);
  const auto unchanged = tinydraw::vector_v2::attract_canvas_edges({36.0F, 36.0F}, shown);
  CHECK(unchanged.x == 36.0F);
  CHECK(unchanged.y == 36.0F);
  const auto snapped_right = tinydraw::vector_v2::attract_canvas_edges({349.0F, 430.0F}, shown);
  CHECK(snapped_right.x == 367.0F);
  CHECK(snapped_right.y == 430.0F);
  const auto snapped_bottom = tinydraw::vector_v2::attract_canvas_edges({349.0F, 430.0F}, hidden);
  CHECK(snapped_bottom.x == 367.0F);
  CHECK(snapped_bottom.y == 447.0F);
}

TEST_CASE("top-edge exit does not become a tap stroke") {
  CHECK_FALSE(tinydraw::vector_v2::chrome_accepts_stroke_finish({193.0F, 0.0F}, false));
  CHECK_FALSE(tinydraw::vector_v2::chrome_accepts_stroke_finish({193.0F, 1.0F}, false));
  CHECK_FALSE(tinydraw::vector_v2::chrome_accepts_stroke_finish({193.0F, 3.0F}, false));
  CHECK(tinydraw::vector_v2::chrome_accepts_stroke_finish({193.0F, 1.0F}, true));
  CHECK(tinydraw::vector_v2::chrome_accepts_stroke_finish({193.0F, 4.0F}, false));
}

TEST_CASE("chrome rendering stays within the framebuffer") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0xFFFFU);
  const auto untouched = pixels[100U * width + 100U];
  paint_chrome(pixels, width, height, {});
  CHECK(pixels[100U * width + 100U] == untouched);
  CHECK(pixels[410U * width + 214U] != 0xFFFFU);

  pixels.assign(pixels.size(), 0x1234U);
  paint_chrome(pixels, width, height, {});
  for (int y = tinydraw::vector_v2::kChromeCanvasBottom; y < 374; ++y) {
    for (int x = 0; x < width; ++x) {
      CHECK(pixels[static_cast<std::size_t>(y * width + x)] != 0x1234U);
    }
  }

  ChromeState popup{.popup = ChromePopup::kTools};
  pixels.assign(pixels.size(), 0x1234U);
  paint_chrome(pixels, width, height, popup);
  for (int y = tinydraw::vector_v2::kChromePopupCanvasBottom; y < 296; ++y) {
    for (int x = 0; x < width; ++x) {
      CHECK(pixels[static_cast<std::size_t>(y * width + x)] != 0x1234U);
    }
  }

  const ChromeState palette{.popup = ChromePopup::kColors};
  paint_chrome(pixels, width, height, palette);
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

  paint_chrome(pen, width, height, {.tool = ChromeTool::kDraw});
  paint_chrome(eraser, width, height, {.tool = ChromeTool::kErase});
  paint_chrome(hand, width, height, {.tool = ChromeTool::kPan});

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

  paint_chrome(pixels, width, height, {.confirm_new = true});

  CHECK(pixels[20U * width + 20U] == 0x1234U);
  CHECK(pixels[140U * width + 180U] == 0xFFFFU);
}

}  // namespace

TEST_CASE("only byte-order-aware chrome states accept a pre-swapped transfer surface") {
  CHECK(tinydraw::vector_v2::chrome_accepts_byte_swapped_staging({}));
  CHECK(tinydraw::vector_v2::chrome_accepts_byte_swapped_staging({.hud_visible = false}));
  CHECK_FALSE(
      tinydraw::vector_v2::chrome_accepts_byte_swapped_staging({.popup = ChromePopup::kColors}));
  CHECK_FALSE(tinydraw::vector_v2::chrome_accepts_byte_swapped_staging({.confirm_new = true}));
  CHECK_FALSE(tinydraw::vector_v2::chrome_accepts_byte_swapped_staging(
      {.export_status = tinydraw::vector_v2::ChromeExportStatus::kSaved}));
  CHECK_FALSE(tinydraw::vector_v2::chrome_accepts_byte_swapped_staging({.history_busy = true}));

  constexpr int width = 368;
  constexpr int height = 448;
  const ChromeState palette{.popup = ChromePopup::kColors};
  const tinydraw::vector_v2::ChromeNavigation navigation{};
  std::vector<std::uint16_t> cache_pixels(tinydraw::vector_v2::kChromeStagingCachePixels);
  tinydraw::vector_v2::ChromeStagingCache cache(cache_pixels);
  REQUIRE(cache.prepare(palette, navigation, 0U));
  std::vector<std::uint16_t> swapped(static_cast<std::size_t>(width * height), 0x3412U);
  const auto before = swapped;
  CHECK_FALSE(cache.paint_prepared({swapped, width, height, 0, 0, true}, palette, navigation, 0U));
  CHECK(swapped == before);

  std::vector<std::uint16_t> host(static_cast<std::size_t>(width * height), 0x1234U);
  REQUIRE(cache.paint_prepared({host, width, height, 0, 0}, palette, navigation, 0U));
  CHECK(host[108U * width + 46U] == tinydraw::vector_v2::kPico8Palettes[0][0]);

  const ChromeState hidden_hud{.hud_visible = false};
  REQUIRE(cache.prepare(hidden_hud, navigation, 0U));
  swapped.assign(swapped.size(), 0x3412U);
  REQUIRE(cache.paint_prepared({swapped, width, height, 0, 0, true}, hidden_hud, navigation, 0U));
  CHECK(swapped[428U * width + 140U] == 0x3412U);
}

TEST_CASE("history control damage is limited to undo and redo dock cells") {
  const auto region = tinydraw::vector_v2::chrome_history_controls_region();
  CHECK(region == tinydraw::vector_v2::ChromeRect{0, 372, 124, 448});
  CHECK(region.x1 < 140);
}

TEST_CASE("prepared staging cache reproduces chrome without mutating canvas source") {
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
  paint_chrome(reference, width, height, state, navigation);

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
    REQUIRE(cache.paint_prepared({strip, width, rows, 0, y}, state, navigation, 7U));
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
    REQUIRE(cache.paint_prepared({strip, width, rows, 0, y, true}, state, navigation, 7U));
  }
  CHECK(swapped_cached == swapped_reference);

  auto moved = navigation;
  moved.level_x += 137;
  moved.level_y += 211;
  std::vector<std::uint16_t> moved_reference = canvas;
  paint_chrome(moved_reference, width, height, state, moved);
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
    REQUIRE(cache.paint_prepared({strip, width, rows, 0, y}, state, moved, 7U));
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
