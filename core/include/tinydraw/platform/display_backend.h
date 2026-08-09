#pragma once

#include <cstdint>

namespace tinydraw {

class DisplayBackend {
 public:
  virtual ~DisplayBackend() = default;
  virtual void push_rect(int x, int y, int width, int height, const std::uint16_t* rgb565) = 0;
  [[nodiscard]] virtual bool busy() const = 0;
};

}  // namespace tinydraw
