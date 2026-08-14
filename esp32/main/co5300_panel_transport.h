#pragma once

#include <cstdint>
#include <memory>

#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

struct TearSignalTiming {
  std::uint32_t rising_edges = 0;
  std::int64_t period_us = -1;
  std::int64_t high_us = -1;
  bool level = false;
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
  [[nodiscard]] std::int64_t transfer_us() const;
  [[nodiscard]] std::uint32_t push_count() const;
  [[nodiscard]] std::uint32_t rejected_push_count() const;
  [[nodiscard]] std::uint32_t submit_count() const;
  [[nodiscard]] std::uint32_t complete_count() const;
  [[nodiscard]] std::int64_t complete_time_us(std::uint32_t sequence) const;
  [[nodiscard]] TearSignalTiming tear_signal_timing() const;
  // Waits for the end of the CO5300 vertical TE pulse. At the measured panel
  // timing this places a top-to-bottom full-frame write safely behind scanout.
  // Returns false on timeout; callers should fail open and still present.
  [[nodiscard]] bool wait_for_safe_frame_start(std::int64_t timeout_us);
  // Microseconds since the last tear falling edge, or -1 before the first
  // edge. A frame writer that starts within a bounded age of the edge stays
  // ahead of the wrapped beam without waiting for the next edge.
  [[nodiscard]] std::int64_t tear_age_us() const;
  [[nodiscard]] bool wait_for_all(std::int64_t timeout_us);

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                 int stride = 0) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tinydraw::esp32
