#include "tinydraw/vector_v2/incremental_document.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/memory_layout.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

std::int64_t g_fake_retention_us = 0;

std::int64_t fake_retention_now_us() { return ++g_fake_retention_us; }

struct Fixture {
  std::array<vector_v2::OperationRecord, 16> records{};
  std::array<vector_v2::CompactOperationSample, 64> samples{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> scratch{};
  std::array<vector_v2::MaterializedUniformStorage, vector_v2::kMaterializedTileIdentityCount>
      uniforms{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy{};
  std::array<vector_v2::MaterializedSlotStorage, 8> slots{};
  std::array<std::uint16_t, 8U * vector_v2::kTilePixels> tile_pool{};
  std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount> raw_slot_directory{};
  std::array<vector_v2::TileKey, 8U + vector_v2::kMaximumVisibleTiles> affected{};
  std::array<std::uint8_t, vector_v2::kInPlaceTileMaskBytes> tile_mask{};
  alignas(vector_v2::kPreparedOperationChordAlign)
      std::array<std::byte, vector_v2::kOperationChordStorageBytes> chord_plans{};
  vector_v2::OperationLog log{records, samples};
  vector_v2::MaterializedCanvas canvas{overview,  uniforms, occupancy,         slots,
                                       tile_pool, {0},      raw_slot_directory};

  Fixture() {
    overview.fill(0xFFFFU);
    REQUIRE(canvas.publish_overview({0}, overview));
  }

  [[nodiscard]] vector_v2::InPlaceAppendWorkspace workspace() {
    return {
        .overview_scratch = scratch,
        .affected_keys = affected,
        .tile_mask = tile_mask,
        .operation_chord_plans = chord_plans,
    };
  }

  [[nodiscard]] std::optional<vector_v2::IncrementalAppendResult> append_and_absorb(
      const vector_v2::OperationAppend& operation,
      std::optional<vector_v2::ViewRequest> priority_view = std::nullopt) {
    if (!vector_v2::append_authority_only(log, operation).has_value()) {
      return std::nullopt;
    }
    return vector_v2::absorb_pending_operation(log, canvas, workspace(), priority_view);
  }
};

constexpr std::array kPen{
    vector_v2::CompactOperationSample{.x_quarter = 3'200, .y_quarter = 6'400, .radius_256 = 5'120},
    vector_v2::CompactOperationSample{.x_quarter = 9'600, .y_quarter = 6'400, .radius_256 = 5'120},
};

void replay_prefix(const vector_v2::OperationLog& log, std::size_t operation_count,
                   std::span<std::uint16_t> pixels) {
  std::fill(pixels.begin(), pixels.end(), 0xFFFFU);
  for (std::size_t index = 0; index < operation_count; ++index) {
    const auto stored = log.operation(index);
    REQUIRE(stored.has_value());
    REQUIRE(vector_v2::apply_incremental_operation(
        {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
        {.zoom = vector_v2::ZoomLevel::k25Percent,
         .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
         .pixels = pixels,
         .stride = vector_v2::kOverviewWidth}));
  }
}

}  // namespace

TEST_CASE("authority append trails until the pending operation is absorbed") {
  Fixture fixture;

  const auto authority = vector_v2::append_authority_only(
      fixture.log, {.color = 0xF800U, .gesture_id = 1U, .samples = kPen});
  REQUIRE(authority.has_value());
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(vector_v2::pending_operation_count(fixture.log, fixture.canvas) == 1U);

  const auto absorbed =
      vector_v2::absorb_pending_operation(fixture.log, fixture.canvas, fixture.workspace());
  REQUIRE(absorbed.has_value());
  CHECK(absorbed->identity == authority->identity);
  CHECK(fixture.canvas.current_revision() == fixture.log.current_revision());
  CHECK(vector_v2::pending_operation_count(fixture.log, fixture.canvas) == 0U);
  CHECK(fixture.overview[100U * vector_v2::kOverviewWidth + 100U] == 0xF800U);
}

TEST_CASE("pending absorption yields within one bounded replay quantum and stays pixel exact") {
  Fixture fixture;
  std::array<vector_v2::CompactOperationSample, 64> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = {
        .x_quarter = static_cast<std::uint16_t>(32U + index * 3U),
        .y_quarter = static_cast<std::uint16_t>(32U + (index % 8U) * 24U),
        .radius_256 = static_cast<std::uint16_t>(384U + (index % 5U) * 64U),
    };
  }
  const vector_v2::OperationAppend operation{
      .color = 0x001FU, .gesture_id = 7U, .samples = samples};
  std::array<std::uint16_t, vector_v2::kTilePixels> paper{};
  paper.fill(0xFFFFU);
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k400Percent, 0, 0};
  REQUIRE(
      fixture.canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, paper));
  REQUIRE(vector_v2::append_authority_only(fixture.log, operation));

  struct YieldAfterOne {
    mutable std::size_t checks = 0;
    static bool requested(const void* context) {
      auto& self = *static_cast<const YieldAfterOne*>(context);
      return ++self.checks > 1U;
    }
  } yield;
  vector_v2::PendingOperationAbsorption state;
  vector_v2::PendingAbsorptionSliceResult slice{};
  std::size_t pauses = 0;
  do {
    yield.checks = 0;
    slice = vector_v2::absorb_pending_operation_slice(
        fixture.log, fixture.canvas, fixture.workspace(), state,
        vector_v2::ViewRequest{.zoom = vector_v2::ZoomLevel::k400Percent,
                               .level_pixels = {0, 0, 64, 64}},
        {.requested = &YieldAfterOne::requested, .context = &yield, .raster_work_px = 64U});
    CHECK(slice.checkpoints <= 2U);
    pauses += slice.status == vector_v2::PendingAbsorptionStatus::kInProgress ? 1U : 0U;
  } while (slice.status == vector_v2::PendingAbsorptionStatus::kInProgress);

  REQUIRE(slice.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK(pauses > 100U);
  CHECK_FALSE(state.active());
  CHECK(fixture.canvas.current_revision() == fixture.log.current_revision());
  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected_overview{};
  replay_prefix(fixture.log, 1U, expected_overview);
  CHECK(fixture.overview == expected_overview);

  auto expected_tile = paper;
  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                         .level_bounds = {0, 0, 64, 64},
                                                         .pixels = expected_tile,
                                                         .stride = 64}));
  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  const auto composition = fixture.canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k400Percent, .level_pixels = {0, 0, 64, 64}}, composed);
  REQUIRE(composition.has_value());
  CHECK(composed == expected_tile);
}

