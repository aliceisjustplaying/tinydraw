#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_H

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
// Exact newest-first painter seam. A set bit means the corresponding surface
// pixel already has its final color and must not be touched by older segments.
// Covered pixels are written and finalized atomically from the caller's point
// of view; uncovered baseline pixels remain unmarked.
[[nodiscard]] bool apply_masked_incremental_segment(const IncrementalSegment& segment,
                                                    const RasterSurface& surface,
                                                    std::span<std::uint8_t> finalized_pixels);

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
