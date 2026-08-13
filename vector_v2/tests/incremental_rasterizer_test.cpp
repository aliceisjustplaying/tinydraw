#include "tinydraw/vector_v2/incremental_rasterizer.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <span>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("incremental operation paints one stroke without clearing prior pixels") {
  std::array<std::uint16_t, 32U * 32U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 512},
  };
  const vector_v2::OperationAppend operation{
      .tool = vector_v2::OperationTool::kPen,
      .color = 0xF800U,
      .samples = samples,
  };
  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k100Percent,
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
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 40, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 768},
  };
  const vector_v2::OperationAppend pen{
      .tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = pen_samples};
  REQUIRE(vector_v2::apply_incremental_operation(pen, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                       .level_bounds = {0, 0, 24, 24},
                                                       .pixels = pixels,
                                                       .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0x001FU);

  const std::array eraser_samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 16, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 64, .radius_256 = 512},
  };
  const vector_v2::OperationAppend eraser{.tool = vector_v2::OperationTool::kEraser,
                                          .samples = eraser_samples};
  REQUIRE(vector_v2::apply_incremental_operation(eraser, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                          .level_bounds = {0, 0, 24, 24},
                                                          .pixels = pixels,
                                                          .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0xFFFFU);
  CHECK(pixels[10U * 24U + 18U] == 0x001FU);
}

TEST_CASE("overview and tile surfaces map the same world operation") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 272, .y_quarter = 16, .radius_256 = 1024},
  };
  const vector_v2::OperationAppend operation{.color = 0x07E0U, .samples = samples};
  REQUIRE(vector_v2::apply_incremental_operation(
      operation, {.zoom = vector_v2::ZoomLevel::k25Percent,
                  .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
                  .pixels = overview,
                  .stride = vector_v2::kOverviewWidth}));
  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                         .level_bounds = {64, 0, 128, 64},
                                                         .pixels = tile,
                                                         .stride = vector_v2::kTileWidth}));
  CHECK(overview[1U * vector_v2::kOverviewWidth + 17U] == 0x07E0U);
  CHECK(tile[4U * vector_v2::kTileWidth + 4U] == 0x07E0U);
}

TEST_CASE("shared operation bounds conservatively include radius and clip to the world") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = vector_v2::kWorldWidth * 4,
                                        .y_quarter = vector_v2::kWorldHeight * 4,
                                        .radius_256 = 512},
  };
  CHECK(vector_v2::operation_world_bounds(samples) ==
        vector_v2::PixelRect{0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight});
  CHECK_FALSE(vector_v2::operation_world_bounds({}));
}

TEST_CASE("affected tile enumeration clips to the bounded world") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 2048},
      vector_v2::CompactOperationSample{.x_quarter = 272, .y_quarter = 272, .radius_256 = 2048},
  };
  const vector_v2::OperationAppend operation{.samples = samples};
  std::array<vector_v2::TileKey, 4> keys{};
  const auto count = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k100Percent, keys);
  REQUIRE(count.has_value());
  CHECK(count->required == 4U);
  CHECK(count->written == 4U);
  CHECK(count->complete());
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 0, 0});
  CHECK(keys[3] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 1, 1});

  std::array<vector_v2::TileKey, 3> too_small{};
  const auto partial =
      vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k100Percent, too_small);
  REQUIRE(partial.has_value());
  CHECK(partial->required == 4U);
  CHECK(partial->written == 3U);
  CHECK_FALSE(partial->complete());
  CHECK_FALSE(vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k25Percent, keys));
}

TEST_CASE("thin stroke bounds include the coarsest tiled paint halo") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 255, .y_quarter = 256, .radius_256 = 1},
  };
  CHECK(vector_v2::operation_world_bounds(samples) == vector_v2::PixelRect{62, 62, 66, 66});
  const vector_v2::OperationAppend operation{.samples = samples};
  std::array<vector_v2::TileKey, 4> keys{};
  const auto at_100 = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k100Percent, keys);
  REQUIRE(at_100.has_value());
  REQUIRE(at_100->complete());
  CHECK(at_100->written == 4U);
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 0, 0});
  CHECK(keys[1] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 1, 0});
}

