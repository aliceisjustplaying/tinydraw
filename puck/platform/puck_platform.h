// The seam between puck's emulator ABI and TinyDraw's ported firmware.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// Everything the board would have supplied and the browser now supplies goes
// through here: the panel's memory, the clock, the finger, the button. The
// product code never sees this header; it sees Co5300PanelTransport,
// esp_timer_get_time(), PhysicalTouch and gpio_get_level(), whose puck
// implementations (../platform/*.cpp) are written against this.

#ifndef TINYDRAW_PUCK_PLATFORM_H
#define TINYDRAW_PUCK_PLATFORM_H

#include <cstddef>
#include <cstdint>

namespace tinydraw::esp32 {
class VectorV2TouchSampler;
}

namespace tinydraw::puck {

inline constexpr int kPanelWidth = 368;
inline constexpr int kPanelHeight = 448;
inline constexpr std::size_t kPanelPixels =
    static_cast<std::size_t>(kPanelWidth) * static_cast<std::size_t>(kPanelHeight);

// ---- the panel -------------------------------------------------------------
//
// RGB565 with the two bytes in the order the CO5300's DMA wants, which is what
// emu_device() declares as "rgb565be". Holding the panel's own byte order
// rather than a tidied copy is the point: the transport's byte-swap staging
// pass is real code on the device, so it stays real here, and what the page
// blits is the memory the panel would have received.
[[nodiscard]] std::uint16_t* framebuffer();

// One window the firmware's push path actually sent, after its own alignment.
void record_push(int x, int y, int width, int height);
void reset_pushes();
[[nodiscard]] std::size_t push_count();
[[nodiscard]] int push_x(std::size_t index);
[[nodiscard]] int push_y(std::size_t index);
[[nodiscard]] int push_w(std::size_t index);
[[nodiscard]] int push_h(std::size_t index);

// Deterministic transport-failure seam for the TinyDraw verification harness.
// The device descriptor declares no sensors; a negative emu_sensor_event()
// arms this counter without widening the public emulator ABI.
void fail_next_panel_streams(std::uint32_t count);
[[nodiscard]] bool consume_panel_stream_failure();

// ---- the clock -------------------------------------------------------------
//
// esp_timer_get_time()'s only source. emu_tick(nowMs) sets the floor; each
// cooperative step of the app's loop advances it a little, so the app's own
// timeouts pass and a replay stays a pure function of the trace.
void clock_set_floor_ms(std::uint32_t now_ms);
void clock_advance_us(std::int64_t microseconds);
[[nodiscard]] std::int64_t clock_now_us();

// ---- input -----------------------------------------------------------------

void latch_touch(int down, int x, int y);
// The contact as the CST820 would report it right now. Returns false for no
// contact, which PhysicalTouch::read turns into TouchRead::kNoTouch.
[[nodiscard]] bool sample_contact(float& x, float& y);

void latch_button(int index, int down);
// Active low, like the real GPIO0 with its pull-up.
[[nodiscard]] int button_level();

// ---- cooperative concurrency ----------------------------------------------
//
// The board polls touch on core 1 every millisecond while the app's loop runs
// on core 0. There is one stack here, so the app's loop pumps the sampler
// itself: every time virtual time crosses a millisecond, the sampler's own
// poll_once() runs. Registered once at startup; called from vTaskDelay,
// taskYIELD, and the emulator's step loop.
void set_touch_sampler(tinydraw::esp32::VectorV2TouchSampler* sampler);
// Advances virtual time by `microseconds` and runs any touch polls that fall
// inside that span.
void pump(std::int64_t microseconds);

}  // namespace tinydraw::puck

#endif  // TINYDRAW_PUCK_PLATFORM_H