TEST_CASE("pending absorption reports idle and rejects missing replay storage") {
  Fixture fixture;
  vector_v2::PendingOperationAbsorption state;
  CHECK(vector_v2::absorb_pending_operation_slice(fixture.log, fixture.canvas, fixture.workspace(),
                                                  state)
            .status == vector_v2::PendingAbsorptionStatus::kIdle);
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0xF800U, .gesture_id = 1U, .samples = kPen}));
  auto invalid = fixture.workspace();
  invalid.operation_chord_plans = {};
  CHECK(vector_v2::absorb_pending_operation_slice(fixture.log, fixture.canvas, invalid, state)
            .status == vector_v2::PendingAbsorptionStatus::kError);
  CHECK_FALSE(state.active());
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("resumable eraser retains visible uniform and offscreen raw tiles exactly") {
  Fixture fixture;
  const vector_v2::TileKey visible{vector_v2::ZoomLevel::k400Percent, 0, 0};
  const vector_v2::TileKey offscreen{vector_v2::ZoomLevel::k400Percent, 1, 0};
  REQUIRE(fixture.canvas.publish_uniform(visible, {0},
                                         vector_v2::MaterializationQuality::kImmediate, 0x001FU));
  std::array<std::uint16_t, vector_v2::kTilePixels> blue{};
  blue.fill(0x001FU);
  REQUIRE(fixture.canvas.publish_tile(offscreen, {0}, vector_v2::MaterializationQuality::kImmediate,
                                      blue));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 128, .y_quarter = 128, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 384, .y_quarter = 128, .radius_256 = 512},
  };
  const vector_v2::OperationAppend eraser{
      .tool = vector_v2::OperationTool::kEraser, .gesture_id = 9U, .samples = samples};
  REQUIRE(vector_v2::append_authority_only(fixture.log, eraser));

  vector_v2::PendingOperationAbsorption state;
  vector_v2::PendingAbsorptionSliceResult slice{};
  do {
    slice = vector_v2::absorb_pending_operation_slice(
        fixture.log, fixture.canvas, fixture.workspace(), state,
        vector_v2::ViewRequest{.zoom = vector_v2::ZoomLevel::k400Percent,
                               .level_pixels = {0, 0, 64, 64}},
        {.raster_work_px = 32U});
  } while (slice.status == vector_v2::PendingAbsorptionStatus::kInProgress);
  REQUIRE(slice.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK(slice.result.published_tiles == 2U);
  CHECK(slice.result.visible_fallback_tiles == 0U);

  std::array<std::uint16_t, 128U * 64U> expected{};
  expected.fill(0x001FU);
  REQUIRE(vector_v2::apply_incremental_operation(eraser, {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                          .level_bounds = {0, 0, 128, 64},
                                                          .pixels = expected,
                                                          .stride = 128}));
  std::array<std::uint16_t, 128U * 64U> composed{};
  REQUIRE(fixture.canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k400Percent, .level_pixels = {0, 0, 128, 64}}, composed));
  CHECK(composed == expected);
}

