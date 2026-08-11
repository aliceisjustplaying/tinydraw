#pragma once

#include "rtc_clock.h"

namespace tinydraw::esp32 {

// Starts one low-priority synchronization attempt. The task tears Wi-Fi down
// completely after NTP succeeds or times out.
[[nodiscard]] bool start_time_sync(RtcClock& clock);

}  // namespace tinydraw::esp32
