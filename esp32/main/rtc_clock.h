#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "pcf85063a.h"
#include "tinydraw/export/fat16_disk.h"

namespace tinydraw::esp32 {

// The board RTC stores UTC calendar fields and keeps ticking while battery power
// is available. An invalid clock is seeded from the firmware build timestamp.
class RtcClock {
 public:
  explicit RtcClock(i2c_master_bus_handle_t bus);
  ~RtcClock();

  RtcClock(const RtcClock&) = delete;
  RtcClock& operator=(const RtcClock&) = delete;

  [[nodiscard]] bool ready() const { return ready_; }
  [[nodiscard]] bool read(FatDateTime& time);
  [[nodiscard]] bool set(const FatDateTime& time);

 private:
  pcf85063a_dev_t device_{};
  bool ready_ = false;
};

}  // namespace tinydraw::esp32
