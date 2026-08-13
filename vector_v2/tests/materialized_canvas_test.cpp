#include "tinydraw/vector_v2/materialized_canvas.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "tinydraw/vector_v2/memory_layout.h"

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("production geometry has fixed world overview and committed zoom identities") {
  CHECK(vector_v2::kWorldWidth == 1472);
  CHECK(vector_v2::kWorldHeight == 1792);
  CHECK(vector_v2::kOverviewWidth == 368);
  CHECK(vector_v2::kOverviewHeight == 448);
  CHECK(vector_v2::kOverviewBytes == 329'728U);
  CHECK(vector_v2::kMaximumVisibleTiles == 56U);

  CHECK(vector_v2::zoom_percent(vector_v2::ZoomLevel::k25Percent) == 25);
  CHECK(vector_v2::zoom_percent(vector_v2::ZoomLevel::k50Percent) == 50);
  CHECK(vector_v2::zoom_percent(vector_v2::ZoomLevel::k100Percent) == 100);
  CHECK(vector_v2::zoom_percent(vector_v2::ZoomLevel::k200Percent) == 200);
  CHECK(vector_v2::zoom_percent(vector_v2::ZoomLevel::k400Percent) == 400);
}

TEST_CASE("production memory plan records every fixed-capacity region") {
  CHECK(sizeof(vector_v2::CompactOperationSample) == 8U);
  CHECK(vector_v2::kOverviewPublicationBytes == 329'728U);
  CHECK(vector_v2::kTileSlotCount == 320U);
  CHECK(vector_v2::kTileSlotCount >= 5U * vector_v2::kMaximumVisibleTiles);
  CHECK(vector_v2::kTilePoolBytes == 2'621'440U);
  CHECK(vector_v2::kTileMetadataBytes ==
        vector_v2::kTileSlotCount * sizeof(vector_v2::MaterializedSlotStorage) +
            vector_v2::kMaterializedTileIdentityCount *
                sizeof(vector_v2::MaterializedUniformStorage) +
            vector_v2::kOccupancyBytes);
  CHECK(vector_v2::kOperationStorageBytes == 720'000U);
  CHECK(vector_v2::kLodStorageBytes == 668'000U);
  CHECK(vector_v2::kRendererWorkspaceBytes == 163'840U);
  CHECK(vector_v2::kDisplayWorkspaceBytes == 103'040U);
  CHECK(vector_v2::kExternalPlanBytes == 5'004'632U);
  CHECK(vector_v2::kTargetContiguousReserveBytes == 1'572'864U);
}

TEST_CASE("world points map to bounded world-aligned tiles") {
  const auto first = vector_v2::tile_key_for_world(vector_v2::ZoomLevel::k100Percent, {0, 0});
  REQUIRE(first.has_value());
  CHECK(first->column == 0U);
  CHECK(first->row == 0U);

  const auto last = vector_v2::tile_key_for_world(
      vector_v2::ZoomLevel::k100Percent, {vector_v2::kWorldWidth - 1, vector_v2::kWorldHeight - 1});
  REQUIRE(last.has_value());
  CHECK(last->column == 22U);
  CHECK(last->row == 27U);
  CHECK(vector_v2::tile_grid(vector_v2::ZoomLevel::k100Percent) == vector_v2::TileGrid{23, 28});
  CHECK(vector_v2::tile_pixel_bounds(*last) == vector_v2::PixelRect{1408, 1728, 1472, 1792});

  CHECK_FALSE(vector_v2::tile_key_for_world(vector_v2::ZoomLevel::k25Percent, {0, 0}));
  CHECK_FALSE(vector_v2::tile_key_for_world(vector_v2::ZoomLevel::k100Percent, {-1, 0}));
  CHECK_FALSE(vector_v2::tile_key_for_world(vector_v2::ZoomLevel::k100Percent,
                                            {vector_v2::kWorldWidth, 0}));
}

TEST_CASE("world bounds map to complete bounded overview regions") {
  CHECK(vector_v2::overview_bounds_for_world({0, 0, 64, 64}) == vector_v2::PixelRect{0, 0, 16, 16});
  CHECK(vector_v2::overview_bounds_for_world({1, 1, 63, 63}) == vector_v2::PixelRect{0, 0, 16, 16});
  CHECK(vector_v2::overview_bounds_for_world(
            {0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight}) ==
        vector_v2::PixelRect{0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight});
  CHECK(vector_v2::overview_bounds_for_world({0, 0, 0, 1}) == vector_v2::PixelRect{});
}

TEST_CASE("edge tiles clip to each zoom extent") {
  const vector_v2::TileGrid grid = vector_v2::tile_grid(vector_v2::ZoomLevel::k50Percent);
  CHECK(grid == vector_v2::TileGrid{12, 14});
  const vector_v2::TileKey edge{
      .zoom = vector_v2::ZoomLevel::k50Percent,
      .column = 11,
      .row = 13,
  };
  CHECK(vector_v2::tile_pixel_bounds(edge) == vector_v2::PixelRect{704, 832, 736, 896});
  CHECK(vector_v2::overview_source_bounds(edge) == vector_v2::PixelRect{352, 416, 368, 448});
  CHECK_FALSE(vector_v2::valid_tile_key({vector_v2::ZoomLevel::k50Percent, 12, 13}));
}

TEST_CASE("canvas rejects invalid storage and non-monotonic overview publication") {
  std::array<std::uint16_t, 4> too_small{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> invalid_tile_pixels{};
  vector_v2::MaterializedCanvas invalid(too_small, slots, invalid_tile_pixels);
  CHECK_FALSE(invalid.ready());
  CHECK_FALSE(invalid.publish_overview({1}, too_small));

  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_pixels);
  CHECK(canvas.ready());
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(canvas.publish_overview({1}, overview));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{1});
  CHECK_FALSE(canvas.publish_overview({1}, overview));
  CHECK_FALSE(canvas.publish_overview({0}, overview));
}

TEST_CASE("lookup never selects a stale tile or stale overview") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 2, 3};

  CHECK_FALSE(canvas.lookup(key));
  CHECK(canvas.publish_overview({0}, overview));
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == vector_v2::SourceKind::kOverview);
  CHECK(fallback->identity.revision == vector_v2::DocumentRevision{0});
  CHECK(fallback->identity.provenance == vector_v2::MaterializationProvenance::kCompleteOverview);

  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  CHECK(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  const auto tile = canvas.lookup(key);
  REQUIRE(tile.has_value());
  CHECK(tile->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(tile->identity.quality == vector_v2::MaterializationQuality::kSettled);

  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  CHECK(canvas.publish_overview({1}, revised_overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> revised_tile{};
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kExact, revised_tile));
  const auto revised_fallback = canvas.lookup(key);
  REQUIRE(revised_fallback.has_value());
  CHECK(revised_fallback->kind == vector_v2::SourceKind::kOverview);
  CHECK(revised_fallback->identity.revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("fixed-capacity slots replace the least recently used slot") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const vector_v2::TileKey first{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey second{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey third{vector_v2::ZoomLevel::k100Percent, 2, 0};

  REQUIRE(canvas.publish_overview({0}, overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  REQUIRE(
      canvas.publish_tile(first, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  REQUIRE(canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kSettled,
                              published_tile));
  REQUIRE(canvas.lookup(first));
  REQUIRE(canvas.mark_used(first));
  REQUIRE(
      canvas.publish_tile(third, {0}, vector_v2::MaterializationQuality::kExact, published_tile));

  CHECK(canvas.lookup(first)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("cache retains every zoom viewport across a disjoint pan fill") {
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto slots =
      std::make_unique<std::array<vector_v2::MaterializedSlotStorage, vector_v2::kTileSlotCount>>();
  auto tile_storage = std::make_unique<
      std::array<std::uint16_t, vector_v2::kTileSlotCount * vector_v2::kTilePixels>>();
  vector_v2::MaterializedCanvas canvas(*overview, *slots, *tile_storage);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};

  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    vector_v2::MaterializationQuality::kImmediate, tile));
      }
    }
  }

  // Fill a second disjoint 400% viewport after the four zoom views. The first
  // viewport at every zoom must still survive.
  for (std::uint16_t row = 8; row < 16; ++row) {
    for (std::uint16_t column = 8; column < 15; ++column) {
      REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, column, row}, {0},
                                  vector_v2::MaterializationQuality::kImmediate, tile));
    }
  }
  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        const vector_v2::TileKey key{zoom, column, row};
        CHECK(canvas.lookup(key)->kind == vector_v2::SourceKind::kTileSlot);
        REQUIRE(canvas.mark_used(key));
      }
    }
  }

  // Consume the remaining slots, then prove the next publication evicts
  // exactly the least-recently-used entry rather than an arbitrary viewport.
  // The disjoint destination predates the mark_used calls above, so its first
  // tile is now the oldest resident entry.
  constexpr std::size_t kRetainedFootprints = 5U;
  constexpr std::size_t kAdditionalSlots =
      vector_v2::kTileSlotCount - kRetainedFootprints * vector_v2::kMaximumVisibleTiles;
  for (std::uint16_t column = 0; column < kAdditionalSlots; ++column) {
    REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, column, 16}, {0},
                                vector_v2::MaterializationQuality::kImmediate, tile));
  }
  const vector_v2::TileKey oldest{vector_v2::ZoomLevel::k400Percent, 8, 8};
  CHECK(canvas.lookup(oldest)->kind == vector_v2::SourceKind::kTileSlot);
  REQUIRE(canvas.publish_tile(
      {vector_v2::ZoomLevel::k400Percent, static_cast<std::uint16_t>(kAdditionalSlots), 16}, {0},
      vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(oldest)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup({vector_v2::ZoomLevel::k50Percent, 0, 0})->kind ==
        vector_v2::SourceKind::kTileSlot);
  CHECK(canvas
            .lookup({vector_v2::ZoomLevel::k400Percent,
                     static_cast<std::uint16_t>(kAdditionalSlots), 16})
            ->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("composing a tile refreshes its LRU recency") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey first{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey second{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey third{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_tile(first, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  REQUIRE(canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}}, composed));

  REQUIRE(canvas.publish_tile(third, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(first)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("paper catalog consumes no raw slots and composes direct fills") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 2, 3};

  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kImmediate));
  CHECK(canvas.uniform_capacity() == vector_v2::kMaterializedTileIdentityCount);
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kUniform);
  CHECK_FALSE(source->slot_index.has_value());
  CHECK(source->uniform_color == 0xFFFFU);

  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  const auto stats = canvas.compose_view({.zoom = vector_v2::ZoomLevel::k100Percent,
                                          .level_pixels = vector_v2::tile_pixel_bounds(key)},
                                         composed);
  REQUIRE(stats.has_value());
  CHECK(stats->uniform_pixels == composed.size());
  CHECK(stats->fallback_pixels == 0U);
  CHECK(stats->immediate_tiles == 1U);
  CHECK(std::all_of(composed.begin(), composed.end(),
                    [](std::uint16_t pixel) { return pixel == 0xFFFFU; }));

  std::array<std::uint16_t, vector_v2::kTilePixels> raw{};
  raw.fill(0x1234U);
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, raw));
  CHECK(canvas.lookup(key)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("incremental mutation reclassifies raw tiles without replaying learned paper") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> raw{};
  raw.front() = 0x1234U;
  std::array<std::uint16_t, vector_v2::kTilePixels> uniform{};
  uniform.fill(0xFFFFU);
  std::array<std::uint16_t, 16U * 16U> next_overview{};
  vector_v2::MaterializedCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey changed{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey learned_paper{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey replacement{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(changed, {0}, vector_v2::MaterializationQuality::kImmediate, raw));
  REQUIRE(
      canvas.publish_uniform(learned_paper, {0}, vector_v2::MaterializationQuality::kImmediate));

  std::array<vector_v2::TileKey, 1> affected{};
  REQUIRE(canvas.resident_tiles_intersecting({0, 0, 128, 64}, affected) == 1U);
  CHECK(affected.front() == changed);
  const std::array publication{vector_v2::TileRevisionPublication{
      .key = changed,
      .quality = vector_v2::MaterializationQuality::kImmediate,
      .pixels = uniform,
  }};
  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = next_overview}, {0, 0, 64, 64}, publication));
  CHECK(canvas.lookup(changed)->kind == vector_v2::SourceKind::kUniform);
  CHECK(canvas.lookup(learned_paper)->kind == vector_v2::SourceKind::kUniform);

  REQUIRE(
      canvas.publish_tile(replacement, {1}, vector_v2::MaterializationQuality::kImmediate, raw));
  CHECK(canvas.lookup(replacement)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(changed)->kind == vector_v2::SourceKind::kUniform);
}

