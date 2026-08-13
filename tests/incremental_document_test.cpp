#include "tinydraw/production/incremental_document.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

namespace production = tinydraw::production;

namespace {

struct Fixture {
  std::array<production::OperationRecord, 4> records{};
  std::array<production::CompactOperationSample, 8> samples{};
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, production::kOverviewPixels> next_overview{};
  std::array<production::MaterializedSlotStorage, 4> slots{};
  std::array<std::uint16_t, 4U * production::kTilePixels> tile_pool{};
  std::array<std::uint16_t, 4U * production::kTilePixels> tile_scratch{};
  std::array<production::TileRevisionPublication, 4> publications{};
  std::array<production::TileKey, 4> affected{};
  production::OperationLog log{records, samples};
  production::MaterializedCanvas canvas{overview, slots, tile_pool};

  production::IncrementalDocumentWorkspace workspace() {
    return {.overview_scratch = next_overview,
            .tile_scratch = tile_scratch,
            .publications = publications,
            .affected_keys = affected};
  }
};

void replay_operations(production::OperationLog& log, const production::OperationReplayRange& range,
                       std::span<std::uint16_t> pixels) {
  for (std::size_t offset = 0; offset < range.operation_count; ++offset) {
    const auto stored = log.operation(range.first_operation + offset);
    REQUIRE(stored.has_value());
    REQUIRE(production::apply_incremental_operation(
        {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
        {.zoom = production::ZoomLevel::k25Percent,
         .level_bounds = {0, 0, production::kOverviewWidth, production::kOverviewHeight},
         .pixels = pixels,
         .stride = production::kOverviewWidth}));
  }
}

}  // namespace

TEST_CASE("incremental document advances log and canvas together") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const production::TileKey at_50{production::ZoomLevel::k50Percent, 0, 0};
  const production::TileKey at_100{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey at_200{production::ZoomLevel::k200Percent, 1, 1};
  const production::TileKey at_400{production::ZoomLevel::k400Percent, 2, 2};
  REQUIRE(
      fixture.canvas.publish_tile(at_50, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_100, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_200, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_400, {0}, production::MaterializationQuality::kSettled, tile));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };

  const auto result = production::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace());
  REQUIRE(result.has_value());
  CHECK(result->identity == production::OperationIdentity{{1}, 0});
  CHECK(result->affected_resident_tiles == 4U);
  CHECK(result->published_tiles == 4U);
  CHECK(result->fallback_tiles == 0U);
  CHECK(fixture.log.current_revision() == production::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{1});
  CHECK(fixture.canvas.lookup(at_50)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_100)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_200)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_400)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_50)->identity.quality ==
        production::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.lookup(at_100)->identity.quality ==
        production::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.lookup(at_200)->identity.quality ==
        production::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.lookup(at_400)->identity.quality ==
        production::MaterializationQuality::kImmediate);
  CHECK(fixture.canvas.overview_pixels()[3U * production::kOverviewWidth + 3U] == 0xF800U);
}

TEST_CASE("XL append preserves every refined tile in a worst-case visible viewport") {
  constexpr std::size_t kVisibleTileCount = production::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<production::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<production::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto overview_scratch =
      std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto slots =
      std::make_unique<std::array<production::MaterializedSlotStorage, kVisibleTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * production::kTilePixels>>();
  auto tile_scratch =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * production::kTilePixels>>();
  auto publications =
      std::make_unique<std::array<production::TileRevisionPublication, kVisibleTileCount>>();
  auto affected = std::make_unique<std::array<production::TileKey, kVisibleTileCount>>();
  production::OperationLog log(*records, *samples);
  production::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, production::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  for (std::uint16_t row = 0; row < 8; ++row) {
    for (std::uint16_t column = 0; column < 7; ++column) {
      REQUIRE(canvas.publish_tile({production::ZoomLevel::k400Percent, column, row}, {0},
                                  production::MaterializationQuality::kImmediate, blank_tile));
    }
  }
  const std::array stroke{
      production::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 1'280},
      production::CompactOperationSample{.x_quarter = 440, .y_quarter = 504, .radius_256 = 1'280},
  };

  const auto result =
      production::append_incrementally(log, canvas, {.color = 0x001FU, .samples = stroke},
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
      CHECK(canvas.lookup({production::ZoomLevel::k400Percent, column, row})->kind ==
            production::SourceKind::kTileSlot);
    }
  }
}

