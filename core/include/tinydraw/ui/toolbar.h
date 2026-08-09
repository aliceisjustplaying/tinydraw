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
  kToggleColors,
  kToggleSizes,
  kSelectSmall,
  kSelectMedium,
  kSelectLarge,
  kUndo,
  kNewDrawing,
};

struct ToolbarState {
  DrawingTool tool = DrawingTool::kPen;
  InkColor color = InkColor::kBlue;
  PenSize size = PenSize::kMedium;
  bool can_undo = false;
  bool colors_open = false;
  bool sizes_open = false;
};

[[nodiscard]] bool toolbar_contains(Point point, const ToolbarState& state);
[[nodiscard]] ToolbarAction toolbar_action_at(Point point, const ToolbarState& state);
[[nodiscard]] std::uint16_t rgb565(InkColor color);
[[nodiscard]] float brush_size(PenSize size);

void draw_toolbar(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state);

}  // namespace tinydraw
