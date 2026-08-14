#pragma once

#include "board_profile.h"
#include "driver/i2c_master.h"

namespace tinydraw::esp32 {

// Owns the board's shared I2C bus, display/touch power reset, and immutable
// hardware-revision resolution. Display, touch, power, and RTC adapters must
// not outlive this object.
class BoardHardware {
 public:
  explicit BoardHardware(BoardSelection selection = BoardSelection::kAuto);
  ~BoardHardware();

  BoardHardware(const BoardHardware&) = delete;
  BoardHardware& operator=(const BoardHardware&) = delete;
  BoardHardware(BoardHardware&&) = delete;
  BoardHardware& operator=(BoardHardware&&) = delete;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] i2c_master_bus_handle_t bus() const;
  [[nodiscard]] const BoardProfileResolution& profile() const;

 private:
  i2c_master_bus_handle_t bus_ = nullptr;
  BoardProfileResolution profile_{};
};

}  // namespace tinydraw::esp32
