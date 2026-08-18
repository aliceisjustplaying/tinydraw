#include "tinydraw/vector_v2/tile_producer.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/adversarial_tapered_corpus.h"
#include "tinydraw/vector_v2/incremental_document.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct PaperFixture {
  std::array<vector_v2::OperationRecord, 4> records{};
  std::array<vector_v2::CompactOperationSample, 16> samples{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> snapshot{};
  std::unique_ptr<
      std::array<vector_v2::MaterializedUniformStorage, vector_v2::kMaterializedTileIdentityCount>>
      uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                             vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile_pool{};
  std::unique_ptr<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>
      raw_slot_directory =
          std::make_unique<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint16_t, vector_v2::kTileProducerPixels> supertask{};
  std::array<std::uint8_t, vector_v2::kTileProducerMaskBytes> mask{};
  std::array<std::uint16_t, vector_v2::kTileProducerSummaryRows> summary_rows{};
  std::array<std::uint32_t, vector_v2::kTileProducerSummaryWords> summary_words{};
  std::array<std::uint32_t, vector_v2::kOperationChordStorageBytes / 4U> chord_plans{};
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{
      overview,           *uniforms, occupancy, slots, tile_pool, vector_v2::DocumentRevision{},
      *raw_slot_directory};
  vector_v2::TileProducer producer{
      log,
      canvas,
      {.supertask_pixels = supertask,
       .finalized_pixels = mask,
       .summary_row_unset = summary_rows,
       .summary_saturated_words = summary_words,
       .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans))}};

  PaperFixture() {
    snapshot.fill(0xFFFFU);
    REQUIRE(canvas.reset_blank({0}));
  }
};

struct AdversarialFixture {
  std::vector<vector_v2::OperationRecord> records = std::vector<vector_v2::OperationRecord>(
      vector_v2::test_support::kAdversarialTaperedOperationCount);
  std::vector<vector_v2::CompactOperationSample> samples =
      std::vector<vector_v2::CompactOperationSample>(
          vector_v2::test_support::kAdversarialTaperedSampleCount);
  std::vector<std::uint16_t> overview = std::vector<std::uint16_t>(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> snapshot =
      std::vector<std::uint16_t>(vector_v2::kOverviewPixels, 0xFFFFU);
  std::unique_ptr<
      std::array<vector_v2::MaterializedUniformStorage, vector_v2::kMaterializedTileIdentityCount>>
      uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                             vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::unique_ptr<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>
      raw_slot_directory =
          std::make_unique<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>();
  std::array<vector_v2::MaterializedSlotStorage, 4> slots{};
  std::vector<std::uint16_t> tile_pool =
      std::vector<std::uint16_t>(slots.size() * vector_v2::kTilePixels);
  std::array<std::uint16_t, vector_v2::kTileProducerPixels> supertask{};
  std::array<std::uint8_t, vector_v2::kTileProducerMaskBytes> mask{};
  std::array<std::uint16_t, vector_v2::kTileProducerSummaryRows> summary_rows{};
  std::array<std::uint32_t, vector_v2::kTileProducerSummaryWords> summary_words{};
  std::array<std::uint32_t, vector_v2::kOperationChordStorageBytes / 4U> chord_plans{};
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{overview,  *uniforms, occupancy,          slots,
                                       tile_pool, {},        *raw_slot_directory};
  vector_v2::TileProducer producer{
      log,
      canvas,
      {.supertask_pixels = supertask,
       .finalized_pixels = mask,
       .summary_row_unset = summary_rows,
       .summary_saturated_words = summary_words,
       .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans))}};

  AdversarialFixture() {
    overview.assign(overview.size(), 0xFFFFU);
    REQUIRE(canvas.publish_overview({0}, overview));
  }
};

