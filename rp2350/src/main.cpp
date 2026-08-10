#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

#include "tinydraw/ui/toolbar.h"

// DEV_Config defines types used by the vendor panel header.
// clang-format off
extern "C" {
#include "DEV_Config.h"
#include "AMOLED_1in8.h"
#include "FT3168.h"
#include "qspi_pio.h"
}
// clang-format on

namespace {

constexpr int kWidth = AMOLED_1IN8_WIDTH;
constexpr int kHeight = AMOLED_1IN8_HEIGHT;
constexpr int kDrawingBottom = 372;
constexpr int kPaletteBackingTop = 179;
constexpr int kDialogBackingX = 26;
constexpr int kDialogBackingY = 124;
constexpr int kDialogBackingWidth = 318;
constexpr int kDialogBackingHeight = 168;
constexpr std::uint16_t kWhite = 0xFFFF;
constexpr std::size_t kPaletteBackingPixels =
    static_cast<std::size_t>(kWidth * (kDrawingBottom - kPaletteBackingTop));

std::array<std::uint16_t, static_cast<std::size_t>(kWidth * kHeight)> framebuffer;
std::array<std::uint16_t, kPaletteBackingPixels> overlay_pixels;
tinydraw::ToolbarState toolbar;

struct OverlayBackup {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool active = false;
};

OverlayBackup overlay_backup;

struct PerformanceStats {
  std::uint32_t updates = 0;
  std::uint32_t strokes = 0;
  std::uint64_t update_us = 0;
  std::uint64_t maximum_update_us = 0;
  std::uint64_t submitted_pixels = 0;
  std::uint64_t touch_interval_us = 0;
  std::uint64_t maximum_touch_interval_us = 0;
  std::uint32_t touch_intervals = 0;
};

PerformanceStats performance;

constexpr std::uint16_t panel_pixel(std::uint16_t color) {
  return static_cast<std::uint16_t>((color << 8U) | (color >> 8U));
}

int brush_radius() {
  switch (toolbar.size) {
    case tinydraw::PenSize::kSmall:
      return 3;
    case tinydraw::PenSize::kMedium:
      return 7;
    case tinydraw::PenSize::kLarge:
      return 12;
    case tinydraw::PenSize::kExtraLarge:
      return 18;
  }
  return 7;
}

void blend_pixel(int x, int y, std::uint16_t color, int coverage) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kDrawingBottom || coverage <= 0) {
    return;
  }
  const auto index = static_cast<std::size_t>(y * kWidth + x);
  const std::uint16_t background = framebuffer[index];
  const auto blend = [coverage](int foreground, int behind) {
    return (foreground * coverage + behind * (255 - coverage) + 127) / 255;
  };
  const int red = blend((color >> 11) & 0x1F, (background >> 11) & 0x1F);
  const int green = blend((color >> 5) & 0x3F, (background >> 5) & 0x3F);
  const int blue = blend(color & 0x1F, background & 0x1F);
  framebuffer[index] = static_cast<std::uint16_t>((red << 11) | (green << 5) | blue);
}

void stamp_dot(int x, int y) {
  constexpr std::array sample_offsets{-3, -1, 1, 3};
  const int radius = brush_radius();
  const int scaled_radius = radius * 8;
  const int radius_squared = scaled_radius * scaled_radius;
  const std::uint16_t color = toolbar.tool == tinydraw::DrawingTool::kEraser
                                  ? kWhite
                                  : tinydraw::rgb565(toolbar.color);
  for (int row = -radius; row <= radius; ++row) {
    for (int column = -radius; column <= radius; ++column) {
      int covered = 0;
      for (const int sample_y : sample_offsets) {
        for (const int sample_x : sample_offsets) {
          const int dx = column * 8 + sample_x;
          const int dy = row * 8 + sample_y;
          covered += dx * dx + dy * dy <= radius_squared ? 1 : 0;
        }
      }
      blend_pixel(x + column, y + row, color, covered * 255 / 16);
    }
  }
}

void present_framebuffer();

void save_overlay(int x, int y, int width, int height) {
  const auto pixel_count = static_cast<std::size_t>(width * height);
  if (overlay_backup.active || x < 0 || y < 0 || x + width > kWidth || y + height > kHeight ||
      pixel_count > overlay_pixels.size()) {
    return;
  }
  for (int row = 0; row < height; ++row) {
    std::copy_n(framebuffer.begin() + static_cast<std::ptrdiff_t>((y + row) * kWidth + x), width,
                overlay_pixels.begin() + static_cast<std::ptrdiff_t>(row * width));
  }
  overlay_backup = {.x = x, .y = y, .width = width, .height = height, .active = true};
}

