#include "tinydraw/production/materialized_canvas.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "tinydraw/production/memory_layout.h"

namespace production = tinydraw::production;

TEST_CASE("production geometry has fixed world overview and committed zoom identities") {
  CHECK(production::kWorldWidth == 1472);
  CHECK(production::kWorldHeight == 1792);
  CHECK(production::kOverviewWidth == 368);
  CHECK(production::kOverviewHeight == 448);
  CHECK(production::kOverviewBytes == 329'728U);
  CHECK(production::kMaximumVisibleTiles == 56U);

  CHECK(production::zoom_percent(production::ZoomLevel::k25Percent) == 25);
  CHECK(production::zoom_percent(production::ZoomLevel::k50Percent) == 50);
  CHECK(production::zoom_percent(production::ZoomLevel::k100Percent) == 100);
  CHECK(production::zoom_percent(production::ZoomLevel::k200Percent) == 200);
  CHECK(production::zoom_percent(production::ZoomLevel::k400Percent) == 400);
}

TEST_CASE("production memory plan records every fixed-capacity region") {
  CHECK(sizeof(production::CompactOperationSample) == 8U);
  CHECK(production::kOverviewPublicationBytes == 329'728U);
  CHECK(production::kTileSlotCount == 256U);
  CHECK(production::kTileSlotCount >= 4U * production::kMaximumVisibleTiles);
  CHECK(production::kTilePoolBytes == 2'097'152U);
  CHECK(production::kTileMetadataBytes ==
        production::kTileSlotCount * sizeof(production::MaterializedSlotStorage));
  CHECK(production::kOperationStorageBytes == 720'000U);
  CHECK(production::kLodStorageBytes == 668'000U);
  CHECK(production::kRendererWorkspaceBytes == 163'840U);
  CHECK(production::kDisplayWorkspaceBytes == 103'040U);
  CHECK(production::kExternalPlanBytes == 4'421'728U);
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

TEST_CASE("world bounds map to complete bounded overview regions") {
  CHECK(production::overview_bounds_for_world({0, 0, 64, 64}) ==
        production::PixelRect{0, 0, 16, 16});
  CHECK(production::overview_bounds_for_world({1, 1, 63, 63}) ==
        production::PixelRect{0, 0, 16, 16});
  CHECK(production::overview_bounds_for_world(
            {0, 0, production::kWorldWidth, production::kWorldHeight}) ==
        production::PixelRect{0, 0, production::kOverviewWidth, production::kOverviewHeight});
  CHECK(production::overview_bounds_for_world({0, 0, 0, 1}) == production::PixelRect{});
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

TEST_CASE("canvas rejects invalid storage and non-monotonic overview publication") {
  std::array<std::uint16_t, 4> too_small{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> invalid_tile_pixels{};
  production::MaterializedCanvas invalid(too_small, slots, invalid_tile_pixels);
  CHECK_FALSE(invalid.ready());
  CHECK_FALSE(invalid.publish_overview({1}, too_small));

  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_pixels{};
  production::MaterializedCanvas canvas(overview, slots, tile_pixels);
  CHECK(canvas.ready());
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
  CHECK(canvas.publish_overview({1}, overview));
  CHECK(canvas.current_revision() == production::DocumentRevision{1});
  CHECK_FALSE(canvas.publish_overview({1}, overview));
  CHECK_FALSE(canvas.publish_overview({0}, overview));
}

TEST_CASE("lookup never selects a stale tile or stale overview") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_pixels{};
  production::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const production::TileKey key{production::ZoomLevel::k100Percent, 2, 3};

  CHECK_FALSE(canvas.lookup(key));
  CHECK(canvas.publish_overview({0}, overview));
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == production::SourceKind::kOverview);
  CHECK(fallback->identity.revision == production::DocumentRevision{0});
  CHECK(fallback->identity.provenance == production::MaterializationProvenance::kCompleteOverview);

  std::array<std::uint16_t, production::kTilePixels> published_tile{};
  CHECK(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, published_tile));
  const auto tile = canvas.lookup(key);
  REQUIRE(tile.has_value());
  CHECK(tile->kind == production::SourceKind::kTileSlot);
  CHECK(tile->identity.quality == production::MaterializationQuality::kSettled);

  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  CHECK(canvas.publish_overview({1}, revised_overview));
  std::array<std::uint16_t, production::kTilePixels> revised_tile{};
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, revised_tile));
  const auto revised_fallback = canvas.lookup(key);
  REQUIRE(revised_fallback.has_value());
  CHECK(revised_fallback->kind == production::SourceKind::kOverview);
  CHECK(revised_fallback->identity.revision == production::DocumentRevision{1});
}

TEST_CASE("fixed-capacity slots replace the least recently used slot") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_pixels{};
  production::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const production::TileKey first{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey second{production::ZoomLevel::k100Percent, 1, 0};
  const production::TileKey third{production::ZoomLevel::k100Percent, 2, 0};

  REQUIRE(canvas.publish_overview({0}, overview));
  std::array<std::uint16_t, production::kTilePixels> published_tile{};
  REQUIRE(canvas.publish_tile(first, {0}, production::MaterializationQuality::kSettled,
                              published_tile));
  REQUIRE(canvas.publish_tile(second, {0}, production::MaterializationQuality::kSettled,
                              published_tile));
  REQUIRE(canvas.lookup(first));
  REQUIRE(canvas.mark_used(first));
  REQUIRE(
      canvas.publish_tile(third, {0}, production::MaterializationQuality::kExact, published_tile));

  CHECK(canvas.lookup(first)->kind == production::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == production::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == production::SourceKind::kTileSlot);
}

TEST_CASE("cache retains one worst-case viewport at every tiled zoom") {
  constexpr std::array zooms{
      production::ZoomLevel::k50Percent,
      production::ZoomLevel::k100Percent,
      production::ZoomLevel::k200Percent,
      production::ZoomLevel::k400Percent,
  };
  auto overview = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto slots = std::make_unique<
      std::array<production::MaterializedSlotStorage, production::kTileSlotCount>>();
  auto tile_storage = std::make_unique<
      std::array<std::uint16_t, production::kTileSlotCount * production::kTilePixels>>();
  production::MaterializedCanvas canvas(*overview, *slots, *tile_storage);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, production::kTilePixels> tile{};

  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    production::MaterializationQuality::kImmediate, tile));
      }
    }
  }

  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        CHECK(canvas.lookup({zoom, column, row})->kind == production::SourceKind::kTileSlot);
      }
    }
  }
}

