#ifndef TINYDRAW_PRODUCTION_OPERATION_H
#define TINYDRAW_PRODUCTION_OPERATION_H

#include <cstdint>

namespace tinydraw::production {

// Append-time sample encoding. Coordinates are quarter world units; radius is
// 1/256 world units; elapsed time is relative to the operation start.
struct CompactOperationSample {
  std::uint16_t x_quarter = 0;
  std::uint16_t y_quarter = 0;
  std::uint16_t radius_256 = 0;
  std::uint16_t elapsed_ms = 0;
};

static_assert(sizeof(CompactOperationSample) == 8);

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_OPERATION_H
