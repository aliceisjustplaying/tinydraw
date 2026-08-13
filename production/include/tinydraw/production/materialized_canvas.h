#ifndef TINYDRAW_PRODUCTION_MATERIALIZED_CANVAS_H
#define TINYDRAW_PRODUCTION_MATERIALIZED_CANVAS_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::production {

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

struct SlotGeneration {
  std::uint32_t value = 0;
  bool operator==(const SlotGeneration&) const = default;
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

enum class MaterializationQuality : std::uint8_t {
  kOverviewFallback,
  kSettled,
  kExact,
};

enum class MaterializationProvenance : std::uint8_t {
  kCompleteOverview,
  kWorldTile,
};

struct MaterializationIdentity {
  DocumentRevision revision{};
  SlotGeneration generation{};
  MaterializationQuality quality = MaterializationQuality::kOverviewFallback;
  MaterializationProvenance provenance = MaterializationProvenance::kCompleteOverview;
  bool operator==(const MaterializationIdentity&) const = default;
};

enum class SourceKind : std::uint8_t {
  kOverview,
  kTileSlot,
};

struct SourceSelection {
  SourceKind kind = SourceKind::kOverview;
  MaterializationIdentity identity{};
  TileKey requested_tile{};
  PixelRect source_pixels{};
  PixelRect destination_pixels{};
  std::optional<std::size_t> slot_index{};
  int source_stride = 0;
  std::uint32_t pin_token = 0;
};

struct TileRevisionPublication {
  TileKey key{};
  MaterializationQuality quality = MaterializationQuality::kSettled;
  std::span<const std::uint16_t> pixels{};
};

struct ViewRequest {
  ZoomLevel zoom = ZoomLevel::k25Percent;
  PixelRect level_pixels{};
};

struct ViewCompositionStats {
  DocumentRevision revision{};
  std::size_t tile_pixels = 0;
  std::size_t fallback_pixels = 0;
  std::size_t settled_tiles = 0;
  std::size_t exact_tiles = 0;
  std::size_t fallback_tiles = 0;
};

class MaterializedSlotStorage {
 public:
  MaterializedSlotStorage() = default;

 private:
  friend class MaterializedCanvas;

  TileKey key_{};
  DocumentRevision revision_{};
  SlotGeneration generation_{};
  MaterializationQuality quality_ = MaterializationQuality::kSettled;
  std::uint64_t last_use_ = 0;
  std::uint32_t pin_count_ = 0;
  bool occupied_ = false;
};

class MaterializedCanvas;

// A pin must not outlive its owning MaterializedCanvas.
class PinnedSource {
 public:
  ~PinnedSource();
  PinnedSource(const PinnedSource&) = delete;
  PinnedSource& operator=(const PinnedSource&) = delete;
  PinnedSource(PinnedSource&& other) noexcept;
  PinnedSource& operator=(PinnedSource&& other) noexcept;

  [[nodiscard]] const SourceSelection& source() const;
  [[nodiscard]] bool valid() const;
  void reset();

 private:
  friend class MaterializedCanvas;
  PinnedSource(MaterializedCanvas& owner, const SourceSelection& source);

  MaterializedCanvas* owner_ = nullptr;
  SourceSelection source_{};
};

class MaterializedCanvas {
 public:
  // All spans are caller-owned and must outlive this object. Slot elements must
  // be constructed before the span is passed; the constructor resets their state.
  MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                     std::span<MaterializedSlotStorage> slots, std::span<std::uint16_t> tile_pixels,
                     DocumentRevision initial_revision = {});

  [[nodiscard]] bool ready() const;
  [[nodiscard]] DocumentRevision current_revision() const;
  [[nodiscard]] std::size_t slot_capacity() const;
  [[nodiscard]] std::span<const std::uint16_t> overview_pixels() const;

