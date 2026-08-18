#ifndef TINYDRAW_VECTOR_V2_MATERIALIZED_CANVAS_H
#define TINYDRAW_VECTOR_V2_MATERIALIZED_CANVAS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {

inline constexpr int kWorldWidth = 1472;
inline constexpr int kWorldHeight = 1792;
inline constexpr int kOverviewWidth = 368;
inline constexpr int kOverviewHeight = 448;
inline constexpr int kTileWidth = 64;
inline constexpr int kTileHeight = 64;
inline constexpr std::size_t kOverviewPixels =
    static_cast<std::size_t>(kOverviewWidth) * static_cast<std::size_t>(kOverviewHeight);
inline constexpr std::size_t kOverviewBytes = kOverviewPixels * sizeof(std::uint16_t);
inline constexpr std::size_t kTilePixels =
    static_cast<std::size_t>(kTileWidth) * static_cast<std::size_t>(kTileHeight);
inline constexpr std::size_t kTileBytes = kTilePixels * sizeof(std::uint16_t);
// A display-sized viewport can cross seven tile columns and eight tile rows
// when its origin is not tile-aligned.
inline constexpr std::size_t kMaximumVisibleTiles = 56;
inline constexpr int kOccupancyCellWorldSize = 16;
inline constexpr int kOccupancyColumns = 92;
inline constexpr int kOccupancyRows = 112;
inline constexpr std::size_t kOccupancyCellCount =
    static_cast<std::size_t>(kOccupancyColumns) * kOccupancyRows;
inline constexpr std::size_t kOccupancyBytes = (kOccupancyCellCount + 7U) / 8U;
inline constexpr std::size_t kMaterializedTileIdentityCount = 168U + 644U + 2'576U + 10'304U;
inline constexpr std::size_t kTiledZoomCount = 4;
// Cooperative composition stops after at most this many complete destination
// rows. A display-width slice therefore writes at most 2,944 pixels before
// returning control to the caller.
inline constexpr int kViewCompositionRowsPerSlice = 8;
// Sentinel for raw_slot_directory entries: the identity has no raw slot.
inline constexpr std::uint16_t kNoRawSlot = 0xFFFFU;

enum class ZoomLevel : std::uint8_t {
  k25Percent,
  k50Percent,
  k100Percent,
  k200Percent,
  k400Percent,
};

// Revisions are monotonic within a document. UINT32_MAX is terminal; callers
// must start a new document identity rather than wrap it to zero.
struct DocumentRevision {
  std::uint32_t value = 0;
  bool operator==(const DocumentRevision&) const = default;
};

struct WorldPoint {
  int x = 0;
  int y = 0;
};

struct PixelRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  bool operator==(const PixelRect&) const = default;
};

struct TileGrid {
  int columns = 0;
  int rows = 0;
  bool operator==(const TileGrid&) const = default;
};

struct TileKey {
  ZoomLevel zoom = ZoomLevel::k50Percent;
  std::uint16_t column = 0;
  std::uint16_t row = 0;
  bool operator==(const TileKey&) const = default;
};

// Ordered from lowest to highest fidelity. Quality downgrade checks apply
// within one document revision; a revision-advancing immediate mutation may
// replace an affected settled tile because the new revision is not settled.
enum class MaterializationQuality : std::uint8_t {
  kOverviewFallback,
  kImmediate,
  kSettled,
};

enum class SourceKind : std::uint8_t {
  kOverview,
  kUniform,
  kTileSlot,
};

struct SourceSelection {
  SourceKind kind = SourceKind::kOverview;
  DocumentRevision revision{};
  MaterializationQuality quality = MaterializationQuality::kOverviewFallback;
};

struct OverviewRevisionPublication {
  PixelRect bounds{};
  std::span<const std::uint16_t> pixels{};
};

enum class OverviewStageStatus : std::uint8_t {
  kInProgress,
  kComplete,
  kError,
};

enum class InPlaceMetadataPhase : std::uint8_t {
  kUniforms,
  kRawSlots,
  kRerenderDamage,
  kOccupancy,
  kComplete,
};

