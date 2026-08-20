// Fake <freertos/FreeRTOS.h> for the wasm build. See ../esp_timer.h's header
// comment for what this directory is.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../../README.md.
//
// THERE IS NO SCHEDULER HERE, and there is not meant to be. The firmware runs
// two things at once: the app's main loop on core 0, and the touch sampler on
// core 1. wasm gives one thread and no way to suspend a C++ frame, so the port
// keeps the app's loop as the only stack and pumps the sampler's own
// poll_once() from it at the board's 1 ms cadence. Nothing here pretends
// otherwise: xTaskCreatePinnedToCore does NOT start anything, it records that
// a task was asked for and reports success, and the emulator drives the one
// task this app has by calling the sampler directly.
//
// The primitives below are therefore uncontended by construction, and that is
// a fact about this build rather than an assumption about the product:
//   - a critical section has nothing to lock out, so it is a no-op;
//   - a semaphore is a plain counter, because the only thing that could ever
//     block on one is the single stack that would also have to post it;
//   - vTaskDelay is the one that does real work: it advances the virtual clock
//     and pumps touch for that many milliseconds, so a delay really does let
//     input arrive, exactly as it does on the board.
//
// A semaphore Take with a nonzero timeout that would have blocked returns
// pdFALSE rather than spinning. Every caller in the ported set treats that as
// "nothing was posted", which is true.

#ifndef TINYDRAW_PUCK_SHIM_FREERTOS_H
#define TINYDRAW_PUCK_SHIM_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS 1U
#define portMAX_DELAY ((TickType_t)0xffffffffU)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

// One stack, so a critical section protects nothing from anything.
typedef struct {
  int owner;
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED \
  { 0 }
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define portENTER_CRITICAL_ISR(mux) ((void)(mux))
#define portEXIT_CRITICAL_ISR(mux) ((void)(mux))

typedef struct {
  TickType_t entered;
} TimeOut_t;

#ifdef __cplusplus
extern "C" {
#endif

// Runs the emulator's cooperative work for `ticks` milliseconds of virtual
// time: advances esp_timer_get_time() and polls touch at the board's 1 ms
// cadence. Never returns to a scheduler, because there is not one.
void vTaskDelay(TickType_t ticks);
void taskYIELD(void);
void vTaskSuspend(void* task);
void vTaskDelete(void* task);
UBaseType_t uxTaskGetStackHighWaterMark(void* task);
TickType_t xTaskGetTickCount(void);
void vTaskSetTimeOutState(TimeOut_t* timeout);
BaseType_t xTaskCheckForTimeOut(TimeOut_t* timeout, TickType_t* remaining_ticks);

#ifdef __cplusplus
}
#endif

#endif  // TINYDRAW_PUCK_SHIM_FREERTOS_H
