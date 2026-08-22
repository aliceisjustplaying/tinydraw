#ifndef TINYDRAW_VECTOR_V2_TILE_PRODUCER_H
#define TINYDRAW_VECTOR_V2_TILE_PRODUCER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

inline constexpr int kTileProducerColumns = 2;
inline constexpr int kTileProducerRows = 2;
inline constexpr int kTileProducerWidth = kTileProducerColumns * kTileWidth;
inline constexpr int kTileProducerHeight = kTileProducerRows * kTileHeight;
inline constexpr std::size_t kTileProducerPixels =
    static_cast<std::size_t>(kTileProducerWidth) * kTileProducerHeight;
inline constexpr std::size_t kTileProducerMaskBytes = (kTileProducerPixels + 7U) / 8U;
inline constexpr std::size_t kTileProducerSummaryRows =
    static_cast<std::size_t>(kTileProducerHeight);
inline constexpr std::size_t kTileProducerSummaryWords = (kTileProducerSummaryRows + 31U) / 32U;
inline constexpr std::size_t kTileProducerOperationBatch = 64;
inline constexpr std::size_t kTileProducerSampleBatch = 96;
// Per-slice work budget for the op-level chord sweep, in window-clipped
// span pixels actually visited (plus a flat per-row scan charge). This is
// the honest slice cost: dense fat batches stop after few rows, hairline
// batches sweep hundreds. The 22k device gate keeps maximum ticks below
// 10.7 ms while reducing producer resumptions.
inline constexpr std::size_t kTileProducerSweepWorkBatch = 22'000;

struct TileProducerWorkspace {
  // Row-major 128x128 supertask surface. Publication reads tiles straight
  // out of this surface with a strided copy; there is no packed staging.
  std::span<std::uint16_t> supertask_pixels{};
  // One finalized bit per supertask pixel for exact newest-first replay.
  std::span<std::uint8_t> finalized_pixels{};
  // Exact row-saturation summary over the supertask mask: per-row unfinalized
  // counts plus a saturated-row bitmap. Lets replay skip saturated rows,
  // segments, operations, and complete groups in O(1) with bit-identical
  // output.
  std::span<std::uint16_t> summary_row_unset{};
  std::span<std::uint32_t> summary_saturated_words{};
  // Opaque storage for one operation's prepared chord batch (H7 op-level
  // sweep), at least kOperationChordStorageBytes and 4-aligned.
  std::span<std::byte> operation_chord_plans{};
  // Optional F11 spatial-query output. Capacity must cover the operation log;
  // when absent or short the producer retains the exact authority scan.
  std::span<std::uint16_t> candidate_indices{};
};

struct TileProductionStep {
  PixelRect level_bounds{};
  std::size_t operations_scanned = 0;
  std::size_t operations_in_authority = 0;
  std::size_t index_candidates = 0;
  std::size_t deduplicated_candidates = 0;
  std::size_t operations_intersecting = 0;
  std::size_t operations_rendered = 0;
  std::size_t groups_published = 0;
  std::size_t raster_steps = 0;
  std::size_t raster_work = 0;
  std::size_t tiles_published = 0;
  // Valid only after publication (`tiles_published != 0`) or on a complete
  // result. Resumable replay-only slices leave this at zero.
  std::size_t visible_tiles_remaining = 0;
  bool complete = false;
};

// Cold-produces provisional world-aligned tiles from a uniform baseline and a
// exact newest-first replay range. A finalized-pixel mask makes first writer
// win, which is equivalent to forward painter order for opaque pen/eraser ops.
// This is the Gate 1 raw-source producer, not a
// settled renderer: output is always kImmediate. All storage is caller-owned.
// Callers serialize the log, canvas, producer, and workspace.
class TileProducer {
 public:
  TileProducer(OperationLog& log, MaterializedCanvas& canvas, TileProducerWorkspace workspace,
               DocumentRevision uniform_baseline_revision = {},
               std::uint16_t baseline_color = 0xFFFFU);

  [[nodiscard]] bool ready() const;
  // Produces the closest missing 2x2 supertask for a tiled viewport. A complete
  // result means every visible key has a current tile at kImmediate or better.
  [[nodiscard]] std::optional<TileProductionStep> produce_next(const ViewRequest& view);
  [[nodiscard]] std::optional<std::size_t> visible_tiles_remaining(const ViewRequest& view) const;
  // Abandons an unpublished group so another serialized renderer may reuse
  // the caller-owned chord workspace. The next produce_next starts fresh.
  void cancel_pending_work();
  // Changes the authoritative uniform snapshot after a coordinated log/canvas
  // reset. Rejected unless both authorities are empty and at this revision.
  [[nodiscard]] bool reset_uniform_baseline(DocumentRevision revision,
                                            std::uint16_t color = 0xFFFFU);
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  // Optional re-render truth observer; completed group renders are classified
  // against damage/eviction state the canvas reports. Null disables.
  void set_rerender_ledger(RerenderLedger* ledger) { rerender_ledger_ = ledger; }
#endif

