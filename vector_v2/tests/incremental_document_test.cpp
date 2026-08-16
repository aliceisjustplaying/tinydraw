#include "tinydraw/vector_v2/incremental_document.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct Fixture {
  std::array<vector_v2::OperationRecord, 4> records{};
  std::array<vector_v2::CompactOperationSample, 8> samples{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> next_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 4> slots{};
  std::array<std::uint16_t, 4U * vector_v2::kTilePixels> tile_pool{};
  std::array<std::uint16_t, 4U * vector_v2::kTilePixels> tile_scratch{};
  std::array<vector_v2::TileRevisionPublication, 4> publications{};
  std::array<vector_v2::TileKey, 4> affected{};
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{overview, slots, tile_pool};

  vector_v2::IncrementalDocumentWorkspace workspace() {
    return {.overview_scratch = next_overview,
            .tile_scratch = tile_scratch,
            .publications = publications,
            .affected_keys = affected};
  }
};

void replay_operations(vector_v2::OperationLog& log, const vector_v2::OperationReplayRange& range,
                       std::span<std::uint16_t> pixels) {
  for (std::size_t offset = 0; offset < range.operation_count; ++offset) {
    const auto stored = log.operation(range.first_operation + offset);
    REQUIRE(stored.has_value());
    REQUIRE(vector_v2::apply_incremental_operation(
        {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
        {.zoom = vector_v2::ZoomLevel::k25Percent,
         .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
         .pixels = pixels,
         .stride = vector_v2::kOverviewWidth}));
  }
}

}  // namespace

TEST_CASE("incremental document advances log and canvas together") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const vector_v2::TileKey at_50{vector_v2::ZoomLevel::k50Percent, 0, 0};
  const vector_v2::TileKey at_100{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey at_200{vector_v2::ZoomLevel::k200Percent, 1, 1};
  const vector_v2::TileKey at_400{vector_v2::ZoomLevel::k400Percent, 2, 2};
  REQUIRE(
      fixture.canvas.publish_tile(at_50, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_100, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_200, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_400, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };

  const auto result = vector_v2::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace());
  REQUIRE(result.has_value());
  CHECK(result->identity == vector_v2::OperationIdentity{{1}, 0});
  CHECK(result->affected_resident_tiles == 4U);
  CHECK(result->published_tiles == 4U);
  CHECK(result->fallback_tiles == 0U);
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.lookup(at_50)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_100)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_200)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_400)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_50)->identity.quality ==
        vector_v2::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.lookup(at_100)->identity.quality ==
        vector_v2::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.lookup(at_200)->identity.quality ==
        vector_v2::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.lookup(at_400)->identity.quality ==
        vector_v2::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.overview_pixels()[3U * vector_v2::kOverviewWidth + 3U] == 0xF800U);
}

TEST_CASE("XL append preserves every refined tile in a worst-case visible viewport") {
  constexpr std::size_t kVisibleTileCount = vector_v2::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<vector_v2::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<vector_v2::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto overview_scratch = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto slots =
      std::make_unique<std::array<vector_v2::MaterializedSlotStorage, kVisibleTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * vector_v2::kTilePixels>>();
  auto tile_scratch =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * vector_v2::kTilePixels>>();
  auto publications =
      std::make_unique<std::array<vector_v2::TileRevisionPublication, kVisibleTileCount>>();
  auto affected = std::make_unique<std::array<vector_v2::TileKey, kVisibleTileCount>>();
  vector_v2::OperationLog log(*records, *samples);
  vector_v2::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  for (std::uint16_t row = 0; row < 8; ++row) {
    for (std::uint16_t column = 0; column < 7; ++column) {
      REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, column, row}, {0},
                                  vector_v2::MaterializationQuality::kImmediate, blank_tile));
    }
  }
  const std::array stroke{
      vector_v2::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 1'280},
      vector_v2::CompactOperationSample{.x_quarter = 440, .y_quarter = 504, .radius_256 = 1'280},
  };

  const auto result =
      vector_v2::append_incrementally(log, canvas, {.color = 0x001FU, .samples = stroke},
                                      {.overview_scratch = *overview_scratch,
                                       .tile_scratch = *tile_scratch,
                                       .publications = *publications,
                                       .affected_keys = *affected});
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == kVisibleTileCount);
  CHECK(result->published_tiles == kVisibleTileCount);
  CHECK(result->fallback_tiles == 0U);
  for (std::uint16_t row = 0; row < 8; ++row) {
    for (std::uint16_t column = 0; column < 7; ++column) {
      CHECK(canvas.lookup({vector_v2::ZoomLevel::k400Percent, column, row})->kind ==
            vector_v2::SourceKind::kTileSlot);
    }
  }
}

TEST_CASE("append preserves a visible refined paper tile without overview fallback") {
  auto records = std::make_unique<std::array<vector_v2::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<vector_v2::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto overview_scratch = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>();
  auto occupancy = std::make_unique<std::array<std::uint8_t, vector_v2::kOccupancyBytes>>();
  auto slots = std::make_unique<
      std::array<vector_v2::MaterializedSlotStorage, vector_v2::kMaximumVisibleTiles>>();
  auto tile_pool = std::make_unique<
      std::array<std::uint16_t, vector_v2::kMaximumVisibleTiles * vector_v2::kTilePixels>>();
  auto tile_scratch = std::make_unique<
      std::array<std::uint16_t, vector_v2::kMaximumVisibleTiles * vector_v2::kTilePixels>>();
  auto frame = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  std::array<vector_v2::TileRevisionPublication, vector_v2::kMaximumVisibleTiles> publications{};
  std::array<vector_v2::TileKey, vector_v2::kMaximumVisibleTiles> affected{};
  vector_v2::OperationLog log(*records, *samples);
  vector_v2::MaterializedCanvas canvas(*overview, *uniforms, *occupancy, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  const vector_v2::TileKey visible_paper{vector_v2::ZoomLevel::k400Percent, 0, 0};
  for (std::uint16_t row = 0; row < 7; ++row) {
    for (std::uint16_t column = 0; column < 6; ++column) {
      REQUIRE(canvas.publish_uniform({vector_v2::ZoomLevel::k400Percent, column, row}, {0},
                                     vector_v2::MaterializationQuality::kImmediate));
    }
  }
  const std::array stroke{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 1'280},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 1'280},
  };
  const vector_v2::ViewRequest visible{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };

  const auto result =
      vector_v2::append_incrementally(log, canvas, {.color = 0x001FU, .samples = stroke},
                                      {.overview_scratch = *overview_scratch,
                                       .tile_scratch = *tile_scratch,
                                       .publications = publications,
                                       .affected_keys = affected},
                                      {.priority_view = visible});

  REQUIRE(result.has_value());
  CHECK(result->fallback_tiles == 0U);
  const auto source = canvas.lookup(visible_paper);
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(source->identity.revision == vector_v2::DocumentRevision{1});
  const auto composed = canvas.compose_view(visible, *frame);
  REQUIRE(composed.has_value());
  CHECK(composed->fallback_pixels == 0U);
  CHECK(composed->fallback_tiles == 0U);
}

