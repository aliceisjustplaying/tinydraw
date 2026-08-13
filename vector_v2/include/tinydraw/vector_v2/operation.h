#ifndef TINYDRAW_VECTOR_V2_OPERATION_H
#define TINYDRAW_VECTOR_V2_OPERATION_H

#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

enum class OperationTool : std::uint8_t {
  kPen,
  kEraser,
};

// Append-time sample encoding. Coordinates are quarter world units; centers
// may lie on the clipped right/bottom world edge. Radius is 1/256 world units
// across the full uint16 range; elapsed time is relative to operation start.
struct CompactOperationSample {
  std::uint16_t x_quarter = 0;
  std::uint16_t y_quarter = 0;
  std::uint16_t radius_256 = 0;
  std::uint16_t elapsed_ms = 0;
  bool operator==(const CompactOperationSample&) const = default;
};

struct OperationRecord {
  std::uint32_t first_sample = 0;
  std::uint16_t sample_count = 0;
  std::uint16_t color = 0;
  std::uint16_t bounds_x0 = 0;
  std::uint16_t bounds_y0 = 0;
  std::uint16_t bounds_x1 = 0;
  std::uint16_t bounds_y1 = 0;
  OperationTool tool = OperationTool::kPen;
  std::uint8_t flags = 0;
  std::uint16_t reserved = 0;
};

struct OperationIdentity {
  DocumentRevision revision{};
  std::uint32_t operation_index = 0;
  bool operator==(const OperationIdentity&) const = default;
};

struct OperationAppend {
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  std::span<const CompactOperationSample> samples{};
};

static_assert(sizeof(CompactOperationSample) == 8);
static_assert(sizeof(OperationRecord) == 20);

// Conservative world-pixel bounds including sample radii, clipped to the
// bounded world. Empty or malformed sample sequences return nullopt.
[[nodiscard]] std::optional<PixelRect> operation_world_bounds(
    std::span<const CompactOperationSample> samples);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_OPERATION_H
