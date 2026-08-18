#include "tinydraw/vector_v2/operation_spatial_index.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/operation_log.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

template <std::size_t Capacity>
struct IndexStorage {
  static constexpr std::size_t kWords = vector_v2::operation_spatial_word_count(Capacity);
  std::array<std::uint64_t, vector_v2::kOperationSpatialCellCount * kWords> cells{};
  std::array<std::uint64_t, kWords> large{};
  vector_v2::OperationSpatialIndex index{Capacity, cells, large};
};

vector_v2::CompactOperationSample point(int x, int y, int radius = 1) {
  return {.x_quarter = static_cast<std::uint16_t>(x * vector_v2::kSampleUnitsPerWorldUnit),
          .y_quarter = static_cast<std::uint16_t>(y * vector_v2::kSampleUnitsPerWorldUnit),
          .radius_256 = static_cast<std::uint16_t>(radius * 256)};
}

}  // namespace

TEST_CASE("spatial index merges cell bits newest-first across partial authority words") {
  IndexStorage<130> storage;
  REQUIRE(storage.index.ready());
  for (std::size_t operation = 0; operation < 130U; ++operation) {
    vector_v2::PixelRect bounds{1000, 1000, 1002, 1002};
    if (operation == 63U) {
      bounds = {120, 8, 136, 16};  // two queried cells: exercises dedupe
    } else if (operation == 64U || operation == 65U) {
      bounds = {8, 8, 16, 16};
    }
    REQUIRE(storage.index.replace(operation, bounds));
  }

  std::array<std::uint16_t, 3> candidates{};
  vector_v2::OperationSpatialQueryStats stats{};
  REQUIRE(storage.index.query({0, 0, 256, 64}, 63U, 3U, candidates, &stats) == 3U);
  CHECK(candidates == std::array<std::uint16_t, 3>{65, 64, 63});
  CHECK(stats.operations_in_authority == 3U);
  CHECK(stats.index_candidates == 4U);
  CHECK(stats.deduplicated_candidates == 3U);
}

TEST_CASE("large operations stay conservative without filling every grid cell") {
  IndexStorage<3> storage;
  REQUIRE(storage.index.replace(0U, {8, 8, 16, 16}));
  REQUIRE(storage.index.replace(1U, {700, 700, 710, 710}));
  REQUIRE(storage.index.replace(2U, {0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight}));

  std::array<std::uint16_t, 3> candidates{};
  vector_v2::OperationSpatialQueryStats stats{};
  REQUIRE(storage.index.query({0, 0, 32, 32}, 0U, 3U, candidates, &stats) == 2U);
  CHECK(candidates[0] == 2U);
  CHECK(candidates[1] == 0U);
  CHECK(stats.index_candidates == 2U);
  CHECK(stats.deduplicated_candidates == 2U);
}

