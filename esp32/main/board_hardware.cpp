#include "board_hardware.h"

#include <array>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kIoExpanderAddress = 0x20;
constexpr std::uint8_t kIoExpanderOutputRegister = 0x01;
constexpr std::uint8_t kIoExpanderConfigRegister = 0x03;
constexpr std::uint8_t kIoExpanderLcdReset = 1U << 0U;
constexpr std::uint8_t kIoExpanderDisplayPower = 1U << 1U;
constexpr std::uint8_t kIoExpanderTouchReset = 1U << 2U;
constexpr std::uint8_t kIoExpanderSdChipSelect = 1U << 7U;
constexpr std::uint8_t kIoExpanderOutputs =
    kIoExpanderLcdReset | kIoExpanderDisplayPower | kIoExpanderTouchReset | kIoExpanderSdChipSelect;
constexpr std::uint8_t kCst820Address = 0x15;
constexpr std::uint8_t kFt3168Address = 0x38;

bool reset_panel_and_touch(i2c_master_bus_handle_t bus) {
  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kIoExpanderAddress;
  device_config.scl_speed_hz = 400000;
  i2c_master_dev_handle_t device = nullptr;
  if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK) {
    return false;
  }
  const auto write = [&](std::uint8_t address, std::uint8_t value) {
    const std::array payload{address, value};
    return i2c_master_transmit(device, payload.data(), payload.size(), 100) == ESP_OK;
  };
  const bool configured =
      write(kIoExpanderConfigRegister, static_cast<std::uint8_t>(~kIoExpanderOutputs));
  const bool powered_down = configured && write(kIoExpanderOutputRegister, kIoExpanderSdChipSelect);
  vTaskDelay(pdMS_TO_TICKS(20));
  const bool powered_up = powered_down && write(kIoExpanderOutputRegister, kIoExpanderOutputs);
  vTaskDelay(pdMS_TO_TICKS(150));
  const bool removed = i2c_master_bus_rm_device(device) == ESP_OK;
  return powered_up && removed;
}

}  // namespace

BoardHardware::BoardHardware(BoardSelection selection) {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = GPIO_NUM_15;
  bus_config.scl_io_num = GPIO_NUM_14;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK) {
    std::printf("TINYDRAW_BOARD_FAIL reason=i2c_bus\n");
    return;
  }
  if (!reset_panel_and_touch(bus_)) {
    std::printf("TINYDRAW_BOARD_FAIL reason=power_reset\n");
    return;
  }

  const BoardProbeEvidence evidence{
      .cst820_at_0x15 = i2c_master_probe(bus_, kCst820Address, 100) == ESP_OK,
      .ft3168_at_0x38 = i2c_master_probe(bus_, kFt3168Address, 100) == ESP_OK,
  };
  profile_ = resolve_board_profile(selection, evidence);
  if (!profile_.ready()) {
    std::printf("TINYDRAW_BOARD_FAIL reason=%s touch_0x15=%u touch_0x38=%u\n",
                profile_.error == BoardProfileError::kAmbiguous ? "ambiguous" : "no_match",
                evidence.cst820_at_0x15, evidence.ft3168_at_0x38);
    return;
  }

  const bool probe_matches = (profile_.revision == BoardRevision::kV1 && evidence.ft3168_at_0x38 &&
                              !evidence.cst820_at_0x15) ||
                             (profile_.revision == BoardRevision::kV2 && evidence.cst820_at_0x15 &&
                              !evidence.ft3168_at_0x38);
  std::printf(
      "TINYDRAW_BOARD_READY revision=%s selected_by_override=%u probe_match=%u "
      "touch_0x15=%u touch_0x38=%u\n",
      board_revision_name(profile_.revision), profile_.selected_by_override, probe_matches,
      evidence.cst820_at_0x15, evidence.ft3168_at_0x38);
}

BoardHardware::~BoardHardware() {
  if (bus_ != nullptr) {
    static_cast<void>(i2c_del_master_bus(bus_));
  }
}

bool BoardHardware::ready() const { return bus_ != nullptr && profile_.ready(); }
i2c_master_bus_handle_t BoardHardware::bus() const { return bus_; }
const BoardProfileResolution& BoardHardware::profile() const { return profile_; }

}  // namespace tinydraw::esp32
