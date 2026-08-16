#include "tinydraw/vector_v2/settled_tile.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct SettleRig {
  std::array<vector_v2::OperationRecord, 8> records{};
  std::array<vector_v2::CompactOperationSample, 64> samples{};
  vector_v2::OperationLog log{records, samples};
  std::vector<std::uint8_t> operation_alpha = std::vector<std::uint8_t>(vector_v2::kTilePixels);
  std::vector<std::uint8_t> accumulated = std::vector<std::uint8_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> red = std::vector<std::uint16_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> green = std::vector<std::uint16_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> blue = std::vector<std::uint16_t>(vector_v2::kTilePixels);

  [[nodiscard]] vector_v2::SettledTileWorkspace workspace() {
    return {.operation_alpha = operation_alpha,
            .accumulated_alpha = accumulated,
            .red = red,
            .green = green,
            .blue = blue};
  }
};

}  // namespace

TEST_CASE("settled tile matches hard-edged interiors and smooths boundaries") {
  SettleRig rig;
  // A fat horizontal stroke through tile (0,0) at 100%: world y=32, radius
  // 8.25 — the fractional radius guarantees the edge rows straddle pixel
  // centers (an exact-half edge is legitimately rendered with no
  // intermediate pixels at all).
  const std::array stroke{
      vector_v2::CompactOperationSample{
          .x_quarter = 8 * 16, .y_quarter = 32 * 16, .radius_256 = 8 * 256 + 64},
      vector_v2::CompactOperationSample{
          .x_quarter = 56 * 16, .y_quarter = 32 * 16, .radius_256 = 8 * 256 + 64, .elapsed_ms = 8},
  };
  REQUIRE(rig.log.append({.color = 0x001FU, .samples = stroke}).has_value());
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  std::vector<std::uint16_t> settled(vector_v2::kTilePixels);
  vector_v2::SettledTileStats stats{};
  REQUIRE(vector_v2::render_settled_tile(rig.log, key, rig.workspace(), settled, &stats));
  CHECK(stats.operations_rendered == 1U);

  // The exact hard-edged reference for the same tile.
  std::vector<std::uint16_t> hard(vector_v2::kTilePixels, 0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0x001FU, .samples = stroke},
      {.zoom = vector_v2::ZoomLevel::k100Percent,
       .level_bounds = {0, 0, vector_v2::kTileWidth, vector_v2::kTileHeight},
       .pixels = hard,
       .stride = vector_v2::kTileWidth}));

  // Deep-interior pixels (well inside the capsule) must match the hard
  // render exactly; the stroke centerline row is interior.
  std::size_t interior_checked = 0;
  for (int x = 16; x < 48; ++x) {
    const std::size_t at = 32U * vector_v2::kTileWidth + static_cast<std::size_t>(x);
    CHECK(settled[at] == hard[at]);
    CHECK(settled[at] == 0x001FU);
    ++interior_checked;
  }
  CHECK(interior_checked == 32U);
  // Boundary rows (just outside the hard edge) must hold intermediate
  // values: neither pure ink nor pure paper everywhere.
  std::size_t intermediate = 0;
  for (int y = 0; y < vector_v2::kTileHeight; ++y) {
    for (int x = 0; x < vector_v2::kTileWidth; ++x) {
      const std::size_t at =
          static_cast<std::size_t>(y) * vector_v2::kTileWidth + static_cast<std::size_t>(x);
      intermediate += settled[at] != 0x001FU && settled[at] != 0xFFFFU ? 1U : 0U;
    }
  }
  CHECK(intermediate > 50U);
  // Deterministic: a second render is bit-identical.
  std::vector<std::uint16_t> again(vector_v2::kTilePixels);
  REQUIRE(vector_v2::render_settled_tile(rig.log, key, rig.workspace(), again));
  CHECK(settled == again);
}

TEST_CASE("settled tile unions self-overlap and composites erasers as paper") {
  SettleRig rig;
  // A self-crossing stroke: coverage in the crossing must not exceed the
  // stroke color (union, not additive darkening).
  const std::array cross{
      vector_v2::CompactOperationSample{
          .x_quarter = 8 * 16, .y_quarter = 8 * 16, .radius_256 = 4 * 256},
      vector_v2::CompactOperationSample{
          .x_quarter = 56 * 16, .y_quarter = 56 * 16, .radius_256 = 4 * 256, .elapsed_ms = 8},
      vector_v2::CompactOperationSample{
          .x_quarter = 8 * 16, .y_quarter = 56 * 16, .radius_256 = 4 * 256, .elapsed_ms = 16},
      vector_v2::CompactOperationSample{
          .x_quarter = 56 * 16, .y_quarter = 8 * 16, .radius_256 = 4 * 256, .elapsed_ms = 24},
  };
  REQUIRE(rig.log.append({.color = 0x0000U, .samples = cross}).has_value());
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};
  std::vector<std::uint16_t> settled(vector_v2::kTilePixels);
  REQUIRE(vector_v2::render_settled_tile(rig.log, key, rig.workspace(), settled));
  // The crossing center is interior to both legs; union keeps it exactly the
  // stroke color (black), never darker-than-black artifacts elsewhere.
  CHECK(settled[32U * vector_v2::kTileWidth + 32U] == 0x0000U);

  // An eraser over the crossing restores paper in its interior.
  const std::array erase{
      vector_v2::CompactOperationSample{
          .x_quarter = 24 * 16, .y_quarter = 32 * 16, .radius_256 = 6 * 256},
      vector_v2::CompactOperationSample{
          .x_quarter = 40 * 16, .y_quarter = 32 * 16, .radius_256 = 6 * 256, .elapsed_ms = 8},
  };
  REQUIRE(
      rig.log.append({.tool = vector_v2::OperationTool::kEraser, .samples = erase}).has_value());
  REQUIRE(vector_v2::render_settled_tile(rig.log, key, rig.workspace(), settled));
  CHECK(settled[32U * vector_v2::kTileWidth + 32U] == 0xFFFFU);
}
