#ifndef TINYDRAW_PRODUCTION_INCREMENTAL_RASTERIZER_H
#define TINYDRAW_PRODUCTION_INCREMENTAL_RASTERIZER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation.h"

namespace tinydraw::production {

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

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_INCREMENTAL_RASTERIZER_H
