#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

struct IncrementalDocumentWorkspace {
  // Compact row-major scratch for the conservative affected overview region.
  // Full overview capacity handles the worst case, but ordinary appends use
  // only their bounded prefix.
  std::span<std::uint16_t> overview_scratch{};
  std::span<std::uint16_t> tile_scratch{};
  std::span<TileRevisionPublication> publications{};
  std::span<TileKey> affected_keys{};
};

struct IncrementalAppendResult {
  OperationIdentity identity{};
  PixelRect affected_world_bounds{};
  std::size_t affected_resident_tiles = 0;
  std::size_t published_tiles = 0;
  std::size_t fallback_tiles = 0;
};

enum class IncrementalPublicationScope : std::uint8_t {
  kAllMaterialized,
  kPriorityView,
  // Publish exact overview authority and invalidate affected detail. The live
  // framebuffer can remain visible while detail refills cooperatively.
  kOverviewOnly,
};

struct IncrementalAppendOptions {
  // Priority-view scope updates only affected materialization intersecting the
  // active tiled view. Other affected identities become correct overview
  // fallback and are replayed cooperatively after input returns.
  std::optional<ViewRequest> priority_view{};
  IncrementalPublicationScope publication_scope = IncrementalPublicationScope::kAllMaterialized;
};

// Coordinates document authority and materialization as one append. All
// workspace is caller-owned. Failure leaves both log and canvas at their prior
// revisions; callers must serialize access to the log, canvas, and workspace.
[[nodiscard]] std::optional<IncrementalAppendResult> append_incrementally(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const IncrementalDocumentWorkspace& workspace, IncrementalAppendOptions options = {});

// Coordinates an authoritative snapshot restore. The caller-owned pixels must
// not alias log or canvas storage. Validation is completed before either state
// module changes. Callers must serialize access.
[[nodiscard]] bool restore_document_snapshot(OperationLog& log, MaterializedCanvas& canvas,
                                             DocumentRevision revision,
                                             std::span<const std::uint16_t> overview_pixels);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H