TEST_CASE("append keeps unaffected cached zoom tiles and invalidates bounded affected tiles") {
  constexpr std::size_t kCachedTileCount = 4U * production::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<production::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<production::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto overview_scratch =
      std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto slots =
      std::make_unique<std::array<production::MaterializedSlotStorage, kCachedTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kCachedTileCount * production::kTilePixels>>();
  auto tile_scratch = std::make_unique<
      std::array<std::uint16_t, production::kMaximumVisibleTiles * production::kTilePixels>>();
  auto publications = std::make_unique<
      std::array<production::TileRevisionPublication, production::kMaximumVisibleTiles>>();
  auto affected = std::make_unique<std::array<production::TileKey, kCachedTileCount>>();
  production::OperationLog log(*records, *samples);
  production::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, production::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  constexpr std::array zooms{
      production::ZoomLevel::k50Percent,
      production::ZoomLevel::k100Percent,
      production::ZoomLevel::k200Percent,
      production::ZoomLevel::k400Percent,
  };
  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    production::MaterializationQuality::kImmediate, blank_tile));
      }
    }
  }
  const std::array local_stroke{
      production::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 1'280},
      production::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 1'280},
  };

  const auto result =
      production::append_incrementally(log, canvas, {.color = 0x001FU, .samples = local_stroke},
                                       {.overview_scratch = *overview_scratch,
                                        .tile_scratch = *tile_scratch,
                                        .publications = *publications,
                                        .affected_keys = *affected});
  REQUIRE(result.has_value());
  CHECK(result->fallback_tiles == 0U);
  CHECK(result->affected_resident_tiles <= production::kMaximumVisibleTiles);
  for (const auto zoom : zooms) {
    CHECK(canvas.lookup({zoom, 6, 7})->kind == production::SourceKind::kTileSlot);
    CHECK(canvas.lookup({zoom, 6, 7})->identity.revision == production::DocumentRevision{1});
  }
}

TEST_CASE("bounded append scratch prioritizes every affected tile in the visible zoom") {
  constexpr std::size_t kCachedTileCount = 4U * production::kMaximumVisibleTiles;
  constexpr std::size_t kVisibleTileCount = production::kMaximumVisibleTiles;
  auto records = std::make_unique<std::array<production::OperationRecord, 1>>();
  auto samples = std::make_unique<std::array<production::CompactOperationSample, 2>>();
  auto overview = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto overview_scratch =
      std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto slots =
      std::make_unique<std::array<production::MaterializedSlotStorage, kCachedTileCount>>();
  auto tile_pool =
      std::make_unique<std::array<std::uint16_t, kCachedTileCount * production::kTilePixels>>();
  auto tile_scratch =
      std::make_unique<std::array<std::uint16_t, kVisibleTileCount * production::kTilePixels>>();
  auto publications =
      std::make_unique<std::array<production::TileRevisionPublication, kVisibleTileCount>>();
  auto affected = std::make_unique<std::array<production::TileKey, kCachedTileCount>>();
  production::OperationLog log(*records, *samples);
  production::MaterializedCanvas canvas(*overview, *slots, *tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, production::kTilePixels> blank_tile{};
  blank_tile.fill(0xFFFFU);
  constexpr std::array zooms{
      production::ZoomLevel::k50Percent,
      production::ZoomLevel::k100Percent,
      production::ZoomLevel::k200Percent,
      production::ZoomLevel::k400Percent,
  };
  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    production::MaterializationQuality::kImmediate, blank_tile));
      }
    }
  }
  const std::array world_spanning_stroke{
      production::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 1'280},
      production::CompactOperationSample{.x_quarter = production::kWorldWidth * 4,
                                         .y_quarter = production::kWorldHeight * 4,
                                         .radius_256 = 1'280},
  };
  const production::ViewRequest visible{
      .zoom = production::ZoomLevel::k400Percent,
      .level_pixels = {63, 63, 63 + production::kOverviewWidth, 63 + production::kOverviewHeight},
  };

  const auto result = production::append_incrementally(
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
      const auto source = canvas.lookup({production::ZoomLevel::k400Percent, column, row});
      REQUIRE(source.has_value());
      CHECK(source->kind == production::SourceKind::kTileSlot);
      CHECK(source->identity.revision == production::DocumentRevision{1});
    }
  }
}

