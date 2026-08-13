#ifndef TINYDRAW_PRODUCTION_TILE_PRODUCER_H
#define TINYDRAW_PRODUCTION_TILE_PRODUCER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/production/incremental_rasterizer.h"
#include "tinydraw/production/operation_log.h"

namespace tinydraw::production {

inline constexpr int kTileProducerColumns = 2;
inline constexpr int kTileProducerRows = 2;
inline constexpr int kTileProducerWidth = kTileProducerColumns * kTileWidth;
inline constexpr int kTileProducerHeight = kTileProducerRows * kTileHeight;
inline constexpr std::size_t kTileProducerPixels =
    static_cast<std::size_t>(kTileProducerWidth) * kTileProducerHeight;

struct TileProducerWorkspace {
  // Row-major 128x128 supertask surface.
  std::span<std::uint16_t> supertask_pixels{};
  // Tightly packed publication scratch for one 64x64-or-smaller edge tile.
  std::span<std::uint16_t> packed_tile_pixels{};
};

struct TileProductionStep {
  PixelRect level_bounds{};
  std::size_t operations_scanned = 0;
  std::size_t operations_rendered = 0;
  std::size_t tiles_published = 0;
  std::size_t visible_tiles_remaining = 0;
  bool complete = false;
};

// Cold-produces provisional world-aligned tiles from a uniform baseline and a
// painter-ordered replay range. This is the Gate 1 raw-source producer, not a
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

 private:
  [[nodiscard]] bool valid_view(const ViewRequest& view) const;
  [[nodiscard]] bool tile_satisfies(TileKey key) const;
  [[nodiscard]] std::optional<TileKey> choose_missing_group(const ViewRequest& view) const;
  [[nodiscard]] std::optional<TileProductionStep> render_group(const ViewRequest& view,
                                                               TileKey group_origin);

  OperationLog& log_;
  MaterializedCanvas& canvas_;
  TileProducerWorkspace workspace_;
  DocumentRevision baseline_revision_{};
  std::uint16_t baseline_color_ = 0xFFFFU;
};

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_TILE_PRODUCER_H