struct Fixture {
  static constexpr std::size_t kOperationCapacity = 96;
  static constexpr std::size_t kSpatialWords =
      vector_v2::operation_spatial_word_count(kOperationCapacity);
  std::array<vector_v2::OperationRecord, kOperationCapacity> records{};
  std::array<vector_v2::CompactOperationSample, 2'048> samples{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::unique_ptr<
      std::array<vector_v2::MaterializedUniformStorage, vector_v2::kMaterializedTileIdentityCount>>
      uniforms = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                             vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::unique_ptr<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>
      raw_slot_directory =
          std::make_unique<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>();
  std::array<vector_v2::MaterializedSlotStorage, 64> slots{};
  std::array<std::uint16_t, 64U * vector_v2::kTilePixels> tile_pool{};
  std::array<std::uint16_t, vector_v2::kTileProducerPixels> supertask{};
  std::array<std::uint8_t, vector_v2::kTileProducerMaskBytes> mask{};
  std::array<std::uint16_t, vector_v2::kTileProducerSummaryRows> summary_rows{};
  std::array<std::uint32_t, vector_v2::kTileProducerSummaryWords> summary_words{};
  std::array<std::uint32_t, vector_v2::kOperationChordStorageBytes / 4U> chord_plans{};
  std::array<std::uint64_t, vector_v2::kOperationSpatialCellCount * kSpatialWords> spatial_cells{};
  std::array<std::uint64_t, kSpatialWords> spatial_large{};
  std::array<std::uint16_t, kOperationCapacity> candidates{};
  vector_v2::OperationSpatialIndex spatial_index{kOperationCapacity, spatial_cells, spatial_large};
  vector_v2::OperationLog log{records, samples, &spatial_index};
  vector_v2::MaterializedCanvas canvas{overview,  *uniforms, occupancy,          slots,
                                       tile_pool, {},        *raw_slot_directory};
  vector_v2::TileProducer producer{
      log,
      canvas,
      {.supertask_pixels = supertask,
       .finalized_pixels = mask,
       .summary_row_unset = summary_rows,
       .summary_saturated_words = summary_words,
       .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans)),
       .candidate_indices = candidates}};

  Fixture() {
    overview.fill(0xFFFFU);
    REQUIRE(canvas.publish_overview({0}, overview));
  }
};

vector_v2::OperationAppend append(std::span<const vector_v2::CompactOperationSample> samples,
                                  std::uint16_t color = 0xF800U,
                                  vector_v2::OperationTool tool = vector_v2::OperationTool::kPen) {
  return {.tool = tool, .color = color, .samples = samples};
}

}  // namespace

TEST_CASE("adversarial tapered corpus is four times the dense physical sample count") {
  AdversarialFixture fixture;
  vector_v2::test_support::AdversarialTaperedCorpusStats stats{};
  REQUIRE(vector_v2::test_support::emit_adversarial_tapered_corpus(
      [&](const vector_v2::OperationAppend& operation) {
        return fixture.log.append(operation).has_value();
      },
      &stats));
  CHECK(stats.operations == 128U);
  CHECK(stats.samples == 4'096U);
  CHECK(stats.erasers == 12U);
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{128});
  REQUIRE(fixture.canvas.publish_overview(fixture.log.current_revision(), fixture.snapshot));

  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {64, 64, 192, 192},
  };
  std::size_t replay_slices = 0;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    replay_slices += step->tiles_published == 0U;
    if (step->complete) {
      break;
    }
  }
  CHECK(replay_slices > 1U);

  std::vector<std::uint16_t> composed(128U * 128U);
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  for (std::size_t index = 0; index < fixture.log.operation_count(); ++index) {
    const auto operation = fixture.log.operation(index);
    REQUIRE(operation.has_value());
    REQUIRE(vector_v2::apply_incremental_operation(
        {.tool = operation->tool, .color = operation->color, .samples = operation->samples},
        {.zoom = view.zoom, .level_bounds = view.level_pixels, .pixels = direct, .stride = 128}));
  }
  CHECK(composed == direct);
}