struct InPlaceMetadataSlice {
  OverviewStageStatus status = OverviewStageStatus::kError;
  InPlaceMetadataPhase phase = InPlaceMetadataPhase::kUniforms;
  std::size_t work_items = 0;
};

class MaterializedCanvas;
class RerenderLedger;

// Caller-owned proof for a cooperatively staged overview publication. Partial
// rows may be abandoned only while an idempotent pending overlay remains the
// presentation authority; cancel() forgets the proof but does not roll pixels
// back. A restarted opaque operation replay converges those pixels exactly.
// Any retained_keys backing passed after overview completion must stay alive
// and immutable through commit or cancel. Resume validation fingerprints its
// pointer and size so each slice remains bounded.
class InPlaceOverviewStage {
 public:
  [[nodiscard]] bool active() const { return canvas_ != nullptr; }
  [[nodiscard]] bool complete() const { return active() && next_row_ == bounds_.y1; }
  void cancel();

 private:
  friend class MaterializedCanvas;

  MaterializedCanvas* canvas_ = nullptr;
  DocumentRevision revision_{};
  PixelRect bounds_{};
  PixelRect affected_world_bounds_{};
  const std::uint16_t* source_ = nullptr;
  std::size_t source_size_ = 0;
  std::uint64_t expected_canvas_epoch_ = 0;
  int next_row_ = 0;
  const TileKey* retained_keys_ = nullptr;
  std::size_t retained_count_ = 0;
  std::optional<std::uint16_t> preserved_uniform_color_{};
  std::optional<ZoomLevel> priority_zoom_{};
  const RerenderLedger* rerender_ledger_ = nullptr;
  InPlaceMetadataPhase metadata_phase_ = InPlaceMetadataPhase::kUniforms;
  std::size_t metadata_zoom_ = 0;
  std::size_t metadata_offset_ = 0;
  std::size_t raw_slot_ = 0;
  std::size_t rerender_plane_ = 0;
  std::size_t rerender_offset_ = 0;
  std::size_t occupancy_offset_ = 0;
  std::size_t cross_zoom_invalidated_ = 0;
  bool metadata_started_ = false;
  bool raw_staging_started_ = false;
};

// Mutable window over one resident raw tile for an in-place revision commit.
// bounds are level-space pixels; pixels rows use kTileWidth stride.
struct InPlaceTileEdit {
  TileKey key{};
  PixelRect bounds{};
  std::span<std::uint16_t> pixels{};
};

struct TileRevisionPublication {
  TileKey key{};
  MaterializationQuality quality = MaterializationQuality::kImmediate;
  std::span<const std::uint16_t> pixels{};
};

struct ViewRequest {
  ZoomLevel zoom = ZoomLevel::k25Percent;
  PixelRect level_pixels{};
  bool operator==(const ViewRequest&) const = default;
};

struct ViewFootprint {
  ZoomLevel zoom = ZoomLevel::k25Percent;
  PixelRect level_pixels{};
  bool valid = false;
};

struct ViewCompositionStats {
  DocumentRevision revision{};
  std::size_t tile_pixels = 0;
  std::size_t overview_pixels = 0;
  std::size_t fallback_pixels = 0;
  std::size_t uniform_pixels = 0;
  std::size_t immediate_tiles = 0;
  std::size_t settled_tiles = 0;
  std::size_t fallback_tiles = 0;
  bool operator==(const ViewCompositionStats&) const = default;
};

enum class ViewCompositionStatus : std::uint8_t {
  kInProgress,
  kComplete,
  kError,
};

struct ViewCompositionSliceResult {
  ViewCompositionStatus status = ViewCompositionStatus::kError;
  ViewCompositionStats stats{};
};

class MaterializedCanvas;

// Caller-owned continuation for one logical composition transaction. While it
// is active the request, destination, and canvas must remain serialized and
// unchanged. cancel() abandons partial destination pixels and makes the cursor
// reusable for a fresh transaction.
class ViewCompositionCursor {
 public:
  void cancel();
  [[nodiscard]] bool active() const { return active_; }

