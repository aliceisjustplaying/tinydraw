#include "tinydraw/production/operation_log.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/production/incremental_rasterizer.h"

namespace production = tinydraw::production;

TEST_CASE("operation log appends ordered samples and advances one revision") {
  std::array<production::OperationRecord, 2> records{};
  std::array<production::CompactOperationSample, 5> storage{};
  production::OperationLog log(records, storage);
  const std::array samples{
      production::CompactOperationSample{
          .x_quarter = 40, .y_quarter = 80, .radius_256 = 512, .elapsed_ms = 0},
      production::CompactOperationSample{
          .x_quarter = 120, .y_quarter = 160, .radius_256 = 768, .elapsed_ms = 12},
  };

  const auto identity =
      log.append({.tool = production::OperationTool::kPen, .color = 0x07E0U, .samples = samples});
  REQUIRE(identity.has_value());
  CHECK(*identity == production::OperationIdentity{{1}, 0});
  CHECK(log.current_revision() == production::DocumentRevision{1});
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == samples.size());

  const auto stored = log.operation(0);
  REQUIRE(stored.has_value());
  CHECK(stored->identity == *identity);
  CHECK(stored->tool == production::OperationTool::kPen);
  CHECK(stored->color == 0x07E0U);
  CHECK(stored->samples.size() == 2U);
  CHECK(stored->samples[1].elapsed_ms == 12U);
  CHECK(stored->world_bounds == production::PixelRect{8, 18, 33, 43});
}

TEST_CASE("stored operation feeds the incremental renderer without translation") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 2> storage{};
  production::OperationLog log(records, storage);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256},
      production::CompactOperationSample{.x_quarter = 40, .y_quarter = 4, .radius_256 = 256},
  };
  REQUIRE(log.append({.color = 0xF800U, .samples = samples}));
  const auto stored = log.operation(0);
  REQUIRE(stored.has_value());
  std::array<std::uint16_t, 16U * 4U> pixels{};
  pixels.fill(0xFFFFU);
  REQUIRE(production::apply_incremental_operation(
      {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
      {.zoom = production::ZoomLevel::k100Percent,
       .level_bounds = {0, 0, 16, 4},
       .pixels = pixels,
       .stride = 16}));
  CHECK(pixels[1U * 16U + 1U] == 0xF800U);
  CHECK(pixels[1U * 16U + 10U] == 0xF800U);
  CHECK(stored->world_bounds == production::PixelRect{0, 0, 12, 3});
}

TEST_CASE("operation log preserves painter order across tools") {
  std::array<production::OperationRecord, 2> records{};
  std::array<production::CompactOperationSample, 2> storage{};
  production::OperationLog log(records, storage);
  const std::array pen_sample{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  const std::array eraser_sample{
      production::CompactOperationSample{.x_quarter = 8, .y_quarter = 8, .radius_256 = 256}};
  REQUIRE(log.append(
      {.tool = production::OperationTool::kPen, .color = 0x001FU, .samples = pen_sample}));
  const auto eraser =
      log.append({.tool = production::OperationTool::kEraser, .samples = eraser_sample});
  REQUIRE(eraser.has_value());
  CHECK(*eraser == production::OperationIdentity{{2}, 1});
  REQUIRE(log.operation(0).has_value());
  REQUIRE(log.operation(1).has_value());
  CHECK(log.operation(0)->tool == production::OperationTool::kPen);
  CHECK(log.operation(1)->tool == production::OperationTool::kEraser);
}

TEST_CASE("operation log append may exactly fill sample capacity") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 2> storage{};
  production::OperationLog log(records, storage);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256},
      production::CompactOperationSample{.x_quarter = 8, .y_quarter = 8, .radius_256 = 256},
  };
  CHECK(log.append({.samples = samples}));
  CHECK(log.sample_count() == log.sample_capacity());
}

TEST_CASE("prepared append advances authority only when published") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 2> storage{};
  production::OperationLog log(records, storage);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256},
      production::CompactOperationSample{.x_quarter = 8, .y_quarter = 8, .radius_256 = 256},
  };
  auto prepared = log.prepare({.color = 0xF800U, .samples = samples});
  REQUIRE(prepared.has_value());
  CHECK(prepared->operation().identity == production::OperationIdentity{{1}, 0});
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == production::DocumentRevision{0});
  CHECK_FALSE(log.prepare({.samples = samples}));
  CHECK_FALSE(log.clear());
  CHECK(log.current_revision() == production::DocumentRevision{0});

  prepared->publish();
  CHECK(prepared->operation().samples.empty());
  prepared->publish();
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == 2U);
  CHECK(log.current_revision() == production::DocumentRevision{1});
}

