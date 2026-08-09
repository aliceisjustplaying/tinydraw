#pragma once

#include "tinydraw/geometry.h"
#include "tinydraw/ink_config.h"
#include "tinydraw/touch_point.h"

namespace tinydraw {

struct InkPoint {
  Point position;
  float pressure;
  float radius;
  float distance;
  float running_length;
  std::uint32_t timestamp_us;
};

class InkStream {
 public:
  explicit InkStream(InkConfig config = {});

  [[nodiscard]] InkPoint begin(TouchPoint point);
  [[nodiscard]] InkPoint update(TouchPoint point);
  void end();

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] const InkConfig& config() const { return config_; }
  void set_config(InkConfig config) { config_ = config; }

 private:
  [[nodiscard]] float radius_for(float pressure) const;

  InkConfig config_;
  InkPoint previous_{};
  bool active_ = false;
};

}  // namespace tinydraw
