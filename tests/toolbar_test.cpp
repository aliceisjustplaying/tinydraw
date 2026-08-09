#include "tinydraw/ui/toolbar.h"

#include <doctest.h>

#include <vector>

TEST_CASE("tldraw-style toolbar maps large touch targets to drawing actions") {
  CHECK(tinydraw::toolbar_action_at({36.0F, 414.0F}) == tinydraw::ToolbarAction::kSelectPen);
  CHECK(tinydraw::toolbar_action_at({84.0F, 414.0F}) == tinydraw::ToolbarAction::kSelectEraser);
  CHECK(tinydraw::toolbar_action_at({130.0F, 414.0F}) == tinydraw::ToolbarAction::kSelectBlack);
  CHECK(tinydraw::toolbar_action_at({166.0F, 414.0F}) == tinydraw::ToolbarAction::kSelectBlue);
  CHECK(tinydraw::toolbar_action_at({202.0F, 414.0F}) == tinydraw::ToolbarAction::kSelectRed);
  CHECK(tinydraw::toolbar_action_at({238.0F, 414.0F}) == tinydraw::ToolbarAction::kSelectGreen);
  CHECK(tinydraw::toolbar_action_at({286.0F, 414.0F}) == tinydraw::ToolbarAction::kCycleSize);
  CHECK(tinydraw::toolbar_action_at({36.0F, 352.0F}) == tinydraw::ToolbarAction::kUndo);
  CHECK(tinydraw::toolbar_action_at({84.0F, 352.0F}) == tinydraw::ToolbarAction::kNewDrawing);
}

TEST_CASE("floating controls consume only their own screen regions") {
  CHECK(tinydraw::toolbar_contains({36.0F, 414.0F}));
  CHECK(tinydraw::toolbar_contains({36.0F, 352.0F}));
  CHECK_FALSE(tinydraw::toolbar_contains({180.0F, 352.0F}));
  CHECK_FALSE(tinydraw::toolbar_contains({180.0F, 300.0F}));
  CHECK(tinydraw::toolbar_action_at({180.0F, 300.0F}) == tinydraw::ToolbarAction::kNone);
}

TEST_CASE("pen size cycles through three bounded choices") {
  CHECK(tinydraw::next_pen_size(tinydraw::PenSize::kSmall) == tinydraw::PenSize::kMedium);
  CHECK(tinydraw::next_pen_size(tinydraw::PenSize::kMedium) == tinydraw::PenSize::kLarge);
  CHECK(tinydraw::next_pen_size(tinydraw::PenSize::kLarge) == tinydraw::PenSize::kSmall);
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kSmall) <
        tinydraw::brush_size(tinydraw::PenSize::kMedium));
  CHECK(tinydraw::brush_size(tinydraw::PenSize::kMedium) <
        tinydraw::brush_size(tinydraw::PenSize::kLarge));
}

TEST_CASE("toolbar rendering reflects tool selection without touching open canvas") {
  const auto pixel_count =
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
  std::vector<std::uint16_t> pen_canvas(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> eraser_canvas = pen_canvas;

  tinydraw::draw_toolbar(pen_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kPen});
  tinydraw::draw_toolbar(eraser_canvas, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight,
                         {.tool = tinydraw::DrawingTool::kEraser});

  CHECK(pen_canvas != eraser_canvas);
  const auto open_canvas_pixel = static_cast<std::size_t>(200 * tinydraw::kCanvasWidth + 180);
  CHECK(pen_canvas[open_canvas_pixel] == 0xFFFFU);
  CHECK(eraser_canvas[open_canvas_pixel] == 0xFFFFU);
}
