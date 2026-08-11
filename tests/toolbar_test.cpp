#include "tinydraw/ui/toolbar.h"

#include <doctest.h>

#include <array>
#include <vector>

TEST_CASE("default toolbar keeps every control in one full-width row") {
  const tinydraw::ToolbarState state;

  CHECK(tinydraw::toolbar_action_at({37.0F, 401.0F}, state) == tinydraw::ToolbarAction::kUndo);
  CHECK(tinydraw::toolbar_action_at({96.0F, 401.0F}, state) ==
        tinydraw::ToolbarAction::kToggleTools);
  CHECK(tinydraw::toolbar_action_at({155.0F, 401.0F}, state) ==
        tinydraw::ToolbarAction::kSelectEraser);
  CHECK(tinydraw::toolbar_action_at({213.0F, 401.0F}, state) ==
        tinydraw::ToolbarAction::kToggleColors);
  CHECK(tinydraw::toolbar_action_at({272.0F, 401.0F}, state) ==
        tinydraw::ToolbarAction::kToggleSizes);
  CHECK(tinydraw::toolbar_action_at({331.0F, 401.0F}, state) ==
        tinydraw::ToolbarAction::kNewDrawing);
}

TEST_CASE("pen and hand controls appear in a modal tool popup") {
  tinydraw::ToolbarState state;
  state.tools_open = true;

  CHECK(tinydraw::toolbar_action_at({96.0F, 331.0F}, state) == tinydraw::ToolbarAction::kSelectPen);
  CHECK(tinydraw::toolbar_action_at({272.0F, 331.0F}, state) ==
        tinydraw::ToolbarAction::kSelectPan);
  CHECK(tinydraw::toolbar_overlay_top(state) == 295);
  CHECK(tinydraw::toolbar_action_at({96.0F, 409.0F}, state) ==
        tinydraw::ToolbarAction::kToggleTools);
  CHECK(tinydraw::toolbar_action_at({213.0F, 409.0F}, state) == tinydraw::ToolbarAction::kNone);
}

TEST_CASE("tldraw color controls fill a three by four popup") {
  tinydraw::ToolbarState state;
  CHECK(tinydraw::toolbar_action_at({52.0F, 207.0F}, state) == tinydraw::ToolbarAction::kNone);
  CHECK_FALSE(tinydraw::toolbar_contains({52.0F, 207.0F}, state));

  state.colors_open = true;
  constexpr std::array colors{
      tinydraw::InkColor::kBlack,       tinydraw::InkColor::kGrey,
      tinydraw::InkColor::kLightViolet, tinydraw::InkColor::kViolet,
      tinydraw::InkColor::kBlue,        tinydraw::InkColor::kLightBlue,
      tinydraw::InkColor::kYellow,      tinydraw::InkColor::kOrange,
      tinydraw::InkColor::kGreen,       tinydraw::InkColor::kLightGreen,
      tinydraw::InkColor::kLightRed,    tinydraw::InkColor::kRed,
  };
  constexpr std::array centers_x{52.0F, 140.0F, 228.0F, 316.0F};
  constexpr std::array centers_y{207.0F, 273.0F, 339.0F};
  for (std::size_t row = 0; row < centers_y.size(); ++row) {
    for (std::size_t column = 0; column < centers_x.size(); ++column) {
      const tinydraw::Point point{centers_x[column], centers_y[row]};
      CHECK(tinydraw::toolbar_action_at(point, state) == tinydraw::ToolbarAction::kSelectColor);
      CHECK(tinydraw::toolbar_color_at(point, state) == colors[row * centers_x.size() + column]);
    }
  }
  CHECK(tinydraw::toolbar_contains({52.0F, 207.0F}, state));
  CHECK(tinydraw::toolbar_overlay_contains({52.0F, 207.0F}, state));
  CHECK(tinydraw::toolbar_overlay_top(state) == 179);
  CHECK(tinydraw::toolbar_action_at({37.0F, 409.0F}, state) == tinydraw::ToolbarAction::kNone);
  CHECK(tinydraw::toolbar_action_at({213.0F, 409.0F}, state) ==
        tinydraw::ToolbarAction::kToggleColors);
  CHECK(tinydraw::toolbar_action_at({331.0F, 409.0F}, state) == tinydraw::ToolbarAction::kNone);
}

