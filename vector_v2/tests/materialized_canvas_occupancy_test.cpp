#include <doctest.h>

#include "test_support/materialized_canvas_fixture.h"
#include "tinydraw/vector_v2/incremental_document.h"

TEST_CASE("paper catalog consumes no raw slots and composes direct fills") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 2, 3};

  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kImmediate));
  const auto source = canvas.lookup(key);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kUniform);
  CHECK(canvas.uniform_color(key) == 0xFFFFU);

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
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey changed{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey learned_paper{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey replacement{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_overview({0}, overview));
  REQUIRE(canvas.publish_tile(changed, {0}, vector_v2::MaterializationQuality::kImmediate, raw));
  REQUIRE(
      canvas.publish_uniform(learned_paper, {0}, vector_v2::MaterializationQuality::kImmediate));

  std::array<vector_v2::TileKey, 1> affected{};
  REQUIRE(canvas.materialized_tiles_intersecting({0, 0, 128, 64}, affected) == 1U);
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
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);

  CHECK_FALSE(canvas.accepts_external_workspace(std::as_bytes(std::span(*uniforms))));
  CHECK_FALSE(canvas.accepts_external_workspace(std::as_bytes(std::span(occupancy))));
}

TEST_CASE("snapshot restore without authority map remains fully conservative") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  snapshot.fill(0xFFFFU);
  snapshot[3U * vector_v2::kOverviewWidth + 2U] = 0x001FU;
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);

  REQUIRE(canvas.restore_snapshot({4}, snapshot));
  CHECK_FALSE(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 0, 0}));
  CHECK_FALSE(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 1, 0}));
  CHECK(std::all_of(occupancy.begin(), occupancy.end(),
                    [](std::uint8_t byte) { return byte == 0xFFU; }));
  CHECK(canvas.overview_pixels()[3U * vector_v2::kOverviewWidth + 2U] == 0x001FU);
}

TEST_CASE("mapped snapshot restore validates both sources before changing canvas") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  snapshot.fill(0xFFFFU);
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  REQUIRE(canvas.reset_blank({3}));

  snapshot[0] = 0x001FU;
  may_ink[0] = 1U;
  CHECK_FALSE(
      canvas.restore_snapshot({4}, snapshot, std::span(may_ink).first(may_ink.size() - 1U)));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{3});
  CHECK(canvas.overview_pixels()[0] == 0xFFFFU);
  CHECK(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 0, 0}));

  REQUIRE(canvas.restore_snapshot({4}, snapshot, may_ink));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{4});
  CHECK(canvas.overview_pixels()[0] == 0x001FU);
  CHECK_FALSE(canvas.certainly_paper({vector_v2::ZoomLevel::k100Percent, 0, 0}));
}

TEST_CASE("blank reset clears materialization and restores paper occupancy") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  snapshot.fill(0xFFFFU);
  snapshot[0] = 0x001FU;
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey raw{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey uniform{vector_v2::ZoomLevel::k100Percent, 1, 0};

  REQUIRE(canvas.restore_snapshot({4}, snapshot));
  REQUIRE(
      canvas.publish_tile(raw, {4}, vector_v2::MaterializationQuality::kSettled, published_tile));
  REQUIRE(canvas.publish_uniform(uniform, {4}, vector_v2::MaterializationQuality::kSettled));
  REQUIRE(canvas.resident_raw_tiles() == 1U);
  REQUIRE(canvas.uniform_color(uniform).has_value());
  REQUIRE_FALSE(canvas.certainly_paper(raw));
  REQUIRE(canvas.reset_blank({5}));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{5});
  CHECK(canvas.resident_raw_tiles() == 0U);
  CHECK_FALSE(canvas.uniform_color(uniform).has_value());
  CHECK(canvas.lookup(raw)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(uniform)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.certainly_paper(raw));
  CHECK(canvas.certainly_paper(uniform));
  CHECK(std::all_of(occupancy.begin(), occupancy.end(),
                    [](std::uint8_t byte) { return byte == 0U; }));
  CHECK(std::all_of(canvas.overview_pixels().begin(), canvas.overview_pixels().end(),
                    [](std::uint16_t pixel) { return pixel == 0xFFFFU; }));
}

