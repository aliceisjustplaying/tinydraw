#pragma once

#include <atomic>
#include <cstdint>
#include <span>

#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

struct Phase2TouchPollStats {
  std::uint32_t polls = 0;
  std::uint32_t average_interval_us = 0;
  std::uint32_t maximum_interval_us = 0;
};

// Measures the real hardware touch loop while the prototype renderer occupies
// both cores. The touch task is the sole writer; the benchmark task controls
// measurement windows and reads snapshots after stopping them.
class Phase2TouchProbe {
 public:
  void begin(std::uint32_t now_us);
  void record(std::uint32_t now_us);
  [[nodiscard]] Phase2TouchPollStats finish();

 private:
  std::atomic<bool> active_{false};
  std::atomic<std::uint32_t> last_us_{0};
  std::atomic<std::uint32_t> polls_{0};
  std::atomic<std::uint32_t> total_interval_us_{0};
  std::atomic<std::uint32_t> maximum_interval_us_{0};
};

void run_phase2_prototype(std::span<std::uint16_t> cache, std::span<std::uint16_t> reference,
                          DisplayBackend& display, Phase2TouchProbe& touch_probe);

}  // namespace tinydraw::esp32