TEST_CASE("append keeps unaffected cached zoom tiles and invalidates bounded affected tiles") {
  constexpr std::size_t kCachedTileCount = 4U * vector_v2::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<vector_v2::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<vector_v2::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto overview_scratch = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto slots = std::make_unique<std::array<vector_v2::MaterializedSlotStorage, kCachedTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kCachedTileCount * vector_v2::kTilePixels>>();
  auto tile_scratch = std::make_unique<
      std::array<std::uint16_t, vector_v2::kMaximumVisibleTiles * vector_v2::kTilePixels>>();
  auto publications = std::make_unique<
      std::array<vector_v2::TileRevisionPublication, vector_v2::kMaximumVisibleTiles>>();
  auto affected = std::make_unique<std::array<vector_v2::TileKey, kCachedTileCount>>();
  vector_v2::OperationLog log(*records, *samples);
  vector_v2::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    vector_v2::MaterializationQuality::kImmediate, blank_tile));
      }
    }
  }
  const std::array local_stroke{
      vector_v2::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 1'280},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 1'280},
  };

  const auto result =
      vector_v2::append_incrementally(log, canvas, {.color = 0x001FU, .samples = local_stroke},
                                      {.overview_scratch = *overview_scratch,
                                       .tile_scratch = *tile_scratch,
                                       .publications = *publications,
                                       .affected_keys = *affected});
  REQUIRE(result.has_value());
  CHECK(result->fallback_tiles == 0U);
  CHECK(result->affected_resident_tiles <= vector_v2::kMaximumVisibleTiles);
  for (const auto zoom : zooms) {
    CHECK(canvas.lookup({zoom, 6, 7})->kind == vector_v2::SourceKind::kTileSlot);
    CHECK(canvas.lookup({zoom, 6, 7})->identity.revision == vector_v2::DocumentRevision{1});
  }
}

TEST_CASE("bounded append scratch prioritizes every affected tile in the visible zoom") {
  constexpr std::size_t kCachedTileCount = 4U * vector_v2::kMaximumVisibleTiles;
  constexpr std::size_t kVisibleTileCount = vector_v2::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<vector_v2::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<vector_v2::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto overview_scratch = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto slots = std::make_unique<std::array<vector_v2::MaterializedSlotStorage, kCachedTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kCachedTileCount * vector_v2::kTilePixels>>();
  auto tile_scratch =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * vector_v2::kTilePixels>>();
  auto publications =
      std::make_unique<std::array<vector_v2::TileRevisionPublication, kVisibleTileCount>>();
  auto affected = std::make_unique<std::array<vector_v2::TileKey, kCachedTileCount>>();
  vector_v2::OperationLog log(*records, *samples);
  vector_v2::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    vector_v2::MaterializationQuality::kImmediate, blank_tile));
      }
    }
  }
  const std::array world_spanning_stroke{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 1'280},
      vector_v2::CompactOperationSample{.x_quarter = vector_v2::kWorldWidth * 4,
                                        .y_quarter = vector_v2::kWorldHeight * 4,
                                        .radius_256 = 1'280},
  };
  const vector_v2::ViewRequest visible{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {63, 63, 63 + vector_v2::kOverviewWidth, 63 + vector_v2::kOverviewHeight},
  };

  const auto result = vector_v2::append_incrementally(
      log, canvas, {.color = 0x001FU, .samples = world_spanning_stroke},
      {.overview_scratch = *overview_scratch,
       .tile_scratch = *tile_scratch,
       .publications = *publications,
       .affected_keys = *affected},
      {.priority_view = visible});
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == kCachedTileCount);
  CHECK(result->published_tiles == kVisibleTileCount);
  CHECK(result->fallback_tiles == kCachedTileCount - kVisibleTileCount);
  for (std::uint16_t row = 0; row < 8; ++row) {
    for (std::uint16_t column = 0; column < 7; ++column) {
      const auto source = canvas.lookup({vector_v2::ZoomLevel::k400Percent, column, row});
      REQUIRE(source.has_value());
      CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
      CHECK(source->identity.revision == vector_v2::DocumentRevision{1});
    }
  }
}

