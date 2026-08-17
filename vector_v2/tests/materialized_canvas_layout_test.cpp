#include <doctest.h>

#include "test_support/materialized_canvas_fixture.h"
#include "tinydraw/vector_v2/memory_layout.h"

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
  CHECK(vector_v2::kTileSlotCount == 448U);
  CHECK(vector_v2::kTileSlotCount >= 5U * vector_v2::kMaximumVisibleTiles);
  CHECK(vector_v2::kTilePoolBytes == 3'670'016U);
  CHECK(vector_v2::kTileMetadataBytes ==
        vector_v2::kTileSlotCount * sizeof(vector_v2::MaterializedSlotStorage) +
            vector_v2::kMaterializedTileIdentityCount *
                sizeof(vector_v2::MaterializedUniformStorage) +
            vector_v2::kOccupancyBytes);
  CHECK(vector_v2::kOperationStorageBytes == 720'000U);
  CHECK(vector_v2::kRendererWorkspaceBytes == 163'840U);
  CHECK(vector_v2::kDisplayWorkspaceBytes == 103'040U);
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
  CHECK_FALSE(vector_v2::valid_tile_key({vector_v2::ZoomLevel::k50Percent, 12, 13}));
}

TEST_CASE("canvas rejects invalid storage and non-monotonic overview publication") {
  std::array<std::uint16_t, 4> too_small{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> invalid_tile_pixels{};
  TestCanvas invalid(too_small, slots, invalid_tile_pixels);
  CHECK_FALSE(invalid.ready());
  CHECK_FALSE(invalid.publish_overview({1}, too_small));

  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  TestCanvas canvas(overview, slots, tile_pixels);
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
  TestCanvas canvas(overview, slots, tile_pixels);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 2, 3};

  CHECK_FALSE(canvas.lookup(key));
  CHECK(canvas.publish_overview({0}, overview));
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == vector_v2::SourceKind::kOverview);
  CHECK(fallback->revision == vector_v2::DocumentRevision{0});

  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  CHECK(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  const auto tile = canvas.lookup(key);
  REQUIRE(tile.has_value());
  CHECK(tile->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(tile->quality == vector_v2::MaterializationQuality::kSettled);

  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  CHECK(canvas.publish_overview({1}, revised_overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> revised_tile{};
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, revised_tile));
  const auto revised_fallback = canvas.lookup(key);
  REQUIRE(revised_fallback.has_value());
  CHECK(revised_fallback->kind == vector_v2::SourceKind::kOverview);
  CHECK(revised_fallback->revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("fixed-capacity slots replace the least recently used slot") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  TestCanvas canvas(overview, slots, tile_pixels);
  const vector_v2::TileKey first{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey second{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey third{vector_v2::ZoomLevel::k100Percent, 2, 0};

  REQUIRE(canvas.publish_overview({0}, overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  REQUIRE(
      canvas.publish_tile(first, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  REQUIRE(canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kSettled,
                              published_tile));
  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  REQUIRE(canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}}, composed));
  REQUIRE(
      canvas.publish_tile(third, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));

  CHECK(canvas.lookup(first)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == vector_v2::SourceKind::kTileSlot);
}
