#pragma once

namespace tinydraw {

inline constexpr int kCanvasWidth = 368;
inline constexpr int kCanvasHeight = 448;

struct Point {
  float x;
  float y;
};

struct Rect {
  int x0;
  int y0;
  int x1;
  int y1;
};

struct PanelGeometry {
  int x_offset = 0;
  int y_offset = 0;
};

}  // namespace tinydraw