TEST_CASE("palette uses tldraw's twelve default colors") {
  constexpr std::array colors{
      tinydraw::InkColor::kBlack,       tinydraw::InkColor::kGrey,
      tinydraw::InkColor::kLightViolet, tinydraw::InkColor::kViolet,
      tinydraw::InkColor::kBlue,        tinydraw::InkColor::kLightBlue,
      tinydraw::InkColor::kYellow,      tinydraw::InkColor::kOrange,
      tinydraw::InkColor::kGreen,       tinydraw::InkColor::kLightGreen,
      tinydraw::InkColor::kLightRed,    tinydraw::InkColor::kRed,
  };
  constexpr std::array rgb565{0x18E3U, 0x9D56U, 0xE43EU, 0xA9F9U, 0x433DU, 0x4D1EU,
                              0xF569U, 0xE343U, 0x0C8DU, 0x4D8BU, 0xFBAEU, 0xE186U};
  for (std::size_t index = 0; index < colors.size(); ++index) {
    CHECK(tinydraw::rgb565(colors[index]) == rgb565[index]);
  }
}

TEST_CASE("four large size controls appear in one second row") {
  tinydraw::ToolbarState state;
  CHECK(tinydraw::toolbar_action_at({52.0F, 331.0F}, state) == tinydraw::ToolbarAction::kNone);

  state.sizes_open = true;
  CHECK(tinydraw::toolbar_action_at({52.0F, 331.0F}, state) ==
        tinydraw::ToolbarAction::kSelectSmall);
  CHECK(tinydraw::toolbar_action_at({140.0F, 331.0F}, state) ==
        tinydraw::ToolbarAction::kSelectMedium);
  CHECK(tinydraw::toolbar_action_at({228.0F, 331.0F}, state) ==
        tinydraw::ToolbarAction::kSelectLarge);
  CHECK(tinydraw::toolbar_action_at({316.0F, 331.0F}, state) ==
        tinydraw::ToolbarAction::kSelectExtraLarge);
  CHECK(tinydraw::toolbar_contains({316.0F, 331.0F}, state));
  CHECK(tinydraw::toolbar_overlay_top(state) == 295);
  CHECK(tinydraw::toolbar_action_at({96.0F, 409.0F}, state) == tinydraw::ToolbarAction::kNone);
  CHECK(tinydraw::toolbar_action_at({272.0F, 409.0F}, state) ==
        tinydraw::ToolbarAction::kToggleSizes);
}

TEST_CASE("floating controls include forgiving vertical tap margins") {
  tinydraw::ToolbarState state;
  CHECK(tinydraw::toolbar_contains({360.0F, 409.0F}, state));
  CHECK(tinydraw::toolbar_contains({96.0F, 367.0F}, state));
  CHECK(tinydraw::toolbar_action_at({96.0F, 367.0F}, state) ==
        tinydraw::ToolbarAction::kToggleTools);
  CHECK_FALSE(tinydraw::toolbar_contains({96.0F, 365.0F}, state));
  CHECK_FALSE(tinydraw::toolbar_contains({180.0F, 331.0F}, state));

  state.colors_open = true;
  CHECK(tinydraw::toolbar_contains({52.0F, 173.0F}, state));
  CHECK(tinydraw::toolbar_action_at({52.0F, 173.0F}, state) ==
        tinydraw::ToolbarAction::kSelectColor);
  CHECK(tinydraw::toolbar_color_at({52.0F, 173.0F}, state) == tinydraw::InkColor::kBlack);
  CHECK_FALSE(tinydraw::toolbar_contains({52.0F, 171.0F}, state));
  CHECK_FALSE(tinydraw::toolbar_contains({180.0F, 150.0F}, state));
}

TEST_CASE("new drawing confirmation captures input and exposes large choices") {
  const tinydraw::ToolbarState state{.confirm_new = true};

  CHECK(tinydraw::toolbar_contains({10.0F, 10.0F}, state));
  CHECK(tinydraw::toolbar_contains({180.0F, 180.0F}, state));
  CHECK(tinydraw::toolbar_action_at({100.0F, 230.0F}, state) ==
        tinydraw::ToolbarAction::kCancelNewDrawing);
  CHECK(tinydraw::toolbar_action_at({260.0F, 230.0F}, state) ==
        tinydraw::ToolbarAction::kConfirmNewDrawing);
  CHECK(tinydraw::toolbar_action_at({180.0F, 160.0F}, state) == tinydraw::ToolbarAction::kNone);

  CHECK(tinydraw::toolbar_overlay_contains({180.0F, 160.0F}, state));
  CHECK_FALSE(tinydraw::toolbar_overlay_contains({10.0F, 10.0F}, state));
  CHECK(tinydraw::toolbar_overlay_top(state) == 125);
}

