#ifndef TINYDRAW_VECTOR_V2_SETTLED_TILE_H
#define TINYDRAW_VECTOR_V2_SETTLED_TILE_H

#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

// Settled analytic-coverage anti-aliasing for one tile (ship contract §4;
// prototype receipts in benchmark-results/settled-aa-prototype/). Replays
// the operations intersecting the tile newest-first with tapered-capsule
// coverage: within one operation self-overlap UNIONs (never darkens),
// across operations coverage composites front-to-back, erasers composite
// opaque white. The frozen RGB565 blend model: 565 expands by bit
// replication, integer accumulation (sum of contributions is bounded by
// 255 so 16-bit channel accumulators are exact), one final round over a
// white background.
//
// The live path stays hard-edged; callers publish the result at
// MaterializationQuality::kSettled under the current revision identity so
// the revisit ledger treats it as cached content.
struct SettledTileWorkspace {
  std::span<std::uint8_t> operation_alpha;    // kTilePixels
  std::span<std::uint8_t> accumulated_alpha;  // kTilePixels
  std::span<std::uint16_t> red;               // kTilePixels
  std::span<std::uint16_t> green;             // kTilePixels
  std::span<std::uint16_t> blue;              // kTilePixels
  // Optional F11 newest-first candidate output. Short/absent storage selects
  // the complete authority scan with identical pixels.
  std::span<std::uint16_t> candidate_indices{};
};

struct SettledTileStats {
  std::size_t operations_scanned = 0;
  std::size_t operations_in_authority = 0;
  std::size_t index_candidates = 0;
  std::size_t deduplicated_candidates = 0;
  std::size_t operations_intersecting = 0;
  std::size_t operations_rendered = 0;
  bool saturated_early = false;
};

[[nodiscard]] bool render_settled_tile(const OperationLog& log, TileKey key,
                                       const SettledTileWorkspace& workspace,
                                       std::span<std::uint16_t> out_pixels,
                                       SettledTileStats* stats = nullptr);

// Window variant for the 25% presentation settle: renders any level-space
// window up to kTileWidth x kTileHeight at the given zoom. At 25% the
// authoritative overview must stay hard-edged (incremental replay depends
// on its exactness), so callers settle into presentation pixels only.
[[nodiscard]] bool render_settled_window(const OperationLog& log, ZoomLevel zoom,
                                         PixelRect window_bounds,
                                         const SettledTileWorkspace& workspace,
                                         std::span<std::uint16_t> out_pixels,
                                         SettledTileStats* stats = nullptr);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_SETTLED_TILE_H
