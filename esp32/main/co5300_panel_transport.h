#pragma once

#include <cstdint>
#include <memory>

#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

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
  [[nodiscard]] bool wait_for_all(std::int64_t timeout_us);

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                 int stride = 0) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tinydraw::esp32