TEST_CASE("catalog and occupancy storage are never accepted as external workspace") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);

  CHECK_FALSE(canvas.accepts_external_workspace(std::as_bytes(std::span(*uniforms))));
  CHECK_FALSE(canvas.accepts_external_workspace(std::as_bytes(std::span(occupancy))));
}

TEST_CASE("snapshot restore derives conservative occupancy from non-paper pixels") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  snapshot.fill(0xFFFFU);
  snapshot[3U * vector_v2::kOverviewWidth + 2U] = 0x001FU;
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);

  REQUIRE(canvas.restore_snapshot({4}, snapshot));
  CHECK_FALSE(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 0, 0}));
  CHECK(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 1, 0}));
  CHECK(canvas.overview_pixels()[3U * vector_v2::kOverviewWidth + 2U] == 0x001FU);
}

TEST_CASE("invalidated learned paper composes updated overview until relearned") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, 16U * 16U> next_overview{};
  next_overview.fill(0x1234U);
  vector_v2::MaterializedCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey paper{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_uniform(paper, {0}, vector_v2::MaterializationQuality::kImmediate));

  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = next_overview}, {0, 0, 64, 64}, {}));
  CHECK(canvas.lookup(paper)->kind == vector_v2::SourceKind::kOverview);
  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}}, composed);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == composed.size());
  CHECK(std::all_of(composed.begin(), composed.end(),
                    [](std::uint16_t pixel) { return pixel == 0x1234U; }));
}