TEST_CASE("mismatched resume preserves the original absorption continuation") {
  Fixture fixture;
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0xF800U, .gesture_id = 1U, .samples = kPen}));
  struct YieldImmediately {
    static bool requested(const void*) { return true; }
  };
  const vector_v2::ViewRequest original{.zoom = vector_v2::ZoomLevel::k400Percent,
                                        .level_pixels = {0, 0, 64, 64}};
  vector_v2::PendingOperationAbsorption state;
  CHECK(vector_v2::absorb_pending_operation_slice(
            fixture.log, fixture.canvas, fixture.workspace(), state, original,
            {.requested = &YieldImmediately::requested, .raster_work_px = 64U})
            .status == vector_v2::PendingAbsorptionStatus::kInProgress);
  REQUIRE(state.active());
  CHECK(vector_v2::absorb_pending_operation_slice(
            fixture.log, fixture.canvas, fixture.workspace(), state,
            vector_v2::ViewRequest{.zoom = vector_v2::ZoomLevel::k200Percent,
                                   .level_pixels = {0, 0, 64, 64}},
            {.raster_work_px = 64U})
            .status == vector_v2::PendingAbsorptionStatus::kError);
  CHECK(state.active());
  const auto recovered = vector_v2::absorb_pending_operation_slice(
      fixture.log, fixture.canvas, fixture.workspace(), state, original, {.raster_work_px = 64U});
  REQUIRE(recovered.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK_FALSE(state.active());
  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected{};
  replay_prefix(fixture.log, 1U, expected);
  CHECK(fixture.overview == expected);
}

