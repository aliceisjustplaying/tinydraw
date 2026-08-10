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
constexpr std::uint16_t kWhite = 0xFFFF;
constexpr std::uint16_t kBlack = 0x1D1D;
constexpr std::uint16_t kBlue = 0x433D;
constexpr std::uint16_t kLightBlue = 0x4D1E;
constexpr std::uint16_t kGrey = 0x9D56;

std::array<std::uint16_t, static_cast<std::size_t>(kWidth* kHeight)> framebuffer;

constexpr std::uint16_t panel_pixel(std::uint16_t color) {
  return static_cast<std::uint16_t>((color << 8U) | (color >> 8U));
}

void set_pixel(int x, int y, std::uint16_t color) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return;
  }
  framebuffer[static_cast<std::size_t>(y * kWidth + x)] = panel_pixel(color);
}

void fill_rect(int x, int y, int width, int height, std::uint16_t color) {
  const int left = std::clamp(x, 0, kWidth);
  const int top = std::clamp(y, 0, kHeight);
  const int right = std::clamp(x + width, 0, kWidth);
  const int bottom = std::clamp(y + height, 0, kHeight);
  for (int row = top; row < bottom; ++row) {
    std::fill(framebuffer.begin() + static_cast<std::ptrdiff_t>(row * kWidth + left),
              framebuffer.begin() + static_cast<std::ptrdiff_t>(row * kWidth + right),
              panel_pixel(color));
  }
}

using Glyph = std::array<std::uint8_t, 7>;

constexpr Glyph glyph(char value) {
  switch (value) {
    case 'A':
      return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'D':
      return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E':
      return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'H':
      return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I':
      return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    case 'N':
      return {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11};
    case 'R':
      return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'T':
      return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'W':
      return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    case 'Y':
      return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    default:
      return {};
  }
}

void draw_text(int x, int y, const char* text, int scale, std::uint16_t color) {
  for (const char* character = text; *character != '\0'; ++character) {
    const auto pixels = glyph(*character);
    for (int row = 0; row < 7; ++row) {
      for (int column = 0; column < 5; ++column) {
        if ((pixels[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0) {
          continue;
        }
        fill_rect(x + column * scale, y + row * scale, scale, scale, color);
      }
    }
    x += 6 * scale;
  }
}

constexpr int kInkRadius = 7;

void stamp_dot(int x, int y) {
  for (int row = -kInkRadius; row <= kInkRadius; ++row) {
    for (int column = -kInkRadius; column <= kInkRadius; ++column) {
      if (column * column + row * row <= kInkRadius * kInkRadius) {
        set_pixel(x + column, y + row, kBlue);
      }
    }
  }
}

void draw_segment(int start_x, int start_y, int end_x, int end_y) {
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

  const int left = std::max(0, std::min(start_x, end_x) - kInkRadius) & ~1;
  const int top = std::max(0, std::min(start_y, end_y) - kInkRadius) & ~1;
  const int right = std::min(kWidth, (std::max(start_x, end_x) + kInkRadius + 2) & ~1);
  const int bottom = std::min(kHeight, (std::max(start_y, end_y) + kInkRadius + 2) & ~1);
  if (right > left && bottom > top) {
    AMOLED_1IN8_DisplayWindows(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
                               static_cast<std::uint32_t>(right),
                               static_cast<std::uint32_t>(bottom), framebuffer.data());
  }
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
  fill_rect(0, 0, kWidth, 76, kBlue);
  draw_text(88, 22, "TINYDRAW", 4, kWhite);
  draw_text(106, 112, "DRAW HERE", 3, kBlack);
  fill_rect(36, 164, kWidth - 72, 2, kGrey);
  fill_rect(36, 388, kWidth - 72, 2, kGrey);
  fill_rect(36, 164, 2, 226, kGrey);
  fill_rect(kWidth - 38, 164, 2, 226, kGrey);
  fill_rect(156, 414, 56, 8, kLightBlue);
  AMOLED_1IN8_Display(framebuffer.data());

  FT3168_Init(FT3168_Point_Mode);
  DEV_KEY_Config(Touch_INT_PIN);
  std::printf("TINYDRAW_RP2350_SMOKE_OK display=SH8601 touch=FT3168\n");

  bool drawing = false;
  int previous_x = 0;
  int previous_y = 0;
  while (true) {
    if (FT3168_ReadState(FT3168_FINGER_NUMBER) != 0) {
      FT3168_Get_Point();
      const int x = static_cast<int>(FT3168.x_point);
      const int y = static_cast<int>(FT3168.y_point);
      draw_segment(drawing ? previous_x : x, drawing ? previous_y : y, x, y);
      previous_x = x;
      previous_y = y;
      drawing = true;
    } else {
      drawing = false;
    }
    sleep_ms(5);
  }
}
