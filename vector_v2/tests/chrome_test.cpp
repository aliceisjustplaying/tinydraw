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

TEST_CASE("full screen palette maps all sixteen swatches and both page actions") {
  const ChromeState state{.popup = ChromePopup::kColors};
  std::array<bool, 16> seen{};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const tinydraw::vector_v2::ChromePoint point{46.0F + column * 92.0F, 108.0F + row * 90.0F};
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

TEST_CASE("chrome canvas clipping follows the visible overlay") {
  ChromeState state;
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 372);
  auto clipped =
      tinydraw::vector_v2::clip_canvas_segment({100.0F, 280.0F}, {120.0F, 400.0F}, state);
  REQUIRE(clipped.has_value());
  CHECK(clipped->y == doctest::Approx(371.0F));

  state.popup = ChromePopup::kTools;
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
  CHECK(tinydraw::vector_v2::chrome_color_at({91.0F, 108.0F}, state) == 0U);
  CHECK(tinydraw::vector_v2::chrome_color_at({92.0F, 108.0F}, state) == 1U);
  CHECK(tinydraw::vector_v2::chrome_color_at({322.0F, 153.0F}, state) == 3U);
  CHECK(tinydraw::vector_v2::chrome_color_at({46.0F, 154.0F}, state) == 4U);
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