TEST_CASE("tile producer rebuilds invalidated pixels after Undo and Redo") {
  PaperFixture fixture;
  const std::array stroke{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 640, .y_quarter = 640, .radius_256 = 512},
  };
  REQUIRE(fixture.log.append({.color = 0xF800U, .gesture_id = 1U, .samples = stroke}));
  fixture.snapshot.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0xF800U, .samples = stroke},
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
       .pixels = fixture.snapshot,
       .stride = vector_v2::kOverviewWidth}));
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  REQUIRE(vector_v2::build_tiled_may_ink(fixture.log, may_ink));
  REQUIRE(
      fixture.canvas.restore_snapshot(fixture.log.current_revision(), fixture.snapshot, may_ink));
  REQUIRE(vector_v2::move_history_incrementally(
      fixture.log, fixture.canvas, vector_v2::HistoryDirection::kUndo, fixture.snapshot));
  REQUIRE(vector_v2::move_history_incrementally(
      fixture.log, fixture.canvas, vector_v2::HistoryDirection::kRedo, fixture.snapshot));

  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    if (step->complete) {
      break;
    }
  }
  std::array<std::uint16_t, 128U * 128U> composed{};
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::array<std::uint16_t, 128U * 128U> direct{};
  direct.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0xF800U, .samples = stroke},
      {.zoom = view.zoom, .level_bounds = view.level_pixels, .pixels = direct, .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer publishes certainly-paper tiles in natural supertask groups") {
  PaperFixture fixture;
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 128, 128},
  };

  const auto step = fixture.producer.produce_next(view);
  REQUIRE(step.has_value());
  CHECK(step->tiles_published == 4U);
  CHECK(step->complete);
  CHECK(step->level_bounds == vector_v2::PixelRect{0, 0, 128, 128});
  for (std::uint16_t row = 0; row < 2; ++row) {
    for (std::uint16_t column = 0; column < 2; ++column) {
      CHECK(fixture.canvas.lookup({vector_v2::ZoomLevel::k100Percent, column, row})->kind ==
            vector_v2::SourceKind::kUniform);
    }
  }
}

TEST_CASE("sparse autosave occupancy publishes blank high zoom group without authority scan") {
  PaperFixture fixture;
  const std::array distant{
      vector_v2::CompactOperationSample{.x_quarter = 1'200U * vector_v2::kSampleUnitsPerWorldUnit,
                                        .y_quarter = 1'500U * vector_v2::kSampleUnitsPerWorldUnit,
                                        .radius_256 = 8U * 256U}};
  REQUIRE(fixture.log.append(append(distant, 0x001FU)));
  REQUIRE(vector_v2::replay_active_overview(fixture.log, fixture.snapshot));
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  REQUIRE(vector_v2::build_tiled_may_ink(fixture.log, may_ink));
  REQUIRE(
      fixture.canvas.restore_snapshot(fixture.log.current_revision(), fixture.snapshot, may_ink));

  const auto step = fixture.producer.produce_next(
      {.zoom = vector_v2::ZoomLevel::k400Percent, .level_pixels = {0, 0, 128, 128}});
  REQUIRE(step.has_value());
  CHECK(step->complete);
  CHECK(step->operations_scanned == 0U);
  CHECK(step->tiles_published == 4U);
  for (std::uint16_t row = 0; row < 2; ++row) {
    for (std::uint16_t column = 0; column < 2; ++column) {
      CHECK(fixture.canvas.lookup({vector_v2::ZoomLevel::k400Percent, column, row})->kind ==
            vector_v2::SourceKind::kUniform);
    }
  }
}

TEST_CASE("authority may-ink preserves a 400 percent hairline on an occupancy boundary") {
  PaperFixture fixture;
  const std::array hairline{
      vector_v2::CompactOperationSample{.x_quarter = 16U * vector_v2::kSampleUnitsPerWorldUnit,
                                        .y_quarter = 16U * vector_v2::kSampleUnitsPerWorldUnit,
                                        .radius_256 = 1U}};
  REQUIRE(fixture.log.append(append(hairline, 0x001FU)));
  REQUIRE(vector_v2::replay_active_overview(fixture.log, fixture.snapshot));
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> may_ink{};
  REQUIRE(vector_v2::build_tiled_may_ink(fixture.log, may_ink));
  REQUIRE(
      fixture.canvas.restore_snapshot(fixture.log.current_revision(), fixture.snapshot, may_ink));

  const vector_v2::ViewRequest view{.zoom = vector_v2::ZoomLevel::k400Percent,
                                    .level_pixels = {64, 64, 128, 128}};
  std::size_t scanned = 0U;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    scanned += step->operations_scanned;
    if (step->complete) {
      break;
    }
  }
  CHECK(scanned > 0U);
  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::array<std::uint16_t, vector_v2::kTilePixels> direct{};
  direct.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      append(hairline, 0x001FU),
      {.zoom = view.zoom, .level_bounds = view.level_pixels, .pixels = direct, .stride = 64}));
  CHECK(std::any_of(direct.begin(), direct.end(),
                    [](std::uint16_t pixel) { return pixel != 0xFFFFU; }));
  CHECK(composed == direct);
}

