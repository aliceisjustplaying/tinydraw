#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

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
  // Shared operation-replay plans. Resumable absorption requires
  // kOperationChordStorageBytes, 4-byte aligned. This may alias the idle tile
  // producer's plan storage when producer and absorption are serialized.
  std::span<std::byte> operation_chord_plans{};
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

// Core-owned cooperative cancellation seam. requested is called between
// bounded useful-work quanta and may combine an input flag, a deadline, or
// both in caller-owned context without allocation.
struct CooperativeWorkLimit {
  bool (*requested)(const void* context) = nullptr;
  const void* context = nullptr;
  // Approximate span pixels between cancellation checks while rasterizing.
  // One live row always completes, so a row wider than this remains atomic.
  std::size_t raster_work_px = 512;

  [[nodiscard]] bool yield_requested() const { return requested != nullptr && requested(context); }
};

enum class PendingAbsorptionStatus : std::uint8_t {
  kInProgress,
  kComplete,
  kIdle,
  kError,
};

enum class PendingAbsorptionWorkUnit : std::uint8_t {
  kNone,
  kCopyOverview,
  kRasterOverview,
  kEnumerate,
  kUniform,
  kVisibleRaw,
  kOffscreenRaw,
  kStageOverview,
  kStageUniforms,
  kStageRawSlots,
  kStageRerenderDamage,
  kStageOccupancy,
  kCommit,
};

struct PendingAbsorptionSliceResult {
  PendingAbsorptionStatus status = PendingAbsorptionStatus::kError;
  // Last bounded unit completed by this call; kNone means it yielded before
  // mutation. Useful for attributing a caller-observed slice maximum.
  PendingAbsorptionWorkUnit work_unit = PendingAbsorptionWorkUnit::kNone;
  // Valid only for kComplete.
  IncrementalAppendResult result{};
  // Cancellation checkpoints reached by this call. Useful for host bounds
  // characterization without coupling the module to a platform clock.
  std::size_t checkpoints = 0;
};

// Caller-owned continuation for one pending operation. While active, the
// canvas, workspace spans, priority view, and retention budget are one
// serialized transaction and must remain unchanged. Authority may append
// later operations, but must not reset/rebase the captured log storage. Partial
// tile pixels are safe to present because the pending overlay remains
// authoritative until the final metadata commit.
class PendingOperationAbsorption {
 public:
  [[nodiscard]] bool active() const { return phase_ != Phase::kIdle; }
  // Abandons only the continuation; partial resident pixels are not rolled
  // back. This is safe while the captured opaque pen/eraser operation remains
  // pending because replay is idempotent. Keep tile production serialized
  // until a restarted absorption drains that pending operation.
  void cancel();

 private:
  friend PendingAbsorptionSliceResult absorb_pending_operation_slice(
      const OperationLog&, MaterializedCanvas&, const InPlaceAppendWorkspace&,
      PendingOperationAbsorption&, std::optional<ViewRequest>, CooperativeWorkLimit,
      InPlaceRetentionBudget);

  enum class Phase : std::uint8_t {
    kIdle,
    kCopyOverview,
    kRasterOverview,
    kEnumerate,
    kUniform,
    kVisibleRaw,
    kOffscreenRaw,
    kStageOverview,
    kStageMetadata,
    kCommit,
  };

  Phase phase_ = Phase::kIdle;
  const OperationLog* log_ = nullptr;
  MaterializedCanvas* canvas_ = nullptr;
  StoredOperation operation_{};
  InPlaceAppendWorkspace workspace_{};
  std::optional<ViewRequest> priority_view_{};
  InPlaceRetentionBudget retention_{};
  PixelRect overview_bounds_{};
  OverviewRevisionPublication overview_publication_{};
  InPlaceAppendPhases phases_{};
  InPlaceRetainDrops drops_{};
  std::size_t copy_row_ = 0;
  std::size_t affected_count_ = 0;
  std::size_t retained_count_ = 0;
  std::size_t scan_index_ = 0;
  std::size_t next_endpoint_ = 0;
  OperationChordBatch chord_batch_{};
  OperationSweepCursor raster_cursor_{};
  InPlaceOverviewStage overview_stage_{};
  InPlaceTileEdit tile_edit_{};
  TileKey tile_key_{};
  bool batch_ready_ = false;
  bool painting_tile_ = false;
};

// Committed-overlay revision split (VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md
// §3.1): the materialized canvas may trail operation authority when a caller
// appends to the log without a paired canvas commit. The pending operation
// range is derived, not stored: it is exactly the log operations between the
// canvas revision and the log revision.
//
// pending_operation_count reports how many operations the canvas has not yet
// absorbed (0 when in lockstep, not ready, or when the canvas revision left
// the log's represented range).
[[nodiscard]] std::size_t pending_operation_count(const OperationLog& log,
                                                  const MaterializedCanvas& canvas);

