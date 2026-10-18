#pragma once

#include <cstddef>

#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw::vector_v2 {

// Streams the exact midpoint-chord geometry consumed by the incremental
// authority rasterizer. Stable geometry lags one point; the provisional chord
// reaches the latest accepted point and is replaced by the next update.
class AuthorityRibbonStream {
 public:
  [[nodiscard]] RibbonUpdate append(InkPoint point, bool provisional_needed = true);
  [[nodiscard]] RibbonUpdate finish(InkPoint point);
  void reset();

  [[nodiscard]] bool active() const { return point_count_ != 0U; }

 private:
  [[nodiscard]] RibbonPrimitive provisional_segment() const;

  InkPoint first_{};
  InkPoint stable_{};
  InkPoint last_{};
  std::size_t point_count_ = 0U;
};

}  // namespace tinydraw::vector_v2
