#include <doctest.h>

#include "test_support/materialized_canvas_fixture.h"
#include "tinydraw/vector_v2/rerender_ledger.h"

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

TEST_CASE("staged overview publication is row-bounded and fails closed on mismatched resume") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_pool{};
  TestCanvas canvas(overview, slots, tile_pool);
  REQUIRE(canvas.publish_overview({0}, overview));
  auto original_entries = std::make_unique<
      std::array<vector_v2::RerenderLedgerEntry, vector_v2::kRerenderLedgerEntryCount>>();
  vector_v2::RerenderLedger original_ledger(*original_entries);
  canvas.set_rerender_ledger(&original_ledger);

  const vector_v2::PixelRect world_bounds{0, 0, 96, 96};
  const vector_v2::PixelRect bounds = vector_v2::overview_bounds_for_world(world_bounds);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  std::array<std::uint16_t, 24U * 24U> patch{};
  patch.fill(0x001FU);
  const vector_v2::OverviewRevisionPublication publication{
      .bounds = bounds,
      .pixels = std::span(patch).first(static_cast<std::size_t>(width * height)),
  };
  vector_v2::InPlaceOverviewStage stage;
  CHECK(canvas.stage_in_place_overview_rows({1}, publication, world_bounds, 1U, stage) ==
        vector_v2::OverviewStageStatus::kInProgress);
  REQUIRE(stage.active());
  CHECK_FALSE(stage.complete());
  const auto first = static_cast<std::size_t>(bounds.y0) * vector_v2::kOverviewWidth +
                     static_cast<std::size_t>(bounds.x0);
  CHECK(canvas.overview_pixels()[first] == 0x001FU);
  CHECK(canvas.overview_pixels()[first + vector_v2::kOverviewWidth] == 0xFFFFU);

  auto different_source = patch;
  const vector_v2::OverviewRevisionPublication mismatched{.bounds = bounds,
                                                          .pixels = different_source};
  CHECK(canvas.stage_in_place_overview_rows({1}, mismatched, world_bounds, 1U, stage) ==
        vector_v2::OverviewStageStatus::kError);
  CHECK(stage.active());
  CHECK(canvas.stage_in_place_overview_rows({1}, publication, world_bounds, 1U, stage) ==
        vector_v2::OverviewStageStatus::kInProgress);

  // Any intervening canvas mutation invalidates the proof before another row
  // or the metadata commit can be accepted.
  canvas.invalidate_identity({vector_v2::ZoomLevel::k100Percent, 0, 0});
  CHECK(canvas.stage_in_place_overview_rows({1}, publication, world_bounds, 1U, stage) ==
        vector_v2::OverviewStageStatus::kError);
  CHECK_FALSE(canvas.commit_staged_in_place_revision({1}, publication, world_bounds, {}, stage));
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});

  stage.cancel();
  while (!stage.complete()) {
    const auto status =
        canvas.stage_in_place_overview_rows({1}, publication, world_bounds, 1U, stage);
    REQUIRE(status != vector_v2::OverviewStageStatus::kError);
  }
  std::array<bool, 4> saw_phase{};
  bool rejected_metadata_mismatch = false;
  while (true) {
    const auto metadata = canvas.stage_in_place_metadata({.revision = {1},
                                                          .overview_publication = publication,
                                                          .affected_world_bounds = world_bounds,
                                                          .retained_keys = {},
                                                          .max_work_items = 1U,
                                                          .stage = stage});
    REQUIRE(metadata.status != vector_v2::OverviewStageStatus::kError);
    CHECK(metadata.work_items <= 1U);
    switch (metadata.phase) {
      case vector_v2::InPlaceMetadataPhase::kUniforms:
        saw_phase[0] = true;
        break;
      case vector_v2::InPlaceMetadataPhase::kRawSlots:
        saw_phase[1] = true;
        break;
      case vector_v2::InPlaceMetadataPhase::kRerenderDamage:
        saw_phase[2] = true;
        break;
      case vector_v2::InPlaceMetadataPhase::kOccupancy:
        saw_phase[3] = true;
        break;
      case vector_v2::InPlaceMetadataPhase::kComplete:
        break;
    }
    if (!rejected_metadata_mismatch) {
      const vector_v2::MaterializedCanvas::InPlaceCommitScope mismatch{
          .preserved_uniform_color = std::nullopt,
          .priority_zoom = vector_v2::ZoomLevel::k100Percent,
          .cross_zoom_invalidated = nullptr};
      const auto rejected = canvas.stage_in_place_metadata({.revision = {1},
                                                            .overview_publication = publication,
                                                            .affected_world_bounds = world_bounds,
                                                            .retained_keys = {},
                                                            .max_work_items = 1U,
                                                            .stage = stage,
                                                            .scope = mismatch});
      CHECK(rejected.status == vector_v2::OverviewStageStatus::kError);
      CHECK(stage.active());
      rejected_metadata_mismatch = true;
    }
    if (metadata.status == vector_v2::OverviewStageStatus::kComplete) {
      break;
    }
  }
  CHECK(std::all_of(saw_phase.begin(), saw_phase.end(), [](bool seen) { return seen; }));

  auto replacement_entries = std::make_unique<
      std::array<vector_v2::RerenderLedgerEntry, vector_v2::kRerenderLedgerEntryCount>>();
  vector_v2::RerenderLedger replacement_ledger(*replacement_entries);
  canvas.set_rerender_ledger(&replacement_ledger);
  CHECK_FALSE(canvas.commit_staged_in_place_revision({1}, publication, world_bounds, {}, stage));
  CHECK(stage.active());
  canvas.set_rerender_ledger(&original_ledger);
  REQUIRE(canvas.commit_staged_in_place_revision({1}, publication, world_bounds, {}, stage));
  CHECK_FALSE(stage.active());
  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{1});
  for (int row = bounds.y0; row < bounds.y1; ++row) {
    const auto offset = static_cast<std::size_t>(row) * vector_v2::kOverviewWidth +
                        static_cast<std::size_t>(bounds.x0);
    CHECK(std::all_of(canvas.overview_pixels().begin() + static_cast<std::ptrdiff_t>(offset),
                      canvas.overview_pixels().begin() +
                          static_cast<std::ptrdiff_t>(offset + static_cast<std::size_t>(width)),
                      [](std::uint16_t pixel) { return pixel == 0x001FU; }));
  }
}

