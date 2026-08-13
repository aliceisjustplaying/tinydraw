#ifndef TINYDRAW_ESP32_PHYSICAL_TOUCH_H
#define TINYDRAW_ESP32_PHYSICAL_TOUCH_H

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "tinydraw/geometry.h"

namespace tinydraw::esp32 {

enum class TouchRead {
  kPoint,
  kNoTouch,
  kError,
};

// Instance-owned CST820/CST816S transport. The I2C bus is exposed only for the
// board's power and RTC adapters, which share this physical bus.
class PhysicalTouch {
 public:
  PhysicalTouch();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] i2c_master_bus_handle_t bus() const;
  [[nodiscard]] TouchRead read(Point& point);

 private:
  i2c_master_bus_handle_t bus_ = nullptr;
  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_touch_handle_t touch_ = nullptr;
  bool ready_ = false;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_PHYSICAL_TOUCH_H
