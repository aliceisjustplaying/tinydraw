#include "tinydraw/production/tile_producer.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace production = tinydraw::production;

namespace {

struct Fixture {
  std::array<production::OperationRecord, 96> records{};
  std::array<production::CompactOperationSample, 2'048> samples{};
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  std::array<production::MaterializedSlotStorage, 64> slots{};
  std::array<std::uint16_t, 64U * production::kTilePixels> tile_pool{};
  std::array<std::uint16_t, production::kTileProducerPixels> supertask{};
  std::array<std::uint16_t, production::kTilePixels> packed{};
  production::OperationLog log{records, samples};
  production::MaterializedCanvas canvas{overview, slots, tile_pool};
  production::TileProducer producer{
      log, canvas, {.supertask_pixels = supertask, .packed_tile_pixels = packed}};

  Fixture() {
    overview.fill(0xFFFFU);
    REQUIRE(canvas.publish_overview({0}, overview));
  }
};

production::OperationAppend append(
    std::span<const production::CompactOperationSample> samples, std::uint16_t color = 0xF800U,
    production::OperationTool tool = production::OperationTool::kPen) {
  return {.tool = tool, .color = color, .samples = samples};
}

}  // namespace

TEST_CASE("tile producer ignores overview fallback when finding missing resident tiles") {
  Fixture fixture;
  REQUIRE(fixture.producer.ready());
  const production::ViewRequest view{
      .zoom = production::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, 128, 128},
  };

  REQUIRE(fixture.canvas.lookup({production::ZoomLevel::k100Percent, 0, 0})->kind ==
          production::SourceKind::kOverview);
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
  const auto source = fixture.canvas.lookup({production::ZoomLevel::k100Percent, 0, 0});
  REQUIRE(source.has_value());
  CHECK(source->kind == production::SourceKind::kTileSlot);
  CHECK(source->identity.quality == production::MaterializationQuality::kImmediate);
}

TEST_CASE("tile producer output equals direct painter-ordered viewport replay") {
  Fixture fixture;
  const std::array first{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 80, .radius_256 = 768},
      production::CompactOperationSample{.x_quarter = 600, .y_quarter = 400, .radius_256 = 1024},
  };
  const std::array second{
      production::CompactOperationSample{.x_quarter = 300, .y_quarter = 100, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 300, .y_quarter = 600, .radius_256 = 512},
  };
  const std::array erased{
      production::CompactOperationSample{.x_quarter = 280, .y_quarter = 260, .radius_256 = 384},
      production::CompactOperationSample{.x_quarter = 360, .y_quarter = 340, .radius_256 = 384},
  };
  REQUIRE(fixture.log.append(append(first, 0xF800U)));
  REQUIRE(fixture.log.append(append(second, 0x001FU)));
  REQUIRE(fixture.log.append(append(erased, 0, production::OperationTool::kEraser)));
  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({3}, revised_overview));

  const production::ViewRequest view{
      .zoom = production::ZoomLevel::k100Percent,
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
    REQUIRE(production::apply_incremental_operation(
        append(stored->samples, stored->color, stored->tool),
        {.zoom = production::ZoomLevel::k100Percent,
         .level_bounds = view.level_pixels,
         .pixels = direct,
         .stride = 192}));
  }
  CHECK(composed == direct);
}

TEST_CASE("tile producer scans painter order once per supertask and skips distant operations") {
  Fixture fixture;
  const std::array visible{
      production::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 256}};
  const std::array distant{production::CompactOperationSample{
      .x_quarter = 4U * 1'000U, .y_quarter = 4U * 1'000U, .radius_256 = 256}};
  REQUIRE(fixture.log.append(append(visible)));
  REQUIRE(fixture.log.append(append(distant)));
  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, revised_overview));

  const auto step = fixture.producer.produce_next(
      {.zoom = production::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}});
  REQUIRE(step.has_value());
  CHECK(step->operations_scanned == 2U);
  CHECK(step->operations_rendered == 1U);
  CHECK(step->tiles_published == 1U);
}