 private:
  friend class MaterializedCanvas;

  const MaterializedCanvas* canvas_ = nullptr;
  ViewRequest request_{};
  std::uint16_t* destination_ = nullptr;
  std::size_t destination_size_ = 0;
  DocumentRevision revision_{};
  std::uint64_t canvas_epoch_ = 0;
  ViewCompositionStats stats_{};
  int next_y_ = 0;
  bool active_ = false;
};

class MaterializedUniformStorage {
 public:
  MaterializedUniformStorage() = default;

 private:
  friend class MaterializedCanvas;

  std::uint16_t color_ = 0xFFFFU;
  MaterializationQuality quality_ = MaterializationQuality::kImmediate;
  bool occupied_ = false;
};

static_assert(sizeof(MaterializedUniformStorage) == 4U);

class MaterializedSlotStorage {
 public:
  MaterializedSlotStorage() = default;

 private:
  friend class MaterializedCanvas;

  TileKey key_{};
  DocumentRevision revision_{};
  MaterializationQuality quality_ = MaterializationQuality::kImmediate;
  std::uint64_t last_use_ = 0;
  bool occupied_ = false;
};

class RerenderLedger;

class MaterializedCanvas {
 public:
  // All spans are caller-owned and must outlive this object. Slot elements must
  // be constructed before the span is passed; the constructor resets their state.
  // The catalog, occupancy map, and raw-slot directory are part of the one
  // production storage shape. raw_slot_directory maps every tile identity to
  // its resident slot (kNoRawSlot when absent).
  MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                     std::span<MaterializedUniformStorage> uniform_catalog,
                     std::span<std::uint8_t> occupancy_bits,
                     std::span<MaterializedSlotStorage> slots, std::span<std::uint16_t> tile_pixels,
                     DocumentRevision initial_revision,
                     std::span<std::uint16_t> raw_slot_directory);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] DocumentRevision current_revision() const;
  [[nodiscard]] std::size_t slot_capacity() const;
  // Occupied raw slots. Idle repair uses this to stop a level sweep once the
  // pool saturates: past that point every publication evicts warmer tiles.
  [[nodiscard]] std::size_t resident_raw_tiles() const;
  [[nodiscard]] std::span<const std::uint16_t> overview_pixels() const;
  [[nodiscard]] bool certainly_paper(TileKey key) const;

  // Logically commits a complete overview and its revision in one call. Once
  // an overview is valid, each publication must advance the revision and use
  // a distinct source buffer. Callers must serialize all canvas operations.
  [[nodiscard]] bool publish_overview(DocumentRevision revision,
                                      std::span<const std::uint16_t> pixels);
  // Replaces materialization from a complete 25% snapshot at any revision and
  // invalidates every tile. Without an authority-derived may-ink map the
  // occupancy state is fully conservative. This is not an ordinary revision
  // publication. Fails when pixels alias owned canvas storage.
  [[nodiscard]] bool restore_snapshot(DocumentRevision revision,
                                      std::span<const std::uint16_t> pixels);
  // Exact bootstrap overload. tiled_may_ink has one bit per 16x16 world cell
  // and must conservatively cover every pixel reachable by active pen
  // authority. Both sources are validated before any canvas state changes.
  [[nodiscard]] bool restore_snapshot(DocumentRevision revision,
                                      std::span<const std::uint16_t> pixels,
                                      std::span<const std::uint8_t> tiled_may_ink);
  // Resets materialization directly to uniform paper without requiring a
  // caller-owned full-overview snapshot.
  [[nodiscard]] bool reset_blank(DocumentRevision revision);
  // Commits exactly the next document revision. The overview publication is a
  // compact, row-major replacement rectangle prepared outside live storage.
  // Tiles intersecting the conservative world bounds are affected at every
  // zoom; affected resident tiles without a publication become overview
  // fallback. Every input is validated before owned pixels or identities
  // change.
  [[nodiscard]] bool commit_incremental_revision(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds, std::span<const TileRevisionPublication> tile_publications);
  // In-place revision protocol. A caller that has serialized access first
  // validates the whole commit with can_edit_in_place_revision, then mutates
  // resident current-revision raw tiles through edit_resident_tile /
  // materialize_uniform_as_raw (which cannot fail in ways that corrupt
  // authority: a failed or abandoned edit is recovered by simply not
  // retaining the key, which invalidates it to correct overview fallback),
  // and finally publishes with commit_in_place_revision listing every key
  // whose pixels are exact for the new revision. No composition may observe
  // the canvas between edits and commit.
  [[nodiscard]] bool can_edit_in_place_revision(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds) const;
  [[nodiscard]] std::optional<InPlaceTileEdit> edit_resident_tile(TileKey key);
  // Converts one resident uniform into a raw slot filled with its color and
  // returns the slot for editing. May evict an LRU slot. Returns
  // nullopt when no slot is available; the caller falls back to invalidation.
  [[nodiscard]] std::optional<InPlaceTileEdit> materialize_uniform_as_raw(TileKey key);
  [[nodiscard]] std::optional<std::uint16_t> uniform_color(TileKey key) const;
  // Optional scope for commit_in_place_revision. preserved_uniform_color
  // exempts no-op uniform targets from invalidation at every zoom;
  // cross_zoom_invalidated (when set) counts identities dropped at zooms
  // other than priority_zoom.
  // No default member initializers: the {} default argument below requires
  // the aggregate to be complete inside the class definition, and value
  // initialization already empties the optionals and nulls the pointer.
  struct InPlaceCommitScope {
    std::optional<std::uint16_t> preserved_uniform_color;
    std::optional<ZoomLevel> priority_zoom;
    std::size_t* cross_zoom_invalidated;
  };
  [[nodiscard]] bool commit_in_place_revision(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds, std::span<const TileKey> retained_keys,
      const InPlaceCommitScope& scope = {});
  // Copies at most max_rows of a validated overview publication into owned
  // storage and advances a caller-owned proof. Every resume must use the same
  // canvas, revision, bounds, source span, affected bounds, and canvas epoch.
  // The serialized caller must keep at most one live stage per canvas. One
  // overview row is the smallest atomic unit.
  [[nodiscard]] OverviewStageStatus stage_in_place_overview_rows(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds, std::size_t max_rows, InPlaceOverviewStage& stage);
  // Advances one bounded metadata phase after every overview row is staged.
  // max_work_items caps identities, slots, damage groups, or occupancy cells;
  // a call never crosses a phase boundary. Resume inputs and the canvas epoch
  // must match the stage proof exactly.
  [[nodiscard]] InPlaceMetadataSlice stage_in_place_metadata(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds, std::span<const TileKey> retained_keys,
      std::size_t max_work_items, InPlaceOverviewStage& stage,
      const InPlaceCommitScope& scope = {});
  // Metadata-only completion for a fully staged overview. Validation is
  // mutation-free; a mismatch leaves the stage live for its original caller.
  [[nodiscard]] bool commit_staged_in_place_revision(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds, std::span<const TileKey> retained_keys,
      InPlaceOverviewStage& stage, const InPlaceCommitScope& scope = {});
  // Defensive recovery: drops any raw slot and uniform entry for key so the
  // identity falls back to the authoritative overview.
  void invalidate_identity(TileKey key);
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  // Optional re-render truth observer. The canvas reports revision damage
  // bounds and raw-slot evictions; it never reads the ledger. Null disables.
  void set_rerender_ledger(RerenderLedger* ledger) { rerender_ledger_ = ledger; }
  [[nodiscard]] RerenderLedger* rerender_ledger() const { return rerender_ledger_; }
