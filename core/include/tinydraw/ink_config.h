#pragma once

namespace tinydraw {

struct InkConfig {
  float size = 6.0F;
  float thinning = 0.55F;
  float smoothing = 0.55F;
  float streamline = 0.35F;
  float pressure_rate = 0.275F;
  float nominal_dt_ms = 8.0F;
  bool simulate_pressure = true;
  bool antialias = true;
  bool end_taper = false;
};

}  // namespace tinydraw
