#include "tinydraw/production/incremental_rasterizer.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <span>

namespace production = tinydraw::production;

TEST_CASE("incremental operation paints one stroke without clearing prior pixels") {
  std::array<std::uint16_t, 32U * 32U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 512},
  };
  const production::OperationAppend operation{
      .tool = production::OperationTool::kPen,
      .color = 0xF800U,
      .samples = samples,
  };
  REQUIRE(production::apply_incremental_operation(operation,
                                                  {.zoom = production::ZoomLevel::k100Percent,
                                                   .level_bounds = {0, 0, 32, 32},
                                                   .pixels = pixels,
                                                   .stride = 32}));
  CHECK(pixels[10U * 32U + 10U] == 0xF800U);
  CHECK(pixels[10U * 32U + 20U] == 0xF800U);
  CHECK(pixels.front() == 0x1111U);
}

TEST_CASE("eraser applies after pen in painter order") {
  std::array<std::uint16_t, 24U * 24U> pixels{};
  pixels.fill(0xFFFFU);
  const std::array pen_samples{
      production::CompactOperationSample{.x_quarter = 16, .y_quarter = 40, .radius_256 = 768},
      production::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 768},
  };
  const production::OperationAppend pen{
      .tool = production::OperationTool::kPen, .color = 0x001FU, .samples = pen_samples};
  REQUIRE(production::apply_incremental_operation(pen, {.zoom = production::ZoomLevel::k100Percent,
                                                        .level_bounds = {0, 0, 24, 24},
                                                        .pixels = pixels,
                                                        .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0x001FU);

  const std::array eraser_samples{
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 16, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 64, .radius_256 = 512},
  };
  const production::OperationAppend eraser{.tool = production::OperationTool::kEraser,
                                           .samples = eraser_samples};
  REQUIRE(
      production::apply_incremental_operation(eraser, {.zoom = production::ZoomLevel::k100Percent,
                                                       .level_bounds = {0, 0, 24, 24},
                                                       .pixels = pixels,
                                                       .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0xFFFFU);
  CHECK(pixels[10U * 24U + 18U] == 0x001FU);
}

TEST_CASE("overview and tile surfaces map the same world operation") {
  std::array<std::uint16_t, production::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  std::array<std::uint16_t, production::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 272, .y_quarter = 16, .radius_256 = 1024},
  };
  const production::OperationAppend operation{.color = 0x07E0U, .samples = samples};
  REQUIRE(production::apply_incremental_operation(
      operation, {.zoom = production::ZoomLevel::k25Percent,
                  .level_bounds = {0, 0, production::kOverviewWidth, production::kOverviewHeight},
                  .pixels = overview,
                  .stride = production::kOverviewWidth}));
  REQUIRE(production::apply_incremental_operation(operation,
                                                  {.zoom = production::ZoomLevel::k100Percent,
                                                   .level_bounds = {64, 0, 128, 64},
                                                   .pixels = tile,
                                                   .stride = production::kTileWidth}));
  CHECK(overview[1U * production::kOverviewWidth + 17U] == 0x07E0U);
  CHECK(tile[4U * production::kTileWidth + 4U] == 0x07E0U);
}

TEST_CASE("shared operation bounds conservatively include radius and clip to the world") {
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 512},
      production::CompactOperationSample{.x_quarter = production::kWorldWidth * 4,
                                         .y_quarter = production::kWorldHeight * 4,
                                         .radius_256 = 512},
  };
  CHECK(production::operation_world_bounds(samples) ==
        production::PixelRect{0, 0, production::kWorldWidth, production::kWorldHeight});
  CHECK_FALSE(production::operation_world_bounds({}));
}

TEST_CASE("affected tile enumeration clips to the bounded world") {
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 2048},
      production::CompactOperationSample{.x_quarter = 272, .y_quarter = 272, .radius_256 = 2048},
  };
  const production::OperationAppend operation{.samples = samples};
  std::array<production::TileKey, 4> keys{};
  const auto count =
      production::affected_tiles(operation, production::ZoomLevel::k100Percent, keys);
  REQUIRE(count.has_value());
  CHECK(count->required == 4U);
  CHECK(count->written == 4U);
  CHECK(count->complete());
  CHECK(keys[0] == production::TileKey{production::ZoomLevel::k100Percent, 0, 0});
  CHECK(keys[3] == production::TileKey{production::ZoomLevel::k100Percent, 1, 1});

  std::array<production::TileKey, 3> too_small{};
  const auto partial =
      production::affected_tiles(operation, production::ZoomLevel::k100Percent, too_small);
  REQUIRE(partial.has_value());
  CHECK(partial->required == 4U);
  CHECK(partial->written == 3U);
  CHECK_FALSE(partial->complete());
  CHECK_FALSE(production::affected_tiles(operation, production::ZoomLevel::k25Percent, keys));
}

