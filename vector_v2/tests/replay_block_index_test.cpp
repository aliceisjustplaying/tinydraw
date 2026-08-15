#include "tinydraw/vector_v2/replay_block_index.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "tinydraw/vector_v2/render_accounting.h"

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("replay block index is conservative and masks queries to an active prefix") {
  std::array<vector_v2::OperationRecord, 40> records{};
  std::array<vector_v2::CompactOperationSample, 40> sample_storage{};
  vector_v2::OperationLog log{records, sample_storage};
  for (std::size_t index = 0; index < 40U; ++index) {
    const int x = index < 16U ? 20 : (index < 32U ? 700 : 1'300);
    const std::array sample{vector_v2::CompactOperationSample{
        .x_quarter = static_cast<std::uint16_t>(x * 4),
        .y_quarter = static_cast<std::uint16_t>(40 * 4),
        .radius_256 = 256,
    }};
    REQUIRE(log.append({.samples = sample}));
  }

  std::array<std::uint32_t, vector_v2::kReplayIndexWords> words{};
  vector_v2::ReplayBlockIndex index{words};
  REQUIRE(index.sync(log));

  auto left = index.query({0, 0, 128, 128}, 0, log.operation_count());
  std::vector<std::size_t> left_candidates;
  while (const auto candidate = vector_v2::ReplayBlockIndex::previous(left)) {
    left_candidates.push_back(*candidate);
  }
  REQUIRE(left_candidates.size() == 16U);
  CHECK(left_candidates.front() == 15U);
  CHECK(left_candidates.back() == 0U);

  // The spatial block containing operation 16 is selected, but the active
  // prefix ends before it. Future undo can therefore move the prefix without
  // rebuilding stored index positions.
  auto prefix = index.query({640, 0, 896, 128}, 0, 16);
  CHECK_FALSE(vector_v2::ReplayBlockIndex::previous(prefix).has_value());
  auto full = index.query({640, 0, 896, 128}, 0, 32);
  REQUIRE(vector_v2::ReplayBlockIndex::previous(full) == 31U);

  REQUIRE(log.reset({50}));
  const std::array reset_sample{vector_v2::CompactOperationSample{
      .x_quarter = 1'000U * 4U, .y_quarter = 1'000U * 4U, .radius_256 = 256}};
  REQUIRE(log.append({.samples = reset_sample}));
  REQUIRE(index.sync(log));
  auto old_cell = index.query({0, 0, 128, 128}, 0, 1);
  CHECK_FALSE(vector_v2::ReplayBlockIndex::previous(old_cell).has_value());
}

TEST_CASE("render accounting keeps durable group keys and reports amplification") {
  std::array<vector_v2::RenderAccountingEntry, 4> storage{};
  vector_v2::RenderAccounting accounting{storage};
  const vector_v2::RenderGroupKey first{.revision = {7},
                                        .zoom = vector_v2::ZoomLevel::k400Percent,
                                        .group_column = 2,
                                        .group_row = 4};
  const vector_v2::RenderGroupKey second{.revision = {7},
                                         .zoom = vector_v2::ZoomLevel::k400Percent,
                                         .group_column = 4,
                                         .group_row = 4};
  accounting.record_attempt(first);
  accounting.record_completion(first);
  accounting.record_reuse(first);
  accounting.record_attempt(second);
  accounting.record_discard(second);
  accounting.record_attempt(second);
  accounting.record_completion(second);

  const auto totals = accounting.totals();
  CHECK(totals.unique_groups == 2U);
  CHECK(totals.attempts == 3U);
  CHECK(totals.completions == 2U);
  CHECK(totals.reuses == 1U);
  CHECK(totals.discards == 1U);
  CHECK(totals.amplification() == doctest::Approx(1.5));
}
