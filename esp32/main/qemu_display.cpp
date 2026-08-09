#include "qemu_display.h"

#include "esp_lcd_qemu_rgb.h"
#include "tinydraw/geometry.h"

namespace tinydraw::esp32 {

QemuDisplayBackend::QemuDisplayBackend() {
  const esp_lcd_rgb_qemu_config_t config{
      .width = kCanvasWidth,
      .height = kCanvasHeight,
      .bpp = RGB_QEMU_BPP_16,
  };
  void* framebuffer = nullptr;
  if (esp_lcd_new_rgb_qemu(&config, &panel_) != ESP_OK || esp_lcd_panel_reset(panel_) != ESP_OK ||
      esp_lcd_panel_init(panel_) != ESP_OK ||
      esp_lcd_rgb_qemu_get_frame_buffer(panel_, &framebuffer) != ESP_OK) {
    if (panel_ != nullptr) {
      esp_lcd_panel_del(panel_);
      panel_ = nullptr;
    }
    return;
  }
  framebuffer_ = static_cast<std::uint16_t*>(framebuffer);
}

QemuDisplayBackend::~QemuDisplayBackend() {
  if (panel_ != nullptr) {
    esp_lcd_panel_del(panel_);
  }
}

std::span<std::uint16_t> QemuDisplayBackend::framebuffer() {
  return framebuffer_ == nullptr
             ? std::span<std::uint16_t>{}
             : std::span(framebuffer_, static_cast<std::size_t>(kCanvasWidth * kCanvasHeight));
}

bool QemuDisplayBackend::refresh() {
  return panel_ != nullptr && esp_lcd_rgb_qemu_refresh(panel_) == ESP_OK;
}

void QemuDisplayBackend::push_rect(int x, int y, int width, int height,
                                   const std::uint16_t* rgb565) {
  if (panel_ == nullptr || rgb565 == nullptr || width <= 0 || height <= 0) {
    return;
  }
  static_cast<void>(
      esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, rgb565));
}

}  // namespace tinydraw::esp32
