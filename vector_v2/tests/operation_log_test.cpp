#include "tinydraw/vector_v2/operation_log.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("operation log appends ordered samples and advances one revision") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 5> storage{};
  vector_v2::OperationLog log(records, storage);
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
}

TEST_CASE("authority read views expose one coherent generation") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};

  const vector_v2::AuthorityReadView empty = log.read_view();
  CHECK(empty.generation == vector_v2::DocumentRevision{0});
  CHECK(empty.active_operation_count == 0U);
  CHECK(empty.retained_operation_count == 0U);
  CHECK(log.unchanged(empty));

  REQUIRE(log.append({.gesture_id = 7U, .samples = sample}));
  CHECK_FALSE(log.unchanged(empty));
  CHECK_FALSE(log.operation(empty, 0));

  const vector_v2::AuthorityReadView one = log.read_view();
  CHECK(one.generation == vector_v2::DocumentRevision{1});
  CHECK(one.active_operation_count == 1U);
  CHECK(one.retained_operation_count == 1U);
  REQUIRE(log.operation(one, 0));
  CHECK(log.operation(one, 0)->gesture_id == 7U);
  REQUIRE(log.retained_operation(one, 0));
  CHECK(log.retained_operation(one, 0)->identity.operation_index == 0U);

  REQUIRE(log.reset({8}));
  CHECK_FALSE(log.unchanged(one));
  CHECK_FALSE(log.retained_operation(one, 0));
}

TEST_CASE("stored operation feeds the incremental renderer without translation") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
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
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
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
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256},
  };
  CHECK(log.append({.samples = samples}));
  CHECK(log.sample_count() == log.sample_capacity());
}

TEST_CASE("prepared append advances authority only when published") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256},
  };
  auto prepared = log.prepare({.color = 0xF800U, .samples = samples});
  REQUIRE(prepared.has_value());
  CHECK(prepared->operation().identity == vector_v2::OperationIdentity{{1}, 0});
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK_FALSE(log.prepare({.samples = samples}));
  CHECK_FALSE(log.clear());
  CHECK(log.current_revision() == vector_v2::DocumentRevision{0});

  prepared->publish();
  CHECK(prepared->operation().samples.empty());
  prepared->publish();
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == 2U);
  CHECK(log.current_revision() == vector_v2::DocumentRevision{1});
}

TEST_CASE("canceling a prepared append leaves authority unchanged") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 1> storage{};
  vector_v2::OperationLog log(records, storage);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  auto prepared = log.prepare({.samples = samples});
  REQUIRE(prepared.has_value());
  prepared->cancel();
  CHECK(prepared->operation().samples.empty());
  prepared->cancel();
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == vector_v2::DocumentRevision{0});
  CHECK(log.append({.samples = samples}) == vector_v2::OperationIdentity{{1}, 0});
}

TEST_CASE("operation log capacity failure is atomic") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
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
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationLog log(records, storage);
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
  std::array<vector_v2::OperationRecord, 3> records{};
  std::array<vector_v2::CompactOperationSample, 3> storage{};
  vector_v2::OperationLog log(records, storage);
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

TEST_CASE("operation log withholds replay ranges while an append is prepared") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 1> storage{};
  vector_v2::OperationLog log(records, storage);
  const std::array sample{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  const vector_v2::OperationLogEpoch epoch = log.epoch();
  auto prepared = log.prepare({.samples = sample});
  REQUIRE(prepared.has_value());

  CHECK_FALSE(log.replay_range(epoch, {0}, {0}));
  prepared->cancel();
  CHECK(log.replay_range(epoch, {0}, {0}) ==
        vector_v2::OperationReplayRange{epoch, {0}, {0}, 0, 0});
}

TEST_CASE("operation log reset adopts snapshot revision and retains caller storage") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 1> storage{};
  vector_v2::OperationLog log(records, storage);
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
