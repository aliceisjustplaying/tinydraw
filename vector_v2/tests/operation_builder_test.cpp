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
  CHECK(append->operation().tool == vector_v2::OperationTool::kPen);
  CHECK(append->operation().color == 0x1234U);
  REQUIRE(append->operation().samples.size() == 3U);
  CHECK(append->operation().samples[0] == vector_v2::CompactOperationSample{20, 40, 768, 0});
  CHECK(append->operation().samples[1] == vector_v2::CompactOperationSample{64, 80, 640, 2});
  CHECK(append->operation().samples[2] == vector_v2::CompactOperationSample{96, 112, 512, 4});
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
  REQUIRE(append->operation().samples.size() == 2U);
  CHECK(append->operation().samples[0].elapsed_ms == 0U);
  CHECK(append->operation().samples[1].x_quarter == 176U);
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

TEST_CASE("operation builder rejects a lift point beyond capacity") {
  std::array<vector_v2::CompactOperationSample, 1> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(builder.begin(vector_v2::OperationTool::kPen, 0x001FU,
                        {.world_x = 1.0F, .world_y = 1.0F, .radius = 1.0F}));
  CHECK_FALSE(
      builder.finish({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F, .timestamp_us = 1'000U}));
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
  CHECK(append->operation().samples.back().elapsed_ms == 1U);
}

TEST_CASE("operation builder saturates elapsed metadata after 65 seconds") {
  std::array<vector_v2::CompactOperationSample, 3> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(builder.begin(vector_v2::OperationTool::kPen, 0,
                        {.world_x = 1.0F, .world_y = 1.0F, .radius = 1.0F, .timestamp_us = 0U}));
  REQUIRE(
      builder.add({.world_x = 2.0F, .world_y = 2.0F, .radius = 1.0F, .timestamp_us = 70'000'000U}));
  const auto append = builder.finish(
      {.world_x = 3.0F, .world_y = 3.0F, .radius = 1.0F, .timestamp_us = 90'000'000U});
  REQUIRE(append.has_value());
  REQUIRE(append->operation().samples.size() == 3U);
  CHECK(append->operation().samples[1].elapsed_ms == 65'535U);
  CHECK(append->operation().samples[2].elapsed_ms == 65'535U);
}

TEST_CASE("operation builder retains 4096 input points and a distinct lift") {
  constexpr std::size_t kInputPoints = 4'096U;
  std::array<vector_v2::CompactOperationSample, kInputPoints + 1U> storage{};
  vector_v2::OperationBuilder builder(storage);
  REQUIRE(builder.begin(vector_v2::OperationTool::kPen, 0x001FU,
                        {.world_x = 10.0F, .world_y = 20.0F, .radius = 1.0F, .timestamp_us = 0U},
                        42U));
  for (std::size_t index = 1U; index < kInputPoints; ++index) {
    REQUIRE(builder.add({.world_x = 10.0F + static_cast<float>(index % 1'000U) * 0.1F,
                         .world_y = 20.0F + static_cast<float>(index / 1'000U),
                         .radius = 1.0F,
                         .timestamp_us = static_cast<std::uint32_t>(index * 16'667U)}));
  }
  const auto append =
      builder.finish({.world_x = 200.0F,
                      .world_y = 30.0F,
                      .radius = 1.0F,
                      .timestamp_us = static_cast<std::uint32_t>(kInputPoints * 16'667U)});

  REQUIRE(append.has_value());
  CHECK(append->operation().gesture_id == 42U);
  CHECK(append->operation().samples.size() == storage.size());
  CHECK(append->operation().samples.back().elapsed_ms == 65'535U);
}
