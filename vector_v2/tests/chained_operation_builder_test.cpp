#include "tinydraw/vector_v2/chained_operation_builder.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

vector_v2::OperationPoint point(float x, std::uint32_t timestamp_us) {
  return {.world_x = x, .world_y = 20.0F, .radius = 2.0F, .timestamp_us = timestamp_us};
}

}  // namespace

TEST_CASE("chained builder overlaps capacity chunks and preserves exact pixels") {
  std::array<vector_v2::CompactOperationSample, 3> chunk_storage{};
  vector_v2::ChainedOperationBuilder chained(chunk_storage);
  REQUIRE(chained.begin(vector_v2::OperationTool::kPen, 0x001FU, 42U, point(10.0F, 0U)));
  CHECK(chained.add(point(20.0F, 1'000U)) == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK(chained.add(point(30.0F, 2'000U)) == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK(chained.add(point(40.0F, 3'000U)) == vector_v2::ChainedOperationStatus::kChunkReady);

  std::vector<std::vector<vector_v2::CompactOperationSample>> chunks;
  auto append = chained.pending_append();
  REQUIRE(append.has_value());
  CHECK(append->gesture_id == 42U);
  chunks.emplace_back(append->samples.begin(), append->samples.end());
  CHECK(chained.acknowledge_commit() == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK(chained.finish(point(50.0F, 4'000U)) ==
        vector_v2::ChainedOperationStatus::kFinalChunkReady);
  append = chained.pending_append();
  REQUIRE(append.has_value());
  chunks.emplace_back(append->samples.begin(), append->samples.end());
  CHECK(chunks[0].back().x_quarter == chunks[1].front().x_quarter);
  CHECK(chained.acknowledge_commit() == vector_v2::ChainedOperationStatus::kComplete);

  std::array<std::uint16_t, 64U * 64U> chained_pixels{};
  std::array<std::uint16_t, 64U * 64U> reference_pixels{};
  chained_pixels.fill(0xFFFFU);
  reference_pixels.fill(0xFFFFU);
  for (const auto& chunk : chunks) {
    REQUIRE(vector_v2::apply_incremental_operation({.tool = vector_v2::OperationTool::kPen,
                                                    .color = 0x001FU,
                                                    .gesture_id = 42U,
                                                    .samples = chunk},
                                                   {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                    .level_bounds = {0, 0, 64, 64},
                                                    .pixels = chained_pixels,
                                                    .stride = 64}));
  }

  std::array<vector_v2::CompactOperationSample, 8> reference_storage{};
  vector_v2::OperationBuilder reference(reference_storage);
  REQUIRE(reference.begin(vector_v2::OperationTool::kPen, 0x001FU, point(10.0F, 0U), 42U));
  REQUIRE(reference.add(point(20.0F, 1'000U)));
  REQUIRE(reference.add(point(30.0F, 2'000U)));
  REQUIRE(reference.add(point(40.0F, 3'000U)));
  const auto full = reference.finish(point(50.0F, 4'000U));
  REQUIRE(full.has_value());
  REQUIRE(vector_v2::apply_incremental_operation(*full, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                         .level_bounds = {0, 0, 64, 64},
                                                         .pixels = reference_pixels,
                                                         .stride = 64}));
  CHECK(chained_pixels == reference_pixels);
}

TEST_CASE("chained builder proactively bounds an interactive chunk below storage capacity") {
  std::array<vector_v2::CompactOperationSample, 8> storage{};
  vector_v2::ChainedOperationBuilder chained(storage, 3U);
  REQUIRE(chained.ready());
  REQUIRE(chained.begin(vector_v2::OperationTool::kPen, 0x001FU, 19U, point(10.0F, 0U)));
  CHECK(chained.add(point(20.0F, 1'000U)) == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK(chained.add(point(30.0F, 2'000U)) == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK(chained.add(point(40.0F, 3'000U)) == vector_v2::ChainedOperationStatus::kChunkReady);
  const auto first = chained.pending_append();
  REQUIRE(first.has_value());
  REQUIRE(first->samples.size() == 3U);
  const std::uint16_t boundary_x = first->samples.back().x_quarter;
  CHECK(boundary_x == 480U);

  CHECK(chained.acknowledge_commit() == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK(chained.sample_count() == 2U);
  CHECK(chained.finish(point(50.0F, 4'000U)) ==
        vector_v2::ChainedOperationStatus::kFinalChunkReady);
  const auto second = chained.pending_append();
  REQUIRE(second.has_value());
  CHECK(second->samples.front().x_quarter == boundary_x);
  CHECK(second->samples[1].x_quarter == 640U);
}

TEST_CASE("chained builder rejects an unusable proactive chunk limit") {
  std::array<vector_v2::CompactOperationSample, 8> storage{};
  vector_v2::ChainedOperationBuilder too_small(storage, 1U);
  vector_v2::ChainedOperationBuilder too_large(storage, storage.size() + 1U);
  CHECK_FALSE(too_small.ready());
  CHECK_FALSE(too_large.ready());
}

TEST_CASE("chained builder splits elapsed time and completes a three minute gesture") {
  std::array<vector_v2::CompactOperationSample, 16> storage{};
  vector_v2::ChainedOperationBuilder chained(storage);
  REQUIRE(chained.begin(vector_v2::OperationTool::kEraser, 0xFFFFU, 7U, point(10.0F, 0U)));

  std::size_t chunks = 0;
  for (std::uint32_t seconds = 30U; seconds <= 180U; seconds += 30U) {
    const auto next = point(10.0F + static_cast<float>(seconds / 30U), seconds * 1'000'000U);
    vector_v2::ChainedOperationStatus status =
        seconds == 180U ? chained.finish(next) : chained.add(next);
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto append = chained.pending_append();
      REQUIRE(append.has_value());
      CHECK(append->gesture_id == 7U);
      CHECK(append->samples.back().elapsed_ms <= 65'535U);
      ++chunks;
      status = chained.acknowledge_commit();
    }
    CHECK((status == vector_v2::ChainedOperationStatus::kAccepted ||
           status == vector_v2::ChainedOperationStatus::kComplete));
  }
  CHECK(chunks >= 3U);
  CHECK_FALSE(chained.active());
}

TEST_CASE("operation log retains logical gesture identity across chunks") {
  std::array<vector_v2::OperationRecord, 4> records{};
  std::array<vector_v2::CompactOperationSample, 8> samples{};
  vector_v2::OperationLog log(records, samples);
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256},
  };
  const std::array second{
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 256},
  };
  REQUIRE(log.append({.gesture_id = 91U, .samples = first}).has_value());
  REQUIRE(log.append({.gesture_id = 91U, .samples = second}).has_value());
  REQUIRE(log.operation(0).has_value());
  REQUIRE(log.operation(1).has_value());
  CHECK(log.operation(0)->gesture_id == 91U);
  CHECK(log.operation(1)->gesture_id == 91U);
}
