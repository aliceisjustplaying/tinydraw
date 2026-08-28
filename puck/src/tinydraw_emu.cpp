// TinyDraw V2 as a puck emulator module: the emu_abi.h side.
//
// SPDX-License-Identifier: MIT
// TinyDraw is Copyright (c) aliceisjustplaying and TinyDraw contributors.
// See ../../LICENSE. This file is new; everything it drives is TinyDraw's own
// firmware, compiled to wasm.
//
// WHAT RUNS HERE. The real V2 application: esp32/main/vector_v2/'s app loop,
// its presenter, its chrome controller, its background pipeline, its live
// stroke session, on top of the whole vector_v2/ authority (OperationLog,
// MaterializedCanvas, TileProducer, settled tiles) and core/'s ink. Not a
// re-implementation of any of it. This file only does what a board does:
// bring the app up, hand it a clock, hand it a finger and a button, and let
// its own loop run.
//
// THE ONE STRUCTURAL DIFFERENCE, stated up front because it is the only place
// the firmware could not be taken as-is. On the device run_vector_v2_app()
// never returns: it spins forever and blocks on FreeRTOS when idle. wasm has
// one thread and no way to suspend a C++ frame mid-call, so the app's loop was
// split into vector_v2_app_start() and vector_v2_app_step()
// (esp32/main/vector_v2/vector_v2_app.h), and emu_tick() calls step() as many
// times as the frame's budget allows. run_vector_v2_app() still exists and is
// still `start, then loop forever`, so the device build's behaviour is
// unchanged. Nothing else about the loop moved: every `continue` became a
// return, and every local became a member of the same session in the same
// order.
//
// TIME. Host time establishes a deterministic floor. Each application step
// then receives one fixed work quantum. Reading the clock never changes it,
// so logging and diagnostics cannot perturb application behaviour.

#include <cstdint>
#include <new>

#include "puck_platform.h"
#include "vector_v2_app.h"

#if defined(TINYDRAW_PUCK_HAVE_ABI_HEADER)
// scripts/puck points CMake at Puck's own contract, so these prototypes are
// checked by the compiler as well as by the canonical loader verification.
extern "C" {
#include "emu_abi.h"
}
#endif

namespace {

// The session outlives every tick, so it is placement-new'd into static
// storage rather than being a function-local with a guard variable.
alignas(tinydraw::esp32::VectorV2AppSession) unsigned char g_session_storage[sizeof(
    tinydraw::esp32::VectorV2AppSession)];
tinydraw::esp32::VectorV2AppSession* g_session = nullptr;
bool g_running = false;
std::uint32_t g_last_now_ms = 0;
bool g_ticked = false;

// How much virtual time one frame may consume. The floor keeps a trace that
// ticks in tiny increments from starving the app of the time its own slice
// budgets are written against; the ceiling keeps a long gap between ticks (a
// paused tab, a sparse trace) from turning into one enormous frame.
constexpr std::int64_t kMinimumFrameBudgetUs = 4'000;
constexpr std::int64_t kMaximumFrameBudgetUs = 40'000;
constexpr std::int64_t kStepQuantumUs = 1'000;
constexpr int kMaximumStepsPerTick = 40;

// MUST match what the app believes: 368x448, and RGB565 in the CO5300's own
// byte order, because the transport's byte-swap staging pass is real code here
// (../platform/co5300_panel_transport.cpp) and the framebuffer holds its
// output.
//
// One button, GPIO0, which is the board's only user button. The firmware maps
// a short release to HUD visibility and a long release to the demo controller.
const char g_device_json[] =
    "{"
    "\"name\":\"TinyDraw V2\","
    "\"panel\":{\"w\":368,\"h\":448,\"format\":\"rgb565be\"},"
    "\"buttons\":[{\"id\":\"hud\",\"label\":\"HUD / DEMO\",\"edge\":\"right\","
    "\"at\":0.16}],"
    "\"touch\":{\"points\":1},"
    "\"gestures\":[{\"id\":\"hud-toggle\",\"label\":\"show / hide HUD\","
    "\"how\":\"Press and release the HUD button to hide the battery, zoom controls, and "
    "minimap. The bottom toolbar remains visible and interactive. Press and release it "
    "again to restore the HUD.\"}]"
    "}";

}  // namespace

