#pragma once

#include "board_hardware.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "tinydraw/geometry.h"

namespace tinydraw::esp32 {

enum class TouchRead {
  kPoint,
  kNoTouch,
  kError,
};

// Revision-selected FT3168/FT5x06-family or CST820/CST816S-family transport.
// The shared I2C bus is owned by BoardHardware and must outlive this object.
class PhysicalTouch {
 public:
  explicit PhysicalTouch(BoardHardware& hardware);
  ~PhysicalTouch();

  PhysicalTouch(const PhysicalTouch&) = delete;
  PhysicalTouch& operator=(const PhysicalTouch&) = delete;
  PhysicalTouch(PhysicalTouch&&) = delete;
  PhysicalTouch& operator=(PhysicalTouch&&) = delete;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] TouchRead read(Point& point);

 private:
  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_touch_handle_t touch_ = nullptr;
  bool ready_ = false;
};

}  // namespace tinydraw::esp32
