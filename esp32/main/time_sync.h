#pragma once

#include <atomic>
#include <cstdint>

#include "rtc_clock.h"

namespace tinydraw::esp32 {

enum class TimeSyncStatus : std::uint8_t {
  kIdle,
  kConnecting,
  kSynchronizing,
  kSucceeded,
  kFailed
};

// Owns one asynchronous Wi-Fi/NTP attempt. A terminal status is published only
// after Wi-Fi has been stopped and deinitialized.
class TimeSyncController {
 public:
  explicit TimeSyncController(RtcClock& clock) : clock_(clock) {}

  TimeSyncController(const TimeSyncController&) = delete;
  TimeSyncController& operator=(const TimeSyncController&) = delete;

  [[nodiscard]] bool available() const;
  [[nodiscard]] bool start();
  void dismiss();
  [[nodiscard]] TimeSyncStatus status() const { return status_.load(); }

 private:
  static void task_entry(void* argument);
  void run();

  RtcClock& clock_;
  std::atomic<TimeSyncStatus> status_{TimeSyncStatus::kIdle};
  std::atomic<bool> running_{false};
};

// Legacy Raster V1 entry point. Starts one low-priority synchronization
// attempt and tears Wi-Fi down completely after NTP succeeds or times out.
[[nodiscard]] bool start_time_sync(RtcClock& clock);

}  // namespace tinydraw::esp32