 private:
  struct GroupPublication {
    PixelRect level_bounds{};
    std::size_t tiles_published = 0;
  };

  enum class OperationGate : std::uint8_t {
    kFailed,
    kConsumed,
    kReady,
  };

  struct ActiveGroup {
    ViewRequest view{};
    TileKey origin{};
    PixelRect bounds{};
    OperationLogEpoch epoch{};
    DocumentRevision revision{};
    std::size_t first_operation = 0;
    std::size_t next_operation = 0;
    std::size_t candidate_count = 0;
    std::size_t next_candidate = 0;
    OperationSpatialQueryStats spatial_stats{};
    bool uses_spatial_index = false;
    bool spatial_stats_pending = false;
    // Current reverse segment endpoint. Zero initializes a newly selected
    // operation; single-sample operations are handled as one bounded unit.
    std::size_t next_sample = 0;
    // Prepared chord-batch sweep state (H7). A batch holds whole endpoint
    // units of the cached operation in the caller-funded chord storage; the
    // sweep resumes at batch_row between producer slices. batch_next_endpoint
    // is the endpoint cursor after this batch (zero = operation exhausted).
    bool batch_active = false;
    std::size_t batch_chords = 0;
    std::size_t batch_next_endpoint = 0;
    std::size_t batch_work = 0;
    PixelRect batch_bounds{};
    int batch_row = 0;
    // Operation-level visibility and saturation gates run once per operation;
    // the passing fetch is cached here so per-segment replay touches neither
    // the log nor the operation-level rectangle math again. The cached spans
    // stay valid because render_active_batch revalidates epoch and revision
    // before every batch.
    std::size_t cached_operation_index = kNoCachedOperation;
    StoredOperation cached_operation{};
    bool active = false;
  };

  static constexpr std::size_t kNoCachedOperation = static_cast<std::size_t>(-1);

  [[nodiscard]] static bool valid_view(const ViewRequest& view);
  void produce_next_into(const ViewRequest& view, std::optional<TileProductionStep>& result);
  [[nodiscard]] bool tile_satisfies(TileKey key, MaterializationQuality quality) const;
  [[nodiscard]] std::optional<TileKey> choose_certain_paper_group(const ViewRequest& view) const;
  [[nodiscard]] bool publish_certain_paper_group(const ViewRequest& view, TileKey origin,
                                                 TileProductionStep& result);
  [[nodiscard]] std::optional<std::size_t> visible_tiles_remaining(
      const ViewRequest& view, MaterializationQuality quality) const;
  [[nodiscard]] std::optional<TileKey> choose_missing_group(const ViewRequest& view) const;
  [[nodiscard]] bool start_group(const ViewRequest& view, TileKey group_origin);
  [[nodiscard]] bool active_group_has_work() const;
  void discard_active_group();
  void consume_active_operation(TileProductionStep& result, std::size_t& operations_consumed);
  [[nodiscard]] OperationGate gate_active_operation(TileProductionStep& result,
                                                    std::size_t& operations_consumed);
  void finish_active_batch(TileProductionStep& result, std::size_t& operations_consumed);
  [[nodiscard]] bool render_active_operation_slice(TileProductionStep& result,
                                                   std::size_t& operations_consumed,
                                                   std::size_t& chords_consumed,
                                                   std::size_t& work_consumed);
  [[nodiscard]] bool render_active_batch(TileProductionStep& result);
  [[nodiscard]] std::optional<GroupPublication> publish_group(PixelRect rendered_bounds,
                                                              PixelRect visible_bounds,
                                                              ZoomLevel zoom,
                                                              DocumentRevision revision);
  [[nodiscard]] bool publish_surface_tile(TileKey key, PixelRect rendered_bounds,
                                          DocumentRevision revision);
  static void include_bounds(PixelRect bounds, GroupPublication& publication);
  OperationLog& log_;
  MaterializedCanvas& canvas_;
  TileProducerWorkspace workspace_;
  MaskedRowSummary summary_;
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  RerenderLedger* rerender_ledger_ = nullptr;
#endif
  DocumentRevision baseline_revision_{};
  std::uint16_t baseline_color_ = 0xFFFFU;
  ActiveGroup active_group_{};
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_TILE_PRODUCER_H