TEST_CASE("cancelled partial tile paint restarts exactly with a changed priority view") {
  Fixture fixture;
  const vector_v2::TileKey uniform{vector_v2::ZoomLevel::k400Percent, 0, 0};
  const vector_v2::TileKey raw{vector_v2::ZoomLevel::k400Percent, 1, 0};
  REQUIRE(fixture.canvas.publish_uniform(uniform, {0},
                                         vector_v2::MaterializationQuality::kImmediate, 0x001FU));
  std::array<std::uint16_t, vector_v2::kTilePixels> blue{};
  blue.fill(0x001FU);
  REQUIRE(
      fixture.canvas.publish_tile(raw, {0}, vector_v2::MaterializationQuality::kImmediate, blue));
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 128, .y_quarter = 128, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 384, .y_quarter = 128, .radius_256 = 512},
  };
  const vector_v2::OperationAppend eraser{
      .tool = vector_v2::OperationTool::kEraser, .gesture_id = 9U, .samples = samples};
  REQUIRE(vector_v2::append_authority_only(fixture.log, eraser));

  std::array<std::uint16_t, 128U * 64U> expected{};
  expected.fill(0x001FU);
  REQUIRE(vector_v2::apply_incremental_operation(eraser, {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                          .level_bounds = {0, 0, 128, 64},
                                                          .pixels = expected,
                                                          .stride = 128}));
  std::size_t expected_uniform_whites = 0;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    expected_uniform_whites += index % 128U < 64U && expected[index] == 0xFFFFU ? 1U : 0U;
  }
  const auto expected_total_whites =
      static_cast<std::size_t>(std::count(expected.begin(), expected.end(), 0xFFFFU));
  REQUIRE(expected_uniform_whites > 0);
  REQUIRE(expected_total_whites > expected_uniform_whites);

  struct YieldAfterOne {
    mutable std::size_t checks = 0;
    static bool requested(const void* context) {
      auto& self = *static_cast<const YieldAfterOne*>(context);
      return ++self.checks > 1U;
    }
  } yield;
  vector_v2::PendingOperationAbsorption state;
  const vector_v2::ViewRequest original_view{.zoom = vector_v2::ZoomLevel::k400Percent,
                                             .level_pixels = {0, 0, 64, 64}};
  bool raw_partially_painted = false;
  for (std::size_t slice_index = 0; slice_index < 1'000U; ++slice_index) {
    yield.checks = 0;
    const auto slice = vector_v2::absorb_pending_operation_slice(
        fixture.log, fixture.canvas, fixture.workspace(), state, original_view,
        {.requested = &YieldAfterOne::requested, .context = &yield, .raster_work_px = 64U});
    REQUIRE(slice.status == vector_v2::PendingAbsorptionStatus::kInProgress);
    const auto painted_whites = static_cast<std::size_t>(
        std::count(fixture.tile_pool.begin(), fixture.tile_pool.end(), 0xFFFFU));
    if (painted_whites > expected_uniform_whites && painted_whites < expected_total_whites) {
      raw_partially_painted = true;
      break;
    }
  }
  REQUIRE(raw_partially_painted);
  REQUIRE(state.active());
  const auto uniform_source = fixture.canvas.lookup(uniform);
  const auto raw_source = fixture.canvas.lookup(raw);
  REQUIRE(uniform_source.has_value());
  REQUIRE(raw_source.has_value());
  CHECK(uniform_source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(raw_source->kind == vector_v2::SourceKind::kTileSlot);

  state.cancel();
  CHECK_FALSE(state.active());
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(vector_v2::pending_operation_count(fixture.log, fixture.canvas) == 1U);

  const vector_v2::ViewRequest changed_view{.zoom = vector_v2::ZoomLevel::k400Percent,
                                            .level_pixels = {64, 0, 128, 64}};
  vector_v2::PendingAbsorptionSliceResult restarted{};
  do {
    restarted =
        vector_v2::absorb_pending_operation_slice(fixture.log, fixture.canvas, fixture.workspace(),
                                                  state, changed_view, {.raster_work_px = 64U});
  } while (restarted.status == vector_v2::PendingAbsorptionStatus::kInProgress);
  REQUIRE(restarted.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK_FALSE(state.active());
  CHECK(fixture.canvas.current_revision() == fixture.log.current_revision());

  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected_overview{};
  replay_prefix(fixture.log, 1U, expected_overview);
  CHECK(fixture.overview == expected_overview);
  std::array<std::uint16_t, 128U * 64U> composed{};
  REQUIRE(fixture.canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k400Percent, .level_pixels = {0, 0, 128, 64}}, composed));
  CHECK(composed == expected);
}