TEST_CASE("tile producer validates uniform baseline reset") {
  Fixture fixture;
  CHECK(fixture.producer.reset_uniform_baseline({0}));
  const std::array point{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(fixture.log.append(append(point)));
  CHECK_FALSE(fixture.producer.reset_uniform_baseline({1}));
}

TEST_CASE("tile producer sliced long strokes equal direct painter replay") {
  Fixture fixture;
  std::vector<production::CompactOperationSample> long_stroke(400);
  for (std::size_t index = 0; index < long_stroke.size(); ++index) {
    long_stroke[index] = {
        .x_quarter = static_cast<std::uint16_t>(32U + index),
        .y_quarter = static_cast<std::uint16_t>(120U + index % 80U),
        .radius_256 = static_cast<std::uint16_t>(index % 2U == 0U ? 5'120U : 3'328U),
    };
  }
  REQUIRE(fixture.log.append(append(long_stroke, 0x001FU)));
  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));
  const production::ViewRequest view{
      .zoom = production::ZoomLevel::k400Percent,
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
  REQUIRE(production::apply_incremental_operation(append(long_stroke, 0x001FU),
                                                  {.zoom = production::ZoomLevel::k400Percent,
                                                   .level_bounds = view.level_pixels,
                                                   .pixels = direct,
                                                   .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer isolates an oversized first segment from later raster work") {
  Fixture fixture;
  const std::array oversized_segments{
      production::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 5'120},
      production::CompactOperationSample{.x_quarter = 100, .y_quarter = 100, .radius_256 = 5'120},
      production::CompactOperationSample{.x_quarter = 20, .y_quarter = 100, .radius_256 = 5'120},
  };
  REQUIRE(fixture.log.append(append(oversized_segments, 0x001FU)));
  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));

  const auto first = fixture.producer.produce_next(
      {.zoom = production::ZoomLevel::k400Percent, .level_pixels = {0, 0, 128, 128}});
  REQUIRE(first.has_value());
  CHECK_FALSE(first->complete);
  CHECK(first->operations_rendered == 1U);
  CHECK(first->tiles_published == 0U);
}

TEST_CASE("tile producer restarts after revision changes during sliced replay") {
  Fixture fixture;
  std::vector<production::CompactOperationSample> long_stroke(400);
  for (std::size_t index = 0; index < long_stroke.size(); ++index) {
    long_stroke[index] = {
        .x_quarter = static_cast<std::uint16_t>(64U + index),
        .y_quarter = static_cast<std::uint16_t>(160U + index % 64U),
        .radius_256 = 3'328U,
    };
  }
  REQUIRE(fixture.log.append(append(long_stroke, 0x001FU)));
  std::array<std::uint16_t, production::kOverviewPixels> overview_one{};
  overview_one.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, overview_one));
  const production::ViewRequest view{
      .zoom = production::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, 128, 128},
  };
  REQUIRE(fixture.producer.produce_next(view));

  const std::array second{
      production::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 2'048},
      production::CompactOperationSample{.x_quarter = 360, .y_quarter = 360, .radius_256 = 2'048},
  };
  REQUIRE(fixture.log.append(append(second, 0xF800U)));
  std::array<std::uint16_t, production::kOverviewPixels> overview_two{};
  overview_two.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({2}, overview_two));
  CHECK_FALSE(fixture.producer.produce_next(view));
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
  REQUIRE(production::apply_incremental_operation(append(long_stroke, 0x001FU),
                                                  {.zoom = production::ZoomLevel::k400Percent,
                                                   .level_bounds = view.level_pixels,
                                                   .pixels = direct,
                                                   .stride = 128}));
  REQUIRE(production::apply_incremental_operation(append(second, 0xF800U),
                                                  {.zoom = production::ZoomLevel::k400Percent,
                                                   .level_bounds = view.level_pixels,
                                                   .pixels = direct,
                                                   .stride = 128}));
  CHECK(composed == direct);
}

TEST_CASE("tile producer rejects 25 percent and aliased or short workspace") {
  Fixture fixture;
  CHECK_FALSE(fixture.producer.produce_next(
      {.zoom = production::ZoomLevel::k25Percent,
       .level_pixels = {0, 0, production::kOverviewWidth, production::kOverviewHeight}}));

  production::TileProducer short_workspace{
      fixture.log,
      fixture.canvas,
      {.supertask_pixels = std::span(fixture.supertask).first(1),
       .packed_tile_pixels = fixture.packed},
  };
  CHECK_FALSE(short_workspace.ready());
  production::TileProducer aliased_workspace{
      fixture.log,
      fixture.canvas,
      {.supertask_pixels = fixture.supertask,
       .packed_tile_pixels = std::span(fixture.supertask).first(production::kTilePixels)},
  };
  CHECK_FALSE(aliased_workspace.ready());
}
