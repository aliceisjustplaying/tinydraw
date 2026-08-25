// Fake <driver/gpio.h> for the wasm build. See ../esp_timer.h.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../../README.md.
//
// GPIO0 is the board's one user button, and the firmware maps a release edge
// on it to "show or hide ordinary chrome" (vector_v2_app.cpp). puck declares
// exactly one button, and emu_button(0, down) drives the level this shim
// reports, so the app's own edge detection is what decides, unchanged.
#ifndef TINYDRAW_PUCK_SHIM_DRIVER_GPIO_H
#define TINYDRAW_PUCK_SHIM_DRIVER_GPIO_H

#include <stdint.h>

typedef enum { GPIO_NUM_0 = 0 } gpio_num_t;
typedef enum { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT = 1 } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE = 0 } gpio_int_type_t;

typedef struct {
  uint64_t pin_bit_mask;
  gpio_mode_t mode;
  gpio_pullup_t pull_up_en;
  gpio_pulldown_t pull_down_en;
  gpio_int_type_t intr_type;
} gpio_config_t;

#ifdef __cplusplus
extern "C" {
#endif

int gpio_config(const gpio_config_t* config);
// The board wires the button to ground through a pull-up, so PRESSED reads 0.
// This shim reproduces that polarity rather than inverting it here, so the
// app's `gpio_get_level(kModeButton) == 0` test is the real one.
int gpio_get_level(gpio_num_t pin);

#ifdef __cplusplus
}
#endif

#endif  // TINYDRAW_PUCK_SHIM_DRIVER_GPIO_H