TEST_CASE("tile producer ignores overview fallback when finding missing resident tiles") {
  Fixture fixture;
  REQUIRE(fixture.producer.ready());
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 128, 128},
  };

  REQUIRE(fixture.canvas.lookup({vector_v2::ZoomLevel::k100Percent, 0, 0})->kind ==
          vector_v2::SourceKind::kOverview);
  REQUIRE(fixture.producer.visible_tiles_remaining(view) == 4U);
  std::size_t published = 0;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    published += step->tiles_published;
    if (step->complete) {
      CHECK(step->visible_tiles_remaining == 0U);
      break;
    }
  }
  CHECK(published == 4U);
  const auto source = fixture.canvas.lookup({vector_v2::ZoomLevel::k100Percent, 0, 0});
  REQUIRE(source.has_value());
  CHECK(source->kind == vector_v2::SourceKind::kUniform);
  CHECK(source->quality == vector_v2::MaterializationQuality::kImmediate);
}

TEST_CASE("tile producer publishes one completed supertask as a group") {
  Fixture fixture;
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {63, 63, 63 + vector_v2::kOverviewWidth, 63 + vector_v2::kOverviewHeight},
  };

  const auto step = fixture.producer.produce_next(view);
  REQUIRE(step.has_value());
  CHECK(step->tiles_published == 4U);
  CHECK(step->level_bounds.x1 - step->level_bounds.x0 == vector_v2::kTileProducerWidth);
  CHECK(step->level_bounds.y1 - step->level_bounds.y0 == vector_v2::kTileProducerHeight);
}

TEST_CASE("tile producer output equals direct painter-ordered viewport replay") {
  Fixture fixture;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 320, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 2400, .y_quarter = 1600, .radius_256 = 1024},
  };
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 1200, .y_quarter = 400, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 1200, .y_quarter = 2400, .radius_256 = 512},
  };
  const std::array erased{
      vector_v2::CompactOperationSample{.x_quarter = 1120, .y_quarter = 1040, .radius_256 = 384},
      vector_v2::CompactOperationSample{.x_quarter = 1440, .y_quarter = 1360, .radius_256 = 384},
  };
  REQUIRE(fixture.log.append(append(first, 0xF800U)));
  REQUIRE(fixture.log.append(append(second, 0x001FU)));
  REQUIRE(fixture.log.append(append(erased, 0, vector_v2::OperationTool::kEraser)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({3}, revised_overview));

  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {32, 16, 224, 176},
  };
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    if (step->complete) {
      break;
    }
  }

  std::vector<std::uint16_t> composed(192U * 160U);
  const auto stats = fixture.canvas.compose_view(view, composed);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == 0U);
  CHECK(stats->immediate_tiles == 12U);
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  for (std::size_t index = 0; index < fixture.log.operation_count(); ++index) {
    const auto stored = fixture.log.operation(index);
    REQUIRE(stored.has_value());
    REQUIRE(
        vector_v2::apply_incremental_operation(append(stored->samples, stored->color, stored->tool),
                                               {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                .level_bounds = view.level_pixels,
                                                .pixels = direct,
                                                .stride = 192}));
  }
  CHECK(composed == direct);
}