TEST_CASE("priority-view append bounds immediate publication to the active zoom") {
  constexpr std::size_t kCachedTileCount = 4U * vector_v2::kMaximumVisibleTiles;
  constexpr std::size_t kVisibleTileCount = vector_v2::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<vector_v2::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<vector_v2::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto overview_scratch = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto slots = std::make_unique<std::array<vector_v2::MaterializedSlotStorage, kCachedTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kCachedTileCount * vector_v2::kTilePixels>>();
  auto tile_scratch =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * vector_v2::kTilePixels>>();
  auto publications =
      std::make_unique<std::array<vector_v2::TileRevisionPublication, kVisibleTileCount>>();
  auto affected = std::make_unique<std::array<vector_v2::TileKey, kCachedTileCount>>();
  vector_v2::OperationLog log(*records, *samples);
  vector_v2::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  for (const auto zoom : zooms) {
    REQUIRE(canvas.publish_tile({zoom, 0, 0}, {0}, vector_v2::MaterializationQuality::kImmediate,
                                blank_tile));
  }
  const std::array stroke{
      vector_v2::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 1'280},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 1'280},
  };
  const vector_v2::ViewRequest visible{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };

  const auto result = vector_v2::append_incrementally(
      log, canvas, {.color = 0x001FU, .samples = stroke},
      {.overview_scratch = *overview_scratch,
       .tile_scratch = *tile_scratch,
       .publications = *publications,
       .affected_keys = *affected},
      {.priority_view = visible,
       .publication_scope = vector_v2::IncrementalPublicationScope::kPriorityView});

  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 1U);
  CHECK(result->published_tiles == 1U);
  CHECK(result->fallback_tiles == 0U);
  CHECK(canvas.lookup({vector_v2::ZoomLevel::k400Percent, 0, 0})->kind ==
        vector_v2::SourceKind::kTileSlot);
  for (const auto zoom : std::span(zooms).first(3)) {
    CHECK(canvas.lookup({zoom, 0, 0})->kind == vector_v2::SourceKind::kOverview);
  }
}

TEST_CASE("bounded overview publication matches full rendering at worst thin-stroke alignment") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  auto full_render = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  full_render->fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 6, .y_quarter = 6, .radius_256 = 1},
      vector_v2::CompactOperationSample{.x_quarter = 70, .y_quarter = 70, .radius_256 = 1},
  };
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0xF800U, .samples = samples},
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
       .pixels = *full_render,
       .stride = vector_v2::kOverviewWidth}));

  REQUIRE(vector_v2::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace()));
  CHECK(std::equal(full_render->begin(), full_render->end(),
                   fixture.canvas.overview_pixels().begin()));
}

TEST_CASE("incremental document falls back excess affected residents when scratch is bounded") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const vector_v2::TileKey first{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey second{vector_v2::ZoomLevel::k200Percent, 1, 1};
  REQUIRE(
      fixture.canvas.publish_tile(first, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };
  auto workspace = fixture.workspace();
  workspace.tile_scratch = std::span(fixture.tile_scratch).first(vector_v2::kTilePixels);
  workspace.publications = std::span(fixture.publications).first(1);

  const auto result = vector_v2::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, workspace);
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 2U);
  CHECK(result->published_tiles == 1U);
  CHECK(result->fallback_tiles == 1U);
  const bool first_is_fallback =
      fixture.canvas.lookup(first)->kind == vector_v2::SourceKind::kOverview;
  const bool second_is_fallback =
      fixture.canvas.lookup(second)->kind == vector_v2::SourceKind::kOverview;
  CHECK(first_is_fallback != second_is_fallback);
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{1});
}

