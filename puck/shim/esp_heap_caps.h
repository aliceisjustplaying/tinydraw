// Fake <esp_heap_caps.h> for the wasm build. See esp_timer.h's header comment
// for what this directory is.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// The board has two heaps with different costs (internal SRAM, octal PSRAM)
// and AppStorage::allocate() places every region deliberately between them,
// with PSRAM fallbacks so allocation is infallible. wasm linear memory is one
// flat heap, so every capability maps to malloc.
//
// That is a REAL difference and it is worth naming rather than hiding: the
// cache-set placement reasoning in vector_v2_app_storage.cpp's comments (8 KiB
// dcache ways, padding the slot directory to keep later allocations on the
// same sets) describes a machine that does not exist here. Nothing about
// correctness depends on it, only speed, and speed is exactly what emu_abi.h
// says never to trust in an emulator. The allocation ORDER and SIZES are
// unchanged, so what the app believes it owns is identical.
//
// Free-space telemetry is unavailable: wasm's allocator does not expose live
// heap extent, and capability heaps do not exist. The query functions return
// zero; allocation success remains the only contract the application uses.

#ifndef TINYDRAW_PUCK_SHIM_ESP_HEAP_CAPS_H
#define TINYDRAW_PUCK_SHIM_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_EXEC 0x00000001U
#define MALLOC_CAP_32BIT 0x00000002U
#define MALLOC_CAP_8BIT 0x00000004U
#define MALLOC_CAP_DMA 0x00000008U
#define MALLOC_CAP_SPIRAM 0x00000400U
#define MALLOC_CAP_INTERNAL 0x00000800U
#define MALLOC_CAP_DEFAULT 0x00001000U

#ifdef __cplusplus
extern "C" {
#endif

void* heap_caps_malloc(size_t size, uint32_t caps);
void* heap_caps_calloc(size_t count, size_t size, uint32_t caps);
void heap_caps_free(void* pointer);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif  // TINYDRAW_PUCK_SHIM_ESP_HEAP_CAPS_H
