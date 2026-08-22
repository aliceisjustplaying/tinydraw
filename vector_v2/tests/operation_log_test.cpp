#include "tinydraw/vector_v2/operation_log.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/test_fixtures.h"

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("operation log appends ordered samples and advances one revision") {
  vector_v2::test::OperationLogFixture<2, 5> fixture;
  auto& log = fixture.log;
  const std::array samples{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 320, .radius_256 = 512, .elapsed_ms = 0},
      vector_v2::CompactOperationSample{
          .x_quarter = 480, .y_quarter = 640, .radius_256 = 768, .elapsed_ms = 12},
  };

  const auto identity =
      log.append({.tool = vector_v2::OperationTool::kPen, .color = 0x07E0U, .samples = samples});
  REQUIRE(identity.has_value());
  CHECK(*identity == vector_v2::OperationIdentity{{1}, 0});
  CHECK(log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == samples.size());

  const auto stored = log.operation(0);
  REQUIRE(stored.has_value());
  CHECK(stored->identity == *identity);
  CHECK(stored->tool == vector_v2::OperationTool::kPen);
  CHECK(stored->color == 0x07E0U);
  CHECK(stored->samples.size() == 2U);
  CHECK(stored->samples[1].elapsed_ms == 12U);
  CHECK(stored->world_bounds == vector_v2::PixelRect{8, 18, 33, 43});

  vector_v2::StoredOperation caller_owned;
  REQUIRE(log.operation(0, caller_owned));
  CHECK(caller_owned.identity == stored->identity);
  CHECK(caller_owned.tool == stored->tool);
  CHECK(caller_owned.color == stored->color);
  CHECK(caller_owned.gesture_id == stored->gesture_id);
  CHECK(caller_owned.world_bounds == stored->world_bounds);
  CHECK(caller_owned.samples.data() == stored->samples.data());
  CHECK(caller_owned.samples.size() == stored->samples.size());
  CHECK_FALSE(log.operation(1, caller_owned));
}

TEST_CASE("Stroke continuations require an exact shared sample boundary") {
  vector_v2::test::OperationLogFixture<2, 4> fixture;
  auto& log = fixture.log;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256},
  };
  const std::array disconnected{
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 256},
  };

  REQUIRE(log.append({.gesture_id = 7U, .samples = first}));
  CHECK_FALSE(log.append({.gesture_id = 7U, .samples = disconnected}));
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == first.size());
}

TEST_CASE("Stroke identity includes tool and color in history") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(log.append({.color = 0x001FU, .gesture_id = 7U, .samples = sample}));
  REQUIRE(log.append({.color = 0xF800U, .gesture_id = 7U, .samples = sample}));

  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  CHECK(undo->change().active_operation_count == 1U);
}

TEST_CASE("prepared history borrows target records and samples without materialization") {
  vector_v2::test::OperationLogFixture<2, 3> fixture;
  auto& log = fixture.log;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 32, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 64, .radius_256 = 512},
  };
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 96, .radius_256 = 768}};
  REQUIRE(log.append({.color = 0x07E0U, .gesture_id = 1U, .samples = first}));
  REQUIRE(log.append({.color = 0xF800U, .gesture_id = 2U, .samples = second}));

  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  const vector_v2::OperationRecord* record = &fixture.records.back();
  std::span<const vector_v2::CompactOperationSample> samples = std::span(fixture.samples).last(1U);
  REQUIRE(undo->target_operation(0U, record, samples));
  CHECK(record == fixture.records.data());
  CHECK(record->color == 0x07E0U);
  CHECK(samples.data() == fixture.samples.data());
  CHECK(samples.size() == first.size());
  CHECK(samples[0] == first[0]);
  CHECK(samples[1] == first[1]);

  CHECK_FALSE(undo->target_operation(1U, record, samples));
  CHECK(record == nullptr);
  CHECK(samples.empty());
  undo->cancel();
  CHECK_FALSE(undo->target_operation(0U, record, samples));
  CHECK(record == nullptr);
  CHECK(samples.empty());

  undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  auto redo = log.prepare_redo();
  REQUIRE(redo.has_value());
  REQUIRE(redo->target_operation(1U, record, samples));
  CHECK(record == fixture.records.data() + 1U);
  CHECK(record->color == 0xF800U);
  CHECK(samples.data() == fixture.samples.data() + first.size());
  CHECK(samples.size() == second.size());
  CHECK(samples.front() == second.front());
  redo->cancel();
}

