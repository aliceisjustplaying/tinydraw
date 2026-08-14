#pragma once

#include <cstdint>
#include <memory>

#include "board_hardware.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

struct TearSignalTiming {
  std::uint32_t rising_edges = 0;
  std::int64_t period_us = -1;
  std::int64_t high_us = -1;
  bool level = false;
};

// Owns the selected board profile's panel, DMA staging, queue capacity, and
// completion telemetry. Input pixels are RGB565 in host byte order. Both
// profiles use the common in-bounds even-window contract; invalid submissions
// fail closed. Callers must serialize submissions and telemetry access.
class PanelTransport final : public DisplayBackend {
 public:
  explicit PanelTransport(BoardHardware& hardware);
  ~PanelTransport() override;

  PanelTransport(const PanelTransport&) = delete;
  PanelTransport& operator=(const PanelTransport&) = delete;
  PanelTransport(PanelTransport&&) = delete;
  PanelTransport& operator=(PanelTransport&&) = delete;

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
  // Waits for the measured V2 CO5300 safe frame-start edge. V1 SH8601 timing
  // is not validated and returns false. Callers must fail open and present.
  [[nodiscard]] bool wait_for_safe_frame_start(std::int64_t timeout_us);
  [[nodiscard]] bool wait_for_all(std::int64_t timeout_us);

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                 int stride = 0) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tinydraw::esp32