#endif
  [[nodiscard]] std::optional<std::size_t> publish_tile(TileKey key, DocumentRevision revision,
                                                        MaterializationQuality quality,
                                                        std::span<const std::uint16_t> pixels);
  // Strided variant: pixels is a width x height window of a larger row-major
  // surface whose rows are source_stride pixels apart. The span must cover
  // exactly (height - 1) * source_stride + width elements starting at the
  // window origin, and source_stride must be >= the tile width. Lets the
  // producer publish straight from its supertask surface without a packed
  // staging copy.
  [[nodiscard]] std::optional<std::size_t> publish_tile(TileKey key, DocumentRevision revision,
                                                        MaterializationQuality quality,
                                                        std::span<const std::uint16_t> pixels,
                                                        std::size_t source_stride);
  [[nodiscard]] std::optional<std::size_t> publish_uniform(TileKey key, DocumentRevision revision,
                                                           MaterializationQuality quality,
                                                           std::uint16_t color = 0xFFFFU);
  [[nodiscard]] std::optional<SourceSelection> lookup(TileKey key) const;
  // Softly protects one recent viewport at each tiled zoom. Protection only
  // changes eviction order: a protected tile can still be replaced
  // when every raw slot belongs to remembered footprints.
  [[nodiscard]] bool remember_view(const ViewRequest& view);
  // The most recent valid view per tiled zoom, as recorded by remember_view.
  // Idle repair uses these so zoom returns land on materialized tiles.
  [[nodiscard]] std::span<const ViewFootprint> recent_views() const { return recent_views_; }
  // Discards every raster tile while retaining document revision and complete
  // overview authority. Used for cold-cache transitions and snapshot policy.
  [[nodiscard]] bool discard_tiles();
  [[nodiscard]] std::optional<ViewCompositionStats> compose_view(
      const ViewRequest& request, std::span<std::uint16_t> destination);
  // Advances one allocation-free row band. The caller can inspect touch
  // urgency after every incomplete result and resume later with the same
  // request, destination, and cursor. Panel submission remains a separate
  // transaction after kComplete.
  [[nodiscard]] ViewCompositionSliceResult compose_view_slice(const ViewRequest& request,
                                                              std::span<std::uint16_t> destination,
                                                              ViewCompositionCursor& cursor);
  // Returns intersecting current-revision raw-slot keys plus uniform keys in
  // priority_view. The visible uniform keys must be materialized before an
  // append invalidates their paper entries, or the committed frame would
  // briefly fall back to the pixelated overview. Fails rather than returning
  // a partial list when output is too small or either bounds set is invalid.
  [[nodiscard]] std::optional<std::size_t> materialized_tiles_intersecting(
      PixelRect world_bounds, std::span<TileKey> output,
      std::optional<ViewRequest> priority_view = std::nullopt,
      bool priority_view_only = false) const;
  // Appends uniform keys intersecting world_bounds inside each remembered
  // view at zooms other than exclude_zoom, continuing an enumeration that
  // already wrote `written` keys. All-zoom absorption uses this so
  // revisit-bound paper tiles can materialize during idle work (déjà-vu
  // fix); failure (output too small) leaves the primary enumeration usable.
  [[nodiscard]] std::optional<std::size_t> append_recent_view_uniform_keys(
      PixelRect world_bounds, std::optional<ZoomLevel> exclude_zoom, std::span<TileKey> output,
      std::size_t written) const;
  // Copies one current resident tile without exposing mutable pool storage.
  [[nodiscard]] bool copy_resident_tile(TileKey key, std::span<std::uint16_t> destination) const;
  // Workspace used to prepare a next revision must not alias live canvas pixels.
  [[nodiscard]] bool accepts_external_workspace(std::span<const std::byte> workspace) const;

 private:
  friend class InPlaceOverviewStage;
  [[nodiscard]] std::optional<std::size_t> find_tile(TileKey key) const;
  [[nodiscard]] std::optional<std::size_t> find_uniform(TileKey key) const;
  [[nodiscard]] std::optional<std::size_t> choose_slot() const;
  [[nodiscard]] std::uint8_t protection_rank(TileKey key) const;
  [[nodiscard]] bool valid_incremental_revision(
      DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
      PixelRect affected_world_bounds,
      std::span<const TileRevisionPublication> tile_publications) const;
  [[nodiscard]] std::optional<std::size_t> append_visible_uniform_keys(PixelRect world_bounds,
                                                                       ViewRequest view,
                                                                       std::span<TileKey> output,
                                                                       std::size_t written) const;
  void write_tile(std::size_t slot_index, const TileRevisionPublication& publication,
                  DocumentRevision revision);
  struct CompositionContext {
    ViewRequest request{};
    std::span<std::uint16_t> destination{};
    ViewCompositionStats stats{};
    int view_width = 0;
  };

  [[nodiscard]] bool valid_view(const ViewRequest& request, std::size_t destination_size) const;
  [[nodiscard]] bool has_complete_source(const ViewRequest& request) const;
  void compose_overview_pixels(PixelRect bounds, CompositionContext& context) const;
  void compose_tile(TileKey key, PixelRect band, CompositionContext& context);
  void compose_raw_pixels(std::size_t slot_index, PixelRect bounds, CompositionContext& context);
  static void compose_uniform_pixels(std::uint16_t color, PixelRect bounds,
                                     CompositionContext& context);
  void compose_fallback_pixels(PixelRect bounds, CompositionContext& context);
  static void include_quality(MaterializationQuality quality, ViewCompositionStats& stats);
  void invalidate_zoom_uniforms(ZoomLevel zoom, PixelRect world_bounds,
                                std::span<const TileKey> retained_keys,
                                const InPlaceCommitScope& scope);
  void invalidate_uniforms(PixelRect world_bounds, std::span<const TileKey> retained_keys = {},
                           const InPlaceCommitScope& scope = {});
  void apply_overview_publication(const OverviewRevisionPublication& overview_publication);
  void finish_in_place_revision(DocumentRevision revision, PixelRect affected_world_bounds,
                                std::span<const TileKey> retained_keys,
                                const InPlaceCommitScope& scope);
  void finish_revision(DocumentRevision revision, PixelRect affected_world_bounds);
  void mark_occupied(PixelRect world_bounds);
  void cancel_in_place_stage(InPlaceOverviewStage& stage);
  [[nodiscard]] bool raw_slot_is_current(const MaterializedSlotStorage& slot) const;
  void clear_uniforms();
  void touch(MaterializedSlotStorage& slot);
  // Sole occupancy transitions: every occupied_ flip goes through these so
  // occupied_slots_ and the raw-slot directory can never desynchronize.
  void release_slot(std::size_t index);
  void claim_slot(std::size_t index);

  std::span<std::uint16_t> overview_pixels_;
  std::span<MaterializedUniformStorage> uniform_catalog_;
  std::span<std::uint8_t> occupancy_bits_;
  std::span<MaterializedSlotStorage> slots_;
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  RerenderLedger* rerender_ledger_ = nullptr;
#endif
  std::span<std::uint16_t> tile_pixels_;
  std::span<std::uint16_t> raw_slot_directory_;
  std::size_t occupied_slots_ = 0;
  DocumentRevision current_revision_{};
  DocumentRevision overview_revision_{};
  std::uint64_t use_clock_ = 0;
  std::array<ViewFootprint, kTiledZoomCount> recent_views_{};
  ZoomLevel active_view_zoom_ = ZoomLevel::k25Percent;
  std::uint64_t composition_epoch_ = 0;
  DocumentRevision staged_in_place_revision_{};
  bool staged_in_place_active_ = false;
  bool overview_valid_ = false;
};

[[nodiscard]] int zoom_percent(ZoomLevel zoom);
// Maps valid world bounds to the complete set of overview pixels that can be
// affected by the immediate rasterizer. The operation-bounds halo and
// center-sampled minimum raster radius must preserve this contract.
[[nodiscard]] PixelRect overview_bounds_for_world(PixelRect world_bounds);
[[nodiscard]] TileGrid tile_grid(ZoomLevel zoom);
[[nodiscard]] std::optional<std::size_t> tile_identity_index(TileKey key);
[[nodiscard]] bool valid_tile_key(TileKey key);
[[nodiscard]] std::optional<TileKey> tile_key_for_world(ZoomLevel zoom, WorldPoint point);
[[nodiscard]] PixelRect tile_pixel_bounds(TileKey key);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_MATERIALIZED_CANVAS_H