TEST_CASE("occupancy is conservative across every zoom and mutation invalidates learned paper") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  snapshot.fill(0xFFFFU);
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  REQUIRE(canvas.restore_snapshot({0}, snapshot));
  const vector_v2::TileKey at_50{vector_v2::ZoomLevel::k50Percent, 0, 0};
  const vector_v2::TileKey at_100{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey at_200{vector_v2::ZoomLevel::k200Percent, 1, 1};
  const vector_v2::TileKey at_400{vector_v2::ZoomLevel::k400Percent, 3, 3};
  CHECK(canvas.certainly_paper(at_50));
  CHECK(canvas.certainly_paper(at_100));
  CHECK(canvas.certainly_paper(at_200));
  CHECK(canvas.certainly_paper(at_400));
  REQUIRE(canvas.publish_uniform(at_100, {0}, vector_v2::MaterializationQuality::kImmediate));

  std::array<std::uint16_t, 4U * 4U> next_overview{};
  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {12, 12, 16, 16}, .pixels = next_overview}, {48, 48, 64, 64}, {}));
  CHECK_FALSE(canvas.certainly_paper(at_50));
  CHECK_FALSE(canvas.certainly_paper(at_100));
  CHECK_FALSE(canvas.certainly_paper(at_200));
  CHECK_FALSE(canvas.certainly_paper(at_400));
  CHECK(canvas.lookup(at_100)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 2, 0}));
}

