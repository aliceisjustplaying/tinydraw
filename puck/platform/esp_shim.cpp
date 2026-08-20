// Implements the fake ESP-IDF surface (../shim/) against puck's emulator.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// THE CLOCK is the interesting one. On the board esp_timer_get_time() is a
// free-running hardware counter and the app's loop spins against it. In the
// emulator the only time that exists is emu_tick(nowMs), and emu_abi.h is
// blunt about why: a firmware that reads its own clock is not reproducible.
//
// So virtual time here is a pure function of the trace: emu_tick sets a floor,
// and every cooperative step of the app's loop advances a small fixed amount
// on top of it. That is not a claim about how long anything takes (nothing in
// an emulator is), it is what keeps the app's own timeouts, retry windows and
// idle thresholds meaningful instead of frozen. Two replays of one trace
// therefore see the identical microsecond at the identical point.
//
// THE CONCURRENCY is the other one. The board polls touch on core 1 every
// millisecond. Here, every microsecond of virtual time that passes runs the
// touch polls that fall inside it, from whatever stack advanced the clock.
// A vTaskDelay(2) really does let two milliseconds of finger movement arrive,
// which is what the app's idle path is written to expect.

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "puck_platform.h"
#include "vector_v2_touch_sampler.h"

struct tinydraw_puck_timer {
  esp_timer_cb_t callback = nullptr;
  void* argument = nullptr;
  std::int64_t due_us = 0;
  bool active = false;
};

namespace tinydraw::puck {
namespace {

// The emulator's panel memory, in the panel's own byte order.
std::uint16_t g_framebuffer[kPanelPixels];

constexpr std::size_t kMaxPushes = 256;  // puck's abiGuard refuses more
struct Push {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};
Push g_pushes[kMaxPushes];
std::size_t g_push_count = 0;
bool g_pushes_collapsed = false;

std::int64_t g_now_us = 0;
std::int64_t g_host_floor_us = 0;
std::uint32_t g_host_last_ms = 0;
bool g_host_clock_started = false;

bool g_touch_down = false;
float g_touch_x = 0.0F;
float g_touch_y = 0.0F;
bool g_button_down = false;

tinydraw::esp32::VectorV2TouchSampler* g_sampler = nullptr;
std::int64_t g_next_poll_us = 0;
// Reentrancy guard: the sampler's poll can itself reach code that advances
// the clock. One level of pumping is the whole design.
bool g_pumping = false;

constexpr std::size_t kMaximumTimers = 8;
tinydraw_puck_timer* g_timers[kMaximumTimers]{};

void fire_due_timers() {
  for (std::size_t deliveries = 0; deliveries < 64; ++deliveries) {
    tinydraw_puck_timer* due = nullptr;
    for (tinydraw_puck_timer* timer : g_timers) {
      if (timer != nullptr && timer->active && timer->due_us <= g_now_us &&
          (due == nullptr || timer->due_us < due->due_us)) {
        due = timer;
      }
    }
    if (due == nullptr) return;
    due->active = false;
    due->callback(due->argument);
  }
}

}  // namespace

std::uint16_t* framebuffer() { return g_framebuffer; }

void record_push(int x, int y, int width, int height) {
  if (width <= 0 || height <= 0) return;
  if (x < 0 || y < 0 || x + width > kPanelWidth || y + height > kPanelHeight) return;
  if (g_pushes_collapsed) return;
  if (g_push_count >= kMaxPushes) {
    g_pushes[0] = Push{0, 0, kPanelWidth, kPanelHeight};
    g_push_count = 1;
    g_pushes_collapsed = true;
    return;
  }
  g_pushes[g_push_count++] = Push{x, y, width, height};
}

void reset_pushes() {
  g_push_count = 0;
  g_pushes_collapsed = false;
}
std::size_t push_count() { return g_push_count; }
int push_x(std::size_t index) { return index < g_push_count ? g_pushes[index].x : 0; }
int push_y(std::size_t index) { return index < g_push_count ? g_pushes[index].y : 0; }
int push_w(std::size_t index) { return index < g_push_count ? g_pushes[index].w : 0; }
int push_h(std::size_t index) { return index < g_push_count ? g_pushes[index].h : 0; }

void clock_set_floor_ms(std::uint32_t now_ms) {
  if (!g_host_clock_started) {
    g_host_floor_us = static_cast<std::int64_t>(now_ms) * 1000;
    g_host_clock_started = true;
  } else {
    g_host_floor_us += static_cast<std::int64_t>(now_ms - g_host_last_ms) * 1000;
  }
  g_host_last_ms = now_ms;
  if (g_host_floor_us > g_now_us) g_now_us = g_host_floor_us;
}

std::int64_t clock_now_us() { return g_now_us; }

void set_touch_sampler(tinydraw::esp32::VectorV2TouchSampler* sampler) {
  g_sampler = sampler;
  g_next_poll_us = g_now_us;
}

void clock_advance_us(std::int64_t microseconds) {
  if (microseconds > 0) g_now_us += microseconds;
}

void pump(std::int64_t microseconds) {
  clock_advance_us(microseconds);
  if (g_pumping) return;
  g_pumping = true;
  fire_due_timers();
  if (g_sampler == nullptr) {
    g_pumping = false;
    return;
  }
  // The board's touch task polls every millisecond (vector_v2_touch_sampler.cpp
  // ends its loop body with vTaskDelay(pdMS_TO_TICKS(1))). Bounded so a long
  // jump in host time cannot turn into an unbounded catch-up loop.
  int polls = 0;
  while (g_next_poll_us <= g_now_us && polls < 4096) {
    g_next_poll_us += 1000;
    g_sampler->poll_once();
    ++polls;
  }
  if (polls >= 4096) g_next_poll_us = g_now_us;
  g_pumping = false;
}

void latch_touch(int down, int x, int y) {
  g_touch_down = down != 0;
  g_touch_x = static_cast<float>(x);
  g_touch_y = static_cast<float>(y);
}

bool sample_contact(float& x, float& y) {
  x = g_touch_x;
  y = g_touch_y;
  return g_touch_down;
}

void latch_button(int index, int down) {
  if (index != 0) return;
  g_button_down = down != 0;
}

int button_level() { return g_button_down ? 0 : 1; }

}  // namespace tinydraw::puck

