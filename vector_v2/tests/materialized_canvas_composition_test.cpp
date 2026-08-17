#include <doctest.h>

#include "test_support/materialized_canvas_fixture.h"

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
  TestCanvas canvas(overview, slots, tile_storage);
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
  TestCanvas canvas(overview, slots, tile_storage);
  std::array<std::uint16_t, 64U * 32U> destination{};
  const vector_v2::ViewRequest request{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 64, 32},
  };

  CHECK_FALSE(canvas.compose_view(request, destination));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k100Percent, 0, 0}, {0},
                              vector_v2::MaterializationQuality::kSettled, published_tile));
  const auto tile_only = canvas.compose_view(request, destination);
  REQUIRE(tile_only.has_value());
  CHECK(tile_only->settled_tiles == 1U);
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
  TestCanvas canvas(overview, slots, tile_storage);
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
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k100Percent, 0, 0}, {0},
                              vector_v2::MaterializationQuality::kSettled, published_tile));

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
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));

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
  CHECK(source->revision == vector_v2::DocumentRevision{0});
}

TEST_CASE("transactional overview publication prevents stale fallback labeling") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x2222U);
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k50Percent, 10, 0}, {0},
                              vector_v2::MaterializationQuality::kSettled,
                              std::span(tile).first(vector_v2::kTilePixels)));

  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0x3333U);
  REQUIRE(canvas.publish_overview({1}, revised_overview));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k50Percent, 10, 0}, {1},
                              vector_v2::MaterializationQuality::kSettled,
                              std::span(tile).first(vector_v2::kTilePixels)));
  std::array<std::uint16_t, 64U * 64U> destination{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k50Percent, .level_pixels = {672, 0, 736, 64}}, destination);
  REQUIRE(stats.has_value());
  CHECK(stats->revision == vector_v2::DocumentRevision{1});
  CHECK(destination[31] == 0x2222U);
  CHECK(destination[32] == 0x3333U);
}

TEST_CASE("discarding tiles preserves current overview") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  TestCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({2}, overview));
  REQUIRE(canvas.publish_tile(key, {2}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.discard_tiles());
  const auto fallback = canvas.lookup(key);
  REQUIRE(fallback.has_value());
  CHECK(fallback->kind == vector_v2::SourceKind::kOverview);
  CHECK(fallback->revision == vector_v2::DocumentRevision{2});
}

TEST_CASE("compose view rejects destinations that alias owned source storage") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, slots, tile_storage);
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
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));

  std::array<std::uint16_t, 368U * 22U> strip{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 350, 368, 372}}, strip);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == strip.size());
  CHECK(strip.front() == overview[87 * vector_v2::kOverviewWidth]);
  CHECK(strip.back() == overview[92 * vector_v2::kOverviewWidth + 91]);
}

TEST_CASE("clipped 50 percent tile stores and composes its packed source") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, 32U * 64U> edge_pixels{};
  for (std::size_t index = 0; index < edge_pixels.size(); ++index) {
    edge_pixels[index] = static_cast<std::uint16_t>(index);
  }
  TestCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey edge{vector_v2::ZoomLevel::k50Percent, 11, 13};
  CHECK_FALSE(canvas.publish_tile(edge, {0}, vector_v2::MaterializationQuality::kSettled,
                                  std::span(tile_storage)));
  REQUIRE(canvas.publish_tile(edge, {0}, vector_v2::MaterializationQuality::kSettled, edge_pixels));
  const auto source = canvas.lookup(edge);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(tile_storage[31] == edge_pixels[31]);
  CHECK(tile_storage[64] == edge_pixels[32]);

  std::array<std::uint16_t, 32U * 64U> composed{};
  const auto stats = canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k50Percent, .level_pixels = {704, 832, 736, 896}}, composed);
  REQUIRE(stats.has_value());
  CHECK(composed == edge_pixels);
}