TEST_CASE("same-revision publication cannot downgrade immediate settled or exact quality") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_pixels{};
  std::array<std::uint16_t, production::kTilePixels> published_tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const production::TileKey key{production::ZoomLevel::k200Percent, 4, 5};

  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kImmediate,
                              published_tile));
  const auto immediate = canvas.lookup(key);
  REQUIRE(immediate.has_value());
  REQUIRE(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, published_tile));
  const auto settled = canvas.lookup(key);
  REQUIRE(settled.has_value());
  CHECK(settled->identity.generation.value > immediate->identity.generation.value);
  CHECK_FALSE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kImmediate,
                                  published_tile));
  REQUIRE(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, published_tile));
  const auto exact = canvas.lookup(key);
  REQUIRE(exact.has_value());
  CHECK(exact->slot_index == settled->slot_index);
  CHECK(exact->identity.generation.value > settled->identity.generation.value);
  CHECK(exact->identity.quality == production::MaterializationQuality::kExact);
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, published_tile));
  CHECK_FALSE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kImmediate,
                                  published_tile));
}

TEST_CASE("view composition uses current tiles and overview for every miss") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  for (int y = 0; y < production::kOverviewHeight; ++y) {
    for (int x = 0; x < production::kOverviewWidth; ++x) {
      overview[static_cast<std::size_t>(y * production::kOverviewWidth + x)] =
          static_cast<std::uint16_t>(y * production::kOverviewWidth + x);
    }
  }
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> published_tile{};
  published_tile.fill(0x1234U);
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile({production::ZoomLevel::k100Percent, 1, 0}, {0},
                              production::MaterializationQuality::kSettled, published_tile));

  std::array<std::uint16_t, 128U * 32U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = production::ZoomLevel::k100Percent, .level_pixels = {0, 0, 128, 32}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->settled_tiles == 1U);
  CHECK(stats->fallback_tiles == 1U);
  CHECK(stats->tile_pixels == 64U * 32U);
  CHECK(stats->fallback_pixels == 64U * 32U);
  CHECK(destination[63] == overview[15]);
  CHECK(destination[64] == 0x1234U);
}