TEST_CASE("tile identity catalog densely covers every tiled zoom") {
  std::array<bool, vector_v2::kMaterializedTileIdentityCount> seen{};
  std::size_t count = 0;
  for (const auto zoom : {vector_v2::ZoomLevel::k50Percent, vector_v2::ZoomLevel::k100Percent,
                          vector_v2::ZoomLevel::k200Percent, vector_v2::ZoomLevel::k400Percent}) {
    const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
    for (int row = 0; row < grid.rows; ++row) {
      for (int column = 0; column < grid.columns; ++column) {
        const auto index = vector_v2::tile_identity_index(
            {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
        REQUIRE(index.has_value());
        REQUIRE(*index < seen.size());
        CHECK_FALSE(seen[*index]);
        seen[*index] = true;
        ++count;
      }
    }
  }
  CHECK(count == vector_v2::kMaterializedTileIdentityCount);
  CHECK(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
}

TEST_CASE("same-revision publication cannot downgrade immediate settled or exact quality") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k200Percent, 4, 5};

  REQUIRE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, published_tile));
  const auto immediate = canvas.lookup(key);
  REQUIRE(immediate.has_value());
  REQUIRE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  const auto settled = canvas.lookup(key);
  REQUIRE(settled.has_value());
  CHECK(settled->identity.generation.value > immediate->identity.generation.value);
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, published_tile));
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kExact, published_tile));
  const auto exact = canvas.lookup(key);
  REQUIRE(exact.has_value());
  CHECK(exact->slot_index == settled->slot_index);
  CHECK(exact->identity.generation.value > settled->identity.generation.value);
  CHECK(exact->identity.quality == vector_v2::MaterializationQuality::kExact);
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, published_tile));
}