TEST_CASE("bounded overview publication matches full rendering at worst thin-stroke alignment") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  auto full_render = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  full_render->fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 6, .y_quarter = 6, .radius_256 = 1},
      production::CompactOperationSample{.x_quarter = 70, .y_quarter = 70, .radius_256 = 1},
  };
  REQUIRE(production::apply_incremental_operation(
      {.color = 0xF800U, .samples = samples},
      {.zoom = production::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, production::kOverviewWidth, production::kOverviewHeight},
       .pixels = *full_render,
       .stride = production::kOverviewWidth}));

  REQUIRE(production::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace()));
  CHECK(std::equal(full_render->begin(), full_render->end(),
                   fixture.canvas.overview_pixels().begin()));
}

TEST_CASE("incremental document falls back excess affected residents when scratch is bounded") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const production::TileKey first{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey second{production::ZoomLevel::k200Percent, 1, 1};
  REQUIRE(
      fixture.canvas.publish_tile(first, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(second, {0}, production::MaterializationQuality::kSettled, tile));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };
  auto workspace = fixture.workspace();
  workspace.tile_scratch = std::span(fixture.tile_scratch).first(production::kTilePixels);
  workspace.publications = std::span(fixture.publications).first(1);

  const auto result = production::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, workspace);
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 2U);
  CHECK(result->published_tiles == 1U);
  CHECK(result->fallback_tiles == 1U);
  const bool first_is_fallback =
      fixture.canvas.lookup(first)->kind == production::SourceKind::kOverview;
  const bool second_is_fallback =
      fixture.canvas.lookup(second)->kind == production::SourceKind::kOverview;
  CHECK(first_is_fallback != second_is_fallback);
  CHECK(fixture.log.current_revision() == production::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{1});
}

TEST_CASE("coarsest tiled append republishes both sides of a tile boundary") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const production::TileKey left{production::ZoomLevel::k50Percent, 0, 0};
  const production::TileKey right{production::ZoomLevel::k50Percent, 1, 0};
  REQUIRE(
      fixture.canvas.publish_tile(left, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(right, {0}, production::MaterializationQuality::kSettled, tile));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 512, .y_quarter = 256, .radius_256 = 1},
  };

  const auto result = production::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace());
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 2U);
  CHECK(result->published_tiles == 2U);
  CHECK(result->fallback_tiles == 0U);
  CHECK(fixture.canvas.lookup(left)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(right)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(left)->identity.revision == production::DocumentRevision{1});
  CHECK(fixture.canvas.lookup(right)->identity.revision == production::DocumentRevision{1});
}

TEST_CASE("incremental document rejects non-exact canvas overview storage before copying") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 1> log_samples{};
  production::OperationLog log(records, log_samples);
  std::array<std::uint16_t, production::kOverviewPixels + 1U> oversized{};
  oversized.fill(0xAAAAU);
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_pool{};
  production::MaterializedCanvas canvas(oversized, slots, tile_pool);
  std::array<std::uint16_t, production::kOverviewPixels + 2U> guarded{};
  guarded.fill(0xBEEFU);
  std::array<production::TileRevisionPublication, 1> publications{};
  std::array<production::TileKey, 1> affected{};
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};

  CHECK_FALSE(production::append_incrementally(
      log, canvas, {.samples = samples},
      {.overview_scratch = std::span(guarded).subspan(1, production::kOverviewPixels),
       .publications = publications,
       .affected_keys = affected}));
  CHECK(guarded.front() == 0xBEEFU);
  CHECK(guarded.back() == 0xBEEFU);
  CHECK(log.current_revision() == production::DocumentRevision{0});
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
}

TEST_CASE("incremental document fails atomically when overview scratch is too small") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };
  auto workspace = fixture.workspace();
  workspace.overview_scratch = std::span(fixture.next_overview).first(1);

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("incremental document rejects workspace aliasing live canvas pixels") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.overview_scratch = fixture.overview;

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("incremental document rejects overlapping publication workspaces") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.tile_scratch = workspace.overview_scratch.first(production::kTilePixels);

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("incremental document rejects metadata that aliases tile scratch") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.tile_scratch =
      std::span(reinterpret_cast<std::uint16_t*>(fixture.publications.data()),
                sizeof(production::TileRevisionPublication) / sizeof(std::uint16_t));

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
}