TEST_CASE("coarsest tiled append republishes both sides of a tile boundary") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const vector_v2::TileKey left{vector_v2::ZoomLevel::k50Percent, 0, 0};
  const vector_v2::TileKey right{vector_v2::ZoomLevel::k50Percent, 1, 0};
  REQUIRE(
      fixture.canvas.publish_tile(left, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(right, {0}, vector_v2::MaterializationQuality::kSettled, tile));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 512, .y_quarter = 256, .radius_256 = 1},
  };

  const auto result = vector_v2::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace());
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 2U);
  CHECK(result->published_tiles == 2U);
  CHECK(result->fallback_tiles == 0U);
  CHECK(fixture.canvas.lookup(left)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(right)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(left)->identity.revision == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.lookup(right)->identity.revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("incremental document rejects non-exact canvas overview storage before copying") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 1> log_samples{};
  vector_v2::OperationLog log(records, log_samples);
  std::array<std::uint16_t, vector_v2::kOverviewPixels + 1U> oversized{};
  oversized.fill(0xAAAAU);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_pool{};
  vector_v2::MaterializedCanvas canvas(oversized, slots, tile_pool);
  std::array<std::uint16_t, vector_v2::kOverviewPixels + 2U> guarded{};
  guarded.fill(0xBEEFU);
  std::array<vector_v2::TileRevisionPublication, 1> publications{};
  std::array<vector_v2::TileKey, 1> affected{};
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};

  CHECK_FALSE(vector_v2::append_incrementally(
      log, canvas, {.samples = samples},
      {.overview_scratch = std::span(guarded).subspan(1, vector_v2::kOverviewPixels),
       .publications = publications,
       .affected_keys = affected}));
  CHECK(guarded.front() == 0xBEEFU);
  CHECK(guarded.back() == 0xBEEFU);
  CHECK(log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("incremental document fails atomically when overview scratch is too small") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };
  auto workspace = fixture.workspace();
  workspace.overview_scratch = std::span(fixture.next_overview).first(1);

  CHECK_FALSE(vector_v2::append_incrementally(fixture.log, fixture.canvas,
                                              {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("incremental document rejects workspace aliasing live canvas pixels") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.overview_scratch = fixture.overview;

  CHECK_FALSE(vector_v2::append_incrementally(fixture.log, fixture.canvas,
                                              {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("incremental document rejects overlapping publication workspaces") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.tile_scratch = workspace.overview_scratch.first(vector_v2::kTilePixels);

  CHECK_FALSE(vector_v2::append_incrementally(fixture.log, fixture.canvas,
                                              {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("incremental document rejects metadata that aliases tile scratch") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.tile_scratch =
      std::span(reinterpret_cast<std::uint16_t*>(fixture.publications.data()),
                sizeof(vector_v2::TileRevisionPublication) / sizeof(std::uint16_t));

  CHECK_FALSE(vector_v2::append_incrementally(fixture.log, fixture.canvas,
                                              {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("incremental document rejects keys that alias canvas slot metadata") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.affected_keys =
      std::span(reinterpret_cast<vector_v2::TileKey*>(fixture.slots.data()), 1U);

  CHECK_FALSE(vector_v2::append_incrementally(fixture.log, fixture.canvas,
                                              {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("incremental document rejects keys that alias operation storage") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.affected_keys =
      std::span(reinterpret_cast<vector_v2::TileKey*>(fixture.records.data()), 1U);

  CHECK_FALSE(vector_v2::append_incrementally(fixture.log, fixture.canvas,
                                              {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("document snapshot restore changes both authorities to an older revision") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({8}, fixture.overview));
  REQUIRE(fixture.log.reset({8}));
  fixture.next_overview.fill(0x1234U);

  const vector_v2::OperationLogEpoch old_epoch = fixture.log.epoch();
  REQUIRE(fixture.log.replay_range(old_epoch, {8}, {8}));
  REQUIRE(vector_v2::restore_document_snapshot(fixture.log, fixture.canvas, {3},
                                               fixture.next_overview));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{3});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{3});
  CHECK(fixture.log.operation_count() == 0U);
  CHECK(fixture.canvas.overview_pixels().front() == 0x1234U);
  CHECK(fixture.log.epoch() != old_epoch);
  CHECK_FALSE(fixture.log.replay_range(old_epoch, {3}, {3}));
}

TEST_CASE("document snapshot restore accepts revision zero at the current revision") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  REQUIRE(fixture.log.reset({0}));
  fixture.next_overview.fill(0x1234U);

  REQUIRE(vector_v2::restore_document_snapshot(fixture.log, fixture.canvas, {0},
                                               fixture.next_overview));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0x1234U);
}

TEST_CASE("document snapshot restore fails atomically while a source is pinned") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, fixture.overview));
  REQUIRE(fixture.log.reset({2}));
  const vector_v2::TileKey missing{vector_v2::ZoomLevel::k100Percent, 0, 0};
  auto pin = fixture.canvas.pin(missing);
  REQUIRE(pin.has_value());
  fixture.next_overview.fill(0x1234U);

  CHECK_FALSE(vector_v2::restore_document_snapshot(fixture.log, fixture.canvas, {1},
                                                   fixture.next_overview));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{2});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{2});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("document snapshot restore fails atomically while an append is prepared") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  auto prepared = fixture.log.prepare({.samples = samples});
  REQUIRE(prepared.has_value());
  fixture.next_overview.fill(0x1234U);

  CHECK_FALSE(vector_v2::restore_document_snapshot(fixture.log, fixture.canvas, {4},
                                                   fixture.next_overview));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
  prepared->cancel();
}

TEST_CASE("settlement rehearsal replays validated slices and rebases after restore") {
  constexpr std::size_t kOperationCount = 8;
  std::array<vector_v2::OperationRecord, kOperationCount + 2U> records{};
  std::array<vector_v2::CompactOperationSample, kOperationCount + 2U> log_samples{};
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto next_overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto replay = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_pool{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_scratch{};
  std::array<vector_v2::TileRevisionPublication, 1> publications{};
  std::array<vector_v2::TileKey, 1> affected{};
  vector_v2::OperationLog log(records, log_samples);
  vector_v2::MaterializedCanvas canvas(*overview, slots, tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::copy(canvas.overview_pixels().begin(), canvas.overview_pixels().end(), replay->begin());
  const vector_v2::OperationLogEpoch original_epoch = log.epoch();
  const auto workspace = vector_v2::IncrementalDocumentWorkspace{
      .overview_scratch = *next_overview,
      .tile_scratch = tile_scratch,
      .publications = publications,
      .affected_keys = affected,
  };

  for (std::size_t index = 0; index < kOperationCount; ++index) {
    const std::array sample{vector_v2::CompactOperationSample{
        .x_quarter = static_cast<std::uint16_t>(40U + index * 32U),
        .y_quarter = static_cast<std::uint16_t>(80U + index * 24U),
        .radius_256 = 256}};
    REQUIRE(vector_v2::append_incrementally(
        log, canvas,
        {.color = static_cast<std::uint16_t>(0x001FU + static_cast<std::uint16_t>(index)),
         .samples = sample},
        workspace));
  }

  const auto first_slice = log.replay_range(original_epoch, {0}, {4});
  REQUIRE(first_slice.has_value());
  replay_operations(log, *first_slice, *replay);
  const auto second_slice = log.replay_range(original_epoch, {4}, {8});
  REQUIRE(second_slice.has_value());
  replay_operations(log, *second_slice, *replay);
  CHECK(std::equal(replay->begin(), replay->end(), canvas.overview_pixels().begin()));

  next_overview->fill(0xFFFFU);
  REQUIRE(vector_v2::restore_document_snapshot(log, canvas, {3}, *next_overview));
  CHECK_FALSE(log.replay_range(original_epoch, {0}, {8}));
  const vector_v2::OperationLogEpoch restored_epoch = log.epoch();
  std::copy(canvas.overview_pixels().begin(), canvas.overview_pixels().end(), replay->begin());
  const std::array restored_sample{
      vector_v2::CompactOperationSample{.x_quarter = 400, .y_quarter = 400, .radius_256 = 512}};
  REQUIRE(vector_v2::append_incrementally(
      log, canvas, {.color = 0xF800U, .samples = restored_sample}, workspace));
  const auto restored_range = log.replay_range(restored_epoch, {3}, {4});
  REQUIRE(restored_range.has_value());
  replay_operations(log, *restored_range, *replay);
  CHECK(std::equal(replay->begin(), replay->end(), canvas.overview_pixels().begin()));
}

TEST_CASE("incremental document can commit with no affected resident tiles") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 400, .y_quarter = 400, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.tile_scratch = {};

  const auto result = vector_v2::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0x001FU, .samples = samples}, workspace);
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 0U);
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{1});
}

// ---------------------------------------------------------------------------
// In-place append equivalence and primitives
// ---------------------------------------------------------------------------

namespace {

struct EquivalenceRig {
  std::vector<vector_v2::OperationRecord> records = std::vector<vector_v2::OperationRecord>(2'000);
  std::vector<vector_v2::CompactOperationSample> samples =
      std::vector<vector_v2::CompactOperationSample>(40'000);
  std::vector<std::uint16_t> overview =
      std::vector<std::uint16_t>(vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> snapshot =
      std::vector<std::uint16_t>(vector_v2::kOverviewPixels, 0xFFFFU);
  std::unique_ptr<
      std::array<vector_v2::MaterializedUniformStorage, vector_v2::kMaterializedTileIdentityCount>>
      uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                             vector_v2::kMaterializedTileIdentityCount>>();
  std::vector<std::uint8_t> occupancy = std::vector<std::uint8_t>(vector_v2::kOccupancyBytes);
  std::vector<vector_v2::MaterializedSlotStorage> slots =
      std::vector<vector_v2::MaterializedSlotStorage>(vector_v2::kTileSlotCount);
  std::vector<std::uint16_t> tile_pool =
      std::vector<std::uint16_t>(slots.size() * vector_v2::kTilePixels);
  std::vector<std::uint16_t> supertask = std::vector<std::uint16_t>(vector_v2::kTileProducerPixels);
  std::vector<std::uint8_t> mask = std::vector<std::uint8_t>(vector_v2::kTileProducerMaskBytes);
  std::vector<std::uint16_t> summary_rows =
      std::vector<std::uint16_t>(vector_v2::kTileProducerSummaryRows);
  std::vector<std::uint32_t> summary_words =
      std::vector<std::uint32_t>(vector_v2::kTileProducerSummaryWords);
  std::vector<std::uint16_t> overview_scratch =
      std::vector<std::uint16_t>(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> tile_scratch =
      std::vector<std::uint16_t>(vector_v2::kMaximumVisibleTiles * vector_v2::kTilePixels);
  std::vector<vector_v2::TileRevisionPublication> publications =
      std::vector<vector_v2::TileRevisionPublication>(vector_v2::kMaximumVisibleTiles);
  std::vector<vector_v2::TileKey> affected_keys =
      std::vector<vector_v2::TileKey>(vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles);
  std::vector<std::uint8_t> chunk_mask =
      std::vector<std::uint8_t>(vector_v2::kInPlaceTileMaskBytes);
  std::vector<std::uint32_t> chord_plans =
      std::vector<std::uint32_t>(vector_v2::kOperationChordStorageBytes / 4U);
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{overview, *uniforms, occupancy, slots, tile_pool};
  vector_v2::TileProducer producer{
      log,
      canvas,
      {.supertask_pixels = supertask,
       .finalized_pixels = mask,
       .summary_row_unset = summary_rows,
       .summary_saturated_words = summary_words,
       .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans))}};

  EquivalenceRig() {
    REQUIRE(vector_v2::restore_document_snapshot(log, canvas, {1}, snapshot));
    REQUIRE(producer.reset_uniform_baseline({1}));
  }

  [[nodiscard]] vector_v2::IncrementalDocumentWorkspace reference_workspace() {
    return {.overview_scratch = overview_scratch,
            .tile_scratch = tile_scratch,
            .publications = publications,
            .affected_keys = affected_keys};
  }

  [[nodiscard]] vector_v2::InPlaceAppendWorkspace in_place_workspace() {
    return {.overview_scratch = overview_scratch,
            .affected_keys = affected_keys,
            .tile_mask = chunk_mask};
  }

  void cold_fill(const vector_v2::ViewRequest& view) {
    for (std::size_t step = 0; step < 100'000U; ++step) {
      const auto produced = producer.produce_next(view);
      REQUIRE(produced.has_value());
      if (produced->complete) {
        return;
      }
    }
    REQUIRE(false);
  }
};

void forward_replay(vector_v2::OperationLog& log, const vector_v2::ViewRequest& view,
                    std::span<std::uint16_t> pixels) {
  std::fill(pixels.begin(), pixels.end(), 0xFFFFU);
  for (std::size_t index = 0; index < log.operation_count(); ++index) {
    const auto stored = log.operation(index);
    REQUIRE(stored.has_value());
    REQUIRE(vector_v2::apply_incremental_operation(
        {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
        {.zoom = view.zoom,
         .level_bounds = view.level_pixels,
         .pixels = pixels,
         .stride = view.level_pixels.x1 - view.level_pixels.x0}));
  }
}

}  // namespace

TEST_CASE("in-place append equals the reference transactional append") {
  EquivalenceRig reference;
  EquivalenceRig in_place;
  const vector_v2::ViewRequest priority_view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {64, 64, 64 + vector_v2::kOverviewWidth, 64 + vector_v2::kOverviewHeight},
  };
  const vector_v2::ViewRequest coarse_view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 256, 256},
  };
  reference.cold_fill(priority_view);
  reference.cold_fill(coarse_view);
  in_place.cold_fill(priority_view);
  in_place.cold_fill(coarse_view);

  // Deterministic multi-gesture chunk stream: pen and eraser, tapered and
  // constant radii, 64-sample chunks overlapping one boundary sample.
  std::uint32_t state = 0x1234'5678U;
  const auto next_random = [&state]() {
    state = state * 1'664'525U + 1'013'904'223U;
    return state >> 8U;
  };
  std::vector<vector_v2::CompactOperationSample> gesture;
  std::size_t chunk_count = 0;
  for (std::size_t gesture_index = 0; gesture_index < 8U; ++gesture_index) {
    gesture.clear();
    int x = 80 + static_cast<int>(next_random() % 400U);
    int y = 80 + static_cast<int>(next_random() % 480U);
    const bool eraser = gesture_index % 3U == 2U;
    const bool constant_radius = gesture_index % 2U == 0U;
    const std::uint16_t base_radius = static_cast<std::uint16_t>(192U + next_random() % 1'600U);
    const std::size_t gesture_samples = 40U + next_random() % 160U;
    for (std::size_t index = 0; index < gesture_samples; ++index) {
      x = std::clamp(x + static_cast<int>(next_random() % 15U) - 7, 0, vector_v2::kWorldWidth * 4);
      y = std::clamp(y + static_cast<int>(next_random() % 15U) - 7, 0, vector_v2::kWorldHeight * 4);
      gesture.push_back({.x_quarter = static_cast<std::uint16_t>(x),
                         .y_quarter = static_cast<std::uint16_t>(y),
                         .radius_256 = constant_radius
                                           ? base_radius
                                           : static_cast<std::uint16_t>(
                                                 128U + (base_radius + index * 173U) % 1'800U),
                         .elapsed_ms = 0U});
    }
    // Chunk with one-sample overlap, exactly like ChainedOperationBuilder.
    std::size_t start = 0;
    while (start < gesture.size()) {
      const std::size_t count = std::min<std::size_t>(64U, gesture.size() - start);
      const auto chunk_samples = std::span(gesture).subspan(start, count);
      for (std::size_t index = 0; index < chunk_samples.size(); ++index) {
        chunk_samples[index].elapsed_ms = static_cast<std::uint16_t>(index * 8U);
      }
      const vector_v2::OperationAppend chunk{
          .tool = eraser ? vector_v2::OperationTool::kEraser : vector_v2::OperationTool::kPen,
          .color = static_cast<std::uint16_t>(next_random() & 0xFFFFU),
          .samples = chunk_samples,
      };
      const auto reference_result = vector_v2::append_incrementally(
          reference.log, reference.canvas, chunk, reference.reference_workspace(),
          {.priority_view = priority_view,
           .publication_scope = vector_v2::IncrementalPublicationScope::kPriorityView});
      const auto in_place_result = vector_v2::append_incrementally_in_place(
          in_place.log, in_place.canvas, chunk, in_place.in_place_workspace(), priority_view);
      REQUIRE(reference_result.has_value());
      REQUIRE(in_place_result.has_value());
      CHECK(reference_result->identity == in_place_result->identity);
      CHECK(reference.log.current_revision() == in_place.log.current_revision());
      CHECK(reference.canvas.current_revision() == in_place.canvas.current_revision());
      ++chunk_count;
      start += count == 64U ? 63U : count;
    }

    // The priority view must stay fully resident and pixel-identical on both
    // paths after every gesture.
    std::vector<std::uint16_t> reference_view(vector_v2::kOverviewPixels);
    std::vector<std::uint16_t> in_place_view(vector_v2::kOverviewPixels);
    const auto reference_stats = reference.canvas.compose_view(priority_view, reference_view);
    const auto in_place_stats = in_place.canvas.compose_view(priority_view, in_place_view);
    REQUIRE(reference_stats.has_value());
    REQUIRE(in_place_stats.has_value());
    CHECK(reference_stats->fallback_pixels == 0U);
    CHECK(in_place_stats->fallback_pixels == 0U);
    CHECK(reference_view == in_place_view);
    // The in-place priority view also equals ground-truth forward replay.
    std::vector<std::uint16_t> direct(vector_v2::kOverviewPixels);
    forward_replay(in_place.log, priority_view, direct);
    CHECK(in_place_view == direct);
    // Both overviews remain identical to each other.
    CHECK(std::equal(reference.canvas.overview_pixels().begin(),
                     reference.canvas.overview_pixels().end(),
                     in_place.canvas.overview_pixels().begin()));
  }
  CHECK(chunk_count >= 16U);

  // In-place bounds mutation to the active zoom: affected raw tiles at
  // other zooms are dropped by the commit and re-produced lazily, exactly
  // like the reference path's priority-view publication scope. Whatever
  // remains resident must equal ground truth — a resident tile may be
  // dropped, but it must never be stale.
  std::vector<std::uint16_t> coarse_direct(256U * 256U);
  forward_replay(in_place.log, coarse_view, coarse_direct);
  std::size_t resident_coarse_tiles = 0;
  std::array<std::uint16_t, vector_v2::kTilePixels> resident_tile{};
  for (std::uint16_t row = 0; row < 4U; ++row) {
    for (std::uint16_t column = 0; column < 4U; ++column) {
      const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, column, row};
      const auto source = in_place.canvas.lookup(key);
      if (!source.has_value() || source->kind == vector_v2::SourceKind::kOverview) {
        continue;
      }
      ++resident_coarse_tiles;
      const auto bounds = vector_v2::tile_pixel_bounds(key);
      const std::size_t pixel_count = static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                      static_cast<std::size_t>(bounds.y1 - bounds.y0);
      REQUIRE(in_place.canvas.copy_resident_tile(key, std::span(resident_tile).first(pixel_count)));
      bool equal = true;
      for (int y = bounds.y0; y < bounds.y1; ++y) {
        for (int x = bounds.x0; x < bounds.x1; ++x) {
          equal =
              equal &&
              resident_tile[static_cast<std::size_t>(y - bounds.y0) *
                                static_cast<std::size_t>(bounds.x1 - bounds.x0) +
                            static_cast<std::size_t>(x - bounds.x0)] ==
                  coarse_direct[static_cast<std::size_t>(y) * 256U + static_cast<std::size_t>(x)];
        }
      }
      CHECK(equal);
    }
  }
  // Under the active-zoom mutation policy the strokes may have dropped any
  // number of coarse tiles; the count is informational, not a bound.
  static_cast<void>(resident_coarse_tiles);
}

TEST_CASE("in-place append bounds mutation to the active zoom") {
  EquivalenceRig rig;
  const vector_v2::ViewRequest fine_view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  const vector_v2::ViewRequest coarse_view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 256, 256},
  };
  // Ink the region first so the cold fills below materialize real raw tiles
  // at both zooms; a blank canvas would give only paper uniforms and the
  // cross-zoom assertions would be vacuous.
  {
    std::array<vector_v2::CompactOperationSample, 12> ink{};
    for (std::size_t index = 0; index < ink.size(); ++index) {
      ink[index] = {.x_quarter = static_cast<std::uint16_t>(20U + index * 40U),
                    .y_quarter = static_cast<std::uint16_t>(460U - index * 36U),
                    .radius_256 = 2'048U,
                    .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
    }
    REQUIRE(vector_v2::append_incrementally_in_place(rig.log, rig.canvas,
                                                     {.color = 0x07E0U, .samples = ink},
                                                     rig.in_place_workspace(), std::nullopt)
                .has_value());
  }
  rig.cold_fill(fine_view);
  rig.cold_fill(coarse_view);
  // The ink diagonal must be resident as raw pixels at both zooms.
  REQUIRE(rig.canvas.lookup({vector_v2::ZoomLevel::k100Percent, 0, 1}).has_value());
  REQUIRE(rig.canvas.lookup({vector_v2::ZoomLevel::k100Percent, 0, 1})->kind ==
          vector_v2::SourceKind::kTileSlot);

  // A pen stroke inside the 400% viewport whose world bounds also cross
  // resident 100% tiles.
  std::array<vector_v2::CompactOperationSample, 8> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = {.x_quarter = static_cast<std::uint16_t>(40U + index * 36U),
                      .y_quarter = static_cast<std::uint16_t>(48U + index * 40U),
                      .radius_256 = 1'024U,
                      .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
  }
  const auto result = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas, {.color = 0x001FU, .samples = samples}, rig.in_place_workspace(),
      fine_view);
  REQUIRE(result.has_value());
  // Cross-zoom resident tiles under the stroke were dropped, not painted.
  CHECK(result->fallback_tiles > 0U);
  const auto affected_bounds = result->affected_world_bounds;
  // Regression pin: the result must carry the operation's real world bounds
  // (publish() clears the prepared view; the bounds were once read after).
  REQUIRE(affected_bounds.x1 > affected_bounds.x0);
  REQUIRE(affected_bounds.y1 > affected_bounds.y0);
  for (std::uint16_t row = 0; row < 4U; ++row) {
    for (std::uint16_t column = 0; column < 4U; ++column) {
      const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, column, row};
      const auto bounds = vector_v2::tile_pixel_bounds(key);
      // 100% level pixels equal world units.
      const bool affected = bounds.x0 < affected_bounds.x1 && affected_bounds.x0 < bounds.x1 &&
                            bounds.y0 < affected_bounds.y1 && affected_bounds.y0 < bounds.y1;
      const auto source = rig.canvas.lookup(key);
      REQUIRE(source.has_value());
      if (affected) {
        CHECK(source->kind == vector_v2::SourceKind::kOverview);
      }
    }
  }
  // The active 400% view stays fully resident and pixel-exact.
  std::vector<std::uint16_t> composed(vector_v2::kOverviewPixels);
  const auto stats = rig.canvas.compose_view(fine_view, composed);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == 0U);
  std::vector<std::uint16_t> direct(vector_v2::kOverviewPixels);
  forward_replay(rig.log, fine_view, direct);
  CHECK(composed == direct);

  // Without a priority view (the 25% product case) every affected raw tile
  // is dropped and only the overview is painted.
  std::array<vector_v2::CompactOperationSample, 4> second{};
  for (std::size_t index = 0; index < second.size(); ++index) {
    second[index] = {.x_quarter = static_cast<std::uint16_t>(60U + index * 48U),
                     .y_quarter = static_cast<std::uint16_t>(120U + index * 32U),
                     .radius_256 = 1'024U,
                     .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
  }
  const auto overview_result = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas, {.color = 0xF800U, .samples = second}, rig.in_place_workspace(),
      std::nullopt);
  REQUIRE(overview_result.has_value());
  CHECK(overview_result->published_tiles == 0U);
  CHECK(overview_result->fallback_tiles == overview_result->affected_resident_tiles);
}

TEST_CASE("in-place append retains matching uniforms under an eraser") {
  EquivalenceRig rig;
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  rig.cold_fill(view);
  const vector_v2::TileKey paper_key{vector_v2::ZoomLevel::k400Percent, 1, 1};
  REQUIRE(rig.canvas.uniform_color(paper_key) == 0xFFFFU);

  std::array<vector_v2::CompactOperationSample, 8> erase{};
  for (std::size_t index = 0; index < erase.size(); ++index) {
    erase[index] = {.x_quarter = static_cast<std::uint16_t>(60U + index * 40U),
                    .y_quarter = static_cast<std::uint16_t>(60U + index * 40U),
                    .radius_256 = 1'024U,
                    .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
  }
  const auto result = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas,
      {.tool = vector_v2::OperationTool::kEraser, .color = 0xFFFFU, .samples = erase},
      rig.in_place_workspace(), view);
  REQUIRE(result.has_value());
  // The paper uniform crossed by the eraser is retained untouched instead of
  // being converted or invalidated.
  CHECK(rig.canvas.uniform_color(paper_key) == 0xFFFFU);
  CHECK(result->fallback_tiles == 0U);
  std::vector<std::uint16_t> composed(vector_v2::kOverviewPixels);
  const auto stats = rig.canvas.compose_view(view, composed);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == 0U);
  std::vector<std::uint16_t> direct(vector_v2::kOverviewPixels);
  forward_replay(rig.log, view, direct);
  CHECK(composed == direct);
}