TEST_CASE("blank and sparse autosave replay restore authority-derived may-ink") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 2> samples{};
  vector_v2::OperationLog log(records, samples);
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> replay{};
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);

  REQUIRE(vector_v2::replay_active_overview(log, replay));
  REQUIRE(vector_v2::build_tiled_may_ink(log, may_ink));
  REQUIRE(canvas.restore_snapshot(log.current_revision(), replay, may_ink));
  CHECK(std::all_of(occupancy.begin(), occupancy.end(),
                    [](std::uint8_t byte) { return byte == 0U; }));

  const std::array stroke{
      vector_v2::CompactOperationSample{.x_quarter = 160U * vector_v2::kSampleUnitsPerWorldUnit,
                                        .y_quarter = 240U * vector_v2::kSampleUnitsPerWorldUnit,
                                        .radius_256 = 8U * 256U}};
  REQUIRE(log.append({.color = 0x001FU, .samples = stroke}));
  REQUIRE(vector_v2::replay_active_overview(log, replay));
  REQUIRE(vector_v2::build_tiled_may_ink(log, may_ink));
  REQUIRE(canvas.restore_snapshot(log.current_revision(), replay, may_ink));

  const auto stored = log.operation(0U);
  REQUIRE(stored.has_value());

  std::size_t occupied_cells = 0U;
  for (int row = 0; row < vector_v2::kOccupancyRows; ++row) {
    for (int column = 0; column < vector_v2::kOccupancyColumns; ++column) {
      const vector_v2::PixelRect cell{column * vector_v2::kOccupancyCellWorldSize,
                                      row * vector_v2::kOccupancyCellWorldSize,
                                      (column + 1) * vector_v2::kOccupancyCellWorldSize,
                                      (row + 1) * vector_v2::kOccupancyCellWorldSize};
      const bool expected_occupied =
          stored->world_bounds.x0 < cell.x1 && cell.x0 < stored->world_bounds.x1 &&
          stored->world_bounds.y0 < cell.y1 && cell.y0 < stored->world_bounds.y1;
      const std::size_t bit = static_cast<std::size_t>(row) * vector_v2::kOccupancyColumns +
                              static_cast<std::size_t>(column);
      const bool actual_occupied =
          (occupancy[bit / 8U] & static_cast<std::uint8_t>(1U << (bit % 8U))) != 0U;
      CHECK(actual_occupied == expected_occupied);
      occupied_cells += actual_occupied;
    }
  }
  CHECK(occupied_cells > 0U);
  CHECK(occupied_cells < vector_v2::kOccupancyCellCount);
  CHECK(std::equal(replay.begin(), replay.end(), canvas.overview_pixels().begin()));
}

TEST_CASE("incremental erase keeps may-ink conservative") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> blank{};
  blank.fill(0xFFFFU);
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  std::array<std::uint16_t, 4U * 4U> ink{};
  std::array<std::uint16_t, 4U * 4U> paper{};
  paper.fill(0xFFFFU);

  REQUIRE(canvas.restore_snapshot({0}, blank, may_ink));
  REQUIRE(canvas.commit_incremental_revision({1}, {.bounds = {0, 0, 4, 4}, .pixels = ink},
                                             {0, 0, 16, 16}, {}));
  REQUIRE_FALSE(canvas.certainly_paper(key));
  REQUIRE(canvas.commit_incremental_revision({2}, {.bounds = {0, 0, 4, 4}, .pixels = paper},
                                             {0, 0, 16, 16}, {}));
  CHECK_FALSE(canvas.certainly_paper(key));
}