TEST_CASE("compose view refuses only when no current source exists") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> published_tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  std::array<std::uint16_t, 64U * 32U> destination{};
  const production::ViewRequest request{
      .zoom = production::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 64, 32},
  };

  CHECK_FALSE(canvas.compose_view(request, destination));
  REQUIRE(canvas.publish_tile({production::ZoomLevel::k100Percent, 0, 0}, {0},
                              production::MaterializationQuality::kExact, published_tile));
  const auto tile_only = canvas.compose_view(request, destination);
  REQUIRE(tile_only.has_value());
  CHECK(tile_only->exact_tiles == 1U);
  CHECK(tile_only->fallback_tiles == 0U);

  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  const auto fallback = canvas.compose_view(request, destination);
  REQUIRE(fallback.has_value());
  CHECK(fallback->fallback_tiles == 1U);
  CHECK(fallback->revision == production::DocumentRevision{1});
}

TEST_CASE("25 percent view copies the complete overview directly") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  overview[10 * production::kOverviewWidth + 20] = 0x4567U;
  std::array<production::MaterializedSlotStorage, 0> slots{};
  std::array<std::uint16_t, 0> tile_storage{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  std::array<std::uint16_t, 16U * 8U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = production::ZoomLevel::k25Percent, .level_pixels = {20, 10, 36, 18}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == destination.size());
  CHECK(destination.front() == 0x4567U);
}

TEST_CASE("unaligned view requires every grid tile when no overview is current") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> published_tile{};
  published_tile.fill(0x1234U);
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_tile({production::ZoomLevel::k100Percent, 0, 0}, {0},
                              production::MaterializationQuality::kExact, published_tile));

  std::array<std::uint16_t, 4> destination{};
  CHECK_FALSE(canvas.compose_view(
      {.zoom = production::ZoomLevel::k100Percent, .level_pixels = {63, 0, 65, 2}}, destination));
}

TEST_CASE("overview publication commits each revision only once") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0x2222U);
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, tile));

  std::array<std::uint16_t, production::kOverviewPixels> replacement{};
  replacement.fill(0x3333U);
  CHECK_FALSE(canvas.publish_overview({0}, replacement));
  std::array<std::uint16_t, 1> composed{};
  REQUIRE(canvas.compose_view(
      {.zoom = production::ZoomLevel::k100Percent, .level_pixels = {64, 0, 65, 1}}, composed));
  CHECK(composed.front() == 0U);
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == production::SourceKind::kTileSlot);
  CHECK(source->identity.revision == production::DocumentRevision{0});
}

TEST_CASE("transactional overview publication prevents stale fallback labeling") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0x2222U);
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile({production::ZoomLevel::k50Percent, 10, 0}, {0},
                              production::MaterializationQuality::kExact,
                              std::span(tile).first(production::kTilePixels)));

  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  revised_overview.fill(0x3333U);
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  REQUIRE(canvas.publish_tile({production::ZoomLevel::k50Percent, 10, 0}, {1},
                              production::MaterializationQuality::kExact,
                              std::span(tile).first(production::kTilePixels)));
  std::array<std::uint16_t, 64U * 64U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = production::ZoomLevel::k50Percent, .level_pixels = {672, 0, 736, 64}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->revision == production::DocumentRevision{1});
  CHECK(destination[31] == 0x2222U);
  CHECK(destination[32] == 0x3333U);
}

