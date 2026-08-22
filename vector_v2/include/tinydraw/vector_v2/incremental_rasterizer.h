#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation.h"

namespace tinydraw::vector_v2 {

struct RasterSurface {
  ZoomLevel zoom = ZoomLevel::k25Percent;
  PixelRect level_bounds{};
  std::span<std::uint16_t> pixels{};
  int stride = 0;
};

// Converts conservative world bounds to the corresponding level-space bounds.
// This is shared by raster clipping and cold-producer culling.
// world_bounds must be normalized and contained by the nonnegative world
// extent. Committed OperationLog records satisfy this contract.
[[nodiscard]] PixelRect operation_level_bounds(PixelRect world_bounds, ZoomLevel zoom);

// Applies one operation to an existing RGB565 materialization. Samples are in
// quarter-world-unit coordinates with radius_256 in 1/256 world units.
// Erasers paint opaque white, preserving the current production semantics.
[[nodiscard]] bool apply_incremental_operation(const OperationAppend& operation,
                                               const RasterSurface& surface);

// Exact per-row saturation summary for a masked raster surface. A row is
// saturated when every pixel in the surface's clipped width is finalized;
// saturated row ranges answer in O(words), letting callers skip rows, whole
// segments, whole operations, and eventually the whole surface without
// touching mask bytes. The summary never approximates: a skipped unit could
// only have written pixels whose finalized bits are already set, so results
// remain bit-identical to unsummarized masked replay. Storage is caller-owned
// and must cover the surface height.
class MaskedRowSummary {
 public:
  MaskedRowSummary() = default;
  MaskedRowSummary(std::span<std::uint16_t> unset_counts, std::span<std::uint32_t> saturated_words);

  [[nodiscard]] bool ready(std::size_t rows) const;
  // Rearms the summary for a surface of rows x width unfinalized pixels.
  void reset(int rows, int width);
  [[nodiscard]] bool row_saturated(int row) const;
  // Inclusive row range; rows outside [0, rows) are rejected as unsaturated.
  [[nodiscard]] bool rows_saturated(int first_row, int last_row) const;
  [[nodiscard]] bool all_saturated() const;
  // Records newly_finalized first-time mask bits on one row.
  void note_finalized(int row, int newly_finalized);