TEST_CASE("sparse full-capacity authority reduces to one queried grid cell") {
  IndexStorage<4'000> storage;
  for (std::size_t operation = 0; operation < 4'000U; ++operation) {
    const std::size_t cell = operation % vector_v2::kOperationSpatialCellCount;
    const int x = static_cast<int>(cell % vector_v2::kOperationSpatialColumns) *
                      vector_v2::kOperationSpatialCellSize +
                  8;
    const int y = static_cast<int>(cell / vector_v2::kOperationSpatialColumns) *
                      vector_v2::kOperationSpatialCellSize +
                  8;
    REQUIRE(storage.index.replace(operation, {x, y, x + 2, y + 2}));
  }

  std::array<std::uint16_t, 4'000> candidates{};
  vector_v2::OperationSpatialQueryStats stats{};
  REQUIRE(storage.index.query({0, 0, 64, 64}, 0U, 4'000U, candidates, &stats) == 24U);
  CHECK(candidates[0] == 3'864U);
  CHECK(candidates[23] == 0U);
  CHECK(stats.operations_in_authority == 4'000U);
  CHECK(stats.index_candidates == 24U);
  CHECK(stats.deduplicated_candidates == 24U);
}

TEST_CASE("dense complete prefix bypasses the query before candidate output") {
  IndexStorage<128> storage;
  for (std::size_t operation = 0; operation < 128U; ++operation) {
    REQUIRE(storage.index.replace(operation, {8, 8, 96, 96}));
  }
  std::array<std::uint16_t, 128> candidates{};
  candidates.fill(0xBEEFU);
  vector_v2::OperationSpatialQueryStats stats{.operations_in_authority = 999U};
  CHECK_FALSE(storage.index.query({0, 0, 32, 32}, 0U, 128U, candidates, &stats).has_value());
  CHECK(std::all_of(candidates.begin(), candidates.end(),
                    [](std::uint16_t candidate) { return candidate == 0xBEEFU; }));
  CHECK(stats.operations_in_authority == 999U);
}

TEST_CASE("operation authority filters undo redo and replaces branched postings") {
  std::array<vector_v2::OperationRecord, 4> records{};
  std::array<vector_v2::CompactOperationSample, 8> samples{};
  IndexStorage<4> storage;
  vector_v2::OperationLog log{records, samples, &storage.index};
  const std::array local{point(16, 16)};
  const std::array distant{point(800, 800)};
  REQUIRE(log.append({.samples = local}));
  REQUIRE(log.append({.samples = distant}));

  std::array<std::uint16_t, 4> candidates{};
  REQUIRE(log.query_spatial({0, 0, 128, 128}, 0U, log.operation_count(), candidates) == 1U);
  CHECK(candidates[0] == 0U);

  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  auto redo = log.prepare_redo();
  REQUIRE(redo.has_value());
  redo->publish();
  REQUIRE(log.query_spatial({0, 0, 128, 128}, 0U, log.operation_count(), candidates) == 1U);

  undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  REQUIRE(log.append({.samples = local}));
  REQUIRE(log.query_spatial({0, 0, 128, 128}, 0U, log.operation_count(), candidates) == 2U);
  CHECK(candidates[0] == 1U);
  CHECK(candidates[1] == 0U);
}

TEST_CASE("prepared history query sees the exact Undo and Redo target prefixes") {
  std::array<vector_v2::OperationRecord, 3> records{};
  std::array<vector_v2::CompactOperationSample, 3> samples{};
  IndexStorage<3> storage;
  vector_v2::OperationLog log{records, samples, &storage.index};
  const std::array local{point(16, 16)};
  const std::array distant{point(800, 800)};
  REQUIRE(log.append({.samples = local}));
  REQUIRE(log.append({.samples = distant}));
  REQUIRE(log.append({.samples = local}));

  std::array<std::uint16_t, 3> candidates{};
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  REQUIRE(undo->query_target_spatial({0, 0, 128, 128}, candidates) == 1U);
  CHECK(candidates[0] == 0U);
  undo->publish();

  auto redo = log.prepare_redo();
  REQUIRE(redo.has_value());
  REQUIRE(redo->query_target_spatial({0, 0, 128, 128}, candidates) == 2U);
  CHECK(candidates[0] == 2U);
  CHECK(candidates[1] == 0U);
  redo->cancel();
  CHECK_FALSE(redo->query_target_spatial({0, 0, 128, 128}, candidates).has_value());
}

TEST_CASE("prepared history query leaves dense target prefixes on the authority fallback") {
  std::array<vector_v2::OperationRecord, 5> records{};
  std::array<vector_v2::CompactOperationSample, 5> samples{};
  IndexStorage<5> storage;
  vector_v2::OperationLog log{records, samples, &storage.index};
  const std::array local{point(16, 16)};
  for (std::uint16_t gesture = 1; gesture <= 5; ++gesture) {
    REQUIRE(log.append({.gesture_id = gesture, .samples = local}));
  }

  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  std::array<std::uint16_t, 5> candidates{};
  vector_v2::OperationSpatialQueryStats stats;
  CHECK_FALSE(undo->query_target_spatial({0, 0, 128, 128}, candidates, &stats).has_value());
}

TEST_CASE("two-operation branch clears every stale redo posting") {
  std::array<vector_v2::OperationRecord, 4> records{};
  std::array<vector_v2::CompactOperationSample, 8> samples{};
  IndexStorage<4> storage;
  vector_v2::OperationLog log{records, samples, &storage.index};
  const std::array local{point(16, 16)};
  const std::array retained{point(400, 400)};
  const std::array stale_first{point(800, 800)};
  const std::array stale_second{point(1200, 1200)};
  REQUIRE(log.append({.samples = local}));
  REQUIRE(log.append({.samples = retained}));
  REQUIRE(log.append({.samples = stale_first}));
  REQUIRE(log.append({.samples = stale_second}));
  for (int count = 0; count < 2; ++count) {
    auto undo = log.prepare_undo();
    REQUIRE(undo.has_value());
    undo->publish();
  }
  REQUIRE(log.append({.samples = local}));
  REQUIRE(log.append({.samples = local}));

  std::array<std::uint16_t, 4> candidates{};
  REQUIRE(log.query_spatial({760, 760, 840, 840}, 0U, log.operation_count(), candidates) == 0U);
  REQUIRE(log.query_spatial({1160, 1160, 1240, 1240}, 0U, log.operation_count(), candidates) == 0U);
  REQUIRE(log.query_spatial({0, 0, 128, 128}, 0U, log.operation_count(), candidates) == 3U);
  CHECK(candidates[0] == 3U);
  CHECK(candidates[1] == 2U);
  CHECK(candidates[2] == 0U);
}

TEST_CASE("reset cannot enable an index overlapping authority records") {
  std::array<std::uint64_t, vector_v2::kOperationSpatialCellCount + 1U> shared{};
  auto records = std::span(reinterpret_cast<vector_v2::OperationRecord*>(shared.data()), 1U);
  std::array<vector_v2::CompactOperationSample, 1> samples{};
  vector_v2::OperationSpatialIndex index{1U, std::span(shared).first(shared.size() - 1U),
                                         std::span(shared).last(1U)};
  vector_v2::OperationLog log{records, samples, &index};
  std::array<std::uint16_t, 1> candidates{};
  CHECK_FALSE(log.query_spatial({0, 0, 128, 128}, 0U, 0U, candidates).has_value());
  REQUIRE(log.reset({8}));
  CHECK_FALSE(log.query_spatial({0, 0, 128, 128}, 0U, 0U, candidates).has_value());
}

TEST_CASE("restore rebuilds retained redo postings before atomic publication") {
  std::array<vector_v2::OperationRecord, 2> source_records{};
  std::array<vector_v2::CompactOperationSample, 2> source_samples{};
  vector_v2::OperationLog source{source_records, source_samples};
  const std::array first{point(16, 16)};
  const std::array second{point(32, 32)};
  REQUIRE(source.append({.samples = first}));
  REQUIRE(source.append({.samples = second}));

  std::array<vector_v2::OperationRecord, 2> restored_records{};
  std::array<vector_v2::CompactOperationSample, 2> restored_samples{};
  IndexStorage<2> storage;
  vector_v2::OperationLog restored{restored_records, restored_samples, &storage.index};
  REQUIRE(restored.restore({.epoch = {7},
                            .generation = {10},
                            .active_operation_count = 1,
                            .records = source_records,
                            .samples = source_samples}));

  std::array<std::uint16_t, 2> candidates{};
  REQUIRE(restored.query_spatial({0, 0, 128, 128}, 0U, restored.operation_count(), candidates) ==
          1U);
  auto redo = restored.prepare_redo();
  REQUIRE(redo.has_value());
  redo->publish();
  REQUIRE(restored.query_spatial({0, 0, 128, 128}, 0U, restored.operation_count(), candidates) ==
          2U);
  CHECK(candidates == std::array<std::uint16_t, 2>{1, 0});
}
