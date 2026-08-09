#pragma once

#include <span>

#include "esp_lcd_panel_ops.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

class QemuDisplayBackend final : public DisplayBackend {
 public:
  QemuDisplayBackend();
  ~QemuDisplayBackend() override;

  QemuDisplayBackend(const QemuDisplayBackend&) = delete;
  QemuDisplayBackend& operator=(const QemuDisplayBackend&) = delete;

  [[nodiscard]] bool ready() const { return panel_ != nullptr && framebuffer_ != nullptr; }
  [[nodiscard]] std::span<std::uint16_t> framebuffer();
  [[nodiscard]] bool refresh();
  void push_rect(int x, int y, int width, int height, const std::uint16_t* rgb565) override;
  [[nodiscard]] bool busy() const override { return false; }

 private:
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::uint16_t* framebuffer_ = nullptr;
};

}  // namespace tinydraw::esp32