TEST_CASE("cancelled partial overview stage restarts with exact pending presentation") {
  Fixture fixture;
  std::array<vector_v2::CompactOperationSample, 64> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = {
        .x_quarter = static_cast<std::uint16_t>(index % 2U == 0U ? 80U : 5'800U),
        .y_quarter = static_cast<std::uint16_t>(80U + index * 6'960U / (samples.size() - 1U)),
        .radius_256 = 2'048U,
    };
  }
  const vector_v2::OperationAppend operation{
      .color = 0x07E0U, .gesture_id = 10U, .samples = samples};
  REQUIRE(vector_v2::append_authority_only(fixture.log, operation));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected{};
  replay_prefix(fixture.log, 1U, expected);
  const auto expected_ink =
      static_cast<std::size_t>(std::count(expected.begin(), expected.end(), 0x07E0U));
  REQUIRE(expected_ink > 1'000U);

  struct YieldAfterOne {
    mutable std::size_t checks = 0;
    static bool requested(const void* context) {
      auto& self = *static_cast<const YieldAfterOne*>(context);
      return ++self.checks > 1U;
    }
  } yield;
  vector_v2::PendingOperationAbsorption state;
  bool partially_staged = false;
  for (std::size_t slice_index = 0; slice_index < 10'000U; ++slice_index) {
    yield.checks = 0;
    const auto slice = vector_v2::absorb_pending_operation_slice(
        fixture.log, fixture.canvas, fixture.workspace(), state, std::nullopt,
        {.requested = &YieldAfterOne::requested, .context = &yield, .raster_work_px = 64U});
    REQUIRE(slice.status == vector_v2::PendingAbsorptionStatus::kInProgress);
    if (slice.work_unit != vector_v2::PendingAbsorptionWorkUnit::kStageOverview) {
      continue;
    }
    const auto staged_ink = static_cast<std::size_t>(
        std::count(fixture.overview.begin(), fixture.overview.end(), 0x07E0U));
    if (staged_ink != 0U && staged_ink < expected_ink) {
      partially_staged = true;
      break;
    }
  }
  REQUIRE(partially_staged);
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(vector_v2::pending_operation_count(fixture.log, fixture.canvas) == 1U);

  auto presented = fixture.overview;
  REQUIRE(vector_v2::overlay_pending_operations(
      fixture.log, fixture.canvas,
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
       .pixels = presented,
       .stride = vector_v2::kOverviewWidth}));
  CHECK(presented == expected);

  state.cancel();
  CHECK_FALSE(state.active());
  const auto restarted =
      vector_v2::absorb_pending_operation_slice(fixture.log, fixture.canvas, fixture.workspace(),
                                                state, std::nullopt, {.raster_work_px = 64U});
  REQUIRE(restarted.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK(fixture.overview == expected);
  CHECK(fixture.canvas.current_revision() == fixture.log.current_revision());
}

TEST_CASE("later authority may append while the oldest absorption is paused") {
  Fixture fixture;
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 640, .y_quarter = 640, .radius_256 = 384},
  };
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0xF800U, .gesture_id = 1U, .samples = kPen}));
  struct YieldImmediately {
    static bool requested(const void*) { return true; }
  };
  vector_v2::PendingOperationAbsorption state;
  CHECK(vector_v2::absorb_pending_operation_slice(
            fixture.log, fixture.canvas, fixture.workspace(), state, std::nullopt,
            {.requested = &YieldImmediately::requested, .raster_work_px = 64U})
            .status == vector_v2::PendingAbsorptionStatus::kInProgress);
  REQUIRE(vector_v2::append_authority_only(
      fixture.log, {.color = 0x07E0U, .gesture_id = 2U, .samples = second}));
  const auto first =
      vector_v2::absorb_pending_operation_slice(fixture.log, fixture.canvas, fixture.workspace(),
                                                state, std::nullopt, {.raster_work_px = 64U});
  REQUIRE(first.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(vector_v2::pending_operation_count(fixture.log, fixture.canvas) == 1U);
  const auto second_absorb =
      vector_v2::absorb_pending_operation_slice(fixture.log, fixture.canvas, fixture.workspace(),
                                                state, std::nullopt, {.raster_work_px = 64U});
  REQUIRE(second_absorb.status == vector_v2::PendingAbsorptionStatus::kComplete);
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{2});
  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected{};
  replay_prefix(fixture.log, 2U, expected);
  CHECK(fixture.overview == expected);
}

