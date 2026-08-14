#include "physical_touch.h"

#include <cstdint>

#include "driver/gpio.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lcd_touch_ft5x06.h"

namespace tinydraw::esp32 {

PhysicalTouch::PhysicalTouch(BoardHardware& hardware) {
  if (!hardware.ready()) {
    return;
  }
  esp_lcd_panel_io_i2c_config_t io_config{};
  io_config.dev_addr = hardware.profile().revision == BoardRevision::kV1
                           ? ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS
                           : ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
  io_config.scl_speed_hz = 400000;
  io_config.control_phase_bytes = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.flags.disable_control_phase = true;
  if (esp_lcd_new_panel_io_i2c(hardware.bus(), &io_config, &io_) != ESP_OK) {
    return;
  }
  esp_lcd_touch_config_t touch_config{};
  touch_config.x_max = kCanvasWidth;
  touch_config.y_max = kCanvasHeight;
  touch_config.rst_gpio_num = GPIO_NUM_NC;
  touch_config.int_gpio_num = GPIO_NUM_21;
  touch_config.levels.reset = 0;
  touch_config.levels.interrupt = 0;
  const esp_err_t created = hardware.profile().revision == BoardRevision::kV1
                                ? esp_lcd_touch_new_i2c_ft5x06(io_, &touch_config, &touch_)
                                : esp_lcd_touch_new_i2c_cst816s(io_, &touch_config, &touch_);
  ready_ = created == ESP_OK;
}

PhysicalTouch::~PhysicalTouch() {
  if (touch_ != nullptr) {
    static_cast<void>(esp_lcd_touch_del(touch_));
  }
  if (io_ != nullptr) {
    static_cast<void>(esp_lcd_panel_io_del(io_));
  }
}

bool PhysicalTouch::ready() const { return ready_; }

TouchRead PhysicalTouch::read(Point& point) {
  if (!ready_ || esp_lcd_touch_read_data(touch_) != ESP_OK) {
    return TouchRead::kError;
  }
  esp_lcd_touch_point_data_t data{};
  std::uint8_t count = 0;
  if (esp_lcd_touch_get_data(touch_, &data, &count, 1) != ESP_OK) {
    return TouchRead::kError;
  }
  if (count == 0U) {
    return TouchRead::kNoTouch;
  }
  point = {.x = static_cast<float>(data.x), .y = static_cast<float>(data.y)};
  return TouchRead::kPoint;
}

}  // namespace tinydraw::esp32
