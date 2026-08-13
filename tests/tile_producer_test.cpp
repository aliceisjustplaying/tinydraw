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
  std::array<production::OperationRecord, 8> records{};
  std::array<production::CompactOperationSample, 24> samples{};
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
  const auto step = fixture.producer.produce_next(view);
  REQUIRE(step.has_value());
  CHECK(step->tiles_published == 4U);
  CHECK(step->visible_tiles_remaining == 0U);
  CHECK(step->complete);
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
  CHECK(step->tiles_published == 4U);
}

TEST_CASE("tile producer 2x AA probe publishes settled output with blended edge pixels") {
  Fixture fixture;
  const std::array diagonal{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 192},
      production::CompactOperationSample{.x_quarter = 240, .y_quarter = 240, .radius_256 = 192},
  };
  REQUIRE(fixture.log.append(append(diagonal, 0x0000U)));
  std::array<std::uint16_t, production::kOverviewPixels> revised_overview{};
  revised_overview.fill(0xFFFFU);
  REQUIRE(fixture.canvas.publish_overview({1}, revised_overview));
  const production::ViewRequest view{
      .zoom = production::ZoomLevel::k100Percent,
      .level_pixels = {0, 0, production::kTileWidth, production::kTileHeight},
  };

  const auto step = fixture.producer.produce_next_2x_aa_100(view);
  REQUIRE(step.has_value());
  CHECK(step->tiles_published == 1U);
  CHECK(step->complete);
  const auto source = fixture.canvas.lookup({production::ZoomLevel::k100Percent, 0, 0});
  REQUIRE(source.has_value());
  CHECK(source->identity.quality == production::MaterializationQuality::kSettled);

  std::array<std::uint16_t, production::kTilePixels> composed{};
  REQUIRE(fixture.canvas.compose_view(view, composed));
  CHECK(std::ranges::any_of(
      composed, [](std::uint16_t pixel) { return pixel != 0x0000U && pixel != 0xFFFFU; }));
}

TEST_CASE("tile producer rejects invalid AA probe and invalid baseline reset") {
  Fixture fixture;
  CHECK_FALSE(fixture.producer.produce_next_2x_aa_100(
      {.zoom = production::ZoomLevel::k400Percent, .level_pixels = {0, 0, 64, 64}}));
  CHECK(fixture.producer.reset_uniform_baseline({0}));
  const std::array point{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(fixture.log.append(append(point)));
  CHECK_FALSE(fixture.producer.reset_uniform_baseline({1}));
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