TEST_CASE("wall pauses between absorption slices do not spend retention budget") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 128, .y_quarter = 128, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 384, .y_quarter = 128, .radius_256 = 512},
  };
  const vector_v2::OperationAppend operation{
      .color = 0x07E0U, .gesture_id = 11U, .samples = samples};
  const vector_v2::ViewRequest view{.zoom = vector_v2::ZoomLevel::k400Percent,
                                    .level_pixels = {0, 0, 64, 64}};
  const auto prepare = [&](Fixture& fixture) {
    std::array<std::uint16_t, vector_v2::kTilePixels> paper{};
    paper.fill(0xFFFFU);
    REQUIRE(fixture.canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, 0, 0}, {0},
                                        vector_v2::MaterializationQuality::kImmediate, paper));
    REQUIRE(fixture.canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, 1, 0}, {0},
                                        vector_v2::MaterializationQuality::kImmediate, paper));
    REQUIRE(vector_v2::append_authority_only(fixture.log, operation));
  };
  const auto absorb = [&](Fixture& fixture, bool pause_between_slices) {
    struct YieldAfterOne {
      mutable std::size_t checks = 0;
      static bool requested(const void* context) {
        auto& self = *static_cast<const YieldAfterOne*>(context);
        return ++self.checks > 1U;
      }
    } yield;
    vector_v2::PendingOperationAbsorption state;
    vector_v2::PendingAbsorptionSliceResult result{};
    g_fake_retention_us = 0;
    do {
      yield.checks = 0;
      result = vector_v2::absorb_pending_operation_slice(
          fixture.log, fixture.canvas, fixture.workspace(), state, view,
          {.requested = pause_between_slices ? &YieldAfterOne::requested : nullptr,
           .context = &yield,
           .raster_work_px = 64U},
          {.now_us = &fake_retention_now_us, .budget_us = 10'000});
      if (result.status == vector_v2::PendingAbsorptionStatus::kInProgress) {
        g_fake_retention_us += 1'000'000;
      }
    } while (result.status == vector_v2::PendingAbsorptionStatus::kInProgress);
    return result;
  };

  Fixture uninterrupted;
  prepare(uninterrupted);
  const auto uninterrupted_result = absorb(uninterrupted, false);
  REQUIRE(uninterrupted_result.status == vector_v2::PendingAbsorptionStatus::kComplete);
  Fixture paused;
  prepare(paused);
  const auto paused_result = absorb(paused, true);
  REQUIRE(paused_result.status == vector_v2::PendingAbsorptionStatus::kComplete);

  CHECK(uninterrupted_result.result.published_tiles == 2U);
  CHECK(uninterrupted_result.result.drops.offscreen_skipped == 0U);
  CHECK(paused_result.result.published_tiles == uninterrupted_result.result.published_tiles);
  CHECK(paused_result.result.fallback_tiles == uninterrupted_result.result.fallback_tiles);
  CHECK(paused_result.result.drops.offscreen_skipped ==
        uninterrupted_result.result.drops.offscreen_skipped);
}

TEST_CASE("pending overlay patches presentation without mutating materialization") {
  Fixture fixture;
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0x001FU, .gesture_id = 1U, .samples = kPen})
              .has_value());
  const auto before = fixture.overview;
  auto composed = fixture.overview;

  REQUIRE(vector_v2::overlay_pending_operations(
      fixture.log, fixture.canvas,
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
       .pixels = composed,
       .stride = vector_v2::kOverviewWidth}));
  CHECK(fixture.overview == before);
  CHECK(composed != before);
  CHECK(composed[100U * vector_v2::kOverviewWidth + 100U] == 0x001FU);
}

