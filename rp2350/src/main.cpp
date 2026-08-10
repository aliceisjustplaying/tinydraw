#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "tinydraw/ink/ink_stream.h"
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

std::array<std::uint16_t, static_cast<std::size_t>(kWidth* kHeight)> framebuffer;
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

struct TracePoint {
  std::uint16_t x;
  std::uint16_t y;
  std::uint32_t timestamp_us;
};

std::array<TracePoint, 256> trace_points;
std::size_t trace_count = 0;

struct TouchEvent {
  std::uint16_t x;
  std::uint16_t y;
  std::uint32_t timestamp_us;
  bool touching;
};

queue_t touch_events;
std::atomic<bool> touch_report_ready = false;

void touch_report_callback(uint gpio, std::uint32_t events) {
  if (gpio == Touch_INT_PIN && (events & GPIO_IRQ_EDGE_RISE) != 0U) {
    touch_report_ready.store(true, std::memory_order_release);
  }
}

void enqueue_touch(TouchEvent event) {
  if (queue_try_add(&touch_events, &event)) {
    return;
  }
  TouchEvent discarded{};
  static_cast<void>(queue_try_remove(&touch_events, &discarded));
  static_cast<void>(queue_try_add(&touch_events, &event));
}

void sample_touch() {
  bool touching = false;
  std::uint16_t last_x = 0;
  std::uint16_t last_y = 0;
  std::uint32_t last_report_us = time_us_32();
  gpio_set_irq_enabled_with_callback(Touch_INT_PIN, GPIO_IRQ_EDGE_RISE, true,
                                     touch_report_callback);
  while (true) {
    if (touch_report_ready.exchange(false, std::memory_order_acquire)) {
      const std::uint32_t now = time_us_32();
      last_report_us = now;
      if (FT3168_ReadState(FT3168_FINGER_NUMBER) != 0) {
        FT3168_Get_Point();
        const auto x = static_cast<std::uint16_t>(FT3168.x_point);
        const auto y = static_cast<std::uint16_t>(FT3168.y_point);
        if (!touching || x != last_x || y != last_y) {
          enqueue_touch({.x = x, .y = y, .timestamp_us = now, .touching = true});
          last_x = x;
          last_y = y;
        }
        touching = true;
      } else if (touching) {
        enqueue_touch({.x = last_x, .y = last_y, .timestamp_us = now, .touching = false});
        touching = false;
      }
    } else {
      const std::uint32_t now = time_us_32();
      if (touching && now - last_report_us >= 25'000U) {
        if (FT3168_ReadState(FT3168_FINGER_NUMBER) == 0) {
          enqueue_touch({.x = last_x, .y = last_y, .timestamp_us = now, .touching = false});
          touching = false;
        }
        last_report_us = now;
      }
      sleep_ms(1);
    }
  }
}

constexpr std::uint16_t panel_pixel(std::uint16_t color) {
  return static_cast<std::uint16_t>((color << 8U) | (color >> 8U));
}

struct StrokePoint {
  float x;
  float y;
  float radius;
};

StrokePoint stroke_point(const tinydraw::InkPoint& point) {
  return {.x = point.position.x, .y = point.position.y, .radius = point.radius};
}

StrokePoint midpoint(StrokePoint first, StrokePoint second) {
  return {
      .x = (first.x + second.x) * 0.5F,
      .y = (first.y + second.y) * 0.5F,
      .radius = (first.radius + second.radius) * 0.5F,
  };
}

void blend_pixel(int x, int y, std::uint16_t color, int coverage) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kDrawingBottom || coverage <= 0) {
    return;
  }
  const auto index = static_cast<std::size_t>(y * kWidth + x);
  if (coverage >= 255) {
    framebuffer[index] = panel_pixel(color);
    return;
  }
  const std::uint16_t background = panel_pixel(framebuffer[index]);
  const auto blend = [coverage](int foreground, int behind) {
    return (foreground * coverage + behind * (255 - coverage) + 127) / 255;
  };
  const int red = blend((color >> 11) & 0x1F, (background >> 11) & 0x1F);
  const int green = blend((color >> 5) & 0x3F, (background >> 5) & 0x3F);
  const int blue = blend(color & 0x1F, background & 0x1F);
  framebuffer[index] = panel_pixel(static_cast<std::uint16_t>((red << 11) | (green << 5) | blue));
}