TEST_CASE("authority read views snapshot generation and retained counts") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};

  const vector_v2::AuthorityReadView empty = log.read_view();
  CHECK(empty.generation == vector_v2::DocumentRevision{0});
  CHECK(empty.active_operation_count == 0U);
  CHECK(empty.retained_operation_count == 0U);

  REQUIRE(log.append({.gesture_id = 7U, .samples = sample}));

  const vector_v2::AuthorityReadView one = log.read_view();
  CHECK(one.generation == vector_v2::DocumentRevision{1});
  CHECK(one.active_operation_count == 1U);
  CHECK(one.retained_operation_count == 1U);
  CHECK(one.retained_sample_count == 1U);
  REQUIRE(log.operation(0));
  CHECK(log.operation(0)->gesture_id == 7U);
  REQUIRE(log.retained_operation(0));
  CHECK(log.retained_operation(0)->identity.operation_index == 0U);

  REQUIRE(log.reset({8}));
  CHECK(log.read_view() != one);
  CHECK_FALSE(log.retained_operation(0));
}

TEST_CASE("Undo moves across every chunk in the final Stroke") {
  vector_v2::test::OperationLogFixture<3, 4> fixture;
  auto& log = fixture.log;
  const std::array first_stroke{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  const std::array second_stroke_first{vector_v2::CompactOperationSample{
      .x_quarter = 160, .y_quarter = 320, .radius_256 = 512, .elapsed_ms = 0}};
  const std::array second_stroke_second{
      second_stroke_first.back(),
      vector_v2::CompactOperationSample{
          .x_quarter = 480, .y_quarter = 640, .radius_256 = 768, .elapsed_ms = 12}};
  REQUIRE(log.append({.gesture_id = 6U, .samples = first_stroke}));
  REQUIRE(log.append({.gesture_id = 7U, .samples = second_stroke_first}));
  REQUIRE(log.append({.gesture_id = 7U, .samples = second_stroke_second}));
  const vector_v2::AuthorityReadView before = log.read_view();

  CHECK(log.can_undo());
  CHECK_FALSE(log.can_redo());
  auto canceled = log.prepare_undo();
  REQUIRE(canceled.has_value());
  CHECK(canceled->change().generation == vector_v2::DocumentRevision{4});
  CHECK(canceled->change().previous_active_operation_count == 3U);
  CHECK(canceled->change().active_operation_count == 1U);
  CHECK(canceled->change().affected_world_bounds == vector_v2::PixelRect{8, 18, 33, 43});
  canceled->cancel();
  CHECK(log.read_view() == before);

  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  CHECK(log.current_revision() == vector_v2::DocumentRevision{4});
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == 1U);
  CHECK(log.can_undo());
  CHECK(log.can_redo());
  CHECK(log.epoch() != before.epoch);
  CHECK_FALSE(log.operation(1));
  const vector_v2::AuthorityReadView after = log.read_view();
  CHECK(after.active_operation_count == 1U);
  CHECK(after.retained_operation_count == 3U);
  REQUIRE(log.retained_operation(2));
  CHECK(log.retained_operation(2)->gesture_id == 7U);
}

TEST_CASE("Redo restores every chunk in the next Stroke") {
  vector_v2::test::OperationLogFixture<3, 4> fixture;
  auto& log = fixture.log;
  const std::array first_stroke{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  const std::array second_stroke_first{vector_v2::CompactOperationSample{
      .x_quarter = 160, .y_quarter = 320, .radius_256 = 512, .elapsed_ms = 0}};
  const std::array second_stroke_second{
      second_stroke_first.back(),
      vector_v2::CompactOperationSample{
          .x_quarter = 480, .y_quarter = 640, .radius_256 = 768, .elapsed_ms = 12}};
  REQUIRE(log.append({.gesture_id = 6U, .samples = first_stroke}));
  REQUIRE(log.append({.gesture_id = 7U, .samples = second_stroke_first}));
  REQUIRE(log.append({.gesture_id = 7U, .samples = second_stroke_second}));
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  const vector_v2::OperationLogEpoch undo_epoch = log.epoch();

  auto redo = log.prepare_redo();
  REQUIRE(redo.has_value());
  CHECK(redo->change().generation == vector_v2::DocumentRevision{5});
  CHECK(redo->change().previous_active_operation_count == 1U);
  CHECK(redo->change().active_operation_count == 3U);
  CHECK(redo->change().affected_world_bounds == vector_v2::PixelRect{8, 18, 33, 43});
  redo->publish();

  CHECK(log.current_revision() == vector_v2::DocumentRevision{5});
  CHECK(log.operation_count() == 3U);
  CHECK(log.sample_count() == 4U);
  CHECK(log.can_undo());
  CHECK_FALSE(log.can_redo());
  CHECK(log.epoch() != undo_epoch);
  REQUIRE(log.operation(2));
  CHECK(log.operation(2)->gesture_id == 7U);
}

TEST_CASE("a prepared history change owns the authority mutation slot") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(log.append({.gesture_id = 1U, .samples = sample}));
  const vector_v2::OperationLogEpoch epoch = log.epoch();
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());

  CHECK_FALSE(log.replay_range(epoch, {0}, {1}));
  CHECK_FALSE(log.append({.gesture_id = 2U, .samples = sample}));
  CHECK_FALSE(log.reset());
  undo->cancel();

  CHECK(log.replay_range(epoch, {0}, {1}) ==
        vector_v2::OperationReplayRange{epoch, {0}, {1}, 0, 1});
  CHECK(log.append({.gesture_id = 2U, .samples = sample}));
}