TEST_CASE("pinned sources cannot be replaced and validate only while pinned") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, tile));

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
  CHECK_FALSE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, tile));
  CHECK_FALSE(canvas.publish_overview({1}, revised_overview));
  pinned_tile->reset();
  CHECK(canvas.pins_outstanding() == 0U);
  CHECK_FALSE(pinned_tile->valid());
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  CHECK_FALSE(canvas.validate(*pinned_tile));

  const production::TileKey missing{production::ZoomLevel::k100Percent, 1, 0};
  const auto unpinned_overview = canvas.lookup(missing);
  REQUIRE(unpinned_overview.has_value());
  auto pinned_overview = canvas.pin(missing);
  REQUIRE(pinned_overview.has_value());
  CHECK(pinned_overview->source().kind == production::SourceKind::kOverview);
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
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey first{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey second{production::ZoomLevel::k100Percent, 1, 0};
  REQUIRE(canvas.publish_tile(first, {0}, production::MaterializationQuality::kExact, tile));
  auto pinned = canvas.pin(first);
  REQUIRE(pinned.has_value());
  CHECK_FALSE(canvas.publish_tile(second, {0}, production::MaterializationQuality::kExact, tile));
  pinned->reset();
  CHECK_FALSE(pinned->valid());
  REQUIRE(canvas.publish_tile(second, {0}, production::MaterializationQuality::kExact, tile));
  CHECK_FALSE(canvas.validate(*pinned));
}

TEST_CASE("discarding tiles preserves current overview and fails while pinned") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({2}, overview));
  REQUIRE(canvas.publish_tile(key, {2}, production::MaterializationQuality::kImmediate, tile));
  auto pinned = canvas.pin(key);
  REQUIRE(pinned.has_value());
  CHECK_FALSE(canvas.discard_tiles());
  CHECK(canvas.lookup(key)->kind == production::SourceKind::kTileSlot);
  pinned->reset();

  REQUIRE(canvas.discard_tiles());
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == production::SourceKind::kOverview);
  CHECK(fallback->identity.revision == production::DocumentRevision{2});
}

TEST_CASE("compose view rejects destinations that alias owned source storage") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  CHECK_FALSE(canvas.compose_view(
      {.zoom = production::ZoomLevel::k25Percent, .level_pixels = {0, 0, 16, 8}},
      std::span(overview).first(16U * 8U)));
  CHECK_FALSE(canvas.compose_view(
      {.zoom = production::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}},
      std::span(tile_storage)));
}

TEST_CASE("walk-shaped bottom strip composes from the complete overview") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  for (std::size_t index = 0; index < overview.size(); ++index) {
    overview[index] = static_cast<std::uint16_t>(index);
  }
  std::array<production::MaterializedSlotStorage, 0> slots{};
  std::array<std::uint16_t, 0> tile_storage{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  std::array<std::uint16_t, 368U * 22U> strip{};
  const auto stats = canvas.compose_view(
      {.zoom = production::ZoomLevel::k100Percent, .level_pixels = {0, 350, 368, 372}}, strip);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == strip.size());
  CHECK(strip.front() == overview[87 * production::kOverviewWidth]);
  CHECK(strip.back() == overview[92 * production::kOverviewWidth + 91]);
}

TEST_CASE("clipped 50 percent tile advertises its padded slot stride") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, 32U * 64U> edge_pixels{};
  for (std::size_t index = 0; index < edge_pixels.size(); ++index) {
    edge_pixels[index] = static_cast<std::uint16_t>(index);
  }
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey edge{production::ZoomLevel::k50Percent, 11, 13};
  CHECK_FALSE(canvas.publish_tile(edge, {0}, production::MaterializationQuality::kSettled,
                                  std::span(tile_storage)));
  REQUIRE(
      canvas.publish_tile(edge, {0}, production::MaterializationQuality::kSettled, edge_pixels));
  const auto source = canvas.lookup(edge);
  REQUIRE(source.has_value());
  CHECK(source->source_pixels == production::PixelRect{0, 0, 32, 64});
  CHECK(source->source_stride == production::kTileWidth);
  REQUIRE(source->slot_index.has_value());
  CHECK(tile_storage[31] == edge_pixels[31]);
  CHECK(tile_storage[64] == edge_pixels[32]);

  std::array<std::uint16_t, 32U * 64U> composed{};
  const auto stats = canvas.compose_view(
      {.zoom = production::ZoomLevel::k50Percent, .level_pixels = {704, 832, 736, 896}}, composed);
  REQUIRE(stats.has_value());
  CHECK(composed == edge_pixels);
}

