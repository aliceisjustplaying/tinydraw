#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"

namespace tinydraw {

enum class DrawingTool { kPen, kEraser };
enum class InkColor { kBlack, kBlue, kRed, kGreen };
enum class PenSize { kSmall, kMedium, kLarge };
enum class ToolbarAction {
  kNone,
  kSelectPen,
  kSelectEraser,
  kSelectBlack,
  kSelectBlue,
  kSelectRed,
  kSelectGreen,
  kCycleSize,
  kUndo,
  kNewDrawing,
};

struct ToolbarState {
  DrawingTool tool = DrawingTool::kPen;
  InkColor color = InkColor::kBlue;
  PenSize size = PenSize::kMedium;
  bool can_undo = false;
};

[[nodiscard]] bool toolbar_contains(Point point);
[[nodiscard]] ToolbarAction toolbar_action_at(Point point);
[[nodiscard]] std::uint16_t rgb565(InkColor color);
[[nodiscard]] float brush_size(PenSize size);
[[nodiscard]] PenSize next_pen_size(PenSize size);

void draw_toolbar(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state);

}  // namespace tinydraw