TEST_CASE("thin stroke bounds include the coarsest tiled paint halo") {
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 255, .y_quarter = 256, .radius_256 = 1},
  };
  CHECK(production::operation_world_bounds(samples) == production::PixelRect{62, 62, 66, 66});
  const production::OperationAppend operation{.samples = samples};
  std::array<production::TileKey, 4> keys{};
  const auto at_100 =
      production::affected_tiles(operation, production::ZoomLevel::k100Percent, keys);
  REQUIRE(at_100.has_value());
  REQUIRE(at_100->complete());
  CHECK(at_100->written == 4U);
  CHECK(keys[0] == production::TileKey{production::ZoomLevel::k100Percent, 0, 0});
  CHECK(keys[1] == production::TileKey{production::ZoomLevel::k100Percent, 1, 0});
}

TEST_CASE("affected tiles cover partial edge grids and high zooms") {
  const std::array edge_sample{
      production::CompactOperationSample{.x_quarter = production::kWorldWidth * 4,
                                         .y_quarter = production::kWorldHeight * 4,
                                         .radius_256 = 256}};
  const production::OperationAppend operation{.samples = edge_sample};
  std::array<production::TileKey, 4> keys{};
  const auto at_50 = production::affected_tiles(operation, production::ZoomLevel::k50Percent, keys);
  REQUIRE(at_50.has_value());
  REQUIRE(at_50->complete());
  CHECK(keys[0] == production::TileKey{production::ZoomLevel::k50Percent, 11, 13});

  const auto at_400 =
      production::affected_tiles(operation, production::ZoomLevel::k400Percent, keys);
  REQUIRE(at_400.has_value());
  REQUIRE(at_400->complete());
  CHECK(keys[0] == production::TileKey{production::ZoomLevel::k400Percent, 91, 111});
}

TEST_CASE("all committed zooms paint the same world center and enumerate its tile") {
  constexpr std::array zooms{
      production::ZoomLevel::k25Percent,  production::ZoomLevel::k50Percent,
      production::ZoomLevel::k100Percent, production::ZoomLevel::k200Percent,
      production::ZoomLevel::k400Percent,
  };
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 400, .y_quarter = 480, .radius_256 = 256},
  };
  const production::OperationAppend operation{.color = 0xF800U, .samples = samples};
  for (const production::ZoomLevel zoom : zooms) {
    std::array<std::uint16_t, production::kTilePixels> tile{};
    tile.fill(0xFFFFU);
    const int percent = production::zoom_percent(zoom);
    const int center_x = 100 * percent / 100;
    const int center_y = 120 * percent / 100;
    const int tile_x = center_x / production::kTileWidth * production::kTileWidth;
    const int tile_y = center_y / production::kTileHeight * production::kTileHeight;
    REQUIRE(production::apply_incremental_operation(
        operation, {.zoom = zoom,
                    .level_bounds = {tile_x, tile_y, tile_x + production::kTileWidth,
                                     tile_y + production::kTileHeight},
                    .pixels = tile,
                    .stride = production::kTileWidth}));
    const std::size_t local_x = static_cast<std::size_t>(center_x - tile_x);
    const std::size_t local_y = static_cast<std::size_t>(center_y - tile_y);
    CHECK(tile[local_y * production::kTileWidth + local_x] == 0xF800U);

    std::array<production::TileKey, 4> affected{};
    const auto result = production::affected_tiles(operation, zoom, affected);
    if (zoom == production::ZoomLevel::k25Percent) {
      CHECK_FALSE(result.has_value());
    } else {
      REQUIRE(result.has_value());
      CHECK(result->complete());
      CHECK(affected[0].column == static_cast<std::uint16_t>(tile_x / production::kTileWidth));
      CHECK(affected[0].row == static_cast<std::uint16_t>(tile_y / production::kTileHeight));
    }
  }
}

TEST_CASE("raster surface honors a stride larger than its visible width") {
  std::array<std::uint16_t, 4U * 3U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(production::apply_incremental_operation({.color = 0xF800U, .samples = samples},
                                                  {.zoom = production::ZoomLevel::k100Percent,
                                                   .level_bounds = {0, 0, 2, 3},
                                                   .pixels = pixels,
                                                   .stride = 4}));
  CHECK(pixels[1U * 4U + 1U] == 0xF800U);
  CHECK(pixels[2] == 0x1111U);
  CHECK(pixels[3] == 0x1111U);
}

TEST_CASE("invalid surface and empty operation fail without changing pixels") {
  std::array<std::uint16_t, 4> pixels{};
  pixels.fill(0x1234U);
  CHECK_FALSE(
      production::apply_incremental_operation({}, {.zoom = production::ZoomLevel::k100Percent,
                                                   .level_bounds = {0, 0, 2, 2},
                                                   .pixels = pixels,
                                                   .stride = 2}));
  CHECK_FALSE(production::apply_incremental_operation(
      {.samples = std::array{production::CompactOperationSample{}}},
      {.zoom = production::ZoomLevel::k100Percent,
       .level_bounds = {-1, 0, 1, 2},
       .pixels = pixels,
       .stride = 2}));
  CHECK(pixels == std::array<std::uint16_t, 4>{0x1234U, 0x1234U, 0x1234U, 0x1234U});
}