// ---- <esp_timer.h> ---------------------------------------------------------
//
extern "C" std::int64_t esp_timer_get_time(void) { return tinydraw::puck::clock_now_us(); }

extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t* args,
                                       esp_timer_handle_t* out_handle) {
  if (args == nullptr || args->callback == nullptr || out_handle == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  for (tinydraw_puck_timer*& slot : tinydraw::puck::g_timers) {
    if (slot != nullptr) continue;
    slot = static_cast<tinydraw_puck_timer*>(std::malloc(sizeof(tinydraw_puck_timer)));
    if (slot == nullptr) return ESP_ERR_NO_MEM;
    *slot = {.callback = args->callback, .argument = args->arg};
    *out_handle = slot;
    return ESP_OK;
  }
  return ESP_ERR_NO_MEM;
}

extern "C" esp_err_t esp_timer_start_once(esp_timer_handle_t timer, std::uint64_t timeout_us) {
  if (timer == nullptr) return ESP_ERR_INVALID_ARG;
  const std::int64_t bounded =
      timeout_us > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
          ? std::numeric_limits<std::int64_t>::max()
          : static_cast<std::int64_t>(timeout_us);
  timer->due_us = tinydraw::puck::clock_now_us() + bounded;
  timer->active = true;
  return ESP_OK;
}

extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  if (timer == nullptr) return ESP_ERR_INVALID_ARG;
  timer->active = false;
  return ESP_OK;
}

extern "C" esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
  if (timer == nullptr) return ESP_ERR_INVALID_ARG;
  for (tinydraw_puck_timer*& slot : tinydraw::puck::g_timers) {
    if (slot != timer) continue;
    slot = nullptr;
    std::free(timer);
    return ESP_OK;
  }
  return ESP_ERR_INVALID_ARG;
}

// ---- <esp_heap_caps.h> -----------------------------------------------------
//
// One flat heap. The capability bits are ignored, which loses the board's
// internal-vs-PSRAM placement (a speed property, and emu_abi.h says never to
// trust speed here) and keeps the allocation order and sizes exactly as
// AppStorage asks for them.

namespace {
}  // namespace

extern "C" void* heap_caps_malloc(std::size_t size, std::uint32_t) {
  return std::malloc(size);
}

extern "C" void* heap_caps_calloc(std::size_t count, std::size_t size, std::uint32_t) {
  return std::calloc(count, size);
}

extern "C" void heap_caps_free(void* pointer) { std::free(pointer); }

// A flat wasm allocator cannot truthfully report ESP-IDF capability heaps.
extern "C" std::size_t heap_caps_get_free_size(std::uint32_t) { return 0U; }