TEST_CASE("history preserves ten levels and treats zero identities as separate Strokes") {
  vector_v2::test::OperationLogFixture<12, 12> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  for (std::size_t index = 0; index < fixture.records.size(); ++index) {
    REQUIRE(log.append({.gesture_id = 0U, .samples = sample}));
  }

  for (std::size_t index = 0; index < 10U; ++index) {
    auto undo = log.prepare_undo();
    REQUIRE(undo.has_value());
    CHECK(undo->change().previous_active_operation_count - undo->change().active_operation_count ==
          1U);
    undo->publish();
  }
  CHECK(log.operation_count() == 2U);

  for (std::size_t index = 0; index < 10U; ++index) {
    auto redo = log.prepare_redo();
    REQUIRE(redo.has_value());
    redo->publish();
  }
  CHECK(log.operation_count() == 12U);
  CHECK_FALSE(log.can_redo());
}

TEST_CASE("history stops before the document generation would wrap") {
  vector_v2::test::OperationLogFixture<1, 1> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(log.reset({std::numeric_limits<std::uint32_t>::max() - 1U}));
  REQUIRE(log.append({.gesture_id = 1U, .samples = sample}));

  CHECK(log.current_revision() ==
        vector_v2::DocumentRevision{std::numeric_limits<std::uint32_t>::max()});
  CHECK_FALSE(log.can_undo());
  CHECK_FALSE(log.prepare_undo());
}

TEST_CASE("new ink after Undo replaces the Redo Stroke") {
  vector_v2::test::OperationLogFixture<3, 3> fixture;
  auto& log = fixture.log;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256}};
  const std::array replacement{
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 256}};
  REQUIRE(log.append({.gesture_id = 1U, .samples = first}));
  REQUIRE(log.append({.gesture_id = 2U, .samples = second}));
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  const vector_v2::OperationLogEpoch undo_epoch = log.epoch();

  CHECK(log.append({.gesture_id = 3U, .samples = replacement}) ==
        vector_v2::OperationIdentity{{4}, 1});

  CHECK(log.current_revision() == vector_v2::DocumentRevision{4});
  CHECK(log.epoch() != undo_epoch);
  CHECK(log.operation_count() == 2U);
  CHECK(log.sample_count() == 2U);
  CHECK_FALSE(log.can_redo());
  const vector_v2::AuthorityReadView after_publish = log.read_view();
  CHECK(after_publish.active_operation_count == 2U);
  CHECK(after_publish.retained_operation_count == 2U);
  REQUIRE(log.operation(1));
  CHECK(log.operation(1)->gesture_id == 3U);
  CHECK(log.operation(1)->samples.front().x_quarter == 48U);
}

TEST_CASE("rejected replacement ink after Undo preserves the Redo Stroke") {
  vector_v2::test::OperationLogFixture<3, 2> fixture;
  auto& log = fixture.log;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256}};
  const std::array too_large{
      vector_v2::CompactOperationSample{
          .x_quarter = 48, .y_quarter = 48, .radius_256 = 256, .elapsed_ms = 0},
      vector_v2::CompactOperationSample{
          .x_quarter = 64, .y_quarter = 64, .radius_256 = 256, .elapsed_ms = 1},
  };
  REQUIRE(log.append({.gesture_id = 1U, .samples = first}));
  REQUIRE(log.append({.gesture_id = 2U, .samples = second}));
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();

  CHECK_FALSE(log.append({.gesture_id = 3U, .samples = too_large}));
  CHECK(log.can_redo());
  const vector_v2::AuthorityReadView after_rejection = log.read_view();
  CHECK(after_rejection.active_operation_count == 1U);
  CHECK(after_rejection.retained_operation_count == 2U);
  REQUIRE(log.retained_operation(1));
  CHECK(log.retained_operation(1)->samples.front().x_quarter == 32U);
}