void restore_overlay() {
  if (!overlay_backup.active) {
    return;
  }
  for (int row = 0; row < overlay_backup.height; ++row) {
    std::copy_n(overlay_pixels.begin() + static_cast<std::ptrdiff_t>(row * overlay_backup.width),
                overlay_backup.width,
                framebuffer.begin() + static_cast<std::ptrdiff_t>(
                                          (overlay_backup.y + row) * kWidth + overlay_backup.x));
  }
  overlay_backup.active = false;
}

void close_popups() {
  toolbar.tools_open = false;
  toolbar.colors_open = false;
  toolbar.sizes_open = false;
}

void save_toolbar_overlay() {
  if (toolbar.confirm_new) {
    save_overlay(kDialogBackingX, kDialogBackingY, kDialogBackingWidth, kDialogBackingHeight);
    return;
  }
  if (toolbar.tools_open || toolbar.colors_open || toolbar.sizes_open) {
    const int top = tinydraw::toolbar_overlay_top(toolbar);
    save_overlay(0, top, kWidth, kDrawingBottom - top);
  }
}

void render_toolbar() {
  tinydraw::draw_toolbar(std::span<std::uint16_t>(framebuffer), kWidth, kHeight, toolbar);
}

void show_toolbar() {
  render_toolbar();
  present_framebuffer();
}

void set_popup(bool tools, bool colors, bool sizes) {
  restore_overlay();
  close_popups();
  toolbar.tools_open = tools;
  toolbar.colors_open = colors;
  toolbar.sizes_open = sizes;
  save_toolbar_overlay();
  show_toolbar();
}

void handle_toolbar(tinydraw::Point point) {
  const auto action = tinydraw::toolbar_action_at(point, toolbar);
  switch (action) {
    case tinydraw::ToolbarAction::kSelectPen:
      restore_overlay();
      close_popups();
      toolbar.tool = tinydraw::DrawingTool::kPen;
      break;
    case tinydraw::ToolbarAction::kSelectPan:
      restore_overlay();
      close_popups();
      toolbar.tool = tinydraw::DrawingTool::kPen;
      break;
    case tinydraw::ToolbarAction::kSelectEraser:
      toolbar.tool = tinydraw::DrawingTool::kEraser;
      break;
    case tinydraw::ToolbarAction::kSelectColor:
      restore_overlay();
      toolbar.color = tinydraw::toolbar_color_at(point, toolbar).value_or(toolbar.color);
      toolbar.tool = tinydraw::DrawingTool::kPen;
      close_popups();
      break;
    case tinydraw::ToolbarAction::kToggleTools:
      set_popup(!toolbar.tools_open, false, false);
      return;
    case tinydraw::ToolbarAction::kToggleColors:
      set_popup(false, !toolbar.colors_open, false);
      return;
    case tinydraw::ToolbarAction::kToggleSizes:
      set_popup(false, false, !toolbar.sizes_open);
      return;
    case tinydraw::ToolbarAction::kSelectSmall:
      restore_overlay();
      toolbar.size = tinydraw::PenSize::kSmall;
      close_popups();
      break;
    case tinydraw::ToolbarAction::kSelectMedium:
      restore_overlay();
      toolbar.size = tinydraw::PenSize::kMedium;
      close_popups();
      break;
    case tinydraw::ToolbarAction::kSelectLarge:
      restore_overlay();
      toolbar.size = tinydraw::PenSize::kLarge;
      close_popups();
      break;
    case tinydraw::ToolbarAction::kSelectExtraLarge:
      restore_overlay();
      toolbar.size = tinydraw::PenSize::kExtraLarge;
      close_popups();
      break;
    case tinydraw::ToolbarAction::kNewDrawing:
      restore_overlay();
      close_popups();
      toolbar.confirm_new = true;
      save_toolbar_overlay();
      break;
    case tinydraw::ToolbarAction::kCancelNewDrawing:
      restore_overlay();
      toolbar.confirm_new = false;
      break;
    case tinydraw::ToolbarAction::kConfirmNewDrawing:
      overlay_backup.active = false;
      std::fill_n(framebuffer.begin(), static_cast<std::size_t>(kWidth * kDrawingBottom), kWhite);
      toolbar.confirm_new = false;
      close_popups();
      break;
    case tinydraw::ToolbarAction::kUndo:
    case tinydraw::ToolbarAction::kNone:
      return;
  }
  show_toolbar();
}

void swap_framebuffer_bytes() {
  for (auto& pixel : framebuffer) {
    pixel = panel_pixel(pixel);
  }
}