TEST_CASE("staged metadata commit matches synchronous mixed materialization commit") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> direct_overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> staged_overview{};
  std::array<vector_v2::MaterializedSlotStorage, 3> direct_slots{};
  std::array<vector_v2::MaterializedSlotStorage, 3> staged_slots{};
  std::array<std::uint16_t, 3U * vector_v2::kTilePixels> direct_pool{};
  std::array<std::uint16_t, 3U * vector_v2::kTilePixels> staged_pool{};
  TestCanvas direct(direct_overview, direct_slots, direct_pool);
  TestCanvas staged(staged_overview, staged_slots, staged_pool);
  REQUIRE(direct.reset_blank({0}));
  REQUIRE(staged.reset_blank({0}));

  auto direct_entries = std::make_unique<
      std::array<vector_v2::RerenderLedgerEntry, vector_v2::kRerenderLedgerEntryCount>>();
  auto staged_entries = std::make_unique<
      std::array<vector_v2::RerenderLedgerEntry, vector_v2::kRerenderLedgerEntryCount>>();
  vector_v2::RerenderLedger direct_ledger(*direct_entries);
  vector_v2::RerenderLedger staged_ledger(*staged_entries);
  direct.set_rerender_ledger(&direct_ledger);
  staged.set_rerender_ledger(&staged_ledger);

  const vector_v2::TileKey raw_kept{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey raw_dropped{vector_v2::ZoomLevel::k200Percent, 0, 0};
  const vector_v2::TileKey raw_unaffected{vector_v2::ZoomLevel::k100Percent, 3, 3};
  const vector_v2::TileKey uniform_kept{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey uniform_preserved{vector_v2::ZoomLevel::k50Percent, 0, 0};
  const vector_v2::TileKey uniform_dropped{vector_v2::ZoomLevel::k400Percent, 0, 0};
  const vector_v2::TileKey uniform_unaffected{vector_v2::ZoomLevel::k100Percent, 4, 4};
  const std::array raw_keys{raw_kept, raw_dropped, raw_unaffected};
  const std::array uniform_keys{uniform_kept, uniform_preserved, uniform_dropped,
                                uniform_unaffected};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x1234U);
  for (const auto key : raw_keys) {
    REQUIRE(direct.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
    REQUIRE(staged.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  }
  for (const auto key : uniform_keys) {
    const std::uint16_t color = key == uniform_preserved ? 0xEEEEU : 0x4321U;
    REQUIRE(direct.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kImmediate, color));
    REQUIRE(staged.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kImmediate, color));
  }
  static_cast<void>(
      direct_ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 0, 0, {0}));
  static_cast<void>(
      staged_ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 0, 0, {0}));

  const vector_v2::PixelRect world_bounds{0, 0, 96, 96};
  const vector_v2::PixelRect bounds = vector_v2::overview_bounds_for_world(world_bounds);
  std::array<std::uint16_t, 24U * 24U> patch{};
  patch.fill(0x001FU);
  const vector_v2::OverviewRevisionPublication publication{
      .bounds = bounds,
      .pixels = std::span(patch).first(static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                       static_cast<std::size_t>(bounds.y1 - bounds.y0)),
  };
  // The unaffected uniform is deliberately superfluous: retained membership
  // must not leak transient state when a caller supplies an extra key.
  const std::array retained{raw_kept, uniform_kept, uniform_unaffected};
  std::size_t direct_cross_zoom = 0;
  std::size_t staged_cross_zoom = 0;
  const vector_v2::MaterializedCanvas::InPlaceCommitScope direct_scope{
      .preserved_uniform_color = 0xEEEEU,
      .priority_zoom = vector_v2::ZoomLevel::k100Percent,
      .cross_zoom_invalidated = &direct_cross_zoom};
  const vector_v2::MaterializedCanvas::InPlaceCommitScope staged_scope{
      .preserved_uniform_color = 0xEEEEU,
      .priority_zoom = vector_v2::ZoomLevel::k100Percent,
      .cross_zoom_invalidated = &staged_cross_zoom};
  REQUIRE(direct.commit_in_place_revision({1}, publication, world_bounds, retained, direct_scope));

  vector_v2::InPlaceOverviewStage stage;
  while (!stage.complete()) {
    REQUIRE(staged.stage_in_place_overview_rows({1}, publication, world_bounds, 3U, stage) !=
            vector_v2::OverviewStageStatus::kError);
  }
  while (true) {
    const auto slice = staged.stage_in_place_metadata({.revision = {1},
                                                       .overview_publication = publication,
                                                       .affected_world_bounds = world_bounds,
                                                       .retained_keys = retained,
                                                       .max_work_items = 3U,
                                                       .stage = stage,
                                                       .scope = staged_scope});
    REQUIRE(slice.status != vector_v2::OverviewStageStatus::kError);
    if (slice.status == vector_v2::OverviewStageStatus::kComplete) {
      break;
    }
  }
  REQUIRE(staged.commit_staged_in_place_revision({1}, publication, world_bounds, retained, stage,
                                                 staged_scope));

  CHECK(staged_overview == direct_overview);
  CHECK(staged_cross_zoom == direct_cross_zoom);
  CHECK(staged_cross_zoom == 2U);
  CHECK(staged.resident_raw_tiles() == direct.resident_raw_tiles());
  for (const auto key : std::array{raw_kept, raw_dropped, raw_unaffected, uniform_kept,
                                   uniform_preserved, uniform_dropped, uniform_unaffected}) {
    const auto direct_source = direct.lookup(key);
    const auto staged_source = staged.lookup(key);
    REQUIRE(direct_source.has_value());
    REQUIRE(staged_source.has_value());
    CHECK(staged_source->kind == direct_source->kind);
    CHECK(staged_source->revision == direct_source->revision);
    CHECK(staged_source->quality == direct_source->quality);
    CHECK(staged.uniform_color(key) == direct.uniform_color(key));
  }
  CHECK(staged.certainly_paper(raw_kept) == direct.certainly_paper(raw_kept));
  CHECK(staged.certainly_paper(raw_unaffected) == direct.certainly_paper(raw_unaffected));
  CHECK_FALSE(staged.certainly_paper(raw_kept));
  CHECK(staged.certainly_paper(raw_unaffected));
  CHECK(direct_ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 0, 0, {1}) ==
        vector_v2::RerenderCause::kExpectedDamage);
  CHECK(staged_ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 0, 0, {1}) ==
        vector_v2::RerenderCause::kExpectedDamage);
}