 private:
  std::span<std::uint16_t> unset_counts_{};
  std::span<std::uint32_t> saturated_words_{};
  int rows_ = 0;
  int unsaturated_rows_ = 0;
};

// Paints the live ink midpoint-curve centerline while respecting a newest-first
// finalized mask. All segments in one operation share a color, so their union
// may be processed forward without changing painter-order semantics.
[[nodiscard]] bool apply_masked_incremental_operation(const OperationAppend& operation,
                                                      const RasterSurface& surface,
                                                      std::span<std::uint8_t> finalized_pixels,
                                                      MaskedRowSummary* summary = nullptr);

// One endpoint's authoritative curve chords for settled rendering and
// characterization tools. The prepared chords carry exactly the level-space
// floats used by immediate raster authority, with subdivision, scaling, and
// reciprocal-length work paid once per endpoint.
struct PreparedCurveStep {
  float first_x = 0;
  float first_y = 0;
  float first_radius = 0;
  float second_x = 0;
  float second_y = 0;
  float second_radius = 0;
  float delta_x = 0;
  float delta_y = 0;
  float inverse_length_squared = 0;
};

struct PreparedCurveUnit {
  std::size_t step_count = 0;
  std::array<PreparedCurveStep, 3> steps{};
};

[[nodiscard]] std::optional<PreparedCurveUnit> prepare_incremental_curve_unit(
    std::span<const CompactOperationSample> samples, std::size_t endpoint, ZoomLevel zoom);

// Operation-level chord table (H7). One group visit prepares a batch of an
// operation's chords — every chord of every endpoint unit, newest first —
// into caller-funded storage, then paints them in a single y-sorted row
// sweep sharing one unfinalized-window scan per row across the whole batch.
// All chords of one operation carry one color, so under the finalized mask
// the written pixel set is identical to whole-operation painting and to any
// endpoint grouping (batches stay exact). Endpoint units are atomic within a
// batch: a unit's chords never split across batches.
//
// Chord-plan storage is opaque caller-funded bytes; the implementation
// static_asserts its layout fits these bounds.
inline constexpr std::size_t kPreparedOperationChordBytes = 128;
inline constexpr std::size_t kPreparedOperationChordAlign = 4;
inline constexpr std::size_t kOperationChordCapacity = 96;
// Plans plus a one-byte-per-chord y-sorted order table: sorting indices
// instead of 128-byte plan structs keeps preparation cheap for the many
// small-footprint operations at 50% zoom.
inline constexpr std::size_t kOperationChordStorageBytes =
    kOperationChordCapacity * kPreparedOperationChordBytes + kOperationChordCapacity;

struct OperationChordBatch {
  std::size_t chord_count = 0;
  // Next endpoint to prepare after this batch (endpoints descend, newest
  // first). Zero when the operation is exhausted.
  std::size_t next_endpoint = 0;
  // Union of the batch chords' surface-clipped bounds. Empty (x1 <= x0)
  // when every chord clipped away.
  PixelRect clipped_bounds{};
  // Sum of the chords' clipped bounding-box areas (legacy work accounting).
  std::size_t raster_work = 0;
};

// Prepares chords for endpoints [first_endpoint .. 2] (descending) of one
// operation, stopping early when chord_storage cannot hold another whole
// unit. first_endpoint follows the producer's resume cursor semantics: for
// operations with at most two samples it is the final sample index. Chord
// geometry is bit-identical to prepare_incremental_curve_unit per endpoint;
// chords fully outside surface_bounds are dropped. The prepared batch is
// sorted by top row for the sweep.
[[nodiscard]] std::optional<OperationChordBatch> prepare_operation_chord_batch(
    std::span<const CompactOperationSample> samples, std::size_t first_endpoint, ZoomLevel zoom,
    PixelRect surface_bounds, std::span<std::byte> chord_storage);

struct OperationSweepSlice {
  // First unfinished row; the batch is complete when this reaches
  // clipped_bounds.y1.
  int next_row = 0;
  // Rows actually swept (rows with no live chord are jumped, not counted).
  int rows_swept = 0;
  // Accumulated work: the window-clipped span pixels visited per active
  // chord per row, plus a small flat per-row scan charge. This is the
  // honest slice cost — a fat batch with every chord active on a row pays
  // per chord, a hairline batch pays a few pixels per row.
  std::size_t work_px = 0;
};

// Persistent position inside an unmasked chord batch. next_chord indexes the
// deterministic active-chord list for next_row, allowing dense rows to yield
// without replaying chords already painted into the destination.
struct OperationSweepCursor {
  int next_row = 0;
  std::size_t next_chord = 0;
};

struct MaskedOperationChordRowsRequest {
  OperationTool tool;
  std::uint16_t color;
  std::span<const std::byte> chord_storage;
  const OperationChordBatch& batch;
  int first_row;
  std::size_t max_work_px;
  const RasterSurface& surface;
  std::span<std::uint8_t> finalized_pixels;
  MaskedRowSummary* summary;
  OperationSweepSlice& slice;
};

struct OperationChordSliceRequest {
  OperationTool tool;
  std::uint16_t color;
  std::span<const std::byte> chord_storage;
  const OperationChordBatch& batch;
  std::size_t max_work_px;
  const RasterSurface& surface;
  OperationSweepCursor& cursor;
  OperationSweepSlice& slice;
};

// Sweeps rows of a prepared batch, resuming at first_row and stopping at a
// row boundary once accumulated work reaches max_work_px (at least one row
// always completes). Per-pixel decisions are identical to the whole-operation
// painter: covers_pixel stays the sole geometry authority and the finalized
// mask keeps every pixel single-writer.
[[nodiscard]] bool apply_masked_operation_chord_rows(
    const MaskedOperationChordRowsRequest& request);

// Intra-row resumable unmasked replay. Stops at a chord boundary once the
// work charge reaches max_work_px; one chord is the smallest atomic unit.
// Initialize cursor to {batch.clipped_bounds.y0, 0}. Completion is
// cursor.next_row == batch.clipped_bounds.y1 with next_chord == 0.
[[nodiscard]] bool apply_operation_chord_slice(const OperationChordSliceRequest& request);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_H