void present_framebuffer() {
  swap_framebuffer_bytes();
  AMOLED_1IN8_Display(framebuffer.data());
  swap_framebuffer_bytes();
}

void send_framebuffer() {
  constexpr auto byte_count = framebuffer.size() * sizeof(framebuffer.front());
  std::printf("TINYDRAW_FRAME %d %d RGB565BE %zu\n", kWidth, kHeight, byte_count);
  std::fflush(stdout);
  swap_framebuffer_bytes();
  std::fwrite(framebuffer.data(), 1, byte_count, stdout);
  swap_framebuffer_bytes();
  std::fflush(stdout);
}

void send_performance() {
  const auto average_update_us =
      performance.updates == 0 ? 0 : performance.update_us / performance.updates;
  const auto average_touch_interval_us =
      performance.touch_intervals == 0
          ? 0
          : performance.touch_interval_us / performance.touch_intervals;
  std::printf(
      "TINYDRAW_PERF updates=%lu strokes=%lu average_us=%llu max_us=%llu pixels=%llu "
      "touch_average_us=%llu touch_max_us=%llu\n",
      static_cast<unsigned long>(performance.updates),
      static_cast<unsigned long>(performance.strokes),
      static_cast<unsigned long long>(average_update_us),
      static_cast<unsigned long long>(performance.maximum_update_us),
      static_cast<unsigned long long>(performance.submitted_pixels),
      static_cast<unsigned long long>(average_touch_interval_us),
      static_cast<unsigned long long>(performance.maximum_touch_interval_us));
  std::fflush(stdout);
}

std::uint32_t flush_ink_bounds(int minimum_x, int minimum_y, int maximum_x, int maximum_y) {
  const int radius = brush_radius();
  const int left = std::max(0, minimum_x - radius) & ~1;
  const int top = std::max(0, minimum_y - radius) & ~1;
  const int right = std::min(kWidth, (maximum_x + radius + 2) & ~1);
  const int bottom = std::min(kDrawingBottom, (maximum_y + radius + 2) & ~1);
  if (right <= left || bottom <= top) {
    return 0;
  }
  present_framebuffer();
  return static_cast<std::uint32_t>(kWidth * kHeight);
}

std::uint32_t draw_segment(int start_x, int start_y, int end_x, int end_y) {
  int x = start_x;
  int y = start_y;
  const int step_x = start_x < end_x ? 1 : -1;
  const int step_y = start_y < end_y ? 1 : -1;
  const int delta_x = std::abs(end_x - start_x);
  const int delta_y = -std::abs(end_y - start_y);
  int error = delta_x + delta_y;

  while (true) {
    stamp_dot(x, y);
    if (x == end_x && y == end_y) {
      break;
    }
    const int doubled_error = error * 2;
    if (doubled_error >= delta_y) {
      error += delta_y;
      x += step_x;
    }
    if (doubled_error <= delta_x) {
      error += delta_x;
      y += step_y;
    }
  }

  return flush_ink_bounds(std::min(start_x, end_x), std::min(start_y, end_y),
                          std::max(start_x, end_x), std::max(start_y, end_y));
}

std::uint32_t draw_curve(int start_x, int start_y, int control_x, int control_y, int end_x,
                         int end_y) {
  const int path_length = std::abs(control_x - start_x) + std::abs(control_y - start_y) +
                          std::abs(end_x - control_x) + std::abs(end_y - control_y);
  const int sample_spacing = brush_radius() <= 3 ? 1 : 2;
  const int steps = std::max(1, (path_length + sample_spacing - 1) / sample_spacing);
  const std::int64_t denominator = static_cast<std::int64_t>(steps) * steps;
  for (int step = 0; step <= steps; ++step) {
    const std::int64_t inverse = steps - step;
    const std::int64_t x_numerator = inverse * inverse * start_x + 2 * inverse * step * control_x +
                                     static_cast<std::int64_t>(step) * step * end_x;
    const std::int64_t y_numerator = inverse * inverse * start_y + 2 * inverse * step * control_y +
                                     static_cast<std::int64_t>(step) * step * end_y;
    stamp_dot(static_cast<int>((x_numerator + denominator / 2) / denominator),
              static_cast<int>((y_numerator + denominator / 2) / denominator));
  }

  return flush_ink_bounds(
      std::min({start_x, control_x, end_x}), std::min({start_y, control_y, end_y}),
      std::max({start_x, control_x, end_x}), std::max({start_y, control_y, end_y}));
}

}  // namespace

