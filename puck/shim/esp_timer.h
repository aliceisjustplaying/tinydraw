// Fake <esp_timer.h> for the wasm build.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// This directory is put ahead of esp32/main/ on the include path, so the SAME
// product translation units compile for wasm without being edited. Each header
// here declares only the surface the ported files actually reach for; nothing
// tries to be a general ESP-IDF.
//
// esp_timer_get_time() is the app's only clock, and in the emulator it is a
// virtual one: emu_tick(nowMs) sets its base and each cooperative step of the
// app's main loop advances it by a fixed amount (see ../platform/esp_shim.cpp).
// That keeps it monotonic, keeps the app's own timeouts meaningful, and keeps
// a replay deterministic, which reading a host clock here would not.

#ifndef TINYDRAW_PUCK_SHIM_ESP_TIMER_H
#define TINYDRAW_PUCK_SHIM_ESP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct tinydraw_puck_timer* esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void* argument);
typedef enum { ESP_TIMER_TASK = 0 } esp_timer_dispatch_t;
typedef struct {
  esp_timer_cb_t callback;
  void* arg;
  esp_timer_dispatch_t dispatch_method;
  const char* name;
  bool skip_unhandled_events;
} esp_timer_create_args_t;

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);

#ifdef __cplusplus
}
#endif

#endif  // TINYDRAW_PUCK_SHIM_ESP_TIMER_H
