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
  std::size_t slot_index = 0;
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
  bool occupied_ = false;
};

class MaterializedCanvas {
 public:
  MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                     std::span<MaterializedSlotStorage> slots,
                     DocumentRevision initial_revision = {});

  [[nodiscard]] bool ready() const;
  [[nodiscard]] DocumentRevision current_revision() const;
  [[nodiscard]] std::size_t slot_capacity() const;
  [[nodiscard]] std::span<const std::uint16_t> overview_pixels() const;

  [[nodiscard]] bool advance_revision(DocumentRevision revision);
  [[nodiscard]] bool publish_overview(DocumentRevision revision);
  [[nodiscard]] std::optional<std::size_t> publish_tile(TileKey key, DocumentRevision revision,
                                                        MaterializationQuality quality);
  [[nodiscard]] std::optional<SourceSelection> lookup(TileKey key);

 private:
  [[nodiscard]] std::optional<std::size_t> find_tile(TileKey key) const;
  [[nodiscard]] std::size_t choose_slot() const;
  [[nodiscard]] SourceSelection select_overview(TileKey requested) const;
  [[nodiscard]] SourceSelection select_tile(TileKey requested, std::size_t slot_index) const;
  void touch(MaterializedSlotStorage& slot);

  std::span<std::uint16_t> overview_pixels_;
  std::span<MaterializedSlotStorage> slots_;
  DocumentRevision current_revision_{};
  DocumentRevision overview_revision_{};
  SlotGeneration overview_generation_{};
  std::uint32_t next_generation_ = 1;
  std::uint64_t use_clock_ = 0;
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