namespace {
std::int64_t fake_commit_clock_now = 0;
std::int64_t fake_commit_clock() { return fake_commit_clock_now += 1'000; }
}  // namespace

TEST_CASE("in-place append keeps visible tiles sharp and drops off-screen at the budget") {
  EquivalenceRig rig;
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  // Ink first so the fill materializes raw tiles at the active zoom.
  {
    std::array<vector_v2::CompactOperationSample, 12> ink{};
    for (std::size_t index = 0; index < ink.size(); ++index) {
      ink[index] = {.x_quarter = static_cast<std::uint16_t>(20U + index * 28U),
                    .y_quarter = static_cast<std::uint16_t>(20U + index * 32U),
                    .radius_256 = 2'048U,
                    .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
    }
    REQUIRE(vector_v2::append_incrementally_in_place(rig.log, rig.canvas,
                                                     {.color = 0x07E0U, .samples = ink},
                                                     rig.in_place_workspace(), std::nullopt)
                .has_value());
  }
  rig.cold_fill(view);

  std::array<vector_v2::CompactOperationSample, 8> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = {.x_quarter = static_cast<std::uint16_t>(30U + index * 30U),
                      .y_quarter = static_cast<std::uint16_t>(30U + index * 34U),
                      .radius_256 = 1'024U,
                      .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
  }
  // An already-expired budget still paints every affected tile that is
  // visible in the priority view: dropping one blurs pixels the user is
  // looking at (rejected on glass), and the viewport bounds the work. The
  // stroke here sits fully inside the view, so nothing falls back.
  fake_commit_clock_now = 0;
  const auto visible = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas, {.color = 0x001FU, .samples = samples}, rig.in_place_workspace(), view,
      {.now_us = &fake_commit_clock, .budget_us = 0});
  REQUIRE(visible.has_value());
  CHECK(visible->affected_resident_tiles > 0U);
  CHECK(visible->published_tiles > 0U);
  CHECK(visible->fallback_tiles == 0U);
  CHECK(rig.log.current_revision() == rig.canvas.current_revision());

  // The same expired budget with the priority view elsewhere at the same
  // zoom drops every affected tile: they are off-screen, so the fallback is
  // invisible and idle repair rebuilds them later.
  rig.cold_fill(view);
  const vector_v2::ViewRequest elsewhere{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {2'000, 2'000, 2'000 + vector_v2::kOverviewWidth,
                       2'000 + vector_v2::kOverviewHeight},
  };
  fake_commit_clock_now = 0;
  const auto starved = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas, {.color = 0x8000U, .samples = samples}, rig.in_place_workspace(),
      elsewhere, {.now_us = &fake_commit_clock, .budget_us = 0});
  REQUIRE(starved.has_value());
  CHECK(starved->affected_resident_tiles > 0U);
  CHECK(starved->published_tiles == 0U);
  CHECK(starved->fallback_tiles == starved->affected_resident_tiles);
  CHECK(rig.log.current_revision() == rig.canvas.current_revision());

  // A generous budget behaves like the unbounded default.
  rig.cold_fill(view);
  fake_commit_clock_now = 0;
  const auto relaxed = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas, {.color = 0xF800U, .samples = samples}, rig.in_place_workspace(), view,
      {.now_us = &fake_commit_clock, .budget_us = 10'000'000});
  REQUIRE(relaxed.has_value());
  CHECK(relaxed->published_tiles > 0U);
  CHECK(relaxed->fallback_tiles == 0U);
  std::vector<std::uint16_t> composed(vector_v2::kOverviewPixels);
  const auto stats = rig.canvas.compose_view(view, composed);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == 0U);
  std::vector<std::uint16_t> direct(vector_v2::kOverviewPixels);
  forward_replay(rig.log, view, direct);
  CHECK(composed == direct);
}

