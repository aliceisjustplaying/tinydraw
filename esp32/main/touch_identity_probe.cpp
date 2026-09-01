#include "touch_identity_probe.h"

#include <array>
#include <cstdint>
#include <cstdio>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kIoExpanderAddress = 0x20;
constexpr std::uint16_t kTouchAddress = 0x15;
constexpr std::uint8_t kIoExpanderOutputRegister = 0x01;
constexpr std::uint8_t kIoExpanderConfigRegister = 0x03;
constexpr std::uint8_t kIoExpanderOutputs = 0x87;
constexpr std::array<std::uint8_t, 3> kIdentityRegisters{0xA7, 0xA8, 0xA9};

esp_err_t write_register(i2c_master_dev_handle_t device, std::uint8_t address, std::uint8_t value) {
  const std::array payload{address, value};
  return i2c_master_transmit(device, payload.data(), payload.size(), 100);
}

}  // namespace

void run_touch_identity_probe() {
  std::printf("TINYDRAW_TOUCH_IDENTITY_START address=0x%02x\n", kTouchAddress);

  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = GPIO_NUM_15;
  bus_config.scl_io_num = GPIO_NUM_14;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  const esp_err_t bus_create = i2c_new_master_bus(&bus_config, &bus);
  if (bus_create != ESP_OK) {
    std::printf("TINYDRAW_TOUCH_IDENTITY_DONE pass=0 stage=bus_create error=%s\n",
                esp_err_to_name(bus_create));
    return;
  }

  const auto add_device = [&](std::uint16_t address, i2c_master_dev_handle_t* device) {
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(bus, &config, device);
  };

  i2c_master_dev_handle_t expander = nullptr;
  i2c_master_dev_handle_t touch = nullptr;
  const esp_err_t expander_add = add_device(kIoExpanderAddress, &expander);
  const esp_err_t touch_add = add_device(kTouchAddress, &touch);
  if (expander_add != ESP_OK || touch_add != ESP_OK) {
    std::printf("TINYDRAW_TOUCH_IDENTITY_DONE pass=0 stage=device_add expander=%s touch=%s\n",
                esp_err_to_name(expander_add), esp_err_to_name(touch_add));
    return;
  }

  const esp_err_t configure = write_register(expander, kIoExpanderConfigRegister,
                                             static_cast<std::uint8_t>(~kIoExpanderOutputs));
  const esp_err_t reset_low = write_register(expander, kIoExpanderOutputRegister, 0x80);
  vTaskDelay(pdMS_TO_TICKS(20));
  const esp_err_t reset_high =
      write_register(expander, kIoExpanderOutputRegister, kIoExpanderOutputs);
  vTaskDelay(pdMS_TO_TICKS(150));
  std::printf("TINYDRAW_TOUCH_IDENTITY_RESET configure=%s low=%s high=%s\n",
              esp_err_to_name(configure), esp_err_to_name(reset_low), esp_err_to_name(reset_high));

  bool passed = configure == ESP_OK && reset_low == ESP_OK && reset_high == ESP_OK;
  for (const std::uint8_t address : kIdentityRegisters) {
    std::uint8_t value = 0;
    const esp_err_t result = i2c_master_transmit_receive(touch, &address, 1, &value, 1, 100);
    std::printf("TINYDRAW_TOUCH_IDENTITY_REGISTER address=0x%02x value=0x%02x result=%s\n", address,
                value, esp_err_to_name(result));
    passed = passed && result == ESP_OK;
  }

  std::printf("TINYDRAW_TOUCH_IDENTITY_DONE pass=%u\n", static_cast<unsigned>(passed));
  std::fflush(stdout);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace tinydraw::esp32
