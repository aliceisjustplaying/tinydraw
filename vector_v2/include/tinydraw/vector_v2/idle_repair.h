#ifndef TINYDRAW_VECTOR_V2_IDLE_REPAIR_H
#define TINYDRAW_VECTOR_V2_IDLE_REPAIR_H

#include <array>
#include <cstddef>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

// Plans idle cache repair. When input is quiet and the visible fill is
// complete, the producer walks these views so pans and zoom returns meet
// materialized tiles instead of cold fallback. Drawing drops affected tiles
// at non-active zooms by design (the in-place commit budget); idle repair is
// the other half of that bargain.
//
// The plan holds, in priority order:
// 1. The active view's cardinal neighbors at the active zoom, one full
//    viewport step away, clamped to the level and deduplicated. Panning in
//    any direction after a quiet moment meets materialized tiles first.
// 2. The remembered view at every other tiled zoom, so zoom returns land
//    sharp.
// 3. At 100% only: the full-level viewport grid. The whole 100% world's raw
//    tiles fit the slot pool in practice, so edge panning at 100% never cold
//    renders once a quiet moment has passed. Larger levels stay
//    neighborhood-only; a full sweep there would just churn the pool.
struct IdleRepairPlan {
  std::array<ViewRequest, 24> views{};
  std::size_t count = 0;
  // Views from this index on belong to the optional full-level sweep. The
  // runner must stop the sweep once the slot pool saturates: a document can
  // exceed pool capacity at 100% (dense hairlines defeat uniform coverage),
  // and sweeping past saturation evicts the warm neighborhood for tiles the
  // user is not near.
  std::size_t grid_start = 0;
};

[[nodiscard]] IdleRepairPlan plan_idle_repair(const ViewRequest& active_view,
                                              std::span<const ViewFootprint> remembered);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_IDLE_REPAIR_H