TEST_CASE("in-place append fails atomically before mutation") {
  EquivalenceRig rig;
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  rig.cold_fill(view);
  const std::vector<std::uint16_t> overview_before(rig.canvas.overview_pixels().begin(),
                                                   rig.canvas.overview_pixels().end());
  const auto revision_before = rig.canvas.current_revision();

  const std::array valid{
      vector_v2::CompactOperationSample{.x_quarter = 100, .y_quarter = 100, .radius_256 = 512},
      vector_v2::CompactOperationSample{
          .x_quarter = 300, .y_quarter = 300, .radius_256 = 512, .elapsed_ms = 8},
  };
  // Invalid sample: zero radius is rejected by log preparation.
  const std::array invalid{
      vector_v2::CompactOperationSample{.x_quarter = 100, .y_quarter = 100, .radius_256 = 0}};
  CHECK_FALSE(vector_v2::append_incrementally_in_place(rig.log, rig.canvas,
                                                       {.color = 0x001FU, .samples = invalid},
                                                       rig.in_place_workspace(), view)
                  .has_value());
  // A pinned source blocks the canvas validation stage.
  auto pin = rig.canvas.pin({vector_v2::ZoomLevel::k400Percent, 0, 0});
  REQUIRE(pin.has_value());
  CHECK_FALSE(vector_v2::append_incrementally_in_place(rig.log, rig.canvas,
                                                       {.color = 0x001FU, .samples = valid},
                                                       rig.in_place_workspace(), view)
                  .has_value());
  pin->reset();
  CHECK(rig.canvas.current_revision() == revision_before);
  CHECK(rig.log.current_revision() == revision_before);
  CHECK(std::equal(overview_before.begin(), overview_before.end(),
                   rig.canvas.overview_pixels().begin()));
  // The same request succeeds once the pin is released.
  CHECK(vector_v2::append_incrementally_in_place(rig.log, rig.canvas,
                                                 {.color = 0x001FU, .samples = valid},
                                                 rig.in_place_workspace(), view)
            .has_value());
}

