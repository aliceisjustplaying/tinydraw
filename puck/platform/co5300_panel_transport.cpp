// Co5300PanelTransport for the wasm build: the same public contract
// (esp32/main/co5300_panel_transport.h, unmodified), landing pixels in the
// emulator's framebuffer instead of on a QSPI bus.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// WHAT IS PRESERVED, because the presenter is real code under test:
//
//   The strip protocol. stream_rect/stream_rect_ring cut the window into
//   strips, hand each one to the caller's PanelStagePatch as a staging
//   surface, and only then send it. VectorV2Presenter does its exposed-region
//   compose, its live ink overlay and its whole chrome paint inside that
//   callback (vector_v2_presenter_staging.cpp's paint_stage_surface), so the
//   callback's shape is load-bearing, not an implementation detail.
//
//   Ring addressing. Panel row y reads buffer row (y + shift_y) % area_height
//   and column x reads (x + shift_x) % area_width. This is how panning avoids
//   copying a whole frame, and getting it wrong here would show up as a
//   scrambled pan that the device does not have.
//
//   The byte swap. The panel's DMA wants RGB565 with the bytes the other way
//   round, and the transport does that swap during staging, offering the
//   patch a pre-swapped buffer when the patch says it can write into one
//   (accepts_byte_swapped). Both paths run here, and the framebuffer holds the
//   swapped result, which is why emu_device() declares "rgb565be": what the
//   page blits is the memory the panel would have received.
//
//   Fail-closed window validation. The CO5300 needs in-bounds, even-aligned
//   windows; an invalid submission is refused rather than clamped, so a
//   geometry bug surfaces here the way it would on glass.
//
// WHAT IS NOT REAL, and must not be read as if it were: every duration. There
// is no bus, no DMA queue and no panel, so prepare/transfer/staging timings
// are the virtual clock's answer, and the "TE" edge below is a counter, not a
// scan-out phase. emu_abi.h's own rule: any question about responsiveness is
// a question for the hardware, always.

#include "co5300_panel_transport.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "esp32s3_timing.h"
#include "esp_timer.h"
#include "puck_platform.h"

namespace tinydraw::esp32 {
namespace {

constexpr int kPanelWidth = puck::kPanelWidth;
constexpr int kPanelHeight = puck::kPanelHeight;

// Match the real transport's DMA transfer capacity so the presenter emits the
// same strip geometry here and on glass.
constexpr std::size_t kStagingPixels = static_cast<std::size_t>(kCo5300TransferPixels);

// One virtual tear edge per 60 Hz frame of virtual time. The presenter waits
// for one before a full-frame present; on the board that wait is a real
// synchronisation with scan-out, and here it is only a gate that opens.
constexpr std::int64_t kTearPeriodUs = 16'667;

[[nodiscard]] std::uint16_t swap_bytes(std::uint16_t value) {
  return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
}

[[nodiscard]] bool valid_window(int x, int y, int width, int height) {
  return width > 0 && height > 0 && x >= 0 && y >= 0 && x + width <= kPanelWidth &&
         y + height <= kPanelHeight && ((x | y | width | height) & 1) == 0;
}

}  // namespace

class Co5300PanelTransport::Impl {
 public:
  std::array<std::uint16_t, kStagingPixels> staging{};
  PanelStagingTiming staging_timing{};
  std::uint32_t pushes = 0;
  std::uint32_t rejected = 0;
  std::uint32_t submits = 0;
  std::uint32_t completes = 0;
  std::int64_t prepare_us = 0;
  std::int64_t acquire_wait_us = 0;
  std::int64_t ring_copy_us = 0;
  std::int64_t patch_us = 0;
  std::int64_t byte_swap_us = 0;
  std::int64_t transfer_us = 0;
  // Completion timestamps for the last few submissions, indexed by sequence.
  static constexpr std::size_t kCompletionHistory = 64;
  std::array<std::int64_t, kCompletionHistory> completed_us{};
  std::uint32_t tear_rising = 0;
  std::uint32_t tear_falling = 0;
  std::int64_t tear_last_us = 0;

  // Copies one source row into staging, ring-addressed and optionally
  // byte-swapped. `first_column` is where the window's leftmost pixel sits in
  // this row: the ring maps it through (panel_x + i + shift_x) % area_width,
  // and a linear source has it at column 0 of the region it was handed.
  static void stage_row(const std::uint16_t* row, int shift_x, int area_width, int panel_x,
                        int width, std::uint16_t* destination, bool swapped, bool linear) {
    if (linear) {
      if (!swapped) {
        std::memcpy(destination, row, static_cast<std::size_t>(width) * sizeof(*row));
        return;
      }
      for (int i = 0; i < width; ++i) destination[i] = swap_bytes(row[i]);
      return;
    }
    for (int i = 0; i < width; ++i) {
      const std::uint16_t pixel = row[(panel_x + i + shift_x) % area_width];
      destination[i] = swapped ? swap_bytes(pixel) : pixel;
    }
  }

