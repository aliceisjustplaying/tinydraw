#include <doctest.h>

#include "test_support/materialized_canvas_fixture.h"

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
  TestCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey updated{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey invalidated{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey carried{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(updated, {0}, vector_v2::MaterializationQuality::kSettled,
                              original_tile));
  REQUIRE(canvas.publish_tile(invalidated, {0}, vector_v2::MaterializationQuality::kSettled,
                              original_tile));
  REQUIRE(canvas.publish_tile(carried, {0}, vector_v2::MaterializationQuality::kSettled,
                              original_tile));
  const std::array publications{vector_v2::TileRevisionPublication{
      .key = updated,
      .quality = vector_v2::MaterializationQuality::kSettled,
      .pixels = updated_tile,
  }};
  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {0, 0, 32, 16}, .pixels = std::span(next_overview).first(32U * 16U)},
      {0, 0, 128, 64}, publications));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(canvas.overview_pixels().front() == 0x2222U);

  const auto updated_after = canvas.lookup(updated);
  REQUIRE(updated_after.has_value());
  CHECK(updated_after->kind == vector_v2::SourceKind::kUniform);
  CHECK(updated_after->quality == vector_v2::MaterializationQuality::kSettled);
  CHECK(canvas.uniform_color(updated) == 0x4444U);
  std::array<std::uint16_t, vector_v2::kTilePixels> copied{};
  REQUIRE(canvas.copy_resident_tile(updated, copied));
  CHECK(copied.front() == 0x4444U);

  const auto invalidated_after = canvas.lookup(invalidated);
  REQUIRE(invalidated_after.has_value());
  CHECK(invalidated_after->kind == vector_v2::SourceKind::kOverview);

  const auto carried_after = canvas.lookup(carried);
  REQUIRE(carried_after.has_value());
  CHECK(carried_after->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(carried_after->revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("revision-advancing mutation may downgrade an affected settled tile") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, 16U * 16U> next_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> settled_tile{};
  std::array<std::uint16_t, vector_v2::kTilePixels> immediate_tile{};
  TestCanvas canvas(overview, slots, tile_storage);
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
  CHECK(source->kind == vector_v2::SourceKind::kUniform);
  CHECK(source->revision == vector_v2::DocumentRevision{1});
  CHECK(source->quality == vector_v2::MaterializationQuality::kImmediate);
}

TEST_CASE("resident tile copies preserve pixels without exposing pool storage") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x1234U);
  std::array<std::uint16_t, vector_v2::kTilePixels> copy{};
  TestCanvas canvas(overview, slots, tile_storage);
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
  TestCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  auto aliased_output = std::span(reinterpret_cast<vector_v2::TileKey*>(slots.data()), 1U);

  CHECK_FALSE(canvas.materialized_tiles_intersecting({0, 0, 64, 64}, aliased_output));
  CHECK(canvas.lookup(key)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("incremental revision invalidates intersecting resident tiles at every zoom") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> next_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  TestCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey at_100{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey at_200{vector_v2::ZoomLevel::k200Percent, 1, 1};
  const vector_v2::TileKey outside{vector_v2::ZoomLevel::k200Percent, 4, 4};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(at_100, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(canvas.publish_tile(at_200, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(canvas.publish_tile(outside, {0}, vector_v2::MaterializationQuality::kSettled, tile));

  REQUIRE(canvas.commit_incremental_revision(
      {1}, {.bounds = {8, 8, 16, 16}, .pixels = std::span(next_overview).first(8U * 8U)},
      {32, 32, 64, 64}, {}));
  CHECK(canvas.lookup(at_100)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(at_200)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(outside)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(outside)->revision == vector_v2::DocumentRevision{1});
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
  TestCanvas canvas(overview, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  const auto before = canvas.lookup(key);
  REQUIRE(before.has_value());

  const vector_v2::OverviewRevisionPublication publication{
      .bounds = {0, 0, 16, 16}, .pixels = std::span(next_overview).first(16U * 16U)};
  CHECK_FALSE(canvas.commit_incremental_revision({1}, publication, {0, 0, 0, 64}, {}));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(canvas.overview_pixels().front() == 0x1111U);
  CHECK(canvas.lookup(key)->kind == before->kind);
  CHECK(canvas.lookup(key)->revision == before->revision);
  CHECK(canvas.lookup(key)->quality == before->quality);
  CHECK(tile_storage.front() == 0x3333U);
}

TEST_CASE("incremental revision rejects incomplete or aliased overview publications") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0x1111U);
  std::array<std::uint16_t, 16U * 16U> compact{};
  compact.fill(0x2222U);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, slots, tile_storage);
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
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({4}, overview));
  const vector_v2::TileKey missing{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const std::array publication{vector_v2::TileRevisionPublication{
      .key = missing,
      .quality = vector_v2::MaterializationQuality::kSettled,
      .pixels = tile,
  }};

  const vector_v2::OverviewRevisionPublication overview_publication{
      .bounds = {0, 0, 16, 16}, .pixels = std::span(next_overview).first(16U * 16U)};
  CHECK_FALSE(canvas.commit_incremental_revision({6}, overview_publication, {0, 0, 64, 64}, {}));
  CHECK_FALSE(
      canvas.commit_incremental_revision({5}, overview_publication, {0, 0, 64, 64}, publication));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{4});
}
