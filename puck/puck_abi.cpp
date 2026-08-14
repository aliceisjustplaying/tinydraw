#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <span>
#include <vector>

#include "tinydraw/app/raster_core.h"
#include "tinydraw/platform/display_backend.h"

namespace {

constexpr std::size_t kMaxPushes = 256U;

struct PushRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

class PuckDisplay final : public tinydraw::DisplayBackend {
 public:
  void push_rect(int x, int y, int width, int height, const std::uint16_t*, int = 0) override {
    if (degraded_) {
      return;
    }
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || width > tinydraw::kCanvasWidth ||
        height > tinydraw::kCanvasHeight || x > tinydraw::kCanvasWidth - width ||
        y > tinydraw::kCanvasHeight - height) {
      // A silent drop trades a loud audit failure for an invisible page/framebuffer
      // desynchronization, so invalid rectangles degrade to an honest full refresh.
      collapse_to_full_panel();
      return;
    }
    if (count_ >= pushes_.size()) {
      // Preserve honest display invalidation under load: collapse the log to a
      // deterministic full-panel refresh and ignore subsequent pushes this tick.
      collapse_to_full_panel();
      return;
    }
    pushes_[count_++] = {.x = x, .y = y, .width = width, .height = height};
  }

  void reset() {
    count_ = 0U;
    degraded_ = false;
  }

  [[nodiscard]] int count() const { return static_cast<int>(count_); }
  [[nodiscard]] const PushRect& at(int index) const {
    static constexpr PushRect empty{};
    return index >= 0 && static_cast<std::size_t>(index) < count_
               ? pushes_[static_cast<std::size_t>(index)]
               : empty;
  }

 private:
  void collapse_to_full_panel() {
    pushes_[0] = {
        .x = 0, .y = 0, .width = tinydraw::kCanvasWidth, .height = tinydraw::kCanvasHeight};
    count_ = 1U;
    degraded_ = true;
  }

  std::array<PushRect, kMaxPushes> pushes_{};
  std::size_t count_ = 0U;
  bool degraded_ = false;
};

struct WasmState {
  static constexpr std::size_t kPixelCount = tinydraw::RasterCore::kPixelCount;

  std::vector<std::uint16_t> committed = std::vector<std::uint16_t>(kPixelCount, 0xFFFFU);
  std::vector<std::uint16_t> visible = std::vector<std::uint16_t>(kPixelCount, 0xFFFFU);
  std::vector<std::uint8_t> coverage = std::vector<std::uint8_t>(kPixelCount, 0U);
  std::vector<std::uint16_t> undo =
      std::vector<std::uint16_t>(tinydraw::TileUndoHistory::kRequiredPixels);
  std::vector<std::uint16_t> world =
      std::vector<std::uint16_t>(tinydraw::WorldCanvas::kRequiredPixels);
  PuckDisplay display;
  tinydraw::RasterCore app{{committed, visible, coverage, undo, world}, display};
};

alignas(WasmState) std::array<std::byte, sizeof(WasmState)> state_storage{};
WasmState* state = nullptr;

struct PendingTouch {
  int down = 0;
  int x = 0;
  int y = 0;
  bool dirty = false;
} pending_touch;

// docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md:155 proves only
// that the PMU button is lower; the V1 side is uncertain, so follow the sibling
// board's stacked right-edge convention.
constexpr char device_json[] =
    "{\"name\":\"TinyDraw Raster V1 interactive core\","
    "\"panel\":{\"w\":368,\"h\":448,\"format\":\"rgb565\"},"
    "\"buttons\":["
    "{\"id\":\"boot\",\"label\":\"BOOT\",\"edge\":\"right\",\"at\":0.38},"
    "{\"id\":\"power\",\"label\":\"Power\",\"edge\":\"right\",\"at\":0.62,"
    "\"longPressMs\":4000}],"
    "\"touch\":{\"points\":1}}";

}  // namespace

extern "C" {

int emu_device() { return static_cast<int>(reinterpret_cast<std::uintptr_t>(device_json)); }

int emu_init() {
  if (state != nullptr) {
    state->~WasmState();
  }
  state = new (state_storage.data()) WasmState;
  pending_touch = {};
  if (!state->app.ready()) {
    std::fputs("TinyDraw Puck: RasterCore storage initialization failed\n", stderr);
    state->~WasmState();
    state = nullptr;
    return 0;
  }
  state->display.reset();
  return 1;
}

void emu_tick(std::uint32_t now_ms) {
  if (state == nullptr) {
    return;
  }
  state->display.reset();
  const std::uint64_t now_us = static_cast<std::uint64_t>(now_ms) * 1'000U;
  if (pending_touch.dirty) {
    state->app.touch(pending_touch.down != 0,
                     {static_cast<float>(pending_touch.x), static_cast<float>(pending_touch.y)},
                     now_us);
    pending_touch.dirty = false;
  }
  // Contract: the core hears every emulator tick after pending input is
  // delivered, including idle ticks with no input.
  state->app.tick(now_us);
}

int emu_fb() {
  return state == nullptr
             ? 0
             : static_cast<int>(reinterpret_cast<std::uintptr_t>(state->app.framebuffer().data()));
}

int emu_push_count() { return state == nullptr ? 0 : state->display.count(); }

int emu_push_x(int index) { return state == nullptr ? 0 : state->display.at(index).x; }
int emu_push_y(int index) { return state == nullptr ? 0 : state->display.at(index).y; }
int emu_push_w(int index) { return state == nullptr ? 0 : state->display.at(index).width; }
int emu_push_h(int index) { return state == nullptr ? 0 : state->display.at(index).height; }

void emu_touch(int down, int x, int y) {
  pending_touch = {.down = down, .x = x, .y = y, .dirty = true};
}

// V1's BOOT demo-recording and PMU power services are outside this
// interactive-core port, so declared physical buttons are explicit no-ops.
void emu_button(int, int) {}
void emu_button_verdict(int, int) {}
void emu_sensor_event(int) {}

}  // extern "C"
