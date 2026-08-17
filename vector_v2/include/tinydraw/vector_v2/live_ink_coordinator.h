#ifndef TINYDRAW_VECTOR_V2_LIVE_INK_COORDINATOR_H
#define TINYDRAW_VECTOR_V2_LIVE_INK_COORDINATOR_H

#include <cstdint>
#include <optional>
#include <utility>

#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"

namespace tinydraw::vector_v2 {

struct LiveInkMoveResult {
  ChainedOperationStatus status = ChainedOperationStatus::kRejected;
  bool visual_passed = false;
  bool commit_failed = false;
};

// The visual adapter always receives the newest provisional tail before the
// authority adapter can commit a ready chunk. Presentation failure is reported
// without discarding accepted authority.
template <typename Present, typename Commit>
LiveInkMoveResult process_live_ink_move(CurvedRibbonStream& ribbon,
                                        ChainedOperationBuilder& builder, InkPoint ink_point,
                                        OperationPoint authority_point, Point visual_endpoint,
                                        std::uint32_t event_us, Present&& present,
                                        Commit&& commit) {
  const RibbonUpdate update = ribbon.append(ink_point, true, visual_endpoint);
  const bool visual_passed = std::forward<Present>(present)(update, event_us);
  ChainedOperationStatus status = builder.add(authority_point);
  bool commit_failed = false;
  if (status == ChainedOperationStatus::kChunkReady) {
    const std::optional<ChainedOperationStatus> continued = std::forward<Commit>(commit)();
    commit_failed = !continued.has_value();
    status = continued.value_or(ChainedOperationStatus::kRejected);
  }
  return {.status = status, .visual_passed = visual_passed, .commit_failed = commit_failed};
}

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_LIVE_INK_COORDINATOR_H
