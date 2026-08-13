#ifndef TINYDRAW_PRODUCTION_OPERATION_BUILDER_H
#define TINYDRAW_PRODUCTION_OPERATION_BUILDER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/operation.h"

namespace tinydraw::production {

struct OperationPoint {
  float world_x = 0.0F;
  float world_y = 0.0F;
  float radius = 0.0F;
  std::uint32_t timestamp_us = 0;
};

// Fixed-capacity collector for one input operation. It quantizes world-space
// points into the persistent append encoding and owns stroke lifecycle. Input
// timestamps may wrap normally but may not move backward. The returned append
// view remains valid until begin or cancel is called again.
class OperationBuilder {
 public:
  explicit OperationBuilder(std::span<CompactOperationSample> storage);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool active() const;
  // True when an input point exceeded fixed capacity. finish may still return
  // the already-collected operation when only its final lift point overflowed.
  [[nodiscard]] bool overflowed() const;
  [[nodiscard]] std::size_t sample_count() const;

  [[nodiscard]] bool begin(OperationTool tool, std::uint16_t color, OperationPoint point);
  [[nodiscard]] bool add(OperationPoint point);
  [[nodiscard]] std::optional<OperationAppend> finish(OperationPoint point);
  void cancel();

 private:
  [[nodiscard]] bool append_point(OperationPoint point, bool retain_duplicate);

  std::span<CompactOperationSample> storage_;
  OperationTool tool_ = OperationTool::kPen;
  std::uint16_t color_ = 0;
  std::uint32_t started_us_ = 0;
  std::uint32_t previous_us_ = 0;
  std::size_t sample_count_ = 0;
  bool active_ = false;
  bool overflowed_ = false;
};

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_OPERATION_BUILDER_H