TEST_CASE("in-place append preserves same-color uniforms at every zoom") {
  EquivalenceRig rig;
  // Two 100% uniforms under the stroke: one matches the painted color and
  // must survive, the other differs and drops as cross-zoom damage.
  const vector_v2::TileKey matching{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey differing{vector_v2::ZoomLevel::k100Percent, 1, 0};
  REQUIRE(rig.canvas.publish_uniform(matching, rig.canvas.current_revision(),
                                     vector_v2::MaterializationQuality::kExact, 0x001FU));
  REQUIRE(rig.canvas.publish_uniform(differing, rig.canvas.current_revision(),
                                     vector_v2::MaterializationQuality::kExact, 0xF800U));

  const vector_v2::ViewRequest priority{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  std::array<vector_v2::CompactOperationSample, 4> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    // World x 20..110 with radius 8: crosses both 64-px 100% tiles.
    samples[index] = {.x_quarter = static_cast<std::uint16_t>((20U + index * 30U) * 4U),
                      .y_quarter = static_cast<std::uint16_t>(20U * 4U),
                      .radius_256 = 8U * 256U,
                      .elapsed_ms = static_cast<std::uint16_t>(index * 8U)};
  }
  const auto result = vector_v2::append_incrementally_in_place(
      rig.log, rig.canvas, {.color = 0x001FU, .samples = samples}, rig.in_place_workspace(),
      priority);
  REQUIRE(result.has_value());

  const auto survivor = rig.canvas.lookup(matching);
  REQUIRE(survivor.has_value());
  CHECK(survivor->kind == vector_v2::SourceKind::kUniform);
  CHECK(survivor->uniform_color == 0x001FU);
  const auto dropped = rig.canvas.lookup(differing);
  CHECK((!dropped.has_value() || dropped->kind == vector_v2::SourceKind::kOverview));
  // The dropped 100% uniform is deferred cold work and must be reported.
  CHECK(result->cross_zoom_invalidated > 0U);
}
