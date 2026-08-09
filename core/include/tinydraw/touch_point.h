#pragma once

#include <cstdint>

namespace tinydraw {

struct TouchPoint {
  float x;
  float y;
  std::uint32_t timestamp_us;
};

}  // namespace tinydraw