TEST_CASE("incremental document rejects keys that alias canvas slot metadata") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.affected_keys =
      std::span(reinterpret_cast<production::TileKey*>(fixture.slots.data()), 1U);

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
}

TEST_CASE("incremental document rejects keys that alias operation storage") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.affected_keys =
      std::span(reinterpret_cast<production::TileKey*>(fixture.records.data()), 1U);

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
}

TEST_CASE("document snapshot restore changes both authorities to an older revision") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({8}, fixture.overview));
  REQUIRE(fixture.log.reset({8}));
  fixture.next_overview.fill(0x1234U);

  const production::OperationLogEpoch old_epoch = fixture.log.epoch();
  REQUIRE(fixture.log.replay_range(old_epoch, {8}, {8}));
  REQUIRE(production::restore_document_snapshot(fixture.log, fixture.canvas, {3},
                                                fixture.next_overview));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{3});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{3});
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

  REQUIRE(production::restore_document_snapshot(fixture.log, fixture.canvas, {0},
                                                fixture.next_overview));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0x1234U);
}

TEST_CASE("document snapshot restore fails atomically while a source is pinned") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, fixture.overview));
  REQUIRE(fixture.log.reset({2}));
  const production::TileKey missing{production::ZoomLevel::k100Percent, 0, 0};
  auto pin = fixture.canvas.pin(missing);
  REQUIRE(pin.has_value());
  fixture.next_overview.fill(0x1234U);

  CHECK_FALSE(production::restore_document_snapshot(fixture.log, fixture.canvas, {1},
                                                    fixture.next_overview));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{2});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{2});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("document snapshot restore fails atomically while an append is prepared") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  auto prepared = fixture.log.prepare({.samples = samples});
  REQUIRE(prepared.has_value());
  fixture.next_overview.fill(0x1234U);

  CHECK_FALSE(production::restore_document_snapshot(fixture.log, fixture.canvas, {4},
                                                    fixture.next_overview));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
  prepared->cancel();
}

TEST_CASE("settlement rehearsal replays validated slices and rebases after restore") {
  constexpr std::size_t kOperationCount = 8;
  std::array<production::OperationRecord, kOperationCount + 2U> records{};
  std::array<production::CompactOperationSample, kOperationCount + 2U> log_samples{};
  auto overview = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto next_overview = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  auto replay = std::make_unique<std::array<std::uint16_t, production::kOverviewPixels>>();
  std::array<production::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, production::kTilePixels> tile_pool{};
  std::array<std::uint16_t, production::kTilePixels> tile_scratch{};
  std::array<production::TileRevisionPublication, 1> publications{};
  std::array<production::TileKey, 1> affected{};
  production::OperationLog log(records, log_samples);
  production::MaterializedCanvas canvas(*overview, slots, tile_pool);
  overview->fill(0xFFFFU);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::copy(canvas.overview_pixels().begin(), canvas.overview_pixels().end(), replay->begin());
  const production::OperationLogEpoch original_epoch = log.epoch();
  const auto workspace = production::IncrementalDocumentWorkspace{
      .overview_scratch = *next_overview,
      .tile_scratch = tile_scratch,
      .publications = publications,
      .affected_keys = affected,
  };

  for (std::size_t index = 0; index < kOperationCount; ++index) {
    const std::array sample{production::CompactOperationSample{
        .x_quarter = static_cast<std::uint16_t>(40U + index * 32U),
        .y_quarter = static_cast<std::uint16_t>(80U + index * 24U),
        .radius_256 = 256}};
    REQUIRE(production::append_incrementally(
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
  REQUIRE(production::restore_document_snapshot(log, canvas, {3}, *next_overview));
  CHECK_FALSE(log.replay_range(original_epoch, {0}, {8}));
  const production::OperationLogEpoch restored_epoch = log.epoch();
  std::copy(canvas.overview_pixels().begin(), canvas.overview_pixels().end(), replay->begin());
  const std::array restored_sample{
      production::CompactOperationSample{.x_quarter = 400, .y_quarter = 400, .radius_256 = 512}};
  REQUIRE(production::append_incrementally(
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
      production::CompactOperationSample{.x_quarter = 400, .y_quarter = 400, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.tile_scratch = {};

  const auto result = production::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0x001FU, .samples = samples}, workspace);
  REQUIRE(result.has_value());
  CHECK(result->affected_resident_tiles == 0U);
  CHECK(fixture.log.current_revision() == production::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{1});
}