TEST_CASE("tile producer scans painter order once per supertask and skips distant operations") {
  Fixture fixture;
  const std::array visible{
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 320, .radius_256 = 256}};
  const std::array distant{vector_v2::CompactOperationSample{
      .x_quarter = 16U * 1'000U, .y_quarter = 16U * 1'000U, .radius_256 = 256}};
  REQUIRE(fixture.log.append(append(visible)));
  REQUIRE(fixture.log.append(append(distant)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, revised_overview));

  const auto step = fixture.producer.produce_next(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}});
  REQUIRE(step.has_value());
  CHECK(step->operations_in_authority == 2U);
  CHECK(step->index_candidates == 1U);
  CHECK(step->deduplicated_candidates == 1U);
  CHECK(step->operations_scanned == 1U);
  CHECK(step->operations_intersecting == 1U);
  CHECK(step->operations_rendered == 1U);
  CHECK(step->tiles_published == 1U);
}

TEST_CASE("tile producer rejects distant segments inside an overlapping operation bound") {
  Fixture fixture;
  const std::array around_view{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 4'800, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 4'800, .y_quarter = 4'800, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 4'800, .y_quarter = 0, .radius_256 = 256},
  };
  REQUIRE(fixture.log.append(append(around_view, 0x001FU)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));

  const auto step = fixture.producer.produce_next(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 128, 128}});
  REQUIRE(step.has_value());
  CHECK(step->complete);
  CHECK(step->operations_scanned == 1U);
  CHECK(step->operations_rendered == 0U);
  CHECK(step->raster_steps == 0U);
  CHECK(step->raster_work == 0U);
  CHECK(step->tiles_published == 4U);
}

