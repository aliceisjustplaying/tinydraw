#include "power_manager.h"

#include <array>

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kAddress = 0x34;
constexpr std::uint8_t kChipIdRegister = 0x03;
constexpr std::uint8_t kChipId = 0x4A;
constexpr std::uint8_t kStatusRegister = 0x00;
constexpr std::uint8_t kAdcControlRegister = 0x30;
constexpr std::uint8_t kBatteryVoltageRegister = 0x34;
constexpr std::uint8_t kBatteryPercentageRegister = 0xA4;
constexpr std::uint8_t kPowerOffControlRegister = 0x22;
constexpr std::uint8_t kPowerKeyTimingRegister = 0x27;
constexpr std::uint8_t kBatteryPresent = 1U << 3U;
constexpr std::uint8_t kVbusGood = 1U << 5U;
constexpr std::uint8_t kBatteryVoltageAdc = 1U << 0U;
constexpr std::uint8_t kChargingDirection = 1U;
constexpr std::uint8_t kEnableLongPressShutdown = 1U << 1U;
constexpr std::uint8_t kRestartAfterLongPress = 1U << 0U;
constexpr std::uint8_t kPowerKeyTimingMask = 0x0FU;
constexpr int kI2cTimeoutMs = 20;

}  // namespace

PowerManager::PowerManager(i2c_master_bus_handle_t bus) {
  if (bus == nullptr) {
    return;
  }

  i2c_device_config_t config{};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = kAddress;
  config.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus, &config, &device_) != ESP_OK) {
    return;
  }

  std::uint8_t chip_id = 0;
  std::uint8_t adc_control = 0;
  if (!read_register(kChipIdRegister, chip_id) || chip_id != kChipId ||
      !read_register(kAdcControlRegister, adc_control) ||
      !write_register(kAdcControlRegister, adc_control | kBatteryVoltageAdc)) {
    static_cast<void>(i2c_master_bus_rm_device(device_));
    device_ = nullptr;
    return;
  }
  ready_ = true;
  power_button_ready_ = configure_power_button();
}

PowerManager::~PowerManager() {
  if (device_ != nullptr) {
    static_cast<void>(i2c_master_bus_rm_device(device_));
  }
}

bool PowerManager::configure_power_button() const {
  std::uint8_t power_off_control = 0;
  std::uint8_t timing = 0;
  if (!read_register(kPowerOffControlRegister, power_off_control) ||
      !read_register(kPowerKeyTimingRegister, timing)) {
    return false;
  }

  power_off_control = static_cast<std::uint8_t>((power_off_control | kEnableLongPressShutdown) &
                                                ~kRestartAfterLongPress);
  timing = static_cast<std::uint8_t>(timing & ~kPowerKeyTimingMask);
  return write_register(kPowerOffControlRegister, power_off_control) &&
         write_register(kPowerKeyTimingRegister, timing);
}

PowerStatus PowerManager::read() const {
  if (!ready_) {
    return {};
  }

  std::array<std::uint8_t, 2> status{};
  if (!read_registers(kStatusRegister, status.data(), status.size())) {
    return {};
  }

  PowerStatus result;
  result.external_power = (status[0] & kVbusGood) != 0U;
  result.charging = (status[1] >> 5U) == kChargingDirection;
  if ((status[0] & kBatteryPresent) == 0U) {
    result.valid = true;
    return result;
  }

  std::uint8_t percentage = 0;
  std::array<std::uint8_t, 2> voltage{};
  if (!read_register(kBatteryPercentageRegister, percentage) || percentage > 100U ||
      !read_registers(kBatteryVoltageRegister, voltage.data(), voltage.size())) {
    return {};
  }
  result.percentage = percentage;
  result.battery_mv = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(voltage[0] & 0x1FU) << 8U) | voltage[1]);
  result.valid = true;
  return result;
}

bool PowerManager::read_register(std::uint8_t address, std::uint8_t& value) const {
  return read_registers(address, &value, 1);
}

bool PowerManager::read_registers(std::uint8_t address, std::uint8_t* values,
                                  std::size_t count) const {
  return device_ != nullptr && values != nullptr && count > 0U &&
         i2c_master_transmit_receive(device_, &address, 1, values, count, kI2cTimeoutMs) == ESP_OK;
}

bool PowerManager::write_register(std::uint8_t address, std::uint8_t value) const {
  const std::array payload{address, value};
  return device_ != nullptr &&
         i2c_master_transmit(device_, payload.data(), payload.size(), kI2cTimeoutMs) == ESP_OK;
}

}  // namespace tinydraw::esp32
