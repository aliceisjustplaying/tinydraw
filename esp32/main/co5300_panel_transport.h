#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

struct TearSignalTiming {
  std::uint32_t rising_edges = 0;
  std::uint32_t falling_edges = 0;
  std::int64_t period_us = -1;
  std::int64_t high_us = -1;
  bool level = false;
};

enum class TearSignalEdge : std::uint8_t {
  kRising,
  kFalling,
};

// Diagnostic TE observation only. Neither edge is assumed to identify an
// optically safe panel phase. Timestamps contain the low 32 bits of the ESP
// timer and can be subtracted as unsigned values across timer rollover.
struct PanelStageSurface {
  int panel_x = 0;
  int panel_y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  // Host-order RGB565. The transport byte-swaps after the patch returns.
  std::span<std::uint16_t> pixels{};
};

struct PanelStagePatch {
  void* context = nullptr;
  bool (*paint)(void* context, const PanelStageSurface& surface) = nullptr;

  [[nodiscard]] bool apply(const PanelStageSurface& surface) const {
    return paint == nullptr || paint(context, surface);
  }
};

struct TearEdgeWaitResult {
  TearSignalEdge selected_edge = TearSignalEdge::kRising;
  bool observed = false;
  bool timed_out = false;
  bool heal_attempted = false;
  bool heal_command_sent = false;
  std::uint32_t edge_count = 0;
  std::uint32_t isr_timestamp_us = 0;
  std::uint32_t task_resume_timestamp_us = 0;
};

// Owns one CO5300 panel, its DMA staging, queue capacity, and completion
// telemetry. Input pixels are RGB565 in host byte order. The panel requires
// in-bounds even-aligned windows; invalid submissions fail closed. Callers must
// serialize submissions and telemetry access.
class Co5300PanelTransport final : public DisplayBackend {
 public:
  Co5300PanelTransport();
  ~Co5300PanelTransport() override;

  Co5300PanelTransport(const Co5300PanelTransport&) = delete;
  Co5300PanelTransport& operator=(const Co5300PanelTransport&) = delete;
  Co5300PanelTransport(Co5300PanelTransport&&) = delete;
  Co5300PanelTransport& operator=(Co5300PanelTransport&&) = delete;

  [[nodiscard]] bool ready() const;
  void reset_timing();
  [[nodiscard]] std::int64_t prepare_us() const;
  [[nodiscard]] std::int64_t acquire_wait_us() const;
  [[nodiscard]] std::int64_t ring_copy_us() const;
  [[nodiscard]] std::int64_t patch_us() const;
  [[nodiscard]] std::int64_t byte_swap_us() const;
  [[nodiscard]] std::int64_t transfer_us() const;
  [[nodiscard]] std::uint32_t push_count() const;
  [[nodiscard]] std::uint32_t rejected_push_count() const;
  [[nodiscard]] std::uint32_t submit_count() const;
  [[nodiscard]] std::uint32_t complete_count() const;
  [[nodiscard]] std::int64_t complete_time_us(std::uint32_t sequence) const;
  [[nodiscard]] TearSignalTiming tear_signal_timing() const;
  // Waits for the next selected TE edge. A selected-edge count change is
  // required even when the shared ISR semaphore was posted by the other edge.
  // On timeout, one rate-limited TEON heal may be attempted; an edge observed
  // afterward is reported together with that attempt. No observed selected
  // edge is a failed result (observed=false), never an inferred success.
  [[nodiscard]] TearEdgeWaitResult wait_for_tear_edge(TearSignalEdge edge, std::int64_t timeout_us);
  // Diagnostic age since the last selected ISR edge, or -1 before the first
  // such edge. The low-32-bit timestamp supports unsigned rollover subtraction;
  // this value is not a visible-row or panel-phase observation.
  [[nodiscard]] std::int64_t tear_age_us(TearSignalEdge edge) const;
  [[nodiscard]] bool wait_for_all(std::int64_t timeout_us);

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                 int stride = 0) override;
  // Ring-addressed push: panel row y reads buffer row (y + shift_y) %
  // area_height and panel column x reads buffer column (x + shift_x) %
  // area_width of the area starting at area_pixels. De-rotation happens
  // inside the byte-swap staging pass, which already touches every pixel, so
  // a rotated source costs the same as a linear one.
  void push_rect_ring(int x, int y, int width, int height, const std::uint16_t* area_pixels,
                      int stride, int shift_x, int shift_y, int area_width, int area_height);

  // Characterization-probe path, not product code: programs one CASET/RASET
  // window, then streams the region as chunked RAMWR (0x2C) plus RAMWRC (0x3C)
  // continuation color transfers, bypassing per-strip window setup. Pixels are
  // RGB565 host order; staging byte-swaps into the shared DMA bounce buffers.
  // strip_rows is clamped to the transfer-buffer capacity. Returns false when
  // any command or submission fails. Completion is observable via
  // submit_count()/complete_count()/wait_for_all() exactly like push_rect.
  bool stream_rect(int x, int y, int width, int height, const std::uint16_t* pixels, int stride,
                   int strip_rows, PanelStagePatch patch = {});
  // Product frame-stream path. Programs one CASET/RASET window, de-rotates
  // each source strip into host-order internal DMA memory, applies patch,
  // byte-swaps, and submits RAMWR/RAMWRC continuations. It never drains;
  // one ordered presentation owner calls wait_for_all exactly once.
  bool stream_rect_ring(int x, int y, int width, int height, const std::uint16_t* area_pixels,
                        int stride, int shift_x, int shift_y, int area_width, int area_height,
                        int strip_rows, PanelStagePatch patch = {});
  // Characterization-probe read of GETSCANLINE (0x45). Returns the raw
  // 10-bit scanline value, or -1 when the QSPI read fails. Unvalidated
  // controller behavior: treat values as diagnostic until calibrated.
  [[nodiscard]] int read_scanline();
  // Characterization-probe register read (read opcode 0x03). Fills length
  // bytes into value; returns false when the QSPI transaction fails. Used
  // with known-nonzero registers as a control for the read path itself.
  bool read_register(std::uint8_t command, std::uint8_t* value, std::size_t length);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tinydraw::esp32
