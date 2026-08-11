#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/geometry.h"

namespace tinydraw {

enum class DrawingTool { kPen, kPan, kEraser };
enum class InkColor {
  kBlack,
  kGrey,
  kLightViolet,
  kViolet,
  kBlue,
  kLightBlue,
  kYellow,
  kOrange,
  kGreen,
  kLightGreen,
  kLightRed,
  kRed,
};
enum class PenSize { kSmall, kMedium, kLarge, kExtraLarge };
enum class ToolbarAction {
  kNone,
  kSelectPen,
  kSelectPan,
  kSelectEraser,
  kSelectColor,
  kToggleTools,
  kToggleColors,
  kToggleSizes,
  kSelectSmall,
  kSelectMedium,
  kSelectLarge,
  kSelectExtraLarge,
  kUndo,
  kNewDrawing,
  kCancelNewDrawing,
  kConfirmNewDrawing,
};

struct ToolbarState {
  DrawingTool tool = DrawingTool::kPen;
  InkColor color = InkColor::kBlue;
  PenSize size = PenSize::kMedium;
  bool can_undo = false;
  bool tools_open = false;
  bool colors_open = false;
  bool sizes_open = false;
  bool confirm_new = false;
  bool recording = false;
  int battery_percentage = -1;
  bool battery_charging = false;
  bool external_power = false;
};

[[nodiscard]] bool toolbar_contains(Point point, const ToolbarState& state);
[[nodiscard]] bool toolbar_overlay_contains(Point point, const ToolbarState& state);
[[nodiscard]] int toolbar_overlay_top(const ToolbarState& state);
[[nodiscard]] ToolbarAction toolbar_action_at(Point point, const ToolbarState& state);
[[nodiscard]] std::optional<InkColor> toolbar_color_at(Point point, const ToolbarState& state);
[[nodiscard]] std::uint16_t rgb565(InkColor color);
[[nodiscard]] float brush_size(PenSize size);

void draw_toolbar(std::span<std::uint16_t> canvas, int width, int height,
                  const ToolbarState& state);

}  // namespace tinydraw