  // Logically commits a complete overview and its revision in one call. Once
  // an overview is valid, each publication must advance the revision and use
  // a distinct source buffer. Callers must serialize all canvas operations.
  [[nodiscard]] bool publish_overview(DocumentRevision revision,
                                      std::span<const std::uint16_t> pixels);
  // Commits exactly the next document revision. Tiles intersecting the
  // conservative world bounds are affected at every zoom; affected resident
  // tiles without a publication become overview fallback. Every input is
  // validated before owned pixels or identities change.
  [[nodiscard]] bool commit_incremental_revision(
      DocumentRevision revision, std::span<const std::uint16_t> next_overview_pixels,
      PixelRect affected_world_bounds, std::span<const TileRevisionPublication> tile_publications);
  [[nodiscard]] std::optional<std::size_t> publish_tile(TileKey key, DocumentRevision revision,
                                                        MaterializationQuality quality,
                                                        std::span<const std::uint16_t> pixels);
  [[nodiscard]] std::optional<SourceSelection> lookup(TileKey key) const;
  [[nodiscard]] std::optional<PinnedSource> pin(TileKey key);
  [[nodiscard]] bool validate(const PinnedSource& source) const;
  [[nodiscard]] std::size_t pins_outstanding() const;
  [[nodiscard]] bool mark_used(TileKey key);
  [[nodiscard]] std::optional<ViewCompositionStats> compose_view(
      const ViewRequest& request, std::span<std::uint16_t> destination);
  // Returns intersecting current-revision resident keys. Fails rather than
  // returning a partial list when output is too small or bounds are invalid.
  [[nodiscard]] std::optional<std::size_t> resident_tiles_intersecting(
      PixelRect world_bounds, std::span<TileKey> output) const;
  // Copies one current resident tile without exposing mutable pool storage.
  [[nodiscard]] bool copy_resident_tile(TileKey key, std::span<std::uint16_t> destination) const;

 private:
  friend class PinnedSource;
  [[nodiscard]] bool validate_selection(const SourceSelection& source) const;
  [[nodiscard]] bool release_pin(const SourceSelection& source);
  [[nodiscard]] std::optional<std::size_t> find_tile(TileKey key) const;
  [[nodiscard]] std::optional<std::size_t> choose_slot() const;
  [[nodiscard]] SourceSelection select_overview(TileKey requested) const;
  [[nodiscard]] bool valid_incremental_revision(
      DocumentRevision revision, std::span<const std::uint16_t> next_overview_pixels,
      PixelRect affected_world_bounds,
      std::span<const TileRevisionPublication> tile_publications) const;
  void write_tile(std::size_t slot_index, const TileRevisionPublication& publication,
                  DocumentRevision revision);
  struct CompositionContext {
    ViewRequest request{};
    std::span<std::uint16_t> destination{};
    ViewCompositionStats stats{};
    int view_width = 0;
  };

  [[nodiscard]] SourceSelection select_tile(TileKey requested, std::size_t slot_index) const;
  [[nodiscard]] bool valid_view(const ViewRequest& request, std::size_t destination_size) const;
  [[nodiscard]] bool has_complete_source(const ViewRequest& request) const;
  [[nodiscard]] std::optional<ViewCompositionStats> compose_overview_view(
      const ViewRequest& request, std::span<std::uint16_t> destination) const;
  void compose_tile(TileKey key, CompositionContext& context);
  [[nodiscard]] SlotGeneration take_generation();
  void touch(MaterializedSlotStorage& slot);

  std::span<std::uint16_t> overview_pixels_;
  std::span<MaterializedSlotStorage> slots_;
  std::span<std::uint16_t> tile_pixels_;
  DocumentRevision current_revision_{};
  DocumentRevision overview_revision_{};
  SlotGeneration overview_generation_{};
  std::uint32_t next_generation_ = 1;
  std::uint64_t use_clock_ = 0;
  std::uint32_t next_pin_token_ = 1;
  std::uint32_t overview_pin_count_ = 0;
  bool overview_valid_ = false;
};

[[nodiscard]] int zoom_percent(ZoomLevel zoom);
[[nodiscard]] TileGrid tile_grid(ZoomLevel zoom);
[[nodiscard]] bool valid_tile_key(TileKey key);
[[nodiscard]] std::optional<TileKey> tile_key_for_world(ZoomLevel zoom, WorldPoint point);
[[nodiscard]] PixelRect tile_pixel_bounds(TileKey key);
[[nodiscard]] PixelRect overview_source_bounds(TileKey key);

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_MATERIALIZED_CANVAS_H