TEST_CASE("cancelled retained-key prepass restores raw and uniform lookup state") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, 2U * vector_v2::kTilePixels> tile_pool{};
  TestCanvas canvas(overview, slots, tile_pool);
  REQUIRE(canvas.reset_blank({0}));

  const vector_v2::TileKey raw{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey uniform{vector_v2::ZoomLevel::k200Percent, 0, 0};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0x1234U);
  REQUIRE(canvas.publish_tile(raw, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(
      canvas.publish_uniform(uniform, {0}, vector_v2::MaterializationQuality::kSettled, 0xEEEEU));

  const vector_v2::PixelRect world_bounds{0, 0, 96, 96};
  const auto bounds = vector_v2::overview_bounds_for_world(world_bounds);
  std::array<std::uint16_t, 24U * 24U> patch{};
  const vector_v2::OverviewRevisionPublication publication{
      .bounds = bounds,
      .pixels = std::span(patch).first(static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                       static_cast<std::size_t>(bounds.y1 - bounds.y0)),
  };
  vector_v2::InPlaceOverviewStage stage;
  while (!stage.complete()) {
    REQUIRE(canvas.stage_in_place_overview_rows({1}, publication, world_bounds, 4U, stage) !=
            vector_v2::OverviewStageStatus::kError);
  }
  const std::array retained{uniform, raw};
  const auto first = canvas.stage_in_place_metadata({.revision = {1},
                                                     .overview_publication = publication,
                                                     .affected_world_bounds = world_bounds,
                                                     .retained_keys = retained,
                                                     .max_work_items = 1U,
                                                     .stage = stage});
  REQUIRE(first.status == vector_v2::OverviewStageStatus::kInProgress);
  CHECK(first.phase == vector_v2::InPlaceMetadataPhase::kUniforms);
  CHECK(first.work_items == 1U);
  stage.cancel();

  CHECK(canvas.current_revision() == vector_v2::DocumentRevision{0});
  const auto raw_source = canvas.lookup(raw);
  const auto uniform_source = canvas.lookup(uniform);
  REQUIRE(raw_source.has_value());
  REQUIRE(uniform_source.has_value());
  CHECK(raw_source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(raw_source->revision == vector_v2::DocumentRevision{0});
  CHECK(uniform_source->kind == vector_v2::SourceKind::kUniform);
  CHECK(uniform_source->quality == vector_v2::MaterializationQuality::kSettled);
  CHECK(canvas.uniform_color(uniform) == 0xEEEEU);
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
