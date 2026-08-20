// Fake <freertos/task.h>. See ./FreeRTOS.h: nothing here starts a thread.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../../README.md.
#ifndef TINYDRAW_PUCK_SHIM_FREERTOS_TASK_H
#define TINYDRAW_PUCK_SHIM_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

#ifdef __cplusplus
extern "C" {
#endif

// Records that a task was requested and hands back a non-null handle, so the
// requester's own "did it start" check passes. It does NOT run anything: the
// emulator drives this app's single task (the touch sampler) by calling its
// poll_once() from the main loop. See ./FreeRTOS.h.
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name, uint32_t stack_words,
                                   void* argument, UBaseType_t priority, TaskHandle_t* out_handle,
                                   BaseType_t core);
BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, uint32_t stack_words, void* argument,
                       UBaseType_t priority, TaskHandle_t* out_handle);

#ifdef __cplusplus
}
#endif

#endif  // TINYDRAW_PUCK_SHIM_FREERTOS_TASK_H
