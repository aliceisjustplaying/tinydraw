#include "physical_touch.h"

#include <cstdint>

#include "driver/gpio.h"
#include "esp_lcd_touch_cst816s.h"
#include "tinydraw/geometry.h"

namespace tinydraw::esp32 {

PhysicalTouch::PhysicalTouch() {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = GPIO_NUM_15;
  bus_config.scl_io_num = GPIO_NUM_14;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK) {
    return;
  }
  esp_lcd_panel_io_i2c_config_t io_config{};
  io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
  io_config.scl_speed_hz = 400000;
  io_config.control_phase_bytes = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.flags.disable_control_phase = true;
  if (esp_lcd_new_panel_io_i2c(bus_, &io_config, &io_) != ESP_OK) {
    return;
  }
  esp_lcd_touch_config_t touch_config{};
  touch_config.x_max = kCanvasWidth;
  touch_config.y_max = kCanvasHeight;
  touch_config.rst_gpio_num = GPIO_NUM_NC;
  touch_config.int_gpio_num = GPIO_NUM_21;
  touch_config.levels.reset = 0;
  touch_config.levels.interrupt = 0;
  ready_ = esp_lcd_touch_new_i2c_cst816s(io_, &touch_config, &touch_) == ESP_OK;
}

bool PhysicalTouch::ready() const { return ready_; }

i2c_master_bus_handle_t PhysicalTouch::bus() const { return bus_; }

TouchRead PhysicalTouch::read(Point& point) {
  if (!ready_ || esp_lcd_touch_read_data(touch_) != ESP_OK) {
    return TouchRead::kError;
  }
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t strength = 0;
  std::uint8_t count = 0;
  if (!esp_lcd_touch_get_coordinates(touch_, &x, &y, &strength, &count, 1) || count == 0U) {
    return TouchRead::kNoTouch;
  }
  point = {.x = static_cast<float>(x), .y = static_cast<float>(y)};
  return TouchRead::kPoint;
}

}  // namespace tinydraw::esp32