TEST_CASE("view composition uses current tiles and overview for every miss") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  for (int y = 0; y < vector_v2::kOverviewHeight; ++y) {
    for (int x = 0; x < vector_v2::kOverviewWidth; ++x) {
      overview[static_cast<std::size_t>(y * vector_v2::kOverviewWidth + x)] =
          static_cast<std::uint16_t>(y * vector_v2::kOverviewWidth + x);
    }
  }
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  published_tile.fill(0x1234U);
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k100Percent, 1, 0}, {0},
                              vector_v2::MaterializationQuality::kSettled, published_tile));

  std::array<std::uint16_t, 128U * 32U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 128, 32}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->settled_tiles == 1U);
  CHECK(stats->fallback_tiles == 1U);
  CHECK(stats->tile_pixels == 64U * 32U);
  CHECK(stats->fallback_pixels == 64U * 32U);
  CHECK(destination[63] == overview[15]);
  CHECK(destination[64] == 0x1234U);
}

TEST_CASE("compose view refuses only when no current source exists") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  std::array<std::uint16_t, 64U * 32U> destination{};
  const vector_v2::ViewRequest request{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 64, 32},
  };

  CHECK_FALSE(canvas.compose_view(request, destination));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k100Percent, 0, 0}, {0},
                              vector_v2::MaterializationQuality::kExact, published_tile));
  const auto tile_only = canvas.compose_view(request, destination);
  REQUIRE(tile_only.has_value());
  CHECK(tile_only->exact_tiles == 1U);
  CHECK(tile_only->fallback_tiles == 0U);

  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  const auto fallback = canvas.compose_view(request, destination);
  REQUIRE(fallback.has_value());
  CHECK(fallback->fallback_tiles == 1U);
  CHECK(fallback->revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("25 percent view copies the complete overview directly") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview[10 * vector_v2::kOverviewWidth + 20] = 0x4567U;
  std::array<vector_v2::MaterializedSlotStorage, 0> slots{};
  std::array<std::uint16_t, 0> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  std::array<std::uint16_t, 16U * 8U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k25Percent, .level_pixels = {20, 10, 36, 18}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->overview_pixels == destination.size());
  CHECK(stats->fallback_pixels == 0U);
  CHECK(stats->fallback_tiles == 0U);
  CHECK(destination.front() == 0x4567U);
}

TEST_CASE("unaligned view requires every grid tile when no overview is current") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  published_tile.fill(0x1234U);
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k100Percent, 0, 0}, {0},
                              vector_v2::MaterializationQuality::kExact, published_tile));

  std::array<std::uint16_t, 4> destination{};
  CHECK_FALSE(canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {63, 0, 65, 2}}, destination));
}

TEST_CASE("overview publication commits each revision only once") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x2222U);
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kExact, tile));

  std::array<std::uint16_t, vector_v2::kOverviewPixels> replacement{};
  replacement.fill(0x3333U);
  CHECK_FALSE(canvas.publish_overview({0}, replacement));
  std::array<std::uint16_t, 1> composed{};
  REQUIRE(canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {64, 0, 65, 1}}, composed));
  CHECK(composed.front() == 0U);
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(source->identity.revision == vector_v2::DocumentRevision{0});
}

TEST_CASE("transactional overview publication prevents stale fallback labeling") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x2222U);
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k50Percent, 10, 0}, {0},
                              vector_v2::MaterializationQuality::kExact,
                              std::span(tile).first(vector_v2::kTilePixels)));

  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0x3333U);
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k50Percent, 10, 0}, {1},
                              vector_v2::MaterializationQuality::kExact,
                              std::span(tile).first(vector_v2::kTilePixels)));
  std::array<std::uint16_t, 64U * 64U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k50Percent, .level_pixels = {672, 0, 736, 64}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->revision == vector_v2::DocumentRevision{1});
  CHECK(destination[31] == 0x2222U);
  CHECK(destination[32] == 0x3333U);
}

