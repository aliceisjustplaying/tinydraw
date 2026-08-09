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
  [[nodiscard]] std::uint32_t refresh_count() const { return refresh_count_; }
  [[nodiscard]] std::uint32_t push_count() const { return push_count_; }
  void push_rect(int x, int y, int width, int height, const std::uint16_t* rgb565) override;

 private:
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::uint16_t* framebuffer_ = nullptr;
  std::uint32_t refresh_count_ = 0U;
  std::uint32_t push_count_ = 0U;
};

}  // namespace tinydraw::esp32
