#include "tinydraw/production/materialized_canvas.h"

#include <doctest.h>

#include <array>
#include <cstdint>

#include "tinydraw/production/memory_layout.h"

namespace production = tinydraw::production;

TEST_CASE("production geometry has fixed world overview and committed zoom identities") {
  CHECK(production::kWorldWidth == 1472);
  CHECK(production::kWorldHeight == 1792);
  CHECK(production::kOverviewWidth == 368);
  CHECK(production::kOverviewHeight == 448);
  CHECK(production::kOverviewBytes == 329'728U);

  CHECK(production::zoom_percent(production::ZoomLevel::k25Percent) == 25);
  CHECK(production::zoom_percent(production::ZoomLevel::k50Percent) == 50);
  CHECK(production::zoom_percent(production::ZoomLevel::k100Percent) == 100);
  CHECK(production::zoom_percent(production::ZoomLevel::k200Percent) == 200);
  CHECK(production::zoom_percent(production::ZoomLevel::k400Percent) == 400);
}

TEST_CASE("production memory plan records every fixed-capacity region") {
  CHECK(sizeof(production::CompactOperationSample) == 8U);
  CHECK(production::kTilePoolBytes == 1'048'576U);
  CHECK(production::kTileMetadataBytes ==
        production::kTileSlotCount * sizeof(production::MaterializedSlotStorage));
  CHECK(production::kOperationStorageBytes == 720'000U);
  CHECK(production::kLodStorageBytes == 668'000U);
  CHECK(production::kRendererWorkspaceBytes == 163'840U);
  CHECK(production::kDisplayWorkspaceBytes == 103'040U);
  CHECK(production::kExternalPlanBytes == 3'038'304U);
  CHECK(production::kTargetContiguousReserveBytes == 1'572'864U);
}

TEST_CASE("world points map to bounded world-aligned tiles") {
  const auto first = production::tile_key_for_world(production::ZoomLevel::k100Percent, {0, 0});
  REQUIRE(first.has_value());
  CHECK(first->column == 0U);
  CHECK(first->row == 0U);

  const auto last =
      production::tile_key_for_world(production::ZoomLevel::k100Percent,
                                     {production::kWorldWidth - 1, production::kWorldHeight - 1});
  REQUIRE(last.has_value());
  CHECK(last->column == 22U);
  CHECK(last->row == 27U);
  CHECK(production::tile_grid(production::ZoomLevel::k100Percent) == production::TileGrid{23, 28});
  CHECK(production::tile_pixel_bounds(*last) == production::PixelRect{1408, 1728, 1472, 1792});

  CHECK_FALSE(production::tile_key_for_world(production::ZoomLevel::k25Percent, {0, 0}));
  CHECK_FALSE(production::tile_key_for_world(production::ZoomLevel::k100Percent, {-1, 0}));
  CHECK_FALSE(production::tile_key_for_world(production::ZoomLevel::k100Percent,
                                             {production::kWorldWidth, 0}));
}

TEST_CASE("edge tiles clip to each zoom extent") {
  const production::TileGrid grid = production::tile_grid(production::ZoomLevel::k50Percent);
  CHECK(grid == production::TileGrid{12, 14});
  const production::TileKey edge{
      .zoom = production::ZoomLevel::k50Percent,
      .column = 11,
      .row = 13,
  };
  CHECK(production::tile_pixel_bounds(edge) == production::PixelRect{704, 832, 736, 896});
  CHECK(production::overview_source_bounds(edge) == production::PixelRect{352, 416, 368, 448});
  CHECK_FALSE(production::valid_tile_key({production::ZoomLevel::k50Percent, 12, 13}));
}

TEST_CASE("canvas rejects invalid storage and non-monotonic revision changes") {
  std::array<std::uint16_t, 4> too_small{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  production::MaterializedCanvas invalid(too_small, slots);
  CHECK_FALSE(invalid.ready());
  CHECK_FALSE(invalid.advance_revision({1}));
  CHECK_FALSE(invalid.publish_overview({0}));

  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  production::MaterializedCanvas canvas(overview, slots);
  CHECK(canvas.ready());
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
  CHECK_FALSE(canvas.advance_revision({0}));
  CHECK(canvas.advance_revision({1}));
  CHECK_FALSE(canvas.advance_revision({1}));
  CHECK_FALSE(canvas.advance_revision({0}));
}

TEST_CASE("lookup never selects a stale tile or stale overview") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 2> slots{};
  production::MaterializedCanvas canvas(overview, slots);
  const production::TileKey key{production::ZoomLevel::k100Percent, 2, 3};

  CHECK_FALSE(canvas.lookup(key));
  CHECK(canvas.publish_overview({0}));
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == production::SourceKind::kOverview);
  CHECK(fallback->identity.revision == production::DocumentRevision{0});
  CHECK(fallback->identity.provenance == production::MaterializationProvenance::kCompleteOverview);

  CHECK(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled));
  const auto tile = canvas.lookup(key);
  REQUIRE(tile.has_value());
  CHECK(tile->kind == production::SourceKind::kTileSlot);
  CHECK(tile->identity.quality == production::MaterializationQuality::kSettled);

  CHECK(canvas.advance_revision({1}));
  CHECK_FALSE(canvas.lookup(key));
  CHECK_FALSE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact));
  CHECK(canvas.publish_overview({1}));
  const auto revised_fallback = canvas.lookup(key);
  REQUIRE(revised_fallback.has_value());
  CHECK(revised_fallback->kind == production::SourceKind::kOverview);
  CHECK(revised_fallback->identity.revision == production::DocumentRevision{1});
}

TEST_CASE("fixed-capacity slots replace the least recently used slot") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 2> slots{};
  production::MaterializedCanvas canvas(overview, slots);
  const production::TileKey first{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey second{production::ZoomLevel::k100Percent, 1, 0};
  const production::TileKey third{production::ZoomLevel::k100Percent, 2, 0};

  REQUIRE(canvas.publish_overview({0}));
  REQUIRE(canvas.publish_tile(first, {0}, production::MaterializationQuality::kSettled));
  REQUIRE(canvas.publish_tile(second, {0}, production::MaterializationQuality::kSettled));
  REQUIRE(canvas.lookup(first));
  REQUIRE(canvas.publish_tile(third, {0}, production::MaterializationQuality::kExact));

  CHECK(canvas.lookup(first)->kind == production::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == production::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == production::SourceKind::kTileSlot);
}

TEST_CASE("publishing the same key advances generation and quality") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  production::MaterializedCanvas canvas(overview, slots);
  const production::TileKey key{production::ZoomLevel::k200Percent, 4, 5};

  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled));
  const auto settled = canvas.lookup(key);
  REQUIRE(settled.has_value());
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact));
  const auto exact = canvas.lookup(key);
  REQUIRE(exact.has_value());
  CHECK(exact->slot_index == settled->slot_index);
  CHECK(exact->identity.generation.value > settled->identity.generation.value);
  CHECK(exact->identity.quality == production::MaterializationQuality::kExact);
}
