#include "tinydraw/vector_v2/settled_tile.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct SettleRig {
  static constexpr std::size_t kOperationCapacity = 8;
  static constexpr std::size_t kSpatialWords =
      vector_v2::operation_spatial_word_count(kOperationCapacity);
  std::array<vector_v2::OperationRecord, kOperationCapacity> records{};
  std::array<vector_v2::CompactOperationSample, 64> samples{};
  std::array<std::uint64_t, vector_v2::kOperationSpatialCellCount * kSpatialWords> spatial_cells{};
  std::array<std::uint64_t, kSpatialWords> spatial_large{};
  std::array<std::uint16_t, kOperationCapacity> candidates{};
  vector_v2::OperationSpatialIndex spatial_index{kOperationCapacity, spatial_cells, spatial_large};
  vector_v2::OperationLog log{records, samples, &spatial_index};
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
            .blue = blue,
            .candidate_indices = candidates};
  }
};

}  // namespace

TEST_CASE("settled rendering resumes in bounded slices with exact pixels and stats") {
  SettleRig rig;
  const std::array first{
      vector_v2::CompactOperationSample{
          .x_quarter = 4 * 16, .y_quarter = 8 * 16, .radius_256 = 5 * 256 + 73},
      vector_v2::CompactOperationSample{
          .x_quarter = 60 * 16, .y_quarter = 55 * 16, .radius_256 = 7 * 256 + 19, .elapsed_ms = 8},
      vector_v2::CompactOperationSample{.x_quarter = 6 * 16,
                                        .y_quarter = 48 * 16,
                                        .radius_256 = 3 * 256 + 111,
                                        .elapsed_ms = 16}};
  const std::array erase{
      vector_v2::CompactOperationSample{
          .x_quarter = 12 * 16, .y_quarter = 30 * 16, .radius_256 = 4 * 256 + 37},
      vector_v2::CompactOperationSample{
          .x_quarter = 52 * 16, .y_quarter = 30 * 16, .radius_256 = 4 * 256 + 37, .elapsed_ms = 8}};
  REQUIRE(rig.log.append({.color = 0xF81FU, .samples = first}));
  REQUIRE(rig.log.append({.tool = vector_v2::OperationTool::kEraser, .samples = erase}));

  const vector_v2::PixelRect bounds{0, 0, vector_v2::kTileWidth, vector_v2::kTileHeight};
  std::vector<std::uint16_t> expected(vector_v2::kTilePixels);
  vector_v2::SettledTileStats expected_stats{};
  REQUIRE(vector_v2::render_settled_window(rig.log, vector_v2::ZoomLevel::k100Percent, bounds,
                                           rig.workspace(), expected, &expected_stats));

  constexpr std::size_t kBudget = 512U;
  std::vector<std::uint16_t> sliced(vector_v2::kTilePixels, 0x1234U);
  vector_v2::SettledRenderCursor cursor;
  std::size_t slices = 0U;
  std::size_t max_slice_work = 0U;
  while (true) {
    const auto slice =
        vector_v2::render_settled_window_slice(rig.log, vector_v2::ZoomLevel::k100Percent, bounds,
                                               rig.workspace(), sliced, cursor, kBudget);
    REQUIRE(slice.status != vector_v2::SettledRenderStatus::kError);
    max_slice_work = std::max(max_slice_work, slice.work_px);
    ++slices;
    if (slice.status == vector_v2::SettledRenderStatus::kComplete) {
      break;
    }
  }

  CHECK(slices > 10U);
  CHECK(max_slice_work <= kBudget + vector_v2::kTileWidth - 1U);
  CHECK(sliced == expected);
  CHECK(cursor.stats().operations_scanned == expected_stats.operations_scanned);
  CHECK(cursor.stats().operations_in_authority == expected_stats.operations_in_authority);
  CHECK(cursor.stats().index_candidates == expected_stats.index_candidates);
  CHECK(cursor.stats().deduplicated_candidates == expected_stats.deduplicated_candidates);
  CHECK(cursor.stats().operations_intersecting == expected_stats.operations_intersecting);
  CHECK(cursor.stats().operations_rendered == expected_stats.operations_rendered);
  CHECK(cursor.stats().candidate_queries == expected_stats.candidate_queries);
  CHECK(cursor.stats().initialize_pixels == expected_stats.initialize_pixels);
  CHECK(cursor.stats().operation_clear_pixels == expected_stats.operation_clear_pixels);
  CHECK(cursor.stats().curve_units_prepared == expected_stats.curve_units_prepared);
  CHECK(cursor.stats().raster_pixels == expected_stats.raster_pixels);
  CHECK(cursor.stats().composite_pixels == expected_stats.composite_pixels);
  CHECK(cursor.stats().fold_pixels == expected_stats.fold_pixels);
  CHECK(cursor.stats().saturated_early == expected_stats.saturated_early);
  CHECK_FALSE(cursor.active());

  std::vector<std::uint16_t> reused(vector_v2::kTilePixels, 0x4321U);
  do {
    const auto slice =
        vector_v2::render_settled_window_slice(rig.log, vector_v2::ZoomLevel::k100Percent, bounds,
                                               rig.workspace(), reused, cursor, kBudget);
    REQUIRE(slice.status != vector_v2::SettledRenderStatus::kError);
  } while (cursor.active());
  CHECK(reused == expected);
}