TEST_CASE("constant-radius collinear line replay matches forward painting exactly") {
  // Reverse replay must use the same per-sample segment decomposition as the
  // forward authority. Coalescing a collinear run into one long capsule is
  // equal only in real arithmetic; covers_pixel float rounding can flip
  // boundary pixels, so no coalescing is permitted.
  Fixture fixture;
  std::array<vector_v2::CompactOperationSample, 100> line{};
  for (std::size_t index = 0; index < line.size(); ++index) {
    line[index] = {.x_quarter = static_cast<std::uint16_t>(40U + index * 4U),
                   .y_quarter = static_cast<std::uint16_t>(40U + index * 8U),
                   .radius_256 = 256};
  }
  REQUIRE(fixture.log.append(append(line, 0x001FU)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));

  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  std::size_t raster_steps = 0;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    raster_steps += step->raster_steps;
    if (step->complete) {
      break;
    }
  }
  // Segments beyond the 128x128 view are bbox-rejected; the visible run must
  // still paint one bounded unit per source segment, never one long capsule.
  CHECK(raster_steps > 1U);

  std::vector<std::uint16_t> composed(128U * 128U);
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      append(line, 0x001FU),
      {.zoom = view.zoom, .level_bounds = view.level_pixels, .pixels = direct, .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer validates uniform baseline reset") {
  Fixture fixture;
  CHECK(fixture.producer.reset_uniform_baseline({0}));
  const std::array point{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(fixture.log.append(append(point)));
  CHECK_FALSE(fixture.producer.reset_uniform_baseline({1}));
}

TEST_CASE("tile producer sliced long strokes equal direct painter replay") {
  Fixture fixture;
  std::vector<vector_v2::CompactOperationSample> long_stroke(400);
  for (std::size_t index = 0; index < long_stroke.size(); ++index) {
    long_stroke[index] = {
        .x_quarter = static_cast<std::uint16_t>(32U + index),
        .y_quarter = static_cast<std::uint16_t>(120U + index % 80U),
        .radius_256 = static_cast<std::uint16_t>(index % 2U == 0U ? 5'120U : 3'328U),
    };
  }
  REQUIRE(fixture.log.append(append(long_stroke, 0x001FU)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  std::size_t partial_steps = 0;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    partial_steps += step->tiles_published == 0U;
    if (step->complete) {
      break;
    }
  }
  // The replay must stay resumable: at least one bounded slice returns
  // before anything publishes. The historical >= 4 floor tracked per-unit
  // work budgets; the op-level chord sweep (H7) saturates this fat stroke's
  // group within fewer, still-bounded slices.
  CHECK(partial_steps >= 1U);

  std::vector<std::uint16_t> composed(128U * 128U);
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(append(long_stroke, 0x001FU),
                                                 {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                  .level_bounds = view.level_pixels,
                                                  .pixels = direct,
                                                  .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("cancelled producer work restarts exactly with reusable chord storage") {
  Fixture fixture;
  std::vector<vector_v2::CompactOperationSample> long_stroke(400);
  for (std::size_t index = 0; index < long_stroke.size(); ++index) {
    long_stroke[index] = {
        .x_quarter = static_cast<std::uint16_t>(32U + index),
        .y_quarter = static_cast<std::uint16_t>(120U + index % 80U),
        .radius_256 = static_cast<std::uint16_t>(index % 2U == 0U ? 5'120U : 3'328U),
    };
  }
  REQUIRE(fixture.log.append(append(long_stroke, 0x001FU)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  const auto partial = fixture.producer.produce_next(view);
  REQUIRE(partial.has_value());
  REQUIRE(partial->tiles_published == 0U);

  fixture.producer.cancel_pending_work();
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    if (step->complete) {
      break;
    }
  }

  std::vector<std::uint16_t> composed(128U * 128U);
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      append(long_stroke, 0x001FU),
      {.zoom = view.zoom, .level_bounds = view.level_pixels, .pixels = direct, .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer isolates an oversized newest segment from older raster work") {
  Fixture fixture;
  const std::array oversized_segments{
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 800, .y_quarter = 800, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 192, .y_quarter = 192, .radius_256 = 5'120},
  };
  REQUIRE(fixture.log.append(append(oversized_segments, 0x001FU)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));

  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  const auto first = fixture.producer.produce_next(view);
  REQUIRE(first.has_value());
  CHECK_FALSE(first->complete);
  // The oversized unit sweeps as one work-budgeted batch: the first slice
  // pauses mid-sweep (rendered work is credited when the batch completes),
  // and no tile publishes before the replay is whole.
  CHECK(first->raster_work <= 3U * vector_v2::kTileProducerPixels);
  CHECK(first->tiles_published == 0U);

  std::size_t rendered = first->operations_rendered;
  std::size_t raster_steps = first->raster_steps;
  std::size_t ticks = 1U;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    rendered += step->operations_rendered;
    raster_steps += step->raster_steps;
    ++ticks;
    REQUIRE(ticks < 64U);
    if (step->complete) {
      break;
    }
    CHECK(step->raster_work <= 3U * vector_v2::kTileProducerPixels);
  }
  CHECK(rendered == 1U);
  CHECK(raster_steps >= 1U);

  std::vector<std::uint16_t> composed(128U * 128U);
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      append(oversized_segments, 0x001FU),
      {.zoom = view.zoom, .level_bounds = view.level_pixels, .pixels = direct, .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer restarts after revision changes during sliced replay") {
  Fixture fixture;
  std::vector<vector_v2::CompactOperationSample> long_stroke(400);
  for (std::size_t index = 0; index < long_stroke.size(); ++index) {
    long_stroke[index] = {
        .x_quarter = static_cast<std::uint16_t>(64U + index),
        .y_quarter = static_cast<std::uint16_t>(160U + index % 64U),
        .radius_256 = 3'328U,
    };
  }
  REQUIRE(fixture.log.append(append(long_stroke, 0x001FU)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview_one{};
  overview_one.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, overview_one));
  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  REQUIRE(fixture.producer.produce_next(view));

  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 320, .radius_256 = 2'048},
      vector_v2::CompactOperationSample{.x_quarter = 1440, .y_quarter = 1440, .radius_256 = 2'048},
  };
  REQUIRE(fixture.log.append(append(second, 0xF800U)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview_two{};
  overview_two.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, overview_two));
  const auto restarted = fixture.producer.produce_next(view);
  REQUIRE(restarted.has_value());
  const bool made_progress = restarted->raster_steps != 0U || restarted->tiles_published != 0U;
  CHECK(made_progress);
  while (!restarted->complete) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    if (step->complete) {
      break;
    }
  }

  std::vector<std::uint16_t> composed(128U * 128U);
  REQUIRE(fixture.canvas.compose_view(view, composed));
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(append(long_stroke, 0x001FU),
                                                 {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                  .level_bounds = view.level_pixels,
                                                  .pixels = direct,
                                                  .stride = 128}));
  REQUIRE(vector_v2::apply_incremental_operation(append(second, 0xF800U),
                                                 {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                  .level_bounds = view.level_pixels,
                                                  .pixels = direct,
                                                  .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer rejects 25 percent and aliased or short workspace") {
  Fixture fixture;
  CHECK_FALSE(fixture.producer.produce_next(
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}}));

  vector_v2::TileProducer short_workspace{
      fixture.log,
      fixture.canvas,
      {.supertask_pixels = std::span(fixture.supertask).first(1),
       .finalized_pixels = fixture.mask,
       .summary_row_unset = fixture.summary_rows,
       .summary_saturated_words = fixture.summary_words,
       .operation_chord_plans = std::as_writable_bytes(std::span(fixture.chord_plans))},
  };
  CHECK_FALSE(short_workspace.ready());
  vector_v2::TileProducer aliased_workspace{
      fixture.log,
      fixture.canvas,
      {.supertask_pixels = fixture.supertask,
       .finalized_pixels = fixture.mask,
       .summary_row_unset = std::span(fixture.supertask).first(vector_v2::kTileProducerSummaryRows),
       .summary_saturated_words = fixture.summary_words,
       .operation_chord_plans = std::as_writable_bytes(std::span(fixture.chord_plans))},
  };
  CHECK_FALSE(aliased_workspace.ready());
}

TEST_CASE("saturated groups complete without replaying buried older work") {
  // Many older multi-sample operations sit entirely under one newest opaque
  // cover. Saturation gating must complete the group in a handful of slices
  // instead of rasterizing every buried segment, and stay bit-exact.
  Fixture fixture;
  std::array<vector_v2::CompactOperationSample, 24> zigzag{};
  for (std::size_t operation = 0; operation < 80U; ++operation) {
    for (std::size_t index = 0; index < zigzag.size(); ++index) {
      const int x = 40 + static_cast<int>((operation * 29U + index * 37U) % 400U);
      const int y = 40 + static_cast<int>((operation * 41U + index * 23U) % 400U);
      zigzag[index] = {.x_quarter = static_cast<std::uint16_t>(x),
                       .y_quarter = static_cast<std::uint16_t>(y),
                       .radius_256 = static_cast<std::uint16_t>(512U + (index % 5U) * 96U),
                       .elapsed_ms = static_cast<std::uint16_t>(index)};
    }
    REQUIRE(
        fixture.log.append(append(zigzag, static_cast<std::uint16_t>(0x0800U + operation * 13U))));
  }
  const bool eraser_cover = false;
  const std::array cover{
      vector_v2::CompactOperationSample{
          .x_quarter = 240, .y_quarter = 1024, .radius_256 = 80 * 256},
      vector_v2::CompactOperationSample{
          .x_quarter = 1840, .y_quarter = 1024, .radius_256 = 80 * 256},
  };
  REQUIRE(fixture.log.append(
      append(cover, 0x07E0U,
             eraser_cover ? vector_v2::OperationTool::kEraser : vector_v2::OperationTool::kPen)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({fixture.log.current_revision()}, revised_overview));

  const vector_v2::ViewRequest view{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_pixels = {0, 32, 128, 96},
  };
  std::size_t steps = 0;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    ++steps;
    REQUIRE(steps < 4'000U);
    if (step->complete) {
      break;
    }
  }
  // The cover saturates the group after its own segment; the 80 buried
  // operations must be consumed through the operation batch (64/slice), not
  // through per-segment raster budgets.
  CHECK(steps <= 6U);

  std::vector<std::uint16_t> composed(128U * 64U);
  const auto stats = fixture.canvas.compose_view(view, composed);
  REQUIRE(stats.has_value());
  CHECK(stats->fallback_pixels == 0U);
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  for (std::size_t index = 0; index < fixture.log.operation_count(); ++index) {
    const auto stored = fixture.log.operation(index);
    REQUIRE(stored.has_value());
    REQUIRE(
        vector_v2::apply_incremental_operation(append(stored->samples, stored->color, stored->tool),
                                               {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                .level_bounds = view.level_pixels,
                                                .pixels = direct,
                                                .stride = 128}));
  }
  CHECK(composed == direct);
}
