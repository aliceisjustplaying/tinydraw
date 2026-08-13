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
inline constexpr std::size_t kTileProducerOperationBatch = 64;
inline constexpr std::size_t kTileProducerSampleBatch = 96;
// Conservative projected bounding-box work budget. It complements the sample
// cap because raster cost also grows with segment length and radius.
inline constexpr std::size_t kTileProducerRasterWorkBatch = 20'000;

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
  // Changes the authoritative uniform snapshot after a coordinated log/canvas
  // reset. Rejected unless both authorities are empty and at this revision.
  [[nodiscard]] bool reset_uniform_baseline(DocumentRevision revision,
                                            std::uint16_t color = 0xFFFFU);

 private:
  struct GroupPublication {
    PixelRect level_bounds{};
    std::size_t tiles_published = 0;
    bool complete = false;
  };

  struct SegmentBatch {
    std::size_t segments = 0;
    std::size_t raster_work = 0;
  };

  struct ActiveGroup {
    ViewRequest view{};
    TileKey origin{};
    PixelRect bounds{};
    OperationLogEpoch epoch{};
    DocumentRevision revision{};
    std::size_t first_operation = 0;
    std::size_t operation_count = 0;
    std::size_t next_operation = 0;
    // Next segment endpoint within the active operation; one means the first
    // segment. Single-sample operations are handled as one bounded unit.
    std::size_t next_sample = 1;
    bool active = false;
  };

  [[nodiscard]] static bool valid_view(const ViewRequest& view);
  [[nodiscard]] bool tile_satisfies(TileKey key, MaterializationQuality quality) const;
  [[nodiscard]] std::optional<std::size_t> visible_tiles_remaining(
      const ViewRequest& view, MaterializationQuality quality) const;
  [[nodiscard]] std::optional<TileKey> choose_missing_group(const ViewRequest& view) const;
  [[nodiscard]] bool start_group(const ViewRequest& view, TileKey group_origin);
  [[nodiscard]] SegmentBatch choose_segment_batch(const StoredOperation& operation,
                                                  std::size_t sample_budget,
                                                  std::size_t raster_work_budget) const;
  [[nodiscard]] bool render_active_operation(const StoredOperation& operation,
                                             TileProductionStep& result,
                                             std::size_t& operations_consumed,
                                             std::size_t& samples_consumed,
                                             std::size_t& raster_work_consumed);
  [[nodiscard]] std::optional<TileProductionStep> render_active_batch();
  [[nodiscard]] std::optional<GroupPublication> publish_next_group_tile(PixelRect rendered_bounds,
                                                                        PixelRect visible_bounds,
                                                                        ZoomLevel zoom,
                                                                        DocumentRevision revision);
  OperationLog& log_;
  MaterializedCanvas& canvas_;
  TileProducerWorkspace workspace_;
  DocumentRevision baseline_revision_{};
  std::uint16_t baseline_color_ = 0xFFFFU;
  ActiveGroup active_group_{};
};

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_TILE_PRODUCER_H
