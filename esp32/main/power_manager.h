#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"

namespace tinydraw::esp32 {

struct PowerStatus {
  int percentage = -1;
  std::uint16_t battery_mv = 0;
  bool charging = false;
  bool external_power = false;
  bool valid = false;

  bool operator==(const PowerStatus&) const = default;
};

class PowerManager {
 public:
  explicit PowerManager(i2c_master_bus_handle_t bus);
  ~PowerManager();

  PowerManager(const PowerManager&) = delete;
  PowerManager& operator=(const PowerManager&) = delete;

  [[nodiscard]] bool ready() const { return ready_; }
  [[nodiscard]] bool power_button_ready() const { return power_button_ready_; }
  [[nodiscard]] PowerStatus read() const;

 private:
  bool configure_power_button() const;
  bool read_register(std::uint8_t address, std::uint8_t& value) const;
  bool read_registers(std::uint8_t address, std::uint8_t* values, std::size_t count) const;
  bool write_register(std::uint8_t address, std::uint8_t value) const;

  i2c_master_dev_handle_t device_ = nullptr;
  bool ready_ = false;
  bool power_button_ready_ = false;
};

}  // namespace tinydraw::esp32
