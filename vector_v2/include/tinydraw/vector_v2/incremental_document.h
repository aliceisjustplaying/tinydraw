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

struct InPlaceAppendWorkspace {
  // Compact row-major scratch for the conservative affected overview region.
  std::span<std::uint16_t> overview_scratch{};
  // Affected resident identity enumeration; must hold every raw slot plus one
  // viewport of uniform keys.
  std::span<TileKey> affected_keys{};
  // One finalized bit per tile pixel. A chunk is a single tool and color, so
  // painting its segments newest-first through this mask writes every covered
  // pixel exactly once while producing the identical pixel union as forward
  // replay; overlapping fat-capsule segments would otherwise rewrite each
  // covered pixel several times per commit.
  std::span<std::uint8_t> tile_mask{};
};

inline constexpr std::size_t kInPlaceTileMaskBytes = (kTilePixels + 7U) / 8U;

// Interactive-path sibling of append_incrementally that paints the new
// operation directly into resident raw tiles instead of copying each affected
// tile out and back. Every fallible step (log preparation, overview scratch,
// canvas validation, enumeration) runs before any owned pixel changes, so
// failure still leaves both authorities at their prior revisions. Affected
// resident raw tiles at every zoom are updated in place; resident uniforms
// whose color equals the painted color are retained untouched; uniforms
// inside priority_view are converted to raw and painted; every other affected
// identity is invalidated to correct overview fallback, exactly like the
// reference path. Composed pixels equal the reference path wherever both are
// resident, and the priority view never falls back. Callers must serialize
// access and must not compose between internal edits (single-threaded use).
[[nodiscard]] std::optional<IncrementalAppendResult> append_incrementally_in_place(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const InPlaceAppendWorkspace& workspace,
    std::optional<ViewRequest> priority_view = std::nullopt);

// Coordinates an authoritative snapshot restore. The caller-owned pixels must
// not alias log or canvas storage. Validation is completed before either state
// module changes. Callers must serialize access.
[[nodiscard]] bool restore_document_snapshot(OperationLog& log, MaterializedCanvas& canvas,
                                             DocumentRevision revision,
                                             std::span<const std::uint16_t> overview_pixels);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H