  void publish(int panel_x, int panel_y, int width, int height, const std::uint16_t* staged,
               int staged_stride, bool staged_swapped) {
    const std::size_t transfer_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(*staged);
    puck::timing::record_internal_read(transfer_bytes);
    puck::timing::record_panel_write(transfer_bytes);
    std::uint16_t* frame = puck::framebuffer();
    for (int row = 0; row < height; ++row) {
      std::uint16_t* out = frame + static_cast<std::size_t>(panel_y + row) * kPanelWidth + panel_x;
      const std::uint16_t* in =
          staged + static_cast<std::size_t>(row) * static_cast<std::size_t>(staged_stride);
      if (staged_swapped) {
        std::memcpy(out, in, static_cast<std::size_t>(width) * sizeof(*out));
        continue;
      }
      for (int i = 0; i < width; ++i) out[i] = swap_bytes(in[i]);
    }
    puck::record_push(panel_x, panel_y, width, height);
    ++pushes;
    ++submits;
    ++completes;
    completed_us[submits % kCompletionHistory] = esp_timer_get_time();
  }

  // The one path both stream_rect and stream_rect_ring reduce to. A linear
  // source is a ring with zero shift over an area the size of the window's
  // own stride, which is why there is one implementation and not two.
  bool stream(int x, int y, int width, int height, const std::uint16_t* area_pixels, int stride,
              int shift_x, int shift_y, int area_width, int area_height, int strip_rows,
              PanelStagePatch patch, bool linear) {
    const bool invalid_ring =
        !linear && (stride < area_width || x + width > area_width || y + height > area_height);
    if (!valid_window(x, y, width, height) || area_pixels == nullptr || stride < width ||
        area_width <= 0 || area_height <= 0 || shift_x < 0 || shift_x >= area_width ||
        shift_y < 0 || shift_y >= area_height || invalid_ring) {
      ++rejected;
      return false;
    }
    int rows = strip_rows <= 0 ? height : strip_rows;
    rows = std::min<int>(rows, static_cast<int>(kStagingPixels / static_cast<std::size_t>(width)));
    if (rows <= 0) {
      ++rejected;
      return false;
    }
    const bool swapped = patch.accepts_byte_swapped;
    staging_timing = {};
    for (int top = y; top < y + height; top += rows) {
      const int strip_height = std::min(rows, y + height - top);
      const std::int64_t staged_started = esp_timer_get_time();
      for (int row = 0; row < strip_height; ++row) {
        const int source_row = linear ? top + row - y : (top + row + shift_y) % area_height;
        const std::uint16_t* source =
            area_pixels + static_cast<std::ptrdiff_t>(source_row) * stride;
        const std::size_t row_bytes = static_cast<std::size_t>(width) * sizeof(*source);
        puck::timing::record_read(source, row_bytes);
        puck::timing::record_internal_write(row_bytes);
        stage_row(source, shift_x, area_width, x, width,
                  staging.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                  swapped, linear);
      }
      ring_copy_us += esp_timer_get_time() - staged_started;

      const PanelStageSurface surface{
          .panel_x = x,
          .panel_y = top,
          .width = width,
          .height = strip_height,
          .stride = width,
          .pixels =
              std::span<std::uint16_t>(staging.data(), static_cast<std::size_t>(width) *
                                                           static_cast<std::size_t>(strip_height)),
          .byte_swapped = swapped,
      };
      const std::int64_t patch_started = esp_timer_get_time();
      if (!patch.apply(surface)) {
        ++rejected;
        return false;
      }
      patch_us += esp_timer_get_time() - patch_started;

      const std::int64_t transfer_started = esp_timer_get_time();
      publish(x, top, width, strip_height, staging.data(), width, swapped);
      transfer_us += esp_timer_get_time() - transfer_started;

      if (staging_timing.strip_count < kMaximumPanelStreamStrips) {
        staging_timing.strips[staging_timing.strip_count++] = {
            .samples = 1,
            .total_us = 0,
            .maximum_us = 0,
            .panel_y = top,
            .rows = strip_height,
            .wire_budget_us = 0,
        };
      }
      ++staging_timing.samples;
    }
    return true;
  }
};

Co5300PanelTransport::Co5300PanelTransport() : impl_(std::make_unique<Impl>()) {
  // Paper, so the first frame the app presents composes over a clean panel
  // rather than over whatever linear memory happened to hold.
  std::uint16_t* frame = puck::framebuffer();
  std::fill_n(frame, puck::kPanelPixels, static_cast<std::uint16_t>(0xFFFFU));
}

Co5300PanelTransport::~Co5300PanelTransport() = default;

bool Co5300PanelTransport::ready() const { return impl_ != nullptr; }

void Co5300PanelTransport::reset_timing() {
  impl_->prepare_us = 0;
  impl_->acquire_wait_us = 0;
  impl_->ring_copy_us = 0;
  impl_->patch_us = 0;
  impl_->byte_swap_us = 0;
  impl_->transfer_us = 0;
  impl_->staging_timing = {};
}

std::int64_t Co5300PanelTransport::prepare_us() const { return impl_->prepare_us; }
std::int64_t Co5300PanelTransport::acquire_wait_us() const { return impl_->acquire_wait_us; }
std::int64_t Co5300PanelTransport::ring_copy_us() const { return impl_->ring_copy_us; }
std::int64_t Co5300PanelTransport::patch_us() const { return impl_->patch_us; }
std::int64_t Co5300PanelTransport::byte_swap_us() const { return impl_->byte_swap_us; }
std::int64_t Co5300PanelTransport::transfer_us() const { return impl_->transfer_us; }

const PanelStagingTiming& Co5300PanelTransport::staging_timing() const {
  return impl_->staging_timing;
}

std::uint32_t Co5300PanelTransport::push_count() const { return impl_->pushes; }
std::uint32_t Co5300PanelTransport::rejected_push_count() const { return impl_->rejected; }
std::uint32_t Co5300PanelTransport::submit_count() const { return impl_->submits; }
std::uint32_t Co5300PanelTransport::complete_count() const { return impl_->completes; }

std::int64_t Co5300PanelTransport::complete_time_us(std::uint32_t sequence) const {
  if (sequence == 0U || sequence > impl_->submits ||
      impl_->submits - sequence >= Impl::kCompletionHistory) {
    return -1;
  }
  return impl_->completed_us[sequence % Impl::kCompletionHistory];
}

TearSignalTiming Co5300PanelTransport::tear_signal_timing() const {
  return {
      .rising_edges = impl_->tear_rising,
      .falling_edges = impl_->tear_falling,
      .period_us = kTearPeriodUs,
      .high_us = kTearPeriodUs / 8,
      .level = (impl_->tear_rising & 1U) != 0U,
  };
}

TearEdgeWaitResult Co5300PanelTransport::wait_for_tear_edge(TearSignalEdge edge, std::int64_t) {
  // On the board this blocks until the panel's TE line moves. There is no
  // scan-out here, so the gate simply opens, and the edge counter advances on
  // the virtual 60 Hz grid so the app's own diagnostics see it move.
  const std::int64_t now = esp_timer_get_time();
  if (now - impl_->tear_last_us >= kTearPeriodUs) {
    impl_->tear_last_us = now;
  }
  if (edge == TearSignalEdge::kRising) {
    ++impl_->tear_rising;
  } else {
    ++impl_->tear_falling;
  }
  return {
      .selected_edge = edge,
      .observed = true,
      .timed_out = false,
      .heal_attempted = false,
      .heal_command_sent = false,
      .edge_count = edge == TearSignalEdge::kRising ? impl_->tear_rising : impl_->tear_falling,
      .isr_timestamp_us = static_cast<std::uint32_t>(now),
      .task_resume_timestamp_us = static_cast<std::uint32_t>(now),
  };
}

std::int64_t Co5300PanelTransport::tear_age_us(TearSignalEdge) const {
  return esp_timer_get_time() - impl_->tear_last_us;
}

bool Co5300PanelTransport::wait_for_all(std::int64_t) {
  // Nothing is ever in flight: publish() completes inside the call.
  return true;
}

void Co5300PanelTransport::push_rect(int x, int y, int width, int height,
                                     const std::uint16_t* pixels, int stride) {
  const int row_stride = stride == 0 ? width : stride;
  static_cast<void>(impl_->stream(x, y, width, height, pixels, row_stride, 0, 0,
                                  std::max(row_stride, width), std::max(height, 1), height, {},
                                  true));
}

void Co5300PanelTransport::push_rect_ring(int x, int y, int width, int height,
                                          const std::uint16_t* area_pixels, int stride, int shift_x,
                                          int shift_y, int area_width, int area_height) {
  static_cast<void>(impl_->stream(x, y, width, height, area_pixels, stride, shift_x, shift_y,
                                  area_width, area_height, height, {}, false));
}

bool Co5300PanelTransport::stream_rect(int x, int y, int width, int height,
                                       const std::uint16_t* pixels, int stride, int strip_rows,
                                       PanelStagePatch patch) {
  // `pixels` is the window's own top-left inside a buffer of `stride`, so this
  // is the linear case: row r of the window is at pixels + r * stride.
  if (pixels == nullptr) return false;
  return impl_->stream(x, y, width, height, pixels, stride, 0, 0, std::max(stride, x + width),
                       std::max(y + height, 1), strip_rows, patch, true);
}

bool Co5300PanelTransport::stream_rect_ring(int x, int y, int width, int height,
                                            const std::uint16_t* area_pixels, int stride,
                                            int shift_x, int shift_y, int area_width,
                                            int area_height, int strip_rows,
                                            PanelStagePatch patch) {
  return impl_->stream(x, y, width, height, area_pixels, stride, shift_x, shift_y, area_width,
                       area_height, strip_rows, patch, false);
}

int Co5300PanelTransport::read_scanline() { return -1; }

bool Co5300PanelTransport::read_register(std::uint8_t, std::uint8_t*, std::size_t) { return false; }

}  // namespace tinydraw::esp32