TEST_CASE("pen sizes expose four bounded choices") {
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kSmall) <
        tinydraw::brush_size(tinydraw::PenSize::kMedium));
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kMedium) <
        tinydraw::brush_size(tinydraw::PenSize::kLarge));
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kLarge) <
        tinydraw::brush_size(tinydraw::PenSize::kExtraLarge));
}

TEST_CASE("toolbar rendering reflects tool and palette state without touching open canvas") {
  const auto pixel_count =
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
  std::vector<std::uint16_t> pen_canvas(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> eraser_canvas = pen_canvas;
  std::vector<std::uint16_t> pan_canvas = pen_canvas;
  std::vector<std::uint16_t> palette_canvas = pen_canvas;

  tinydraw::draw_toolbar(pen_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kPen});
  tinydraw::draw_toolbar(eraser_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kEraser});
  tinydraw::draw_toolbar(pan_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kPan});
  tinydraw::draw_toolbar(palette_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.colors_open = true});

  CHECK(pen_canvas != eraser_canvas);
  CHECK(pen_canvas != pan_canvas);
  CHECK(pen_canvas != palette_canvas);
  const auto open_canvas_pixel = static_cast<std::size_t>(150 * tinydraw::kCanvasWidth + 180);
  const auto top_border_pixel = static_cast<std::size_t>(373 * tinydraw::kCanvasWidth);
  const auto bottom_border_pixel = static_cast<std::size_t>(444 * tinydraw::kCanvasWidth + 367);
  CHECK(pen_canvas[open_canvas_pixel] == 0xFFFFU);
  CHECK(eraser_canvas[open_canvas_pixel] == 0xFFFFU);
  CHECK(palette_canvas[open_canvas_pixel] == 0xFFFFU);
  CHECK(pen_canvas[top_border_pixel] == 0xDEDBU);
  CHECK(pen_canvas[bottom_border_pixel] == 0xDEDBU);
}

TEST_CASE("recording indicator appears only while recording") {
  const auto pixel_count =
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
  std::vector<std::uint16_t> idle(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> recording = idle;

  tinydraw::draw_toolbar(idle, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, {});
  tinydraw::draw_toolbar(recording, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.recording = true});

  const auto indicator = static_cast<std::size_t>(382 * tinydraw::kCanvasWidth + 184);
  CHECK(idle[indicator] != 0xE186U);
  CHECK(recording[indicator] == 0xE186U);
}

TEST_CASE("battery status appears in the toolbar without covering the canvas") {
  const auto pixel_count =
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
  std::vector<std::uint16_t> absent(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> half = absent;
  std::vector<std::uint16_t> charging = absent;

  const tinydraw::ToolbarState half_state{.battery_percentage = 50};
  const tinydraw::ToolbarState charging_state{
      .battery_percentage = 50, .battery_charging = true, .external_power = true};
  tinydraw::draw_toolbar(absent, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, {});
  tinydraw::draw_toolbar(half, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, half_state);
  tinydraw::draw_toolbar(charging, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, charging_state);

  const auto battery_outline = static_cast<std::size_t>(31 * tinydraw::kCanvasWidth + 214);
  const auto open_canvas_pixel = static_cast<std::size_t>(100 * tinydraw::kCanvasWidth + 307);
  CHECK(absent != half);
  CHECK(half != charging);
  CHECK(half[battery_outline] == 0x2104U);
  CHECK(charging[battery_outline] == 0x349FU);
  CHECK(half[open_canvas_pixel] == 0xFFFFU);
  CHECK(charging[open_canvas_pixel] == 0xFFFFU);
  CHECK_FALSE(tinydraw::toolbar_contains({300.0F, 30.0F}, half_state));
  CHECK(tinydraw::toolbar_overlay_contains({300.0F, 30.0F}, half_state));
  const auto overlay = tinydraw::battery_overlay_rect(half_state);
  REQUIRE(overlay.has_value());
  CHECK(overlay->x0 == 201);
  CHECK(overlay->y0 == 17);
  CHECK(overlay->x1 == 348);
  CHECK(overlay->y1 == 72);
}

TEST_CASE("new drawing confirmation renders without covering the canvas") {
  const auto pixel_count =
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
  std::vector<std::uint16_t> canvas(pixel_count, 0x001FU);

  tinydraw::draw_toolbar(canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.confirm_new = true});

  const auto untouched_pixel = static_cast<std::size_t>(20 * tinydraw::kCanvasWidth + 20);
  const auto dialog_pixel = static_cast<std::size_t>(140 * tinydraw::kCanvasWidth + 180);
  CHECK(canvas[untouched_pixel] == 0x001FU);
  CHECK(canvas[dialog_pixel] == 0xFFFFU);
}