TEST_CASE("failed absorption leaves the operation pending for retry") {
  Fixture fixture;
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0xF800U, .gesture_id = 1U, .samples = kPen})
              .has_value());
  auto invalid = fixture.workspace();
  invalid.tile_mask = {};

  CHECK_FALSE(vector_v2::absorb_pending_operation(fixture.log, fixture.canvas, invalid));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(vector_v2::pending_operation_count(fixture.log, fixture.canvas) == 1U);
  CHECK(vector_v2::absorb_pending_operation(fixture.log, fixture.canvas, fixture.workspace()));
}

TEST_CASE("every absorbed prefix and pending overlay equal direct vector replay") {
  Fixture fixture;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 960, .y_quarter = 160, .radius_256 = 768},
  };
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 320, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 960, .radius_256 = 512},
  };
  const std::array third{
      vector_v2::CompactOperationSample{.x_quarter = 640, .y_quarter = 160, .radius_256 = 384},
  };
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0xF800U, .gesture_id = 1U, .samples = first})
              .has_value());
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0x07E0U, .gesture_id = 2U, .samples = second})
              .has_value());
  REQUIRE(vector_v2::append_authority_only(
              fixture.log,
              {.tool = vector_v2::OperationTool::kEraser, .gesture_id = 3U, .samples = third})
              .has_value());

  auto patched = fixture.overview;
  REQUIRE(vector_v2::overlay_pending_operations(
      fixture.log, fixture.canvas,
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
       .pixels = patched,
       .stride = vector_v2::kOverviewWidth}));
  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected{};
  replay_prefix(fixture.log, 3U, expected);
  CHECK(patched == expected);

  for (std::size_t prefix = 1; prefix <= 3U; ++prefix) {
    REQUIRE(vector_v2::absorb_pending_operation(fixture.log, fixture.canvas, fixture.workspace()));
    replay_prefix(fixture.log, prefix, expected);
    CHECK(fixture.overview == expected);
  }
}