TEST_CASE("affected tiles cover partial edge grids and high zooms") {
  const std::array edge_sample{
      vector_v2::CompactOperationSample{.x_quarter = vector_v2::kWorldWidth * 4,
                                        .y_quarter = vector_v2::kWorldHeight * 4,
                                        .radius_256 = 256}};
  const vector_v2::OperationAppend operation{.samples = edge_sample};
  std::array<vector_v2::TileKey, 4> keys{};
  const auto at_50 = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k50Percent, keys);
  REQUIRE(at_50.has_value());
  REQUIRE(at_50->complete());
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k50Percent, 11, 13});

  const auto at_400 = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k400Percent, keys);
  REQUIRE(at_400.has_value());
  REQUIRE(at_400->complete());
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k400Percent, 91, 111});
}

TEST_CASE("all committed zooms paint the same world center and enumerate its tile") {
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k25Percent,  vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent, vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 400, .y_quarter = 480, .radius_256 = 256},
  };
  const vector_v2::OperationAppend operation{.color = 0xF800U, .samples = samples};
  for (const vector_v2::ZoomLevel zoom : zooms) {
    std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
    tile.fill(0xFFFFU);
    const int percent = vector_v2::zoom_percent(zoom);
    const int center_x = 100 * percent / 100;
    const int center_y = 120 * percent / 100;
    const int tile_x = center_x / vector_v2::kTileWidth * vector_v2::kTileWidth;
    const int tile_y = center_y / vector_v2::kTileHeight * vector_v2::kTileHeight;
    REQUIRE(vector_v2::apply_incremental_operation(
        operation, {.zoom = zoom,
                    .level_bounds = {tile_x, tile_y, tile_x + vector_v2::kTileWidth,
                                     tile_y + vector_v2::kTileHeight},
                    .pixels = tile,
                    .stride = vector_v2::kTileWidth}));
    const std::size_t local_x = static_cast<std::size_t>(center_x - tile_x);
    const std::size_t local_y = static_cast<std::size_t>(center_y - tile_y);
    CHECK(tile[local_y * vector_v2::kTileWidth + local_x] == 0xF800U);

    std::array<vector_v2::TileKey, 4> affected{};
    const auto result = vector_v2::affected_tiles(operation, zoom, affected);
    if (zoom == vector_v2::ZoomLevel::k25Percent) {
      CHECK_FALSE(result.has_value());
    } else {
      REQUIRE(result.has_value());
      CHECK(result->complete());
      CHECK(affected[0].column == static_cast<std::uint16_t>(tile_x / vector_v2::kTileWidth));
      CHECK(affected[0].row == static_cast<std::uint16_t>(tile_y / vector_v2::kTileHeight));
    }
  }
}

TEST_CASE("raster surface honors a stride larger than its visible width") {
  std::array<std::uint16_t, 4U * 3U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(vector_v2::apply_incremental_operation({.color = 0xF800U, .samples = samples},
                                                 {.zoom = vector_v2::ZoomLevel::k100Percent,
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
  CHECK_FALSE(vector_v2::apply_incremental_operation({}, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                          .level_bounds = {0, 0, 2, 2},
                                                          .pixels = pixels,
                                                          .stride = 2}));
  CHECK_FALSE(vector_v2::apply_incremental_operation(
      {.samples = std::array{vector_v2::CompactOperationSample{}}},
      {.zoom = vector_v2::ZoomLevel::k100Percent,
       .level_bounds = {-1, 0, 1, 2},
       .pixels = pixels,
       .stride = 2}));
  CHECK(pixels == std::array<std::uint16_t, 4>{0x1234U, 0x1234U, 0x1234U, 0x1234U});
}