TEST_CASE("pinned sources cannot be replaced and validate only while pinned") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kExact, tile));

  const auto unpinned_lookup = canvas.lookup(key);
  REQUIRE(unpinned_lookup.has_value());
  auto pinned_tile = canvas.pin(key);
  REQUIRE(pinned_tile.has_value());
  CHECK(canvas.validate(*pinned_tile));
  auto second_tile_pin = canvas.pin(key);
  REQUIRE(second_tile_pin.has_value());
  CHECK(canvas.pins_outstanding() == 2U);
  second_tile_pin->reset();
  CHECK(canvas.pins_outstanding() == 1U);
  CHECK(unpinned_lookup->pin_token == 0U);
  CHECK_FALSE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  CHECK_FALSE(canvas.publish_overview({1}, revised_overview));
  pinned_tile->reset();
  CHECK(canvas.pins_outstanding() == 0U);
  CHECK_FALSE(pinned_tile->valid());
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  CHECK_FALSE(canvas.validate(*pinned_tile));

  const vector_v2::TileKey missing{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const auto unpinned_overview = canvas.lookup(missing);
  REQUIRE(unpinned_overview.has_value());
  auto pinned_overview = canvas.pin(missing);
  REQUIRE(pinned_overview.has_value());
  CHECK(pinned_overview->source().kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.validate(*pinned_overview));
  auto second_overview_pin = canvas.pin(missing);
  REQUIRE(second_overview_pin.has_value());
  CHECK(canvas.pins_outstanding() == 2U);
  second_overview_pin->reset();
  CHECK(canvas.pins_outstanding() == 1U);
  CHECK(unpinned_overview->pin_token == 0U);
  CHECK_FALSE(canvas.publish_overview({2}, overview));
  pinned_overview->reset();
  CHECK(canvas.pins_outstanding() == 0U);
  CHECK_FALSE(pinned_overview->valid());
}

TEST_CASE("all pinned slots prevent eviction until one is released") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey first{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey second{vector_v2::ZoomLevel::k100Percent, 1, 0};
  REQUIRE(canvas.publish_tile(first, {0}, vector_v2::MaterializationQuality::kExact, tile));
  auto pinned = canvas.pin(first);
  REQUIRE(pinned.has_value());
  CHECK_FALSE(canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kExact, tile));
  pinned->reset();
  CHECK_FALSE(pinned->valid());
  REQUIRE(canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kExact, tile));
  CHECK_FALSE(canvas.validate(*pinned));
}

TEST_CASE("discarding tiles preserves current overview and fails while pinned") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({2}, overview));
  REQUIRE(canvas.publish_tile(key, {2}, vector_v2::MaterializationQuality::kImmediate, tile));
  auto pinned = canvas.pin(key);
  REQUIRE(pinned.has_value());
  CHECK_FALSE(canvas.discard_tiles());
  CHECK(canvas.lookup(key)->kind == vector_v2::SourceKind::kTileSlot);
  pinned->reset();

  REQUIRE(canvas.discard_tiles());
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == vector_v2::SourceKind::kOverview);
  CHECK(fallback->identity.revision == vector_v2::DocumentRevision{2});
}

TEST_CASE("compose view rejects destinations that alias owned source storage") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  CHECK_FALSE(
      canvas.compose_view({.zoom = vector_v2::ZoomLevel::k25Percent, .level_pixels = {0, 0, 16, 8}},
                          std::span(overview).first(16U * 8U)));
  CHECK_FALSE(canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}},
      std::span(tile_storage)));
}

TEST_CASE("walk-shaped bottom strip composes from the complete overview") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  for (std::size_t index = 0; index < overview.size(); ++index) {
    overview[index] = static_cast<std::uint16_t>(index);
  }
  std::array<vector_v2::MaterializedSlotStorage, 0> slots{};
  std::array<std::uint16_t, 0> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  std::array<std::uint16_t, 368U * 22U> strip{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 350, 368, 372}}, strip);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == strip.size());
  CHECK(strip.front() == overview[87 * vector_v2::kOverviewWidth]);
  CHECK(strip.back() == overview[92 * vector_v2::kOverviewWidth + 91]);
}

