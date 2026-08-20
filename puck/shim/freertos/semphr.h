// Fake <freertos/semphr.h>. A counter, not a synchronisation primitive: see
// ./FreeRTOS.h for why that is honest in a single-stack build.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../../README.md.
#ifndef TINYDRAW_PUCK_SHIM_FREERTOS_SEMPHR_H
#define TINYDRAW_PUCK_SHIM_FREERTOS_SEMPHR_H

#include "freertos/FreeRTOS.h"

typedef struct {
  int count;
  int maximum;
  int created;
} StaticSemaphore_t;

typedef StaticSemaphore_t* SemaphoreHandle_t;

#ifdef __cplusplus
extern "C" {
#endif

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage);
SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t maximum, UBaseType_t initial,
                                                 StaticSemaphore_t* storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout_ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);

#ifdef __cplusplus
}
#endif

#endif  // TINYDRAW_PUCK_SHIM_FREERTOS_SEMPHR_H