void stamp_dot(StrokePoint point) {
  constexpr std::array sample_offsets{-3, -1, 1, 3};
  const int extent = static_cast<int>(std::ceil(point.radius)) + 1;
  const int point_x8 = static_cast<int>(std::lround(point.x * 8.0F));
  const int point_y8 = static_cast<int>(std::lround(point.y * 8.0F));
  const int scaled_radius = static_cast<int>(std::lround(point.radius * 8.0F));
  const int radius_squared = scaled_radius * scaled_radius;
  const std::uint16_t color =
      toolbar.tool == tinydraw::DrawingTool::kEraser ? kWhite : tinydraw::rgb565(toolbar.color);
  const int center_x = static_cast<int>(std::lround(point.x));
  const int center_y = static_cast<int>(std::lround(point.y));
  for (int row = -extent; row <= extent; ++row) {
    for (int column = -extent; column <= extent; ++column) {
      const int center_dx = (center_x + column) * 8 - point_x8;
      const int center_dy = (center_y + row) * 8 - point_y8;
      const int far_x = std::abs(center_dx) + 3;
      const int far_y = std::abs(center_dy) + 3;
      if (far_x * far_x + far_y * far_y <= radius_squared) {
        blend_pixel(center_x + column, center_y + row, color, 255);
        continue;
      }
      const int near_x = std::max(0, std::abs(center_dx) - 3);
      const int near_y = std::max(0, std::abs(center_dy) - 3);
      if (near_x * near_x + near_y * near_y > radius_squared) {
        continue;
      }
      int covered = 0;
      for (const int sample_y : sample_offsets) {
        for (const int sample_x : sample_offsets) {
          const int dx = center_dx + sample_x;
          const int dy = center_dy + sample_y;
          covered += dx * dx + dy * dy <= radius_squared ? 1 : 0;
        }
      }
      blend_pixel(center_x + column, center_y + row, color, covered * 255 / 16);
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

void swap_framebuffer_bytes();

void render_toolbar() {
  swap_framebuffer_bytes();
  tinydraw::draw_toolbar(std::span<std::uint16_t>(framebuffer), kWidth, kHeight, toolbar);
  swap_framebuffer_bytes();
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
      std::fill_n(framebuffer.begin(), static_cast<std::size_t>(kWidth * kDrawingBottom),
                  panel_pixel(kWhite));
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

void present_framebuffer() { AMOLED_1IN8_Display(framebuffer.data()); }

void send_framebuffer() {
  constexpr auto byte_count = framebuffer.size() * sizeof(framebuffer.front());
  std::printf("TINYDRAW_FRAME %d %d RGB565BE %zu\n", kWidth, kHeight, byte_count);
  std::fflush(stdout);
  std::fwrite(framebuffer.data(), 1, byte_count, stdout);
  std::fflush(stdout);
}

void send_trace() {
  std::printf("TINYDRAW_TRACE count=%zu\n", trace_count);
  for (std::size_t index = 0; index < trace_count; ++index) {
    const auto& point = trace_points[index];
    std::printf("%u,%u,%lu\n", point.x, point.y, static_cast<unsigned long>(point.timestamp_us));
  }
  std::printf("TINYDRAW_TRACE_END\n");
  std::fflush(stdout);
}

void run_partial_display_probe() {
  constexpr int left = 104;
  constexpr int top = 96;
  constexpr int width = 160;
  constexpr int height = 160;
  std::fill(framebuffer.begin(), framebuffer.end(), panel_pixel(kWhite));
  render_toolbar();
  present_framebuffer();

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::uint16_t color = 0;
      if (x >= width / 2 && y < height / 2) {
        color = 0xF800;
      } else if (x < width / 2 && y >= height / 2) {
        color = 0x07E0;
      } else if (x >= width / 2 && y >= height / 2) {
        color = 0x001F;
      }
      const auto pixel = panel_pixel(color);
      overlay_pixels[static_cast<std::size_t>(y * width + x)] = pixel;
      framebuffer[static_cast<std::size_t>((top + y) * kWidth + left + x)] = pixel;
    }
  }

  const auto started = time_us_64();
  AMOLED_1IN8_DisplayWindowPacked(left, top, left + width, top + height, overlay_pixels.data());
  const auto elapsed_us = time_us_64() - started;
  std::printf("TINYDRAW_WINDOW_PROBE us=%llu\n", static_cast<unsigned long long>(elapsed_us));
  std::fflush(stdout);
}

void send_performance() {
  const auto display_started = time_us_64();
  present_framebuffer();
  const auto display_probe_us = time_us_64() - display_started;
  const auto average_update_us =
      performance.updates == 0 ? 0 : performance.update_us / performance.updates;
  const auto average_touch_interval_us =
      performance.touch_intervals == 0
          ? 0
          : performance.touch_interval_us / performance.touch_intervals;
  std::printf(
      "TINYDRAW_PERF updates=%lu strokes=%lu average_us=%llu max_us=%llu pixels=%llu "
      "touch_average_us=%llu touch_max_us=%llu display_probe_us=%llu\n",
      static_cast<unsigned long>(performance.updates),
      static_cast<unsigned long>(performance.strokes),
      static_cast<unsigned long long>(average_update_us),
      static_cast<unsigned long long>(performance.maximum_update_us),
      static_cast<unsigned long long>(performance.submitted_pixels),
      static_cast<unsigned long long>(average_touch_interval_us),
      static_cast<unsigned long long>(performance.maximum_touch_interval_us),
      static_cast<unsigned long long>(display_probe_us));
  std::fflush(stdout);
}

std::uint32_t submit_stroke_band(std::span<const StrokePoint> points) {
  float minimum_y = points.front().y;
  float maximum_y = points.front().y;
  float maximum_radius = points.front().radius;
  for (const auto& point : points.subspan(1)) {
    minimum_y = std::min(minimum_y, point.y);
    maximum_y = std::max(maximum_y, point.y);
    maximum_radius = std::max(maximum_radius, point.radius);
  }
  const int top = std::max(0, static_cast<int>(std::floor(minimum_y - maximum_radius - 2.0F)));
  const int bottom =
      std::min(kDrawingBottom, static_cast<int>(std::ceil(maximum_y + maximum_radius + 2.0F)));
  if (top >= bottom) {
    return 0;
  }
  AMOLED_1IN8_DisplayWindowPacked(0, static_cast<std::uint32_t>(top), kWidth,
                                  static_cast<std::uint32_t>(bottom),
                                  framebuffer.data() + static_cast<std::ptrdiff_t>(top * kWidth));
  return static_cast<std::uint32_t>(kWidth * (bottom - top));
}

float sample_spacing(float minimum_radius) {
  return std::clamp(minimum_radius * 0.5F, 0.75F, 2.0F);
}

std::uint32_t draw_segment(StrokePoint start, StrokePoint end) {
  const float distance = std::hypot(end.x - start.x, end.y - start.y);
  const int steps = std::max(
      1,
      static_cast<int>(std::ceil(distance / sample_spacing(std::min(start.radius, end.radius)))));
  for (int step = 0; step <= steps; ++step) {
    const float amount = static_cast<float>(step) / static_cast<float>(steps);
    stamp_dot({
        .x = start.x + (end.x - start.x) * amount,
        .y = start.y + (end.y - start.y) * amount,
        .radius = start.radius + (end.radius - start.radius) * amount,
    });
  }
  const std::array bounds{start, end};
  return submit_stroke_band(bounds);
}

std::uint32_t draw_curve(StrokePoint start, StrokePoint control, StrokePoint end) {
  const float path_length = std::hypot(control.x - start.x, control.y - start.y) +
                            std::hypot(end.x - control.x, end.y - control.y);
  const float minimum_radius = std::min({start.radius, control.radius, end.radius});
  const int steps =
      std::max(1, static_cast<int>(std::ceil(path_length / sample_spacing(minimum_radius))));
  for (int step = 0; step <= steps; ++step) {
    const float amount = static_cast<float>(step) / static_cast<float>(steps);
    const float inverse = 1.0F - amount;
    stamp_dot({
        .x = inverse * inverse * start.x + 2.0F * inverse * amount * control.x +
             amount * amount * end.x,
        .y = inverse * inverse * start.y + 2.0F * inverse * amount * control.y +
             amount * amount * end.y,
        .radius = inverse * inverse * start.radius + 2.0F * inverse * amount * control.radius +
                  amount * amount * end.radius,
    });
  }
  const std::array bounds{start, control, end};
  return submit_stroke_band(bounds);
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

  std::fill(framebuffer.begin(), framebuffer.end(), panel_pixel(kWhite));
  render_toolbar();
  present_framebuffer();

  FT3168_Init(FT3168_Point_Mode);
  DEV_KEY_Config(Touch_INT_PIN);
  queue_init(&touch_events, sizeof(TouchEvent), 32);
  multicore_launch_core1(sample_touch);
  std::printf("TINYDRAW_RP2350_OK display=SH8601 touch=FT3168\n");

  tinydraw::InkConfig brush;
  brush.size = tinydraw::brush_size(toolbar.size);
  tinydraw::InkStream ink(brush);
  bool touch_down = false;
  bool toolbar_gesture = false;
  tinydraw::Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  bool drawing = false;
  int stroke_samples = 0;
  StrokePoint previous{};
  StrokePoint curve_start{};
  tinydraw::TouchPoint last_touch{};
  std::uint32_t previous_touch_us = 0;

  const auto finish_stroke = [&] {
    if (!drawing) {
      return;
    }
    const auto update_started = time_us_64();
    std::uint32_t submitted_pixels = 0;
    if (stroke_samples >= 2) {
      last_touch.timestamp_us = static_cast<std::uint32_t>(update_started);
      const StrokePoint end = stroke_point(ink.finish(last_touch));
      submitted_pixels = draw_curve(curve_start, previous, end);
    } else {
      ink.end();
      submitted_pixels = draw_segment(previous, previous);
    }
    const auto update_us = time_us_64() - update_started;
    ++performance.updates;
    performance.update_us += update_us;
    performance.maximum_update_us = std::max(performance.maximum_update_us, update_us);
    performance.submitted_pixels += submitted_pixels;
    ++performance.strokes;
    drawing = false;
  };

  while (true) {
    const int command = getchar_timeout_us(0);
    if (command == 'S') {
      send_framebuffer();
    } else if (command == 'P') {
      send_performance();
    } else if (command == 'T') {
      send_trace();
    } else if (command == 'W') {
      run_partial_display_probe();
    }

    TouchEvent event{};
    if (!queue_try_remove(&touch_events, &event)) {
      sleep_ms(1);
      continue;
    }
    if (!event.touching) {
      if (touch_down && toolbar_gesture) {
        const float divisor = static_cast<float>(toolbar_samples == 0 ? 1 : toolbar_samples);
        handle_toolbar({.x = toolbar_sum.x / divisor, .y = toolbar_sum.y / divisor});
        auto config = ink.config();
        config.size = tinydraw::brush_size(toolbar.size);
        ink.set_config(config);
      } else if (touch_down) {
        finish_stroke();
      }
      touch_down = false;
      toolbar_gesture = false;
      toolbar_sum = {};
      toolbar_samples = 0;
      continue;
    }

    const int x = static_cast<int>(event.x);
    const int y = static_cast<int>(event.y);
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
    last_touch = {
        .x = static_cast<float>(x), .y = static_cast<float>(y), .timestamp_us = event.timestamp_us};
    if (!drawing) {
      trace_count = 0;
    }
    if (trace_count < trace_points.size()) {
      trace_points[trace_count++] = {
          .x = static_cast<std::uint16_t>(x),
          .y = static_cast<std::uint16_t>(y),
          .timestamp_us = last_touch.timestamp_us,
      };
    }
    const StrokePoint current =
        stroke_point(drawing ? ink.update(last_touch) : ink.begin(last_touch));
    std::uint32_t submitted_pixels = 0;
    if (!drawing) {
      stroke_samples = 1;
    } else if (stroke_samples == 1) {
      previous.radius = current.radius;
      curve_start = midpoint(previous, current);
      submitted_pixels = draw_segment(previous, curve_start);
      stroke_samples = 2;
    } else {
      const StrokePoint curve_end = midpoint(previous, current);
      submitted_pixels = draw_curve(curve_start, previous, curve_end);
      curve_start = curve_end;
      ++stroke_samples;
    }
    const auto update_us = time_us_64() - update_started;
    ++performance.updates;
    performance.update_us += update_us;
    performance.maximum_update_us = std::max(performance.maximum_update_us, update_us);
    performance.submitted_pixels += submitted_pixels;
    if (drawing) {
      const std::uint32_t touch_interval_us = last_touch.timestamp_us - previous_touch_us;
      performance.touch_interval_us += touch_interval_us;
      performance.maximum_touch_interval_us = std::max(
          performance.maximum_touch_interval_us, static_cast<std::uint64_t>(touch_interval_us));
      ++performance.touch_intervals;
    }
    previous = current;
    previous_touch_us = last_touch.timestamp_us;
    drawing = true;
  }
}