TEST_CASE("clipped 50 percent tile advertises its padded slot stride") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, 32U * 64U> edge_pixels{};
  for (std::size_t index = 0; index < edge_pixels.size(); ++index) {
    edge_pixels[index] = static_cast<std::uint16_t>(index);
  }
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey edge{vector_v2::ZoomLevel::k50Percent, 11, 13};
  CHECK_FALSE(canvas.publish_tile(edge, {0}, vector_v2::MaterializationQuality::kSettled,
                                  std::span(tile_storage)));
  REQUIRE(canvas.publish_tile(edge, {0}, vector_v2::MaterializationQuality::kSettled, edge_pixels));
  const auto source = canvas.lookup(edge);
  REQUIRE(source.has_value());
  CHECK(source->source_pixels == vector_v2::PixelRect{0, 0, 32, 64});
  CHECK(source->source_stride == vector_v2::kTileWidth);
  REQUIRE(source->slot_index.has_value());
  CHECK(tile_storage[31] == edge_pixels[31]);
  CHECK(tile_storage[64] == edge_pixels[32]);

  std::array<std::uint16_t, 32U * 64U> composed{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k50Percent, .level_pixels = {704, 832, 736, 896}}, composed);
  REQUIRE(stats.has_value());
  CHECK(composed == edge_pixels);
}

TEST_CASE("incremental revision updates affected tiles and carries unaffected tiles") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, vector_v2::kOverviewPixels> next_overview{};
  next_overview.fill(0x2222U);
  std::array<vector_v2::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> original_tile{};
  original_tile.fill(0x3333U);
  std::array<std::uint16_t, vector_v2::kTilePixels> updated_tile{};
  updated_tile.fill(0x4444U);
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey updated{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey invalidated{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey carried{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(updated, {0}, vector_v2::MaterializationQuality::kSettled,
                              original_tile));
  REQUIRE(canvas.publish_tile(invalidated, {0}, vector_v2::MaterializationQuality::kExact,
                              original_tile));
  REQUIRE(
      canvas.publish_tile(carried, {0}, vector_v2::MaterializationQuality::kExact, original_tile));
  const auto carried_before = canvas.lookup(carried);
  REQUIRE(carried_before.has_value());

  const std::array publications{vector_v2::TileRevisionPublication{
      .key = updated,
      .quality = vector_v2::MaterializationQuality::kExact,
      .pixels = updated_tile,
  }};
  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 32, 16}, .pixels = std::span(next_overview).first(32U * 16U)},
      {0, 0, 128, 64}, publications));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(canvas.overview_pixels().front() == 0x2222U);

  const auto updated_after = canvas.lookup(updated);
  REQUIRE(updated_after.has_value());
  CHECK(updated_after->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(updated_after->identity.quality == vector_v2::MaterializationQuality::kExact);
  REQUIRE(updated_after->slot_index.has_value());
  CHECK(tile_storage[*updated_after->slot_index * vector_v2::kTilePixels] == 0x4444U);

  const auto invalidated_after = canvas.lookup(invalidated);
  REQUIRE(invalidated_after.has_value());
  CHECK(invalidated_after->kind == vector_v2::SourceKind::kOverview);

  const auto carried_after = canvas.lookup(carried);
  REQUIRE(carried_after.has_value());
  CHECK(carried_after->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(carried_after->slot_index == carried_before->slot_index);
  CHECK(carried_after->identity.generation == carried_before->identity.generation);
  CHECK(carried_after->identity.revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("revision-advancing mutation may downgrade an affected settled tile") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, 16U * 16U> next_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> settled_tile{};
  std::array<std::uint16_t, vector_v2::kTilePixels> immediate_tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, settled_tile));
  const std::array publication{vector_v2::TileRevisionPublication{
      .key = key,
      .quality = vector_v2::MaterializationQuality::kImmediate,
      .pixels = immediate_tile,
  }};

  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = next_overview}, {0, 0, 64, 64}, publication));
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(source->identity.revision == vector_v2::DocumentRevision{1});
  CHECK(source->identity.quality == vector_v2::MaterializationQuality::kImmediate);
}

TEST_CASE("resident tile copies preserve pixels without exposing pool storage") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x1234U);
  std::array<std::uint16_t, vector_v2::kTilePixels> copy{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(canvas.copy_resident_tile(key, copy));
  CHECK(copy == tile);
  copy.front() = 0xFFFFU;
  CHECK(tile_storage.front() == 0x1234U);
  CHECK_FALSE(canvas.copy_resident_tile({vector_v2::ZoomLevel::k100Percent, 1, 0}, copy));
  CHECK_FALSE(canvas.copy_resident_tile(key, std::span(copy).first(copy.size() - 1U)));
  CHECK_FALSE(canvas.copy_resident_tile(key, std::span(overview).first(vector_v2::kTilePixels)));
  CHECK(overview.front() == 0U);
}

