#include "tinydraw/vector_v2/tile_producer.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

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
  std::array<std::uint16_t, vector_v2::kTileProducerPixels> supertask{};
  std::array<std::uint16_t, vector_v2::kTilePixels> packed{};
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{overview, *uniforms, occupancy, slots, tile_pool};
  vector_v2::TileProducer producer{
      log, canvas, {.supertask_pixels = supertask, .packed_tile_pixels = packed}};

  PaperFixture() {
    snapshot.fill(0xFFFFU);
    REQUIRE(canvas.restore_snapshot({0}, snapshot));
  }
};

struct Fixture {
  std::array<vector_v2::OperationRecord, 96> records{};
  std::array<vector_v2::CompactOperationSample, 2'048> samples{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 64> slots{};
  std::array<std::uint16_t, 64U * vector_v2::kTilePixels> tile_pool{};
  std::array<std::uint16_t, vector_v2::kTileProducerPixels> supertask{};
  std::array<std::uint16_t, vector_v2::kTilePixels> packed{};
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{overview, slots, tile_pool};
  vector_v2::TileProducer producer{
      log, canvas, {.supertask_pixels = supertask, .packed_tile_pixels = packed}};

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
  CHECK(source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(source->identity.quality == vector_v2::MaterializationQuality::kImmediate);
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
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 80, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 600, .y_quarter = 400, .radius_256 = 1024},
  };
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 300, .y_quarter = 100, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 300, .y_quarter = 600, .radius_256 = 512},
  };
  const std::array erased{
      vector_v2::CompactOperationSample{.x_quarter = 280, .y_quarter = 260, .radius_256 = 384},
      vector_v2::CompactOperationSample{.x_quarter = 360, .y_quarter = 340, .radius_256 = 384},
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
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 256}};
  const std::array distant{vector_v2::CompactOperationSample{
      .x_quarter = 4U * 1'000U, .y_quarter = 4U * 1'000U, .radius_256 = 256}};
  REQUIRE(fixture.log.append(append(visible)));
  REQUIRE(fixture.log.append(append(distant)));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, revised_overview));

  const auto step = fixture.producer.produce_next(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}});
  REQUIRE(step.has_value());
  CHECK(step->operations_scanned == 2U);
  CHECK(step->operations_rendered == 1U);
  CHECK(step->tiles_published == 1U);
}

TEST_CASE("tile producer rejects distant segments inside an overlapping operation bound") {
  Fixture fixture;
  const std::array around_view{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 1'200, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 1'200, .y_quarter = 1'200, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 1'200, .y_quarter = 0, .radius_256 = 256},
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

TEST_CASE("tile producer coalesces exact constant-radius line runs") {
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
  const auto step = fixture.producer.produce_next(view);
  REQUIRE(step.has_value());
  CHECK(step->complete);
  CHECK(step->operations_scanned == 1U);
  CHECK(step->operations_rendered == 1U);
  CHECK(step->raster_steps == 1U);

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
      vector_v2::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
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
  CHECK(partial_steps >= 4U);

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

TEST_CASE("tile producer isolates an oversized first segment from later raster work") {
  Fixture fixture;
  const std::array oversized_segments{
      vector_v2::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 5'120},
      vector_v2::CompactOperationSample{.x_quarter = 100, .y_quarter = 100, .radius_256 = 5'120},
      vector_v2::CompactOperationSample{.x_quarter = 20, .y_quarter = 100, .radius_256 = 5'120},
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
  CHECK(first->operations_rendered == 1U);
  CHECK(first->raster_steps >= 1U);
  CHECK(first->raster_work <=
        vector_v2::kTileProducerRasterWorkBatch + vector_v2::kTileProducerPixels);
  CHECK(first->tiles_published == 0U);

  std::size_t ticks = 1U;
  while (true) {
    const auto step = fixture.producer.produce_next(view);
    REQUIRE(step.has_value());
    ++ticks;
    REQUIRE(ticks < 64U);
    if (step->complete) {
      break;
    }
    CHECK(step->raster_steps >= 1U);
    CHECK(step->raster_work <=
          vector_v2::kTileProducerRasterWorkBatch + vector_v2::kTileProducerPixels);
  }

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
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 2'048},
      vector_v2::CompactOperationSample{.x_quarter = 360, .y_quarter = 360, .radius_256 = 2'048},
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
       .packed_tile_pixels = fixture.packed},
  };
  CHECK_FALSE(short_workspace.ready());
  vector_v2::TileProducer aliased_workspace{
      fixture.log,
      fixture.canvas,
      {.supertask_pixels = fixture.supertask,
       .packed_tile_pixels = std::span(fixture.supertask).first(vector_v2::kTilePixels)},
  };
  CHECK_FALSE(aliased_workspace.ready());
}
