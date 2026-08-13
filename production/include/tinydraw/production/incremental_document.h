#ifndef TINYDRAW_PRODUCTION_INCREMENTAL_DOCUMENT_H
#define TINYDRAW_PRODUCTION_INCREMENTAL_DOCUMENT_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/production/incremental_rasterizer.h"
#include "tinydraw/production/operation_log.h"

namespace tinydraw::production {

struct IncrementalDocumentWorkspace {
  std::span<std::uint16_t> next_overview{};
  std::span<std::uint16_t> tile_scratch{};
  std::span<TileRevisionPublication> publications{};
  std::span<TileKey> affected_keys{};
};

struct IncrementalAppendResult {
  OperationIdentity identity{};
  std::size_t affected_resident_tiles = 0;
  std::size_t published_tiles = 0;
};

// Coordinates document authority and materialization as one append. All
// workspace is caller-owned. Failure leaves both log and canvas at their prior
// revisions; callers must serialize access to the log, canvas, and workspace.
[[nodiscard]] std::optional<IncrementalAppendResult> append_incrementally(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const IncrementalDocumentWorkspace& workspace);

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_INCREMENTAL_DOCUMENT_H