TEST_CASE("resident enumeration rejects output that aliases slot metadata") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  auto aliased_output = std::span(reinterpret_cast<vector_v2::TileKey*>(slots.data()), 1U);

  CHECK_FALSE(canvas.resident_tiles_intersecting({0, 0, 64, 64}, aliased_output));
  CHECK(canvas.lookup(key)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("incremental revision invalidates intersecting resident tiles at every zoom") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> next_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey at_100{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey at_200{vector_v2::ZoomLevel::k200Percent, 1, 1};
  const vector_v2::TileKey outside{vector_v2::ZoomLevel::k200Percent, 4, 4};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(at_100, {0}, vector_v2::MaterializationQuality::kExact, tile));
  REQUIRE(canvas.publish_tile(at_200, {0}, vector_v2::MaterializationQuality::kExact, tile));
  REQUIRE(canvas.publish_tile(outside, {0}, vector_v2::MaterializationQuality::kExact, tile));

  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {8, 8, 16, 16}, .pixels = std::span(next_overview).first(8U * 8U)},
      {32, 32, 64, 64}, {}));
  CHECK(canvas.lookup(at_100)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(at_200)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(outside)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(outside)->identity.revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("incremental revision rejection leaves pixels and identities unchanged") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, vector_v2::kOverviewPixels> next_overview{};
  next_overview.fill(0x2222U);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x3333U);
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kExact, tile));
  const auto before = canvas.lookup(key);
  REQUIRE(before.has_value());

  const vector_v2::OverviewRevisionPublication publication{
      .bounds = {0, 0, 16, 16}, .pixels = std::span(next_overview).first(16U * 16U)};
  CHECK_FALSE(canvas.commit_incremental_revision({1}, publication, {0, 0, 0, 64}, {}));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
  CHECK(canvas.lookup(key)->identity == before->identity);
  CHECK(tile_storage.front() == 0x3333U);

  auto pin = canvas.pin(key);
  REQUIRE(pin.has_value());
  CHECK_FALSE(canvas.commit_incremental_revision({1}, publication, {0, 0, 64, 64}, {}));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
}

TEST_CASE("incremental revision rejects incomplete or aliased overview publications") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, 16U * 16U> compact{};
  compact.fill(0x2222U);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  CHECK_FALSE(canvas.commit_incremental_revision({1}, {.bounds = {0, 0, 15, 16}, .pixels = compact},
                                                 {0, 0, 64, 64}, {}));
  CHECK_FALSE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = std::span(compact).first(compact.size() - 1U)},
      {0, 0, 64, 64}, {}));
  CHECK_FALSE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = std::span(overview).first(compact.size())},
      {0, 0, 64, 64}, {}));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
}

TEST_CASE("incremental revision requires next revision and resident affected publications") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> next_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  vector_v2::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({4}, overview));
  const vector_v2::TileKey missing{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const std::array publication{vector_v2::TileRevisionPublication{
      .key = missing,
      .quality = vector_v2::MaterializationQuality::kExact,
      .pixels = tile,
  }};

  const vector_v2::OverviewRevisionPublication overview_publication{
      .bounds = {0, 0, 16, 16}, .pixels = std::span(next_overview).first(16U * 16U)};
  CHECK_FALSE(canvas.commit_incremental_revision({6}, overview_publication, {0, 0, 64, 64}, {}));
  CHECK_FALSE(
      canvas.commit_incremental_revision({5}, overview_publication, {0, 0, 64, 64}, publication));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{4});
}

TEST_CASE("canvas accepts caller-owned dynamically constructed slot storage") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto slots = std::make_unique<vector_v2::MaterializedSlotStorage[]>(2);
  auto tile_storage = std::make_unique<std::uint16_t[]>(2 * vector_v2::kTilePixels);
  vector_v2::MaterializedCanvas canvas(overview, std::span(slots.get(), 2),
                                       std::span(tile_storage.get(), 2 * vector_v2::kTilePixels));
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xABCDU);

  REQUIRE(canvas.ready());
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kExact, tile));
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
}