extern "C" std::size_t heap_caps_get_largest_free_block(std::uint32_t) { return 0U; }

// ---- <driver/gpio.h> -------------------------------------------------------

extern "C" int gpio_config(const gpio_config_t*) { return 0; }

extern "C" int gpio_get_level(gpio_num_t) { return tinydraw::puck::button_level(); }

// ---- <freertos/*> ----------------------------------------------------------

extern "C" void vTaskDelay(TickType_t ticks) {
  tinydraw::puck::pump(static_cast<std::int64_t>(ticks) * 1000);
}

extern "C" void taskYIELD(void) { tinydraw::puck::pump(0); }
extern "C" void vTaskSuspend(void*) {}
extern "C" void vTaskDelete(void*) {}

extern "C" UBaseType_t uxTaskGetStackHighWaterMark(void*) {
  // The app prints this once at startup. There is no task stack here; the
  // module's stack size is fixed by puck/CMakeLists.txt.
  return 0;
}

extern "C" TickType_t xTaskGetTickCount(void) {
  return static_cast<TickType_t>(tinydraw::puck::clock_now_us() / 1000);
}

extern "C" void vTaskSetTimeOutState(TimeOut_t* timeout) {
  if (timeout != nullptr) timeout->entered = xTaskGetTickCount();
}

extern "C" BaseType_t xTaskCheckForTimeOut(TimeOut_t* timeout, TickType_t* remaining_ticks) {
  if (timeout == nullptr || remaining_ticks == nullptr) return pdTRUE;
  const TickType_t elapsed = xTaskGetTickCount() - timeout->entered;
  if (elapsed >= *remaining_ticks) {
    *remaining_ticks = 0;
    return pdTRUE;
  }
  *remaining_ticks -= elapsed;
  return pdFALSE;
}

extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, std::uint32_t, void*,
                                              UBaseType_t, TaskHandle_t* out_handle, BaseType_t) {
  // Deliberately does not run the entry point: see ../shim/freertos/task.h.
  // A non-null handle is what the requester checks, and the emulator drives
  // this app's one task from the main loop.
  if (out_handle != nullptr) *out_handle = reinterpret_cast<TaskHandle_t>(1);
  return pdPASS;
}

extern "C" BaseType_t xTaskCreate(TaskFunction_t entry, const char* name, std::uint32_t stack_words,
                                  void* argument, UBaseType_t priority, TaskHandle_t* out_handle) {
  return xTaskCreatePinnedToCore(entry, name, stack_words, argument, priority, out_handle, 0);
}

extern "C" SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) {
  if (storage == nullptr) return nullptr;
  storage->count = 0;
  storage->maximum = 1;
  storage->created = 1;
  return storage;
}

extern "C" SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) {
  if (storage == nullptr) return nullptr;
  storage->count = 1;
  storage->maximum = 1;
  storage->created = 1;
  return storage;
}

extern "C" SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t maximum, UBaseType_t initial,
                                                            StaticSemaphore_t* storage) {
  if (storage == nullptr) return nullptr;
  storage->count = static_cast<int>(initial);
  storage->maximum = static_cast<int>(maximum);
  storage->created = 1;
  return storage;
}

extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout_ticks) {
  if (semaphore == nullptr) return pdFALSE;
  if (semaphore->count > 0) {
    --semaphore->count;
    return pdTRUE;
  }
  if (timeout_ticks == 0) return pdFALSE;
  // A blocking take is where the app says "I have nothing to do for N ms", and
  // on the board that is exactly when the other core's touch polls land. So a
  // wait here really does let time pass and input arrive, one millisecond at a
  // time, and returns the moment the thing being waited on shows up.
  //
  // Bounded even for portMAX_DELAY: an unbounded wait in a single-stack build
  // is a hang, and nothing in the ported set waits forever except the touch
  // sampler's teardown, which this build never reaches.
  const TickType_t limit = timeout_ticks == portMAX_DELAY ? 64U : timeout_ticks;
  for (TickType_t elapsed = 0; elapsed < limit; ++elapsed) {
    tinydraw::puck::pump(1000);
    if (semaphore->count > 0) {
      --semaphore->count;
      return pdTRUE;
    }
  }
  return pdFALSE;
}

extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  if (semaphore == nullptr) return pdFALSE;
  if (semaphore->count >= semaphore->maximum) return pdFALSE;
  ++semaphore->count;
  return pdTRUE;
}