TEST_CASE("stored operation feeds the incremental renderer without translation") {
  vector_v2::test::OperationLogFixture<1, 2> fixture;
  auto& log = fixture.log;
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 16, .radius_256 = 256},
  };
  REQUIRE(log.append({.color = 0xF800U, .samples = samples}));
  const auto stored = log.operation(0);
  REQUIRE(stored.has_value());
  std::array<std::uint16_t, 16U * 4U> pixels{};
  pixels.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
      {.zoom = vector_v2::ZoomLevel::k100Percent,
       .level_bounds = {0, 0, 16, 4},
       .pixels = pixels,
       .stride = 16}));
  CHECK(pixels[1U * 16U + 1U] == 0xF800U);
  CHECK(pixels[1U * 16U + 10U] == 0xF800U);
  CHECK(stored->world_bounds == vector_v2::PixelRect{0, 0, 12, 3});
}

TEST_CASE("operation log preserves painter order across tools") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array pen_sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  const std::array eraser_sample{
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256}};
  REQUIRE(log.append(
      {.tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = pen_sample}));
  const auto eraser =
      log.append({.tool = vector_v2::OperationTool::kEraser, .samples = eraser_sample});
  REQUIRE(eraser.has_value());
  CHECK(*eraser == vector_v2::OperationIdentity{{2}, 1});
  REQUIRE(log.operation(0).has_value());
  REQUIRE(log.operation(1).has_value());
  CHECK(log.operation(0)->tool == vector_v2::OperationTool::kPen);
  CHECK(log.operation(1)->tool == vector_v2::OperationTool::kEraser);
}

TEST_CASE("operation log append may exactly fill sample capacity") {
  vector_v2::test::OperationLogFixture<1, 2> fixture;
  auto& log = fixture.log;
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256},
  };
  CHECK(log.append({.samples = samples}));
  CHECK(log.sample_count() == log.sample_capacity());
}

TEST_CASE("operation log capacity failure is atomic") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(log.append({.samples = first}));
  const auto before = log.operation(0);
  REQUIRE(before.has_value());
  const std::array too_many{
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 256},
  };

  CHECK_FALSE(log.append({.samples = too_many}));
  CHECK(log.current_revision() == vector_v2::DocumentRevision{1});
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == 1U);
  CHECK(log.operation(0)->samples.front().x_quarter == before->samples.front().x_quarter);
}

TEST_CASE("operation log rejects malformed samples without mutation") {
  vector_v2::test::OperationLogFixture<1, 2> fixture;
  auto& log = fixture.log;
  const std::array reversed_time{
      vector_v2::CompactOperationSample{
          .x_quarter = 16, .y_quarter = 16, .radius_256 = 256, .elapsed_ms = 2},
      vector_v2::CompactOperationSample{
          .x_quarter = 32, .y_quarter = 32, .radius_256 = 256, .elapsed_ms = 1},
  };
  CHECK_FALSE(log.append({.samples = reversed_time}));
  const std::array outside{
      vector_v2::CompactOperationSample{.x_quarter = 24000, .y_quarter = 16, .radius_256 = 256}};
  CHECK_FALSE(log.append({.samples = outside}));
  const std::array zero_radius{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 0}};
  CHECK_FALSE(log.append({.samples = zero_radius}));
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("operation log exposes only represented contiguous replay ranges") {
  vector_v2::test::OperationLogFixture<3, 3> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(log.reset({8}));
  const vector_v2::OperationLogEpoch epoch = log.epoch();
  REQUIRE(log.append({.color = 0x001FU, .samples = sample}));
  REQUIRE(log.append({.tool = vector_v2::OperationTool::kEraser, .samples = sample}));
  REQUIRE(log.append({.color = 0xF800U, .samples = sample}));

  CHECK(log.replay_range(epoch, {8}, {11}) ==
        vector_v2::OperationReplayRange{epoch, {8}, {11}, 0, 3});
  CHECK(log.replay_range(epoch, {9}, {11}) ==
        vector_v2::OperationReplayRange{epoch, {9}, {11}, 1, 2});
  CHECK(log.replay_range(epoch, {10}, {10}) ==
        vector_v2::OperationReplayRange{epoch, {10}, {10}, 2, 0});
  CHECK(log.operation(1)->identity.revision == vector_v2::DocumentRevision{10});
  CHECK_FALSE(log.replay_range(epoch, {7}, {9}));
  CHECK_FALSE(log.replay_range(epoch, {9}, {8}));
  CHECK_FALSE(log.replay_range(epoch, {8}, {12}));

  REQUIRE(log.reset({8}));
  CHECK(log.epoch() != epoch);
  CHECK_FALSE(log.replay_range(epoch, {8}, {8}));
  CHECK(log.replay_range(log.epoch(), {8}, {8}) ==
        vector_v2::OperationReplayRange{log.epoch(), {8}, {8}, 0, 0});
}