TEST_CASE("canceling a prepared append leaves authority unchanged") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 1> storage{};
  production::OperationLog log(records, storage);
  const std::array samples{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  auto prepared = log.prepare({.samples = samples});
  REQUIRE(prepared.has_value());
  prepared->cancel();
  CHECK(prepared->operation().samples.empty());
  prepared->cancel();
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == production::DocumentRevision{0});
  CHECK(log.append({.samples = samples}) == production::OperationIdentity{{1}, 0});
}

TEST_CASE("operation log capacity failure is atomic") {
  std::array<production::OperationRecord, 2> records{};
  std::array<production::CompactOperationSample, 2> storage{};
  production::OperationLog log(records, storage);
  const std::array first{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(log.append({.samples = first}));
  const auto before = log.operation(0);
  REQUIRE(before.has_value());
  const std::array too_many{
      production::CompactOperationSample{.x_quarter = 8, .y_quarter = 8, .radius_256 = 256},
      production::CompactOperationSample{.x_quarter = 12, .y_quarter = 12, .radius_256 = 256},
  };

  CHECK_FALSE(log.append({.samples = too_many}));
  CHECK(log.current_revision() == production::DocumentRevision{1});
  CHECK(log.operation_count() == 1U);
  CHECK(log.sample_count() == 1U);
  CHECK(log.operation(0)->samples.front().x_quarter == before->samples.front().x_quarter);
}

TEST_CASE("operation log rejects malformed samples without mutation") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 2> storage{};
  production::OperationLog log(records, storage);
  const std::array reversed_time{
      production::CompactOperationSample{
          .x_quarter = 4, .y_quarter = 4, .radius_256 = 256, .elapsed_ms = 2},
      production::CompactOperationSample{
          .x_quarter = 8, .y_quarter = 8, .radius_256 = 256, .elapsed_ms = 1},
  };
  CHECK_FALSE(log.append({.samples = reversed_time}));
  const std::array outside{
      production::CompactOperationSample{.x_quarter = 6000, .y_quarter = 4, .radius_256 = 256}};
  CHECK_FALSE(log.append({.samples = outside}));
  const std::array zero_radius{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 0}};
  CHECK_FALSE(log.append({.samples = zero_radius}));
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == production::DocumentRevision{0});
}

TEST_CASE("operation log exposes only represented contiguous replay ranges") {
  std::array<production::OperationRecord, 3> records{};
  std::array<production::CompactOperationSample, 3> storage{};
  production::OperationLog log(records, storage);
  const std::array sample{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(log.reset({8}));
  REQUIRE(log.append({.color = 0x001FU, .samples = sample}));
  REQUIRE(log.append({.tool = production::OperationTool::kEraser, .samples = sample}));
  REQUIRE(log.append({.color = 0xF800U, .samples = sample}));

  CHECK(log.replay_range({8}, {11}) == production::OperationReplayRange{{8}, {11}, 0, 3});
  CHECK(log.replay_range({9}, {11}) == production::OperationReplayRange{{9}, {11}, 1, 2});
  CHECK(log.replay_range({10}, {10}) == production::OperationReplayRange{{10}, {10}, 2, 0});
  CHECK_FALSE(log.replay_range({7}, {9}));
  CHECK_FALSE(log.replay_range({9}, {8}));
  CHECK_FALSE(log.replay_range({8}, {12}));
}

TEST_CASE("operation log withholds replay ranges while an append is prepared") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 1> storage{};
  production::OperationLog log(records, storage);
  const std::array sample{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  auto prepared = log.prepare({.samples = sample});
  REQUIRE(prepared.has_value());

  CHECK_FALSE(log.replay_range({0}, {0}));
  prepared->cancel();
  CHECK(log.replay_range({0}, {0}) == production::OperationReplayRange{{0}, {0}, 0, 0});
}

TEST_CASE("operation log reset adopts snapshot revision and retains caller storage") {
  std::array<production::OperationRecord, 1> records{};
  std::array<production::CompactOperationSample, 1> storage{};
  production::OperationLog log(records, storage);
  const std::array sample{
      production::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
  REQUIRE(log.append({.samples = sample}));
  REQUIRE(log.reset({8}));
  CHECK(log.ready());
  CHECK(log.operation_count() == 0U);
  CHECK(log.sample_count() == 0U);
  CHECK(log.current_revision() == production::DocumentRevision{8});
  CHECK_FALSE(log.operation(0));
  CHECK(log.append({.samples = sample}) == production::OperationIdentity{{9}, 0});
  REQUIRE(log.operation(0).has_value());
  CHECK(log.operation(0)->identity == production::OperationIdentity{{9}, 0});
  REQUIRE(log.clear());
  CHECK(log.current_revision() == production::DocumentRevision{0});
}
