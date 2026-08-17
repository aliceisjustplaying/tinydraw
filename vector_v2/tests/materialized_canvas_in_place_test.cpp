#include <doctest.h>

#include "test_support/materialized_canvas_fixture.h"

TEST_CASE("canvas accepts caller-owned dynamically constructed slot storage") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto slots = std::make_unique<vector_v2::MaterializedSlotStorage[]>(2);
  auto tile_storage = std::make_unique<std::uint16_t[]>(2 * vector_v2::kTilePixels);
  TestCanvas canvas(overview, std::span(slots.get(), 2),
                    std::span(tile_storage.get(), 2 * vector_v2::kTilePixels));
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xABCDU);

  REQUIRE(canvas.ready());
  REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("in-place edit primitives enforce residency and slot pressure") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_pool{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_pool);
  REQUIRE(canvas.publish_overview({0}, overview));

  const vector_v2::TileKey raw_key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey uniform_key{vector_v2::ZoomLevel::k100Percent, 1, 0};
  CHECK_FALSE(canvas.edit_resident_tile(raw_key).has_value());
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x1234U);
  REQUIRE(canvas.publish_tile(raw_key, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.publish_uniform(uniform_key, {0}, vector_v2::MaterializationQuality::kImmediate,
                                 0xFFFFU));
  CHECK(canvas.uniform_color(uniform_key) == 0xFFFFU);
  CHECK_FALSE(canvas.uniform_color(raw_key).has_value());

  const auto edit = canvas.edit_resident_tile(raw_key);
  REQUIRE(edit.has_value());
  CHECK(edit->bounds == vector_v2::tile_pixel_bounds(raw_key));
  edit->pixels[0] = 0xBEEF;

  // Conversion evicts the raw slot (single-slot canvas) and fills
  // with the uniform color; the uniform entry is consumed.
  const auto converted = canvas.materialize_uniform_as_raw(uniform_key);
  REQUIRE(converted.has_value());
  CHECK(converted->pixels[0] == 0xFFFFU);
  CHECK_FALSE(canvas.uniform_color(uniform_key).has_value());
  CHECK_FALSE(canvas.edit_resident_tile(raw_key).has_value());

  // invalidate_identity drops the raw slot; the identity becomes fallback.
  canvas.invalidate_identity(uniform_key);
  CHECK_FALSE(canvas.edit_resident_tile(uniform_key).has_value());
  const auto source = canvas.lookup(uniform_key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kOverview);
}

TEST_CASE("commit_in_place_revision retains listed keys and rejects invalid commits") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  std::array<vector_v2::MaterializedSlotStorage, 4> slots{};
  std::array<std::uint16_t, 4U * vector_v2::kTilePixels> tile_pool{};
  TestCanvas canvas(overview, slots, tile_pool);
  REQUIRE(canvas.publish_overview({0}, overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x0F0FU);
  const vector_v2::TileKey retained_key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey dropped_key{vector_v2::ZoomLevel::k200Percent, 0, 0};
  const vector_v2::TileKey unaffected_key{vector_v2::ZoomLevel::k100Percent, 3, 3};
  REQUIRE(
      canvas.publish_tile(retained_key, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(
      canvas.publish_tile(dropped_key, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.publish_tile(unaffected_key, {0}, vector_v2::MaterializationQuality::kImmediate,
                              tile));

  const vector_v2::PixelRect world_bounds{0, 0, 96, 96};
  const vector_v2::PixelRect overview_bounds = vector_v2::overview_bounds_for_world(world_bounds);
  std::array<std::uint16_t, 24U * 24U> patch{};
  patch.fill(0x001FU);
  const vector_v2::OverviewRevisionPublication publication{
      .bounds = overview_bounds,
      .pixels =
          std::span(patch).first(static_cast<std::size_t>(overview_bounds.x1 - overview_bounds.x0) *
                                 static_cast<std::size_t>(overview_bounds.y1 - overview_bounds.y0)),
  };
  const std::array retained{retained_key};
  // Wrong revision step is rejected before any mutation.
  CHECK_FALSE(canvas.can_edit_in_place_revision({2}, publication, world_bounds));
  CHECK_FALSE(canvas.commit_in_place_revision({2}, publication, world_bounds, retained));
  REQUIRE(canvas.can_edit_in_place_revision({1}, publication, world_bounds));
  REQUIRE(canvas.commit_in_place_revision({1}, publication, world_bounds, retained));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{1});

  // Retained and unaffected identities stay resident at the new revision; the
  // affected unlisted identity fell back.
  const auto kept = canvas.lookup(retained_key);
  REQUIRE(kept.has_value());
  CHECK(kept->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(kept->revision == vector_v2::DocumentRevision{1});
  const auto untouched = canvas.lookup(unaffected_key);
  REQUIRE(untouched.has_value());
  CHECK(untouched->kind == vector_v2::SourceKind::kTileSlot);
  const auto dropped = canvas.lookup(dropped_key);
  REQUIRE(dropped.has_value());
  CHECK(dropped->kind == vector_v2::SourceKind::kOverview);
  // The overview patch landed.
  CHECK(canvas.overview_pixels()[static_cast<std::size_t>(overview_bounds.y0) *
                                     vector_v2::kOverviewWidth +
                                 static_cast<std::size_t>(overview_bounds.x0)] == 0x001FU);
}

TEST_CASE("raw publication cannot downgrade a settled uniform") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 2, 3};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kSettled));

  // An immediate-quality raw publication is a same-revision downgrade of the
  // settled uniform representation and must be rejected.
  std::vector<std::uint16_t> pixels(vector_v2::kTilePixels, 0x1234U);
  CHECK_FALSE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, pixels)
                  .has_value());
  auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kUniform);

  // Equal quality may replace the representation.
  CHECK(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, pixels)
            .has_value());
  source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
}
