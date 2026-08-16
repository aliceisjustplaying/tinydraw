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

// Wall time of each in-place commit phase, measured only when a time source
// is provided. Only offscreen raw-tile retention is deadline-bounded; every
// other phase is workload-bounded, so these attributions — not the retention
// budget — explain the caller-visible poll gap.
struct InPlaceAppendPhases {
  std::int64_t prepare_us = 0;           // validation + log preparation
  std::int64_t overview_us = 0;          // overview scratch copy + affected replay
  std::int64_t enumerate_us = 0;         // resident enumeration + edit validation
  std::int64_t uniform_retain_us = 0;    // uniform retention / materialize + paint
  std::int64_t raw_retain_us = 0;        // visible raw in-place painting (budget-exempt)
  std::int64_t offscreen_retain_us = 0;  // budget-bounded offscreen raw painting
  std::int64_t commit_us = 0;            // invalidation + revision metadata commit
};

// Drop attribution for the in-place retain passes. A counted drop is an
// affected identity that was sharp (uniform or current raw) before this
// commit and was not retained, so it composes as pixelated overview
// fallback until a producer pass repairs it. The visible_* causes
// decompose visible_fallback_tiles — each one is on-glass mid-stroke
// pixelation. offscreen_skipped counts active-zoom identities outside the
// priority view dropped by accepted lazy-repair policy (retention-budget
// exhaustion, non-priority uniform invalidation, failed offscreen edits).
struct InPlaceRetainDrops {
  std::uint32_t visible_uniform_no_slot = 0;     // materialize_uniform_as_raw nullopt
  std::uint32_t visible_uniform_paint_fail = 0;  // materialized, then paint failed
  std::uint32_t visible_raw_edit_fail = 0;       // edit_resident_tile nullopt
  std::uint32_t visible_raw_paint_fail = 0;      // paint failed, identity invalidated
  std::uint32_t offscreen_skipped = 0;
};

struct IncrementalAppendResult {
  OperationIdentity identity{};
  PixelRect affected_world_bounds{};
  std::size_t affected_resident_tiles = 0;
  std::size_t published_tiles = 0;
  std::size_t fallback_tiles = 0;
  // Dropped tiles that intersect the priority view: each one is a visible
  // blur. Visible tiles are budget-exempt, so this stays zero unless a
  // paint itself failed; off-view drops appear only in fallback_tiles.
  std::size_t visible_fallback_tiles = 0;
  // Identities invalidated at zooms other than the priority zoom by an
  // in-place commit; the cold work this stroke deferred to later visits.
  std::size_t cross_zoom_invalidated = 0;
  InPlaceAppendPhases phases{};
  InPlaceRetainDrops drops{};
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

// Optional wall-clock bound for the offscreen raw-tile retention phase of an
// in-place commit — and only that phase. Log preparation, the overview
// region replay, enumeration, uniform retention, visible raw tiles (dropping
// one is a visible blur, rejected on glass), and the metadata commit are all
// workload-bounded and run to completion regardless of the deadline. When the
// deadline passes, every unpainted *offscreen* affected tile is dropped to
// correct overview fallback for lazy re-production. A null time source keeps
// offscreen painting unbounded; the time source also enables the per-phase
// timing in IncrementalAppendResult.
struct InPlaceRetentionBudget {
  std::int64_t (*now_us)() = nullptr;
  std::int64_t budget_us = 0;
};

// Interactive-path sibling of append_incrementally that paints the new
// operation directly into resident raw tiles instead of copying each affected
// tile out and back. Every fallible step (log preparation, overview scratch,
// canvas validation, enumeration) runs before any owned pixel changes, so
// failure still leaves both authorities at their prior revisions. Mutation is
// bounded to the active zoom: affected resident raw tiles at priority_view's
// zoom are updated in place; resident uniforms whose color equals the painted
// color are retained untouched at every zoom; uniforms inside priority_view
// are converted to raw and painted; every other affected identity - including
// every raw tile at another zoom - is invalidated to correct overview
// fallback and re-produced lazily on its next visit. Composed pixels equal
// ground-truth replay wherever both are resident, and the priority view never
// falls back. Callers must serialize access and must not compose between
// internal edits (single-threaded use).
[[nodiscard]] std::optional<IncrementalAppendResult> append_incrementally_in_place(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const InPlaceAppendWorkspace& workspace,
    std::optional<ViewRequest> priority_view = std::nullopt, InPlaceRetentionBudget budget = {});

// Coordinates an authoritative snapshot restore. The caller-owned pixels must
// not alias log or canvas storage. Validation is completed before either state
// module changes. Callers must serialize access.
[[nodiscard]] bool restore_document_snapshot(OperationLog& log, MaterializedCanvas& canvas,
                                             DocumentRevision revision,
                                             std::span<const std::uint16_t> overview_pixels);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H