TEST_CASE("operation log restore rejects malformed persistence without mutation") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array original{
      vector_v2::CompactOperationSample{
          .x_quarter = 16, .y_quarter = 16, .radius_256 = 256, .elapsed_ms = 0},
  };
  REQUIRE(log.append({.color = 0x001FU, .gesture_id = 1U, .samples = original}));
  const vector_v2::AuthorityReadView before = log.read_view();

  std::array<vector_v2::OperationRecord, 1> restored_records{{
      {.first_sample = 0,
       .sample_count = 1,
       .color = 0xF800U,
       .bounds_x0 = 99,
       .bounds_y0 = 99,
       .bounds_x1 = 100,
       .bounds_y1 = 100,
       .tool = vector_v2::OperationTool::kPen,
       .gesture_id = 7U},
  }};
  const std::array restored_samples{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 160, .radius_256 = 256, .elapsed_ms = 0},
  };
  CHECK_FALSE(log.restore({.epoch = {9},
                           .generation = {8},
                           .active_operation_count = 1,
                           .records = restored_records,
                           .samples = restored_samples}));
  CHECK(log.read_view() == before);
  REQUIRE(log.operation(0));
  CHECK(log.operation(0)->color == 0x001FU);
  CHECK(log.operation(0)->samples.front() == original.front());
}

TEST_CASE("operation log restore rejects a disconnected Stroke continuation") {
  vector_v2::test::OperationLogFixture<2, 2> fixture;
  auto& log = fixture.log;
  const std::array restored_samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 256},
  };
  std::array<vector_v2::OperationRecord, 2> restored_records{};
  for (std::size_t index = 0; index < restored_records.size(); ++index) {
    const auto bounds =
        vector_v2::operation_world_bounds(std::span(restored_samples).subspan(index, 1));
    REQUIRE(bounds.has_value());
    restored_records[index] = {
        .first_sample = static_cast<std::uint32_t>(index),
        .sample_count = 1,
        .bounds_x0 = static_cast<std::uint16_t>(bounds->x0),
        .bounds_y0 = static_cast<std::uint16_t>(bounds->y0),
        .bounds_x1 = static_cast<std::uint16_t>(bounds->x1),
        .bounds_y1 = static_cast<std::uint16_t>(bounds->y1),
        .gesture_id = 7U,
    };
  }

  CHECK_FALSE(log.restore({.generation = {2},
                           .active_operation_count = 2,
                           .records = restored_records,
                           .samples = restored_samples}));
  CHECK(log.read_view() == vector_v2::AuthorityReadView{});
}

TEST_CASE("operation log reset adopts snapshot revision and retains caller storage") {
  vector_v2::test::OperationLogFixture<1, 1> fixture;
  auto& log = fixture.log;
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(log.append({.samples = sample}));
  REQUIRE(log.reset({8}));
  CHECK(log.ready());
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == vector_v2::DocumentRevision{8});
  CHECK_FALSE(log.operation(0));
  CHECK(log.append({.samples = sample}) == vector_v2::OperationIdentity{{9}, 0});
  REQUIRE(log.operation(0).has_value());
  CHECK(log.operation(0)->identity == vector_v2::OperationIdentity{{9}, 0});
  REQUIRE(log.clear());
  CHECK(log.current_revision() == vector_v2::DocumentRevision{0});
}

TEST_CASE("operation log with overlapping record and sample storage is not ready") {
  std::array<vector_v2::OperationRecord, 4> records{};
  // Alias the record storage as sample storage; ready() must reject the
  // overlap before any append can corrupt records through the sample span.
  // The aliased span is never dereferenced.
  const std::span<vector_v2::CompactOperationSample> aliased{
      reinterpret_cast<vector_v2::CompactOperationSample*>(records.data()), 8};

  vector_v2::OperationLog log(records, aliased);
  CHECK(!log.ready());

  std::array<vector_v2::CompactOperationSample, 8> separate{};
  vector_v2::OperationLog healthy(records, separate);
  CHECK(healthy.ready());
}
