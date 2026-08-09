#include "tinydraw/ui/toolbar.h"

#include <doctest.h>

#include <vector>

TEST_CASE("bottom toolbar fills the row with finger-sized primary controls") {
  const tinydraw::ToolbarState state;

  CHECK(tinydraw::toolbar_action_at({52.0F, 409.0F}, state) == tinydraw::ToolbarAction::kSelectPen);
  CHECK(tinydraw::toolbar_action_at({140.0F, 409.0F}, state) ==
        tinydraw::ToolbarAction::kSelectEraser);
  CHECK(tinydraw::toolbar_action_at({228.0F, 409.0F}, state) ==
        tinydraw::ToolbarAction::kToggleColors);
  CHECK(tinydraw::toolbar_action_at({316.0F, 409.0F}, state) ==
        tinydraw::ToolbarAction::kToggleSizes);
  CHECK(tinydraw::toolbar_action_at({52.0F, 331.0F}, state) == tinydraw::ToolbarAction::kUndo);
  CHECK(tinydraw::toolbar_action_at({140.0F, 331.0F}, state) ==
        tinydraw::ToolbarAction::kNewDrawing);
}

TEST_CASE("four large color controls appear only while the palette is open") {
  tinydraw::ToolbarState state;
  CHECK(tinydraw::toolbar_action_at({52.0F, 253.0F}, state) == tinydraw::ToolbarAction::kNone);
  CHECK_FALSE(tinydraw::toolbar_contains({52.0F, 253.0F}, state));

  state.colors_open = true;
  CHECK(tinydraw::toolbar_action_at({52.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectBlack);
  CHECK(tinydraw::toolbar_action_at({140.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectBlue);
  CHECK(tinydraw::toolbar_action_at({228.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectRed);
  CHECK(tinydraw::toolbar_action_at({316.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectGreen);
  CHECK(tinydraw::toolbar_contains({52.0F, 253.0F}, state));
}

TEST_CASE("three large size controls appear only while the size palette is open") {
  tinydraw::ToolbarState state;
  CHECK(tinydraw::toolbar_action_at({66.0F, 253.0F}, state) == tinydraw::ToolbarAction::kNone);

  state.sizes_open = true;
  CHECK(tinydraw::toolbar_action_at({66.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectSmall);
  CHECK(tinydraw::toolbar_action_at({184.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectMedium);
  CHECK(tinydraw::toolbar_action_at({302.0F, 253.0F}, state) ==
        tinydraw::ToolbarAction::kSelectLarge);
  CHECK(tinydraw::toolbar_contains({302.0F, 253.0F}, state));
}

TEST_CASE("floating controls consume only their own screen regions") {
  const tinydraw::ToolbarState state;
  CHECK(tinydraw::toolbar_contains({360.0F, 409.0F}, state));
  CHECK(tinydraw::toolbar_contains({52.0F, 331.0F}, state));
  CHECK_FALSE(tinydraw::toolbar_contains({250.0F, 331.0F}, state));
  CHECK_FALSE(tinydraw::toolbar_contains({180.0F, 200.0F}, state));
}

TEST_CASE("pen sizes expose three bounded choices") {
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kSmall) <
        tinydraw::brush_size(tinydraw::PenSize::kMedium));
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kMedium) <
        tinydraw::brush_size(tinydraw::PenSize::kLarge));
}

TEST_CASE("toolbar rendering reflects tool and palette state without touching open canvas") {
  const auto pixel_count =
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
  std::vector<std::uint16_t> pen_canvas(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> eraser_canvas = pen_canvas;
  std::vector<std::uint16_t> palette_canvas = pen_canvas;

  tinydraw::draw_toolbar(pen_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kPen});
  tinydraw::draw_toolbar(eraser_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kEraser});
  tinydraw::draw_toolbar(palette_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.colors_open = true});

  CHECK(pen_canvas != eraser_canvas);
  CHECK(pen_canvas != palette_canvas);
  const auto open_canvas_pixel = static_cast<std::size_t>(150 * tinydraw::kCanvasWidth + 180);
  CHECK(pen_canvas[open_canvas_pixel] == 0xFFFFU);
  CHECK(eraser_canvas[open_canvas_pixel] == 0xFFFFU);
  CHECK(palette_canvas[open_canvas_pixel] == 0xFFFFU);
}