// Deferred-commit entry (design §3): publishes operation authority without
// touching the canvas, leaving the new operation in the pending range. The
// input path pays only log validation and the sample copy; presentation
// must patch composed pixels with overlay_pending_operations and a drain
// loop absorbs the range via absorb_pending_operation. The result carries
// the identity, the operation's world bounds, and prepare_us; every canvas
// field (published/fallback/drops/other phases) is zero by construction.
[[nodiscard]] std::optional<IncrementalAppendResult> append_authority_only(
    OperationLog& log, const OperationAppend& append_request, InPlaceRetentionBudget budget = {});
[[nodiscard]] std::optional<IncrementalAppendResult> append_authority_only(
    OperationLog& log, const BuiltOperation& operation, InPlaceRetentionBudget budget = {});

enum class HistoryDirection : std::uint8_t {
  kUndo,
  kRedo,
};

// Moves authority across one whole Stroke and rebuilds only its damaged
// overview rectangle from painter-ordered vector truth. Affected tiles are
// invalidated; unaffected materialization advances to the new generation.
// Failure leaves both authority and canvas unchanged. Pending appends must be
// drained so log and canvas revisions are equal before entry.
[[nodiscard]] std::optional<HistoryChange> move_history_incrementally(
    OperationLog& log, MaterializedCanvas& canvas, HistoryDirection direction,
    std::span<std::uint16_t> overview_scratch);

// Absorbs the oldest pending operation into the canvas through the same
// committed-overlay phase machinery, advancing the canvas by
// exactly one revision. Authority is never touched. Returns nullopt when
// nothing is pending or when a fallible step rejects; the canvas then keeps
// its prior revision and the call may be retried. While the canvas trails,
// a drain loop may retry later. Callers must serialize access.
[[nodiscard]] std::optional<IncrementalAppendResult> absorb_pending_operation(
    const OperationLog& log, MaterializedCanvas& canvas, const InPlaceAppendWorkspace& workspace,
    std::optional<ViewRequest> priority_view = std::nullopt, InPlaceRetentionBudget budget = {});

// Advances one pending operation until the cooperative limit requests a yield
// or the revision commits. kInProgress retains all cursor state. kComplete and
// kIdle reset it for reuse. Initial kError leaves canvas unchanged; a mismatched
// resume returns kError with the original continuation active and recoverable.
[[nodiscard]] PendingAbsorptionSliceResult absorb_pending_operation_slice(
    const OperationLog& log, MaterializedCanvas& canvas, const InPlaceAppendWorkspace& workspace,
    PendingOperationAbsorption& state, std::optional<ViewRequest> priority_view = std::nullopt,
    CooperativeWorkLimit limit = {}, InPlaceRetentionBudget retention = {});

// The committed overlay (design §3.2): paints the pending operation range —
// the log operations the canvas has not yet absorbed — clipped to a
// level-space window, in painter order, through the same rasterizer the
// producer and appends use. Callers patch composed presentation pixels with
// this before submit so glass stays exact while materialization trails.
// A window with no intersecting pending operation is left untouched.
// Returns false only on invalid state (never partially paints an operation
// that fails validation before its first pixel). Empty pending range is
// success.
[[nodiscard]] bool overlay_pending_operations(const OperationLog& log,
                                              const MaterializedCanvas& canvas,
                                              const RasterSurface& surface);

// Rebuilds a complete 25% overview from the active authority prefix. Retained
// Redo operations are deliberately excluded. Failure leaves output unspecified.
[[nodiscard]] bool replay_active_overview(const OperationLog& log, std::span<std::uint16_t> output);

// Builds the conservative tiled may-ink proof for the active authority
// prefix. Pen bounds set bits; erasers cannot add ink and are ignored.
[[nodiscard]] bool build_tiled_may_ink(const OperationLog& log, std::span<std::uint8_t> output);

// Coordinates an authoritative snapshot restore. The caller-owned pixels must
// not alias log or canvas storage. Validation is completed before either state
// module changes. Callers must serialize access.
[[nodiscard]] bool restore_document_snapshot(OperationLog& log, MaterializedCanvas& canvas,
                                             DocumentRevision revision,
                                             std::span<const std::uint16_t> overview_pixels);

// Coordinates a new-document reset directly to paper without a full overview
// snapshot allocation. Callers must serialize access.
[[nodiscard]] bool reset_blank_document(OperationLog& log, MaterializedCanvas& canvas,
                                        DocumentRevision revision);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_DOCUMENT_H