TEST_CASE("current-revision authority may-ink replacement validates and changes no pixels") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> blank{};
  blank.fill(0xFFFFU);
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k400Percent, 0, 0};

  REQUIRE(canvas.restore_snapshot({7}, blank, may_ink));
  REQUIRE(canvas.certainly_paper(key));
  may_ink[0] = 1U;
  CHECK_FALSE(canvas.replace_tiled_may_ink({6}, may_ink));
  CHECK_FALSE(canvas.replace_tiled_may_ink({7}, std::span(may_ink).first(may_ink.size() - 1U)));
  CHECK_FALSE(canvas.replace_tiled_may_ink({7}, occupancy));
  REQUIRE(canvas.replace_tiled_may_ink({7}, may_ink));
  CHECK_FALSE(canvas.certainly_paper(key));
  CHECK(std::all_of(canvas.overview_pixels().begin(), canvas.overview_pixels().end(),
                    [](std::uint16_t pixel) { return pixel == 0xFFFFU; }));
}

TEST_CASE("partial erase preserves occupancy for ink remaining in the same cell") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  snapshot.fill(0xFFFFU);
  snapshot[0] = 0x001FU;
  snapshot[3U * vector_v2::kOverviewWidth + 3U] = 0x001FU;
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const std::array<std::uint16_t, 1> paper{0xFFFFU};

  may_ink[0] = 1U;
  REQUIRE(canvas.restore_snapshot({0}, snapshot, may_ink));
  REQUIRE(canvas.commit_incremental_revision({1}, {.bounds = {0, 0, 1, 1}, .pixels = paper},
                                             {0, 0, 4, 4}, {}));
  CHECK_FALSE(canvas.certainly_paper(key));
}

TEST_CASE("in-place history publications preserve conservative world-edge occupancy") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> blank{};
  blank.fill(0xFFFFU);
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  const vector_v2::TileKey edge{vector_v2::ZoomLevel::k100Percent, 22, 27};
  std::array<std::uint16_t, 4U * 4U> ink{};
  std::array<std::uint16_t, 4U * 4U> paper{};
  paper.fill(0xFFFFU);
  constexpr vector_v2::PixelRect world_edge{1456, 1776, 1472, 1792};
  constexpr vector_v2::PixelRect overview_edge{364, 444, 368, 448};

  REQUIRE(canvas.restore_snapshot({7}, blank, may_ink));
  REQUIRE(canvas.commit_in_place_revision({8}, {.bounds = overview_edge, .pixels = ink}, world_edge,
                                          {}));
  REQUIRE_FALSE(canvas.certainly_paper(edge));
  REQUIRE(canvas.commit_in_place_revision({9}, {.bounds = overview_edge, .pixels = paper},
                                          world_edge, {}));
  CHECK_FALSE(canvas.certainly_paper(edge));
  REQUIRE(canvas.commit_in_place_revision({10}, {.bounds = overview_edge, .pixels = ink},
                                          world_edge, {}));
  CHECK_FALSE(canvas.certainly_paper(edge));
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
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
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
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_storage{};
  TestCanvas canvas(overview, *uniforms, occupancy, slots, tile_storage);
  REQUIRE(canvas.restore_snapshot({0}, snapshot, may_ink));
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

TEST_CASE("same-revision publication cannot downgrade settled quality") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_pixels{};
  std::array<std::uint16_t, vector_v2::kTilePixels> published_tile{};
  TestCanvas canvas(overview, slots, tile_pixels);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k200Percent, 4, 5};

  REQUIRE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, published_tile));
  const auto immediate = canvas.lookup(key);
  REQUIRE(immediate.has_value());
  REQUIRE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
  const auto settled = canvas.lookup(key);
  REQUIRE(settled.has_value());
  CHECK(settled->quality == vector_v2::MaterializationQuality::kSettled);
  CHECK_FALSE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, published_tile));
  REQUIRE(
      canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kSettled, published_tile));
}
