#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
constexpr int kToolbarTop = 384;
constexpr int kToolbarCells = 5;
constexpr std::uint16_t kWhite = 0xFFFF;
constexpr std::uint16_t kBlack = 0x18E3;
constexpr std::uint16_t kBlue = 0x433D;
constexpr std::uint16_t kRed = 0xE186;
constexpr std::uint16_t kGreen = 0x0C8D;
constexpr std::uint16_t kGrey = 0x9D56;
constexpr std::uint16_t kToolbar = 0xEF7D;
constexpr std::uint16_t kSelected = 0xD61F;
constexpr std::array<std::uint16_t, 4> kColors{kBlack, kBlue, kRed, kGreen};
constexpr std::array<int, 3> kRadii{3, 7, 12};

enum class Tool { kPen, kEraser };

std::array<std::uint16_t, static_cast<std::size_t>(kWidth* kHeight)> framebuffer;
Tool tool = Tool::kPen;
std::size_t color_index = 1;
std::size_t size_index = 1;

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

void set_pixel(int x, int y, std::uint16_t color) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return;
  }
  framebuffer[static_cast<std::size_t>(y * kWidth + x)] = color;
}

void blend_pixel(int x, int y, std::uint16_t color, int coverage) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kToolbarTop || coverage <= 0) {
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

void fill_rect(int x, int y, int width, int height, std::uint16_t color) {
  const int left = std::clamp(x, 0, kWidth);
  const int top = std::clamp(y, 0, kHeight);
  const int right = std::clamp(x + width, 0, kWidth);
  const int bottom = std::clamp(y + height, 0, kHeight);
  for (int row = top; row < bottom; ++row) {
    std::fill(framebuffer.begin() + static_cast<std::ptrdiff_t>(row * kWidth + left),
              framebuffer.begin() + static_cast<std::ptrdiff_t>(row * kWidth + right),
              color);
  }
}

void fill_circle(int center_x, int center_y, int radius, std::uint16_t color,
                 int clip_bottom = kHeight) {
  for (int row = -radius; row <= radius; ++row) {
    for (int column = -radius; column <= radius; ++column) {
      if (column * column + row * row <= radius * radius && center_y + row < clip_bottom) {
        set_pixel(center_x + column, center_y + row, color);
      }
    }
  }
}

void stamp_dot(int x, int y) {
  constexpr std::array sample_offsets{-3, -1, 1, 3};
  const int radius = kRadii[size_index];
  const int scaled_radius = radius * 8;
  const int radius_squared = scaled_radius * scaled_radius;
  const std::uint16_t color = tool == Tool::kEraser ? kWhite : kColors[color_index];
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

void draw_toolbar() {
  fill_rect(0, kToolbarTop, kWidth, kHeight - kToolbarTop, kToolbar);
  fill_rect(0, kToolbarTop, kWidth, 2, kGrey);
  const int cell_width = kWidth / kToolbarCells;
  const int pen_center = cell_width / 2;
  const int eraser_center = pen_center + cell_width;
  if (tool == Tool::kPen) {
    fill_rect(5, kToolbarTop + 7, cell_width - 10, 50, kSelected);
  } else {
    fill_rect(cell_width + 5, kToolbarTop + 7, cell_width - 10, 50, kSelected);
  }

  for (int offset = -12; offset <= 12; ++offset) {
    fill_circle(pen_center + offset, kToolbarTop + 32 - offset, 2, kBlack);
  }
  fill_rect(eraser_center - 13, kToolbarTop + 22, 26, 20, kWhite);
  fill_rect(eraser_center - 13, kToolbarTop + 22, 26, 2, kBlack);
  fill_rect(eraser_center - 13, kToolbarTop + 40, 26, 2, kBlack);
  fill_rect(eraser_center - 13, kToolbarTop + 22, 2, 20, kBlack);
  fill_rect(eraser_center + 11, kToolbarTop + 22, 2, 20, kBlack);

  fill_circle(pen_center + cell_width * 2, kToolbarTop + 32, 13, kColors[color_index]);
  fill_circle(pen_center + cell_width * 3, kToolbarTop + 32, kRadii[size_index], kBlack);

  const int new_center = pen_center + cell_width * 4;
  fill_rect(new_center - 14, kToolbarTop + 16, 28, 32, kWhite);
  fill_rect(new_center - 2, kToolbarTop + 23, 4, 18, kBlack);
  fill_rect(new_center - 9, kToolbarTop + 30, 18, 4, kBlack);
}

void present_framebuffer();

void handle_toolbar(int x) {
  const int cell = std::clamp(x * kToolbarCells / kWidth, 0, kToolbarCells - 1);
  if (cell == 0) {
    tool = Tool::kPen;
  } else if (cell == 1) {
    tool = Tool::kEraser;
  } else if (cell == 2) {
    color_index = (color_index + 1) % kColors.size();
    tool = Tool::kPen;
  } else if (cell == 3) {
    size_index = (size_index + 1) % kRadii.size();
  } else {
    fill_rect(0, 0, kWidth, kToolbarTop, kWhite);
  }
  draw_toolbar();
  present_framebuffer();
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
  const int radius = kRadii[size_index];
  const int left = std::max(0, minimum_x - radius) & ~1;
  const int top = std::max(0, minimum_y - radius) & ~1;
  const int right = std::min(kWidth, (maximum_x + radius + 2) & ~1);
  const int bottom = std::min(kToolbarTop, (maximum_y + radius + 2) & ~1);
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
  const int sample_spacing = kRadii[size_index] <= 3 ? 1 : 2;
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
  draw_toolbar();
  present_framebuffer();

  FT3168_Init(FT3168_Point_Mode);
  DEV_KEY_Config(Touch_INT_PIN);
  std::printf("TINYDRAW_RP2350_SMOKE_OK display=SH8601 touch=FT3168\n");

  bool touch_down = false;
  bool toolbar_gesture = false;
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
      finish_stroke();
      touch_down = false;
      toolbar_gesture = false;
      sleep_ms(5);
      continue;
    }

    FT3168_Get_Point();
    const int x = static_cast<int>(FT3168.x_point);
    const int y = static_cast<int>(FT3168.y_point);
    if (!touch_down && y >= kToolbarTop) {
      handle_toolbar(x);
      toolbar_gesture = true;
    }
    touch_down = true;
    if (toolbar_gesture || y >= kToolbarTop) {
      finish_stroke();
      toolbar_gesture = true;
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
