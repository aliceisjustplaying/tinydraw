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

  CHECK(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled,
                            std::span(tile_pixels).first(production::kTilePixels)));
  const auto tile = canvas.lookup(key);
  REQUIRE(tile.has_value());
  CHECK(tile->kind == production::SourceKind::kTileSlot);
  CHECK(tile->identity.quality == production::MaterializationQuality::kSettled);

  CHECK(canvas.publish_overview({1}, overview));
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, tile_pixels));
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
  REQUIRE(canvas.publish_tile(first, {0}, production::MaterializationQuality::kSettled,
                              std::span(tile_pixels).first(production::kTilePixels)));
  REQUIRE(canvas.publish_tile(second, {0}, production::MaterializationQuality::kSettled,
                              std::span(tile_pixels).first(production::kTilePixels)));
  REQUIRE(canvas.lookup(first));
  REQUIRE(canvas.mark_used(first));
  REQUIRE(canvas.publish_tile(third, {0}, production::MaterializationQuality::kExact,
                              std::span(tile_pixels).first(production::kTilePixels)));

  CHECK(canvas.lookup(first)->kind == production::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == production::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == production::SourceKind::kTileSlot);
}

TEST_CASE("publishing the same key advances generation and quality") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, slots.size() * production::kTilePixels> tile_pixels{};
  production::MaterializedCanvas canvas(overview, slots, tile_pixels);
  const production::TileKey key{production::ZoomLevel::k200Percent, 4, 5};

  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kSettled, tile_pixels));
  const auto settled = canvas.lookup(key);
  REQUIRE(settled.has_value());
  REQUIRE(canvas.publish_tile(key, {0}, production::MaterializationQuality::kExact, tile_pixels));
  const auto exact = canvas.lookup(key);
  REQUIRE(exact.has_value());
  CHECK(exact->slot_index == settled->slot_index);
  CHECK(exact->identity.generation.value > settled->identity.generation.value);
  CHECK(exact->identity.quality == production::MaterializationQuality::kExact);
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

  REQUIRE(canvas.publish_overview({1}, overview));
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

  overview.fill(0x3333U);
  CHECK_FALSE(canvas.publish_overview({0}, overview));
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

  overview.fill(0x3333U);
  REQUIRE(canvas.publish_overview({1}, overview));
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
