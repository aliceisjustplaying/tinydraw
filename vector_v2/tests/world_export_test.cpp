#include "tinydraw/vector_v2/world_export.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct ExportFixture {
  std::array<vector_v2::OperationRecord, 16> records{};
  std::array<vector_v2::CompactOperationSample, 256> samples{};
  vector_v2::OperationLog log{records, samples};

  void append_document() {
    // Pen strokes across band boundaries, a tapered stroke, and an eraser,
    // including geometry at the world edges.
    const std::array first{
        vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 1'024},
        vector_v2::CompactOperationSample{
            .x_quarter = 23200, .y_quarter = 28400, .radius_256 = 1'024, .elapsed_ms = 8},
    };
    const std::array second{
        vector_v2::CompactOperationSample{.x_quarter = 8000, .y_quarter = 400, .radius_256 = 2'048},
        vector_v2::CompactOperationSample{
            .x_quarter = 8400, .y_quarter = 14000, .radius_256 = 384, .elapsed_ms = 8},
        vector_v2::CompactOperationSample{
            .x_quarter = 3600, .y_quarter = 27600, .radius_256 = 2'400, .elapsed_ms = 16},
    };
    const std::array erased{
        vector_v2::CompactOperationSample{
            .x_quarter = 7200, .y_quarter = 7200, .radius_256 = 1'600},
        vector_v2::CompactOperationSample{
            .x_quarter = 12000, .y_quarter = 10400, .radius_256 = 1'600, .elapsed_ms = 8},
    };
    const std::array dot{
        vector_v2::CompactOperationSample{.x_quarter = 23520, .y_quarter = 80, .radius_256 = 900},
    };
    REQUIRE(log.append({.tool = vector_v2::OperationTool::kPen, .color = 0xF800U, .samples = first})
                .has_value());
    REQUIRE(
        log.append({.tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = second})
            .has_value());
    REQUIRE(
        log.append({.tool = vector_v2::OperationTool::kEraser, .color = 0xFFFFU, .samples = erased})
            .has_value());
    REQUIRE(log.append({.tool = vector_v2::OperationTool::kPen, .color = 0x07E0U, .samples = dot})
                .has_value());
  }
};

struct SettledWorkspaceStorage {
  std::vector<std::uint8_t> operation_alpha = std::vector<std::uint8_t>(vector_v2::kTilePixels);
  std::vector<std::uint8_t> accumulated_alpha = std::vector<std::uint8_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> red = std::vector<std::uint16_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> green = std::vector<std::uint16_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> blue = std::vector<std::uint16_t>(vector_v2::kTilePixels);

  [[nodiscard]] vector_v2::SettledTileWorkspace workspace() {
    return {.operation_alpha = operation_alpha,
            .accumulated_alpha = accumulated_alpha,
            .red = red,
            .green = green,
            .blue = blue};
  }
};

}  // namespace

TEST_CASE("world band renderer equals one-shot forward replay") {
  ExportFixture fixture;
  fixture.append_document();

  // Ground truth: the complete world in one buffer, forward painter order.
  std::vector<std::uint16_t> world(static_cast<std::size_t>(vector_v2::kWorldWidth) *
                                       static_cast<std::size_t>(vector_v2::kWorldHeight),
                                   0xFFFFU);
  for (std::size_t index = 0; index < fixture.log.operation_count(); ++index) {
    const auto stored = fixture.log.operation(index);
    REQUIRE(stored.has_value());
    REQUIRE(vector_v2::apply_incremental_operation(
        {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
        {.zoom = vector_v2::ZoomLevel::k100Percent,
         .level_bounds = {0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight},
         .pixels = world,
         .stride = vector_v2::kWorldWidth}));
  }

  // A deliberately awkward band height (37 rows) exercises ragged band
  // boundaries including the final partial band.
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * 37U);
  vector_v2::WorldBandRenderer renderer(fixture.log, band);
  REQUIRE(renderer.ready());
  CHECK(renderer.band_rows() == 37);
  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  for (int y = 0; y < vector_v2::kWorldHeight; ++y) {
    REQUIRE(renderer.render_row(y, row));
    const auto expected = std::span(world).subspan(
        static_cast<std::size_t>(y) * vector_v2::kWorldWidth, vector_v2::kWorldWidth);
    REQUIRE(std::equal(row.begin(), row.end(), expected.begin()));
  }
}

TEST_CASE("settled world band renderer stitches the production AA windows exactly") {
  ExportFixture fixture;
  fixture.append_document();
  constexpr int kBandRows = 17;
  constexpr int kFirstRow = 31;
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * kBandRows);
  std::vector<std::uint16_t> window(vector_v2::kTilePixels);
  SettledWorkspaceStorage storage;
  vector_v2::SettledWorldBandRenderer renderer(fixture.log, band, window, storage.workspace());
  REQUIRE(renderer.ready());
  CHECK(renderer.band_rows() == kBandRows);

  std::vector<std::uint16_t> expected(band.size());
  for (int first_column = 0; first_column < vector_v2::kWorldWidth;
       first_column += vector_v2::kTileWidth) {
    const int width = std::min(vector_v2::kTileWidth, vector_v2::kWorldWidth - first_column);
    const auto rendered = std::span(window).first(static_cast<std::size_t>(width) * kBandRows);
    REQUIRE(vector_v2::render_settled_window(
        fixture.log, vector_v2::ZoomLevel::k100Percent,
        {first_column, kFirstRow, first_column + width, kFirstRow + kBandRows}, storage.workspace(),
        rendered));
    for (int row = 0; row < kBandRows; ++row) {
      std::copy_n(rendered.begin() + static_cast<std::ptrdiff_t>(row * width), width,
                  expected.begin() +
                      static_cast<std::ptrdiff_t>(row * vector_v2::kWorldWidth + first_column));
    }
  }

  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  std::size_t blended_pixels = 0;
  for (int y = kFirstRow; y < kFirstRow + kBandRows; ++y) {
    REQUIRE(renderer.render_row(y, row));
    const auto expected_row = std::span(expected).subspan(
        static_cast<std::size_t>(y - kFirstRow) * vector_v2::kWorldWidth, vector_v2::kWorldWidth);
    CHECK(std::equal(row.begin(), row.end(), expected_row.begin()));
    blended_pixels +=
        static_cast<std::size_t>(std::count_if(row.begin(), row.end(), [](auto pixel) {
          return pixel != 0xFFFFU && pixel != 0xF800U && pixel != 0x001FU && pixel != 0x07E0U;
        }));
  }
  CHECK(blended_pixels > 0U);
}

TEST_CASE("settled world band renderer rejects an authority change") {
  ExportFixture fixture;
  fixture.append_document();
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * 4U);
  std::vector<std::uint16_t> window(vector_v2::kTilePixels);
  SettledWorkspaceStorage storage;
  vector_v2::SettledWorldBandRenderer renderer(fixture.log, band, window, storage.workspace());
  REQUIRE(renderer.ready());

  const std::array extra{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(fixture.log.append({.color = 0x0000U, .samples = extra}).has_value());
  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  CHECK_FALSE(renderer.ready());
  CHECK_FALSE(renderer.render_row(0, row));
}

TEST_CASE("world band renderer rejects invalid requests") {
  ExportFixture fixture;
  fixture.append_document();
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * 8U);
  vector_v2::WorldBandRenderer renderer(fixture.log, band);
  REQUIRE(renderer.ready());
  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  CHECK_FALSE(renderer.render_row(-1, row));
  CHECK_FALSE(renderer.render_row(vector_v2::kWorldHeight, row));
  CHECK_FALSE(renderer.render_row(0, std::span(row).first(vector_v2::kWorldWidth - 1)));

  // A band shorter than one row is not usable.
  std::vector<std::uint16_t> tiny(static_cast<std::size_t>(vector_v2::kWorldWidth) - 1U);
  vector_v2::WorldBandRenderer short_renderer(fixture.log, tiny);
  CHECK_FALSE(short_renderer.ready());
}
