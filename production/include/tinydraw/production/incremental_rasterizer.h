#ifndef TINYDRAW_PRODUCTION_INCREMENTAL_RASTERIZER_H
#define TINYDRAW_PRODUCTION_INCREMENTAL_RASTERIZER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation.h"

namespace tinydraw::production {

enum class OperationTool : std::uint8_t {
  kPen,
  kEraser,
};

struct IncrementalOperation {
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  std::span<const CompactOperationSample> samples{};
};

struct RasterSurface {
  ZoomLevel zoom = ZoomLevel::k25Percent;
  PixelRect level_bounds{};
  std::span<std::uint16_t> pixels{};
  int stride = 0;
};

// Applies one operation to an existing RGB565 materialization. Samples are in
// quarter-world-unit coordinates with radius_256 in 1/256 world units.
// Erasers paint opaque white, preserving the current production semantics.
[[nodiscard]] bool apply_incremental_operation(const IncrementalOperation& operation,
                                               const RasterSurface& surface);

// Returns every world-aligned tile touched at zoom, or nullopt when output is
// too small. No partial key list is reported on failure.
[[nodiscard]] std::optional<std::size_t> affected_tiles(const IncrementalOperation& operation,
                                                        ZoomLevel zoom, std::span<TileKey> output);

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_INCREMENTAL_RASTERIZER_H
