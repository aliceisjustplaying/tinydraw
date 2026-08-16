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
[[nodiscard]] PixelRect operation_level_bounds(PixelRect world_bounds, ZoomLevel zoom);

// Applies one operation to an existing RGB565 materialization. Samples are in
// quarter-world-unit coordinates with radius_256 in 1/256 world units.
// Erasers paint opaque white, preserving the current production semantics.
[[nodiscard]] bool apply_incremental_operation(const OperationAppend& operation,
                                               const RasterSurface& surface);

// Each source segment is one bounded raster unit. Surface clipping limits one
// unit to the caller's work surface, while the cold producer resumes between
// source segments without changing pixels or painter order. first_step is
// zero-based and step_count must remain within the segment.
struct IncrementalSegment {
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  CompactOperationSample first{};
  CompactOperationSample second{};
};

// Conservative level-space bounds for one segment, including projected radii.
// This lets callers reject irrelevant segments before subdivision or raster work.
[[nodiscard]] PixelRect incremental_segment_level_bounds(CompactOperationSample first,
                                                         CompactOperationSample second,
                                                         ZoomLevel zoom);
[[nodiscard]] std::size_t incremental_segment_step_count(CompactOperationSample first,
                                                         CompactOperationSample second,
                                                         ZoomLevel zoom);
[[nodiscard]] std::size_t incremental_segment_step_work(CompactOperationSample first,
                                                        CompactOperationSample second,
                                                        ZoomLevel zoom, PixelRect clip,
                                                        std::size_t step);
[[nodiscard]] bool apply_incremental_segment_steps(const IncrementalSegment& segment,
                                                   const RasterSurface& surface,
                                                   std::size_t first_step, std::size_t step_count);
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

// Exact newest-first painter seam. A set bit means the corresponding surface
// pixel already has its final color and must not be touched by older segments.
// Covered pixels are written and finalized atomically from the caller's point
// of view; uncovered baseline pixels remain unmarked. When a summary is
// supplied it must have been reset for this surface and fed every prior
// masked paint on it; the painter keeps it exact and uses it to skip
// saturated rows.
[[nodiscard]] bool apply_masked_incremental_segment(const IncrementalSegment& segment,
                                                    const RasterSurface& surface,
                                                    std::span<std::uint8_t> finalized_pixels,
                                                    MaskedRowSummary* summary = nullptr);

// Paints the live ink midpoint-curve centerline while respecting a newest-first
// finalized mask. All segments in one operation share a color, so their union
// may be processed forward without changing painter-order semantics.
[[nodiscard]] bool apply_masked_incremental_operation(const OperationAppend& operation,
                                                      const RasterSurface& surface,
                                                      std::span<std::uint8_t> finalized_pixels,
                                                      MaskedRowSummary* summary = nullptr);

// Curved operations with three or more samples replay as endpoint-indexed
// units [2, sample_count). One- and two-sample operations use their final
// sample index as a single unit. This keeps cold replay resumable without
// changing the geometry used by forward authority.
[[nodiscard]] std::size_t incremental_curve_unit_step_count(
    std::span<const CompactOperationSample> samples, std::size_t endpoint, ZoomLevel zoom);
[[nodiscard]] std::optional<PixelRect> incremental_curve_step_level_bounds(
    std::span<const CompactOperationSample> samples, std::size_t endpoint, std::size_t step_index,
    ZoomLevel zoom);
[[nodiscard]] bool apply_masked_incremental_curve_step(const OperationAppend& operation,
                                                       std::size_t endpoint, std::size_t step_index,
                                                       const RasterSurface& surface,
                                                       std::span<std::uint8_t> finalized_pixels,
                                                       MaskedRowSummary* summary = nullptr);

// One endpoint's curve unit prepared once and replayed step by step. The
// prepared chords carry exactly the level-space floats the per-call unit
// computation produces, so step bounds and painted pixels are bit-identical
// to the unprepared entry points while the subdivision, scaling, and
// reciprocal-length work is paid once per endpoint instead of once per
// step-count, step-bounds, and step-apply call.
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
[[nodiscard]] std::optional<PixelRect> prepared_curve_step_level_bounds(
    const PreparedCurveUnit& unit, std::size_t step_index, ZoomLevel zoom);
[[nodiscard]] bool apply_masked_prepared_curve_step(OperationTool tool, std::uint16_t color,
                                                    const PreparedCurveUnit& unit,
                                                    std::size_t step_index,
                                                    const RasterSurface& surface,
                                                    std::span<std::uint8_t> finalized_pixels,
                                                    MaskedRowSummary* summary = nullptr);

// Paints every chord of one prepared unit in a single row sweep over their
// union bounds, sharing one unfinalized-window scan per row. All chords of a
// unit carry one color, so with the finalized mask the written pixel set is
// identical to sequential per-chord painting in any order.
[[nodiscard]] bool apply_masked_prepared_curve_unit(OperationTool tool, std::uint16_t color,
                                                    const PreparedCurveUnit& unit,
                                                    const RasterSurface& surface,
                                                    std::span<std::uint8_t> finalized_pixels,
                                                    MaskedRowSummary* summary = nullptr);

// Operation-level chord table (H7). One group visit prepares a batch of an
// operation's chords — every chord of every endpoint unit, newest first —
// into caller-funded storage, then paints them in a single y-sorted row
// sweep sharing one unfinalized-window scan per row across the whole batch.
// All chords of one operation carry one color, so under the finalized mask
// the written pixel set is identical to sequential per-unit painting in any
// order and to any endpoint grouping (batches stay exact). Endpoint units
// are atomic within a batch: a unit's chords never split across batches.
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
  // First unswept row; the batch is complete when this reaches
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

// Sweeps rows of a prepared batch, resuming at first_row and stopping at a
// row boundary once accumulated work reaches max_work_px (at least one row
// always completes). Per-pixel decisions are identical to the per-unit
// painters: covers_pixel stays the sole geometry authority and the
// finalized mask keeps every pixel single-writer.
[[nodiscard]] bool apply_masked_operation_chord_rows(
    OperationTool tool, std::uint16_t color, std::span<const std::byte> chord_storage,
    const OperationChordBatch& batch, int first_row, std::size_t max_work_px,
    const RasterSurface& surface, std::span<std::uint8_t> finalized_pixels,
    MaskedRowSummary* summary, OperationSweepSlice& slice);

struct AffectedTileResult {
  std::size_t required = 0;
  std::size_t written = 0;
  [[nodiscard]] bool complete() const { return required == written; }
};

// Enumerates world-aligned tiles touched at zoom. Returns nullopt for an empty
// operation or the overview level. If output is short, its prefix is filled and
// required reports the capacity needed for a complete list.
[[nodiscard]] std::optional<AffectedTileResult> affected_tiles(const OperationAppend& operation,
                                                               ZoomLevel zoom,
                                                               std::span<TileKey> output);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_H