int main() {
  if (DEV_Module_Init() != 0) {
    return 1;
  }

  QSPI_GPIO_Init(qspi);
  QSPI_PIO_Init(qspi);
  QSPI_4Wrie_Mode(&qspi);
  AMOLED_1IN8_Init();
  AMOLED_1IN8_SetBrightness(80);

  std::fill(framebuffer.begin(), framebuffer.end(), kWhite);
  render_toolbar();
  present_framebuffer();

  FT3168_Init(FT3168_Point_Mode);
  DEV_KEY_Config(Touch_INT_PIN);
  std::printf("TINYDRAW_RP2350_OK display=SH8601 touch=FT3168\n");

  bool touch_down = false;
  bool toolbar_gesture = false;
  tinydraw::Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  bool drawing = false;
  int stroke_samples = 0;
  int previous_x = 0;
  int previous_y = 0;
  int curve_start_x = 0;
  int curve_start_y = 0;
  std::uint64_t previous_touch_us = 0;

  const auto finish_stroke = [&] {
    if (!drawing) {
      return;
    }
    if (stroke_samples >= 2) {
      const auto update_started = time_us_64();
      performance.submitted_pixels +=
          draw_curve(curve_start_x, curve_start_y, previous_x, previous_y, previous_x, previous_y);
      const auto update_us = time_us_64() - update_started;
      ++performance.updates;
      performance.update_us += update_us;
      performance.maximum_update_us = std::max(performance.maximum_update_us, update_us);
    }
    ++performance.strokes;
    drawing = false;
  };

  while (true) {
    const int command = getchar_timeout_us(0);
    if (command == 'S') {
      send_framebuffer();
    } else if (command == 'P') {
      send_performance();
    }

    if (FT3168_ReadState(FT3168_FINGER_NUMBER) == 0) {
      if (touch_down && toolbar_gesture) {
        const float divisor = static_cast<float>(toolbar_samples == 0 ? 1 : toolbar_samples);
        handle_toolbar({.x = toolbar_sum.x / divisor, .y = toolbar_sum.y / divisor});
      } else if (touch_down) {
        finish_stroke();
      }
      touch_down = false;
      toolbar_gesture = false;
      toolbar_sum = {};
      toolbar_samples = 0;
      sleep_ms(5);
      continue;
    }

    FT3168_Get_Point();
    const int x = static_cast<int>(FT3168.x_point);
    const int y = static_cast<int>(FT3168.y_point);
    const tinydraw::Point point{.x = static_cast<float>(x), .y = static_cast<float>(y)};
    if (!touch_down) {
      touch_down = true;
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_gesture = true;
        toolbar_sum = point;
        toolbar_samples = 1;
        sleep_ms(5);
        continue;
      }
      if (toolbar.tools_open || toolbar.colors_open || toolbar.sizes_open) {
        restore_overlay();
        close_popups();
        render_toolbar();
      }
    }
    if (toolbar_gesture) {
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_sum.x += point.x;
        toolbar_sum.y += point.y;
        ++toolbar_samples;
      }
      sleep_ms(5);
      continue;
    }
    if (y >= kDrawingBottom) {
      sleep_ms(5);
      continue;
    }

    const auto update_started = time_us_64();
    std::uint32_t submitted_pixels = 0;
    if (!drawing) {
      submitted_pixels = draw_segment(x, y, x, y);
      stroke_samples = 1;
    } else if (stroke_samples == 1) {
      curve_start_x = (previous_x + x) / 2;
      curve_start_y = (previous_y + y) / 2;
      submitted_pixels = draw_segment(previous_x, previous_y, curve_start_x, curve_start_y);
      stroke_samples = 2;
    } else {
      const int curve_end_x = (previous_x + x) / 2;
      const int curve_end_y = (previous_y + y) / 2;
      submitted_pixels = draw_curve(curve_start_x, curve_start_y, previous_x, previous_y,
                                    curve_end_x, curve_end_y);
      curve_start_x = curve_end_x;
      curve_start_y = curve_end_y;
      ++stroke_samples;
    }
    const auto update_us = time_us_64() - update_started;
    ++performance.updates;
    performance.update_us += update_us;
    performance.maximum_update_us = std::max(performance.maximum_update_us, update_us);
    performance.submitted_pixels += submitted_pixels;
    if (drawing) {
      const auto touch_interval_us = update_started - previous_touch_us;
      performance.touch_interval_us += touch_interval_us;
      performance.maximum_touch_interval_us =
          std::max(performance.maximum_touch_interval_us, touch_interval_us);
      ++performance.touch_intervals;
    }
    previous_x = x;
    previous_y = y;
    previous_touch_us = update_started;
    drawing = true;
  }
}