TEST_CASE("settled rendering rejects authority changes between slices") {
  SettleRig rig;
  const std::array stroke{
      vector_v2::CompactOperationSample{
          .x_quarter = 8 * 16, .y_quarter = 8 * 16, .radius_256 = 2 * 256},
      vector_v2::CompactOperationSample{
          .x_quarter = 56 * 16, .y_quarter = 56 * 16, .radius_256 = 2 * 256, .elapsed_ms = 8}};
  REQUIRE(rig.log.append({.color = 0x001FU, .samples = stroke}));
  std::vector<std::uint16_t> output(vector_v2::kTilePixels, 0xAAAAU);
  vector_v2::SettledRenderCursor cursor;
  const vector_v2::PixelRect bounds{0, 0, vector_v2::kTileWidth, vector_v2::kTileHeight};
  const auto first = vector_v2::render_settled_window_slice(
      rig.log, vector_v2::ZoomLevel::k100Percent, bounds, rig.workspace(), output, cursor, 16U);
  REQUIRE(first.status == vector_v2::SettledRenderStatus::kInProgress);
  REQUIRE(cursor.active());

  REQUIRE(rig.log.append({.color = 0xF800U, .samples = stroke}));
  const auto stale = vector_v2::render_settled_window_slice(
      rig.log, vector_v2::ZoomLevel::k100Percent, bounds, rig.workspace(), output, cursor, 16U);
  CHECK(stale.status == vector_v2::SettledRenderStatus::kError);
  CHECK_FALSE(cursor.active());
}

TEST_CASE("settled spatial replay fetches only conservative local candidates") {
  SettleRig rig;
  const std::array local{
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = 256U},
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 24U, .y_quarter = 16U * 16U, .radius_256 = 256U, .elapsed_ms = 8U}};
  const std::array distant{
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 1'000U, .y_quarter = 16U * 1'000U, .radius_256 = 256U},
      vector_v2::CompactOperationSample{.x_quarter = 16U * 1'008U,
                                        .y_quarter = 16U * 1'000U,
                                        .radius_256 = 256U,
                                        .elapsed_ms = 8U}};
  REQUIRE(rig.log.append({.color = 0x001FU, .samples = local}));
  REQUIRE(rig.log.append({.color = 0xF800U, .samples = distant}));

  std::vector<std::uint16_t> settled(vector_v2::kTilePixels);
  vector_v2::SettledTileStats stats{};
  REQUIRE(vector_v2::render_settled_tile(rig.log, {vector_v2::ZoomLevel::k100Percent, 0, 0},
                                         rig.workspace(), settled, &stats));
  CHECK(stats.operations_in_authority == 2U);
  CHECK(stats.index_candidates == 1U);
  CHECK(stats.deduplicated_candidates == 1U);
  CHECK(stats.operations_scanned == 1U);
  CHECK(stats.operations_intersecting == 1U);
  CHECK(stats.operations_rendered == 1U);
}

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
