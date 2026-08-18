#ifndef TINYDRAW_VECTOR_V2_SETTLED_TILE_H
#define TINYDRAW_VECTOR_V2_SETTLED_TILE_H

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
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
  std::size_t candidate_queries = 0;
  std::size_t initialize_pixels = 0;
  std::size_t operation_clear_pixels = 0;
  std::size_t curve_units_prepared = 0;
  std::size_t raster_pixels = 0;
  std::size_t composite_pixels = 0;
  std::size_t fold_pixels = 0;
  bool saturated_early = false;
};

enum class SettledRenderStatus : std::uint8_t {
  kInProgress,
  kComplete,
  kError,
};

struct SettledRenderSlice {
  SettledRenderStatus status = SettledRenderStatus::kError;
  // Scheduling charge consumed by this call. Raster rows are atomic, so a
  // call may exceed max_work_px by at most one tile row.
  std::size_t work_px = 0;
  // Complete with zero intersecting ink: out_pixels are exact paper white
  // and the caller may skip publication and presentation entirely.
  bool no_ink = false;
};

class SettledRenderCursor;

struct SettledRenderRequest {
  const OperationLog& log;
  ZoomLevel zoom;
  PixelRect window_bounds;
  const SettledTileWorkspace& workspace;
  std::span<std::uint16_t> out_pixels;
  SettledRenderCursor& cursor;
  std::size_t max_work_px;
};

// Caller-owned continuation for settled rendering. It fingerprints the
// authority, request, buffers, and workspace at the first slice; any change
// while active fails closed. No heap storage is owned by the cursor.
class SettledRenderCursor {
 public:
  SettledRenderCursor() = default;

  void cancel();
  [[nodiscard]] bool active() const;
  [[nodiscard]] const SettledTileStats& stats() const;

 private:
  friend SettledRenderSlice render_settled_window_slice(const SettledRenderRequest&);

  struct WorkBudget;

  enum class Phase : std::uint8_t {
    kIdle,
    kInitialize,
    kQueryCandidates,
    kScanOperation,
    kClearOperation,
    kPrepareEndpoint,
    kRasterChord,
    kCompositeOperation,
    kFinalFold,
  };

  [[nodiscard]] bool bind(const SettledRenderRequest& request);
  [[nodiscard]] SettledRenderSlice advance(WorkBudget& budget);
  void advance_initialize(WorkBudget& budget);
  void advance_candidate_query(WorkBudget& budget);
  void advance_operation_scan(WorkBudget& budget);
  void advance_operation_clear(WorkBudget& budget);
  void advance_endpoint_preparation(WorkBudget& budget);
  void advance_chord_raster(WorkBudget& budget);
  void prepare_chord(const PreparedCurveStep& chord);
  void raster_chord_row();
  void advance_operation_composite(WorkBudget& budget);
  void composite_pixels(std::size_t first_at, std::size_t count, std::uint16_t red,
                        std::uint16_t green, std::uint16_t blue);
  void advance_final_fold(WorkBudget& budget);

  Phase phase_ = Phase::kIdle;
  const OperationLog* log_ = nullptr;
  AuthorityReadView authority_{};
  ZoomLevel zoom_ = ZoomLevel::k25Percent;
  PixelRect window_bounds_{};
  PixelRect world_bounds_{};
  SettledTileWorkspace workspace_{};
  std::span<std::uint16_t> out_pixels_{};
  SettledTileStats stats_{};
  int width_ = 0;
  int height_ = 0;
  std::size_t pixel_count_ = 0;
  std::size_t initialize_at_ = 0;
  std::size_t replay_count_ = 0;
  std::size_t replay_index_ = 0;
  std::size_t operation_index_ = 0;
  std::size_t clear_row_ = 0;
  std::size_t endpoint_ = 0;
  std::size_t step_ = 0;
  std::size_t composite_row_ = 0;
  std::size_t composite_x_ = 0;
  std::size_t fold_at_ = 0;
  std::size_t saturated_pixels_ = 0;
  OperationTool operation_tool_ = OperationTool::kPen;
  std::uint16_t operation_color_ = 0;
  std::span<const CompactOperationSample> operation_samples_{};
  PreparedCurveUnit prepared_unit_{};
  bool use_candidates_ = false;
  bool operation_touched_ = false;
  int chord_x0_ = 0;
  int chord_x1_ = 0;
  int chord_y1_ = 0;
  int chord_next_y_ = 0;
  float chord_ax_ = 0;
  float chord_ay_ = 0;
  float chord_delta_x_ = 0;
  float chord_delta_y_ = 0;
  float chord_inverse_length_squared_ = 0;
  float chord_first_radius_ = 0;
  float chord_radius_delta_ = 0;
  std::array<std::uint8_t, kTileHeight> operation_min_x_{};
  std::array<std::uint8_t, kTileHeight> operation_max_x_{};
  std::uint8_t operation_min_y_ = 0;
  std::uint8_t operation_max_y_ = 0;
};

// Advances one settled window render with a caller-selected work budget.
// Raster rows are indivisible to keep continuation state small; every other
// pixel loop stops at max_work_px. A complete result is bit-identical to
// render_settled_window, including replay stats.
[[nodiscard]] SettledRenderSlice render_settled_window_slice(const SettledRenderRequest& request);

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