TEST_CASE("incremental revision updates affected tiles and carries unaffected tiles") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, production::kOverviewPixels> next_overview{};
  next_overview.fill(0x2222U);
  std::array<production::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> original_tile{};
  original_tile.fill(0x3333U);
  std::array<std::uint16_t, production::kTilePixels> updated_tile{};
  updated_tile.fill(0x4444U);
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey updated{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey invalidated{production::ZoomLevel::k100Percent, 1, 0};
  const production::TileKey carried{production::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(updated, {0}, production::MaterializationQuality::kSettled,
                              original_tile));
  REQUIRE(canvas.publish_tile(invalidated, {0}, production::MaterializationQuality::kExact,
                              original_tile));
  REQUIRE(
      canvas.publish_tile(carried, {0}, production::MaterializationQuality::kExact, original_tile));
  const auto carried_before = canvas.lookup(carried);
  REQUIRE(carried_before.has_value());

  const std::array publications{production::TileRevisionPublication{
      .key = updated,
      .quality = production::MaterializationQuality::kExact,
      .pixels = updated_tile,
  }};
  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 32, 16}, .pixels = std::span(next_overview).first(32U * 16U)},
      {0, 0, 128, 64}, publications));
  CHECK(canvas.current_revision() == production::DocumentRevision{1});
  CHECK(canvas.overview_pixels().front() == 0x2222U);

  const auto updated_after = canvas.lookup(updated);
  REQUIRE(updated_after.has_value());
  CHECK(updated_after->kind == production::SourceKind::kTileSlot);
  CHECK(updated_after->identity.quality == production::MaterializationQuality::kExact);
  REQUIRE(updated_after->slot_index.has_value());
  CHECK(tile_storage[*updated_after->slot_index * production::kTilePixels] == 0x4444U);

  const auto invalidated_after = canvas.lookup(invalidated);
  REQUIRE(invalidated_after.has_value());
  CHECK(invalidated_after->kind == production::SourceKind::kOverview);

  const auto carried_after = canvas.lookup(carried);
  REQUIRE(carried_after.has_value());
  CHECK(carried_after->kind == production::SourceKind::kTileSlot);
  CHECK(carried_after->slot_index == carried_before->slot_index);
  CHECK(carried_after->identity.generation == carried_before->identity.generation);
  CHECK(carried_after->identity.revision == production::DocumentRevision{1});
}

TEST_CASE("revision-advancing mutation may downgrade an affected settled tile") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, 16U * 16U> next_overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> settled_tile{};
  std::array<std::uint16_t, production::kTilePixels> immediate_tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, settled_tile));
  const std::array publication{production::TileRevisionPublication{
      .key = key,
      .quality = production::MaterializationQuality::kImmediate,
      .pixels = immediate_tile,
  }};

  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = next_overview}, {0, 0, 64, 64}, publication));
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == production::SourceKind::kTileSlot);
  CHECK(source->identity.revision == production::DocumentRevision{1});
  CHECK(source->identity.quality == production::MaterializationQuality::kImmediate);
}

TEST_CASE("resident tile copies preserve pixels without exposing pool storage") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0x1234U);
  std::array<std::uint16_t, production::kTilePixels> copy{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(canvas.copy_resident_tile(key, copy));
  CHECK(copy == tile);
  copy.front() = 0xFFFFU;
  CHECK(tile_storage.front() == 0x1234U);
  CHECK_FALSE(canvas.copy_resident_tile({production::ZoomLevel::k100Percent, 1, 0}, copy));
  CHECK_FALSE(canvas.copy_resident_tile(key, std::span(copy).first(copy.size() - 1U)));
  CHECK_FALSE(canvas.copy_resident_tile(key, std::span(overview).first(production::kTilePixels)));
  CHECK(overview.front() == 0U);
}

TEST_CASE("resident enumeration rejects output that aliases slot metadata") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, tile));
  auto aliased_output = std::span(reinterpret_cast<production::TileKey*>(slots.data()), 1U);

  CHECK_FALSE(canvas.resident_tiles_intersecting({0, 0, 64, 64}, aliased_output));
  CHECK(canvas.lookup(key)->kind == production::SourceKind::kTileSlot);
}

