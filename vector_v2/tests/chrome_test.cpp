#include "tinydraw/vector_v2/chrome.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using tinydraw::vector_v2::ChromeAction;
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
      const tinydraw::vector_v2::ChromePoint point{48.0F + column * 92.0F, 82.0F + row * 94.0F};
      const auto color = tinydraw::vector_v2::chrome_color_at(point, state);
      REQUIRE(color.has_value());
      seen[*color] = true;
      CHECK(tinydraw::vector_v2::chrome_action_at(point, state) == ChromeAction::kSelectColor);
    }
  }
  for (const bool present : seen) {
    CHECK(present);
  }
  CHECK(tinydraw::vector_v2::chrome_action_at({30.0F, 20.0F}, state) ==
        ChromeAction::kPreviousPalette);
  CHECK(tinydraw::vector_v2::chrome_action_at({340.0F, 20.0F}, state) ==
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
  CHECK(tinydraw::vector_v2::chrome_canvas_bottom(state) == 288);
  clipped = tinydraw::vector_v2::clip_canvas_segment({100.0F, 280.0F}, {120.0F, 331.0F}, state);
  REQUIRE(clipped.has_value());
  CHECK(clipped->y == doctest::Approx(287.0F));
  CHECK_FALSE(tinydraw::vector_v2::clip_canvas_segment({120.0F, 331.0F}, {140.0F, 340.0F}, state)
                  .has_value());
}

TEST_CASE("palette hit testing matches rendered integer cell boundaries") {
  const ChromeState state{.popup = ChromePopup::kColors};
  CHECK(tinydraw::vector_v2::chrome_color_at({89.0F, 82.0F}, state) == 0U);
  CHECK_FALSE(tinydraw::vector_v2::chrome_color_at({90.0F, 82.0F}, state).has_value());
  CHECK(tinydraw::vector_v2::chrome_color_at({96.0F, 82.0F}, state) == 1U);
  CHECK(tinydraw::vector_v2::chrome_color_at({276.0F, 82.0F}, state) == 3U);
}

TEST_CASE("chrome rendering stays within the framebuffer") {
  constexpr int width = 368;
  constexpr int height = 448;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width * height), 0xFFFFU);
  const auto untouched = pixels[100U * width + 100U];
  tinydraw::vector_v2::draw_chrome(pixels, width, height, {});
  CHECK(pixels[100U * width + 100U] == untouched);
  CHECK(pixels[410U * width + 214U] != 0xFFFFU);

  const ChromeState palette{.popup = ChromePopup::kColors};
  tinydraw::vector_v2::draw_chrome(pixels, width, height, palette);
  CHECK(pixels[82U * width + 48U] == tinydraw::vector_v2::kPico8Palettes[0][0]);
}

}  // namespace
