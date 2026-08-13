#include "tinydraw/production/incremental_document.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace production = tinydraw::production;

namespace {

struct Fixture {
  std::array<production::OperationRecord, 4> records{};
  std::array<production::CompactOperationSample, 8> samples{};
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<std::uint16_t, production::kOverviewPixels> next_overview{};
  std::array<production::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, 2U * production::kTilePixels> tile_pool{};
  std::array<std::uint16_t, 2U * production::kTilePixels> tile_scratch{};
  std::array<production::TileRevisionPublication, 2> publications{};
  std::array<production::TileKey, 2> affected{};
  production::OperationLog log{records, samples};
  production::MaterializedCanvas canvas{overview, slots, tile_pool};

  production::IncrementalDocumentWorkspace workspace() {
    return {.next_overview = next_overview,
            .tile_scratch = tile_scratch,
            .publications = publications,
            .affected_keys = affected};
  }
};

}  // namespace

TEST_CASE("incremental document advances log and canvas together") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const production::TileKey at_100{production::ZoomLevel::k100Percent, 0, 0};
  const production::TileKey at_200{production::ZoomLevel::k200Percent, 1, 1};
  REQUIRE(
      fixture.canvas.publish_tile(at_100, {0}, production::MaterializationQuality::kSettled, tile));
  REQUIRE(
      fixture.canvas.publish_tile(at_200, {0}, production::MaterializationQuality::kSettled, tile));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };

  const auto result = production::append_incrementally(
      fixture.log, fixture.canvas, {.color = 0xF800U, .samples = samples}, fixture.workspace());
  REQUIRE(result.has_value());
  CHECK(result->identity == production::OperationIdentity{{1}, 0});
  CHECK(result->affected_resident_tiles == 2U);
  CHECK(result->published_tiles == 2U);
  CHECK(result->fallback_tiles == 0U);
  CHECK(fixture.log.current_revision() == production::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{1});
  CHECK(fixture.canvas.lookup(at_100)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.canvas.lookup(at_200)->kind == production::SourceKind::kTileSlot);
  CHECK(fixture.next_overview[3U * production::kOverviewWidth + 3U] == 0xF800U);
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
      {.next_overview = std::span(guarded).subspan(1, production::kOverviewPixels),
       .publications = publications,
       .affected_keys = affected}));
  CHECK(guarded.front() == 0xBEEFU);
  CHECK(guarded.back() == 0xBEEFU);
  CHECK(log.current_revision() == production::DocumentRevision{0});
  CHECK(canvas.current_revision() == production::DocumentRevision{0});
}

TEST_CASE("incremental document rejects workspace aliasing live canvas pixels") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({0}, fixture.overview));
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256}};
  auto workspace = fixture.workspace();
  workspace.next_overview = fixture.overview;

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
  workspace.tile_scratch = workspace.next_overview.first(production::kTilePixels);

  CHECK_FALSE(production::append_incrementally(fixture.log, fixture.canvas,
                                               {.color = 0xF800U, .samples = samples}, workspace));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{0});
  CHECK(fixture.canvas.overview_pixels().front() == 0xFFFFU);
}

TEST_CASE("document snapshot restore changes both authorities to an older revision") {
  Fixture fixture;
  fixture.overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({8}, fixture.overview));
  REQUIRE(fixture.log.reset({8}));
  fixture.next_overview.fill(0x1234U);

  REQUIRE(production::restore_document_snapshot(fixture.log, fixture.canvas, {3},
                                                fixture.next_overview));
  CHECK(fixture.log.current_revision() == production::DocumentRevision{3});
  CHECK(fixture.canvas.current_revision() == production::DocumentRevision{3});
  CHECK(fixture.log.operation_count() == 0U);
  CHECK(fixture.canvas.overview_pixels().front() == 0x1234U);
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
