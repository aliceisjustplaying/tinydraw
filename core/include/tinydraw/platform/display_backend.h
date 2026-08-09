#pragma once

#include <cstdint>

namespace tinydraw {

class DisplayBackend {
 public:
  virtual ~DisplayBackend() = default;

  // Completes or copies the transfer before returning. The backend must not
  // retain rgb565; it owns any DMA staging required by its hardware.
  virtual void push_rect(int x, int y, int width, int height, const std::uint16_t* rgb565) = 0;
};

}  // namespace tinydraw
