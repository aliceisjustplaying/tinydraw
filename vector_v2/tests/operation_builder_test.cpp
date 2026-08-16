#include "tinydraw/vector_v2/operation_builder.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <limits>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("operation builder quantizes one bounded stroke") {
  std::array<vector_v2::CompactOperationSample, 3> storage{};
  vector_v2::OperationBuilder builder(storage);
  CHECK(builder.ready());
  REQUIRE(
      builder.begin(vector_v2::OperationTool::kPen, 0x1234U,
                    {.world_x = 1.25F, .world_y = 2.5F, .radius = 3.0F, .timestamp_us = 10'000U}));
  REQUIRE(builder.add({.world_x = 4.0F, .world_y = 5.0F, .radius = 2.5F, .timestamp_us = 12'999U}));
  const auto append =
      builder.finish({.world_x = 6.0F, .world_y = 7.0F, .radius = 2.0F, .timestamp_us = 14'001U});
  REQUIRE(append.has_value());
  CHECK_FALSE(builder.active());
  CHECK(append->tool == vector_v2::OperationTool::kPen);
  CHECK(append->color == 0x1234U);
  REQUIRE(append->samples.size() == 3U);
  CHECK(append->samples[0] == vector_v2::CompactOperationSample{20, 40, 768, 0});
  CHECK(append->samples[1] == vector_v2::CompactOperationSample{64, 80, 640, 2});
  CHECK(append->samples[2] == vector_v2::CompactOperationSample{96, 112, 512, 4});
}

TEST_CASE("operation builder coalesces quantized duplicate positions") {
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(
      builder.begin(vector_v2::OperationTool::kEraser, 0,
                    {.world_x = 10.0F, .world_y = 20.0F, .radius = 2.0F, .timestamp_us = 1'000U}));
  REQUIRE(
      builder.add({.world_x = 10.01F, .world_y = 20.01F, .radius = 2.0F, .timestamp_us = 3'000U}));
  const auto append =
      builder.finish({.world_x = 11.0F, .world_y = 20.0F, .radius = 2.0F, .timestamp_us = 5'000U});
  REQUIRE(append.has_value());
  REQUIRE(append->samples.size() == 2U);
  CHECK(append->samples[0].elapsed_ms == 0U);
  CHECK(append->samples[1].x_quarter == 176U);
}

TEST_CASE("operation builder rejects malformed points and invalid time") {
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationBuilder builder(storage);
  CHECK_FALSE(builder.begin(vector_v2::OperationTool::kPen, 0,
                            {.world_x = -1.0F, .world_y = 0.0F, .radius = 1.0F}));
  CHECK_FALSE(builder.begin(
      vector_v2::OperationTool::kPen, 0,
      {.world_x = 0.0F, .world_y = 0.0F, .radius = std::numeric_limits<float>::infinity()}));
  REQUIRE(builder.begin(vector_v2::OperationTool::kPen, 0,
                        {.world_x = 1.0F, .world_y = 1.0F, .radius = 1.0F, .timestamp_us = 100U}));
  CHECK_FALSE(builder.add({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F, .timestamp_us = 99U}));
  CHECK_FALSE(builder.active());
  CHECK_FALSE(builder.finish({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F}));
}

TEST_CASE("operation builder publishes an exactly full stroke without its lift point") {
  std::array<vector_v2::CompactOperationSample, 1> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(builder.begin(vector_v2::OperationTool::kPen, 0x001FU,
                        {.world_x = 1.0F, .world_y = 1.0F, .radius = 1.0F}));
  const auto append =
      builder.finish({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F, .timestamp_us = 1'000U});
  REQUIRE(append.has_value());
  CHECK(append->samples.size() == 1U);
  CHECK(append->samples.front().x_quarter == 16U);
  CHECK(builder.overflowed());
  CHECK_FALSE(builder.active());
}

TEST_CASE("operation builder reports capacity exhaustion until cancellation") {
  std::array<vector_v2::CompactOperationSample, 1> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(builder.begin(vector_v2::OperationTool::kPen, 0,
                        {.world_x = 1.0F, .world_y = 1.0F, .radius = 1.0F}));
  CHECK_FALSE(
      builder.add({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F, .timestamp_us = 1'000U}));
  CHECK(builder.overflowed());
  CHECK_FALSE(builder.active());
  CHECK_FALSE(builder.finish({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F}));
  builder.cancel();
  CHECK_FALSE(builder.overflowed());
  CHECK(builder.sample_count() == 0U);
}

TEST_CASE("operation builder handles uint32 timestamp wrap") {
  std::array<vector_v2::CompactOperationSample, 2> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(builder.begin(
      vector_v2::OperationTool::kPen, 0,
      {.world_x = 1.0F, .world_y = 1.0F, .radius = 1.0F, .timestamp_us = 0xFFFF'FF00U}));
  const auto append = builder.finish(
      {.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F, .timestamp_us = 0x0000'02E8U});
  REQUIRE(append.has_value());
  CHECK(append->samples.back().elapsed_ms == 1U);
}