TEST_CASE("deferred absorption retains affected raw tiles at every zoom") {
  Fixture fixture;
  std::array<std::uint16_t, vector_v2::kTilePixels> paper{};
  paper.fill(0xFFFFU);
  const vector_v2::TileKey fine{vector_v2::ZoomLevel::k400Percent, 0, 0};
  const vector_v2::TileKey coarse{vector_v2::ZoomLevel::k100Percent, 0, 0};
  REQUIRE(
      fixture.canvas.publish_tile(fine, {0}, vector_v2::MaterializationQuality::kSettled, paper));
  REQUIRE(
      fixture.canvas.publish_tile(coarse, {0}, vector_v2::MaterializationQuality::kSettled, paper));
  const std::array stroke{
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 80, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
  };
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0x001FU, .gesture_id = 1U, .samples = stroke})
              .has_value());
  REQUIRE(vector_v2::absorb_pending_operation(
      fixture.log, fixture.canvas, fixture.workspace(),
      vector_v2::ViewRequest{.zoom = vector_v2::ZoomLevel::k400Percent,
                             .level_pixels = {0, 0, 64, 64}}));

  const auto fine_source = fixture.canvas.lookup(fine);
  const auto coarse_source = fixture.canvas.lookup(coarse);
  REQUIRE(fine_source.has_value());
  REQUIRE(coarse_source.has_value());
  CHECK(fine_source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(coarse_source->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(fine_source->revision == vector_v2::DocumentRevision{1});
  CHECK(coarse_source->revision == vector_v2::DocumentRevision{1});
}

TEST_CASE("history moves whole gestures after pending work is drained") {
  Fixture fixture;
  const std::array eraser{
      vector_v2::CompactOperationSample{
          .x_quarter = 6'400, .y_quarter = 4'800, .radius_256 = 5'120},
      vector_v2::CompactOperationSample{
          .x_quarter = 6'400, .y_quarter = 8'000, .radius_256 = 5'120},
  };
  REQUIRE(fixture.append_and_absorb({.color = 0xF800U, .gesture_id = 1U, .samples = kPen}));
  const auto line_pixels = fixture.overview;
  REQUIRE(fixture.append_and_absorb(
      {.tool = vector_v2::OperationTool::kEraser, .gesture_id = 2U, .samples = eraser}));
  const auto erased_pixels = fixture.overview;
  CHECK(erased_pixels != line_pixels);

  REQUIRE(vector_v2::move_history_incrementally(
      fixture.log, fixture.canvas, vector_v2::HistoryDirection::kUndo, fixture.scratch));
  CHECK(fixture.overview == line_pixels);
  REQUIRE(vector_v2::move_history_incrementally(
      fixture.log, fixture.canvas, vector_v2::HistoryDirection::kRedo, fixture.scratch));
  CHECK(fixture.overview == erased_pixels);
}

TEST_CASE("history refuses while materialization trails authority") {
  Fixture fixture;
  REQUIRE(vector_v2::append_authority_only(fixture.log,
                                           {.color = 0xF800U, .gesture_id = 1U, .samples = kPen})
              .has_value());

  CHECK_FALSE(vector_v2::move_history_incrementally(
      fixture.log, fixture.canvas, vector_v2::HistoryDirection::kUndo, fixture.scratch));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("snapshot restore resets both authorities and invalidates old replay ranges") {
  Fixture fixture;
  REQUIRE(fixture.append_and_absorb({.color = 0xF800U, .gesture_id = 1U, .samples = kPen}));
  const vector_v2::OperationLogEpoch old_epoch = fixture.log.epoch();
  fixture.scratch.fill(0x1234U);

  REQUIRE(vector_v2::restore_document_snapshot(fixture.log, fixture.canvas, {7}, fixture.scratch));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{7});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{7});
  CHECK(fixture.log.operation_count() == 0U);
  CHECK(fixture.overview.front() == 0x1234U);
  CHECK(fixture.log.epoch() != old_epoch);
  CHECK_FALSE(fixture.log.replay_range(old_epoch, {0}, {1}));
}

TEST_CASE("blank reset clears authority and overview without snapshot storage") {
  Fixture fixture;
  fixture.scratch.fill(0x1234U);
  REQUIRE(vector_v2::restore_document_snapshot(fixture.log, fixture.canvas, {4}, fixture.scratch));

  REQUIRE(vector_v2::reset_blank_document(fixture.log, fixture.canvas, {5}));
  CHECK(fixture.log.current_revision() == vector_v2::DocumentRevision{5});
  CHECK(fixture.canvas.current_revision() == vector_v2::DocumentRevision{5});
  CHECK(fixture.log.operation_count() == 0U);
  CHECK(std::all_of(fixture.overview.begin(), fixture.overview.end(),
                    [](std::uint16_t pixel) { return pixel == 0xFFFFU; }));
}

TEST_CASE("active-overview replay excludes retained redo operations") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 4> storage{};
  vector_v2::OperationLog log(records, storage);
  const std::array eraser{
      vector_v2::CompactOperationSample{
          .x_quarter = 6'400, .y_quarter = 4'800, .radius_256 = 5'120},
  };
  REQUIRE(log.append({.color = 0x001FU, .gesture_id = 1U, .samples = kPen}));
  REQUIRE(
      log.append({.tool = vector_v2::OperationTool::kEraser, .gesture_id = 2U, .samples = eraser}));
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();

  std::array<std::uint16_t, vector_v2::kOverviewPixels> replay{};
  std::array<std::uint16_t, vector_v2::kOverviewPixels> expected{};
  expected.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0x001FU, .samples = kPen},
      {.zoom = vector_v2::ZoomLevel::k25Percent,
       .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
       .pixels = expected,
       .stride = vector_v2::kOverviewWidth}));

  REQUIRE(vector_v2::replay_active_overview(log, replay));
  CHECK(replay == expected);
}