extern "C" {

int emu_device(void) { return static_cast<int>(reinterpret_cast<std::uintptr_t>(g_device_json)); }

int emu_init(void) {
  if (g_session != nullptr) return g_running ? 1 : 0;
  g_session = new (static_cast<void*>(g_session_storage)) tinydraw::esp32::VectorV2AppSession();
  // The app's own bring-up: allocate ~7.6 MiB of document working set, restore
  // authority, bootstrap the canvas, build the chrome, present the first
  // frame. It prints TINYDRAW_LIVE_FAIL and returns false if any of that
  // fails, and the reason reaches the page's console pane through WASI stdout.
  g_running = tinydraw::esp32::vector_v2_app_start(*g_session);
  if (!g_running) return 0;
  // Only now does the sampler exist. From here the emulator pumps its
  // poll_once() at the board's 1 ms cadence whenever virtual time advances.
  tinydraw::puck::set_touch_sampler(&*g_session->touch_sampler);
  return 1;
}

void emu_tick(std::uint32_t now_ms) {
  tinydraw::puck::reset_pushes();
  if (!g_running) return;
  tinydraw::puck::clock_set_floor_ms(now_ms);

  std::int64_t budget_us =
      g_ticked ? static_cast<std::int64_t>(now_ms - g_last_now_ms) * 1000 : kMinimumFrameBudgetUs;
  if (budget_us < kMinimumFrameBudgetUs) budget_us = kMinimumFrameBudgetUs;
  if (budget_us > kMaximumFrameBudgetUs) budget_us = kMaximumFrameBudgetUs;
  g_last_now_ms = now_ms;
  g_ticked = true;

  const std::int64_t deadline_us = tinydraw::puck::clock_now_us() + budget_us;
  int steps = 0;
  while (tinydraw::puck::clock_now_us() < deadline_us && steps < kMaximumStepsPerTick) {
    tinydraw::puck::pump(kStepQuantumUs);
    tinydraw::esp32::vector_v2_app_step(*g_session);
    ++steps;
    if (!g_session->running) {
      g_running = false;
      break;
    }
  }
}

int emu_fb(void) {
  return static_cast<int>(reinterpret_cast<std::uintptr_t>(tinydraw::puck::framebuffer()));
}

int emu_push_count(void) { return static_cast<int>(tinydraw::puck::push_count()); }
int emu_push_x(int i) { return i < 0 ? 0 : tinydraw::puck::push_x(static_cast<std::size_t>(i)); }
int emu_push_y(int i) { return i < 0 ? 0 : tinydraw::puck::push_y(static_cast<std::size_t>(i)); }
int emu_push_w(int i) { return i < 0 ? 0 : tinydraw::puck::push_w(static_cast<std::size_t>(i)); }
int emu_push_h(int i) { return i < 0 ? 0 : tinydraw::puck::push_h(static_cast<std::size_t>(i)); }

void emu_touch(int down, int x, int y) { tinydraw::puck::latch_touch(down, x, y); }

void emu_button(int index, int down) { tinydraw::puck::latch_button(index, down); }

// Raw GPIO down/up timing belongs to the firmware; the host verdict is not an
// input source for TinyDraw.
void emu_button_verdict(int, int) {}

// TinyDraw currently declares no sensors. Keep the ABI total so replay code
// never needs a sensorless-device special case.
void emu_sensor_event(int index) {
  if (index < 0) {
    tinydraw::puck::fail_next_panel_streams(static_cast<std::uint32_t>(-index));
  }
}

}  // extern "C"
