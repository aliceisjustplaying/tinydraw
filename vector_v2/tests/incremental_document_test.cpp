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