TEST_CASE("incremental revision invalidates intersecting resident tiles at every zoom") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, production::kOverviewPixels> next_overview{};
  std::array<production::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey at_100{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey at_200{production::ZoomLevel::k200Percent, 1, 1};
  const production::TileKey outside{production::ZoomLevel::k200Percent, 4, 4};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(at_100, {0}, production::MaterializationQuality::kExact, tile));
  REQUIRE(canvas.publish_tile(at_200, {0}, production::MaterializationQuality::kExact, tile));
  REQUIRE(canvas.publish_tile(outside, {0}, production::MaterializationQuality::kExact, tile));

  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {8, 8, 16, 16}, .pixels = std::span(next_overview).first(8U * 8U)},
      {32, 32, 64, 64}, {}));
  CHECK(canvas.lookup(at_100)->kind == production::SourceKind::kOverview);
  CHECK(canvas.lookup(at_200)->kind == production::SourceKind::kOverview);
  CHECK(canvas.lookup(outside)->kind == production::SourceKind::kTileSlot);
  CHECK(canvas.lookup(outside)->identity.revision == production::DocumentRevision{1});
}

TEST_CASE("incremental revision rejection leaves pixels and identities unchanged") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, production::kOverviewPixels> next_overview{};
  next_overview.fill(0x2222U);
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0x3333U);
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, tile));
  const auto before = canvas.lookup(key);
  REQUIRE(before.has_value());

  const production::OverviewRevisionPublication publication{
      .bounds = {0, 0, 16, 16}, .pixels = std::span(next_overview).first(16U * 16U)};
  CHECK_FALSE(canvas.commit_incremental_revision({1}, publication, {0, 0, 0, 64}, {}));
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
  CHECK(canvas.lookup(key)->identity == before->identity);
  CHECK(tile_storage.front() == 0x3333U);

  auto pin = canvas.pin(key);
  REQUIRE(pin.has_value());
  CHECK_FALSE(canvas.commit_incremental_revision({1}, publication, {0, 0, 64, 64}, {}));
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
}

TEST_CASE("incremental revision rejects incomplete or aliased overview publications") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, 16U * 16U> compact{};
  compact.fill(0x2222U);
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  CHECK_FALSE(canvas.commit_incremental_revision({1}, {.bounds = {0, 0, 15, 16}, .pixels = compact},
                                                 {0, 0, 64, 64}, {}));
  CHECK_FALSE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = std::span(compact).first(compact.size() - 1U)},
      {0, 0, 64, 64}, {}));
  CHECK_FALSE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 16, 16}, .pixels = std::span(overview).first(compact.size())},
      {0, 0, 64, 64}, {}));
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
}

TEST_CASE("incremental revision requires next revision and resident affected publications") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, production::kOverviewPixels> next_overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_storage{};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  production::MaterializedCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({4}, overview));
  const production::TileKey missing{production::ZoomLevel::k100Percent, 0, 0};
  const std::array publication{production::TileRevisionPublication{
      .key = missing,
      .quality = production::MaterializationQuality::kExact,
      .pixels = tile,
  }};

  const production::OverviewRevisionPublication overview_publication{
      .bounds = {0, 0, 16, 16}, .pixels = std::span(next_overview).first(16U * 16U)};
  CHECK_FALSE(canvas.commit_incremental_revision({6}, overview_publication, {0, 0, 64, 64}, {}));
  CHECK_FALSE(
      canvas.commit_incremental_revision({5}, overview_publication, {0, 0, 64, 64}, publication));
  CHECK(canvas.current_revision() == production::DocumentRevision{4});
}

TEST_CASE("canvas accepts caller-owned dynamically constructed slot storage") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  auto slots = std::make_unique<production::MaterializedSlotStorage[]>(2);
  auto tile_storage = std::make_unique<std::uint16_t[]>(2 * production::kTilePixels);
  production::MaterializedCanvas canvas(overview, std::span(slots.get(), 2),
                                        std::span(tile_storage.get(), 2 * production::kTilePixels));
  const production::TileKey key{production::ZoomLevel::k100Percent, 0, 0};
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0xABCDU);

  REQUIRE(canvas.ready());
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, tile));
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == production::SourceKind::kTileSlot);
}
