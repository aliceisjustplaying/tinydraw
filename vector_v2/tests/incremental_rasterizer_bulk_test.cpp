#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("fresh constant capsule covering the surface completes in one bounded sweep") {
  constexpr int kSize = 128;
  constexpr std::uint16_t kRadius = 40U * 256U;
  const std::array samples{
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = kRadius},
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = kRadius},
  };
  std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
  const auto bytes = std::as_writable_bytes(std::span(storage));
  const auto batch = vector_v2::prepare_operation_chord_batch(
      samples, samples.size() - 1U, vector_v2::ZoomLevel::k400Percent, {0, 0, kSize, kSize}, bytes);
  REQUIRE(batch.has_value());
  REQUIRE(batch->chord_count == 1U);

  std::array<std::uint16_t, kSize * kSize> pixels{};
  pixels.fill(0xFFFFU);
  std::array<std::uint8_t, (kSize * kSize) / 8U> mask{};
  std::array<std::uint16_t, kSize> unset_rows{};
  std::array<std::uint32_t, kSize / 32U> saturated_rows{};
  vector_v2::MaskedRowSummary summary{unset_rows, saturated_rows};
  summary.reset(kSize, kSize);
  const vector_v2::RasterSurface surface{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_bounds = {0, 0, kSize, kSize},
      .pixels = pixels,
      .stride = kSize,
  };
  vector_v2::OperationSweepSlice slice{};
  REQUIRE(vector_v2::apply_masked_operation_chord_rows({
      .tool = vector_v2::OperationTool::kPen,
      .color = 0x001FU,
      .chord_storage = bytes,
      .batch = *batch,
      .first_row = batch->clipped_bounds.y0,
      .max_work_px = 16'000U,
      .surface = surface,
      .finalized_pixels = mask,
      .summary = &summary,
      .slice = slice,
  }));

  CHECK(slice.next_row == kSize);
  CHECK(slice.work_px < 16'000U);
  CHECK(summary.all_saturated());
  CHECK(std::all_of(pixels.begin(), pixels.end(),
                    [](std::uint16_t pixel) { return pixel == 0x001FU; }));
  CHECK(std::all_of(mask.begin(), mask.end(), [](std::uint8_t byte) { return byte == 0xFFU; }));
}

TEST_CASE("constant capsule bulk coverage never fills an uncovered corner") {
  constexpr int kSize = 128;
  constexpr std::uint16_t kRadius = 22U * 256U;
  const std::array samples{
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = kRadius},
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = kRadius},
  };
  std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
  const auto bytes = std::as_writable_bytes(std::span(storage));
  const auto batch = vector_v2::prepare_operation_chord_batch(
      samples, samples.size() - 1U, vector_v2::ZoomLevel::k400Percent, {0, 0, kSize, kSize}, bytes);
  REQUIRE(batch.has_value());

  std::array<std::uint16_t, kSize * kSize> reference{};
  std::array<std::uint16_t, kSize * kSize> actual{};
  reference.fill(0xFFFFU);
  actual.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = samples},
      {.zoom = vector_v2::ZoomLevel::k400Percent,
       .level_bounds = {0, 0, kSize, kSize},
       .pixels = reference,
       .stride = kSize}));
  REQUIRE(reference.front() == 0xFFFFU);

  std::array<std::uint8_t, (kSize * kSize) / 8U> mask{};
  std::array<std::uint16_t, kSize> unset_rows{};
  std::array<std::uint32_t, kSize / 32U> saturated_rows{};
  vector_v2::MaskedRowSummary summary{unset_rows, saturated_rows};
  summary.reset(kSize, kSize);
  const vector_v2::RasterSurface surface{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_bounds = {0, 0, kSize, kSize},
      .pixels = actual,
      .stride = kSize,
  };
  int row = batch->clipped_bounds.y0;
  while (row < batch->clipped_bounds.y1) {
    vector_v2::OperationSweepSlice slice{};
    REQUIRE(vector_v2::apply_masked_operation_chord_rows({
        .tool = vector_v2::OperationTool::kPen,
        .color = 0x001FU,
        .chord_storage = bytes,
        .batch = *batch,
        .first_row = row,
        .max_work_px = 16'000U,
        .surface = surface,
        .finalized_pixels = mask,
        .summary = &summary,
        .slice = slice,
    }));
    REQUIRE(slice.next_row > row);
    row = slice.next_row;
  }
  CHECK(actual == reference);
  CHECK(actual.front() == 0xFFFFU);
}

TEST_CASE("constant capsule bulk coverage preserves a newer finalized pixel") {
  constexpr int kSize = 128;
  constexpr std::uint16_t kRadius = 40U * 256U;
  const std::array samples{
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = kRadius},
      vector_v2::CompactOperationSample{
          .x_quarter = 16U * 16U, .y_quarter = 16U * 16U, .radius_256 = kRadius},
  };
  std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
  const auto bytes = std::as_writable_bytes(std::span(storage));
  const auto batch = vector_v2::prepare_operation_chord_batch(
      samples, samples.size() - 1U, vector_v2::ZoomLevel::k400Percent, {0, 0, kSize, kSize}, bytes);
  REQUIRE(batch.has_value());

  std::array<std::uint16_t, kSize * kSize> pixels{};
  pixels.fill(0xFFFFU);
  pixels.front() = 0xF800U;
  std::array<std::uint8_t, (kSize * kSize) / 8U> mask{};
  mask.front() = 0x01U;
  std::array<std::uint16_t, kSize> unset_rows{};
  std::array<std::uint32_t, kSize / 32U> saturated_rows{};
  vector_v2::MaskedRowSummary summary{unset_rows, saturated_rows};
  summary.reset(kSize, kSize);
  summary.note_finalized(0, 1);
  const vector_v2::RasterSurface surface{
      .zoom = vector_v2::ZoomLevel::k400Percent,
      .level_bounds = {0, 0, kSize, kSize},
      .pixels = pixels,
      .stride = kSize,
  };
  int row = batch->clipped_bounds.y0;
  while (row < batch->clipped_bounds.y1) {
    vector_v2::OperationSweepSlice slice{};
    REQUIRE(vector_v2::apply_masked_operation_chord_rows({
        .tool = vector_v2::OperationTool::kPen,
        .color = 0x001FU,
        .chord_storage = bytes,
        .batch = *batch,
        .first_row = row,
        .max_work_px = 16'000U,
        .surface = surface,
        .finalized_pixels = mask,
        .summary = &summary,
        .slice = slice,
    }));
    REQUIRE(slice.next_row > row);
    row = slice.next_row;
  }
  CHECK(pixels.front() == 0xF800U);
  CHECK(std::all_of(pixels.begin() + 1, pixels.end(),
                    [](std::uint16_t pixel) { return pixel == 0x001FU; }));
  CHECK(summary.all_saturated());
}

TEST_CASE("random constant capsule bulk sweeps match the forward coverage oracle") {
  constexpr int kSize = 64;
  std::uint32_t random = 0xC011A9E5U;
  const auto next = [&random]() {
    random = random * 1'664'525U + 1'013'904'223U;
    return random;
  };
  for (int iteration = 0; iteration < 256; ++iteration) {
    const std::uint16_t radius = static_cast<std::uint16_t>(1U + next() % (24U * 256U));
    std::array samples{
        vector_v2::CompactOperationSample{
            .x_quarter = static_cast<std::uint16_t>(next() % (16U * 16U)),
            .y_quarter = static_cast<std::uint16_t>(next() % (16U * 16U)),
            .radius_256 = radius},
        vector_v2::CompactOperationSample{
            .x_quarter = static_cast<std::uint16_t>(next() % (16U * 16U)),
            .y_quarter = static_cast<std::uint16_t>(next() % (16U * 16U)),
            .radius_256 = radius},
    };
    if (iteration % 3 == 0) {
      samples[1].x_quarter = samples[0].x_quarter;
      samples[1].y_quarter = samples[0].y_quarter;
    }
    const auto tool =
        iteration % 2 == 0 ? vector_v2::OperationTool::kPen : vector_v2::OperationTool::kEraser;
    const std::uint16_t initial = tool == vector_v2::OperationTool::kPen ? 0xFFFFU : 0x07E0U;
    const std::uint16_t applied = tool == vector_v2::OperationTool::kPen ? 0x001FU : 0xFFFFU;

    std::array<std::uint16_t, kSize * kSize> reference{};
    std::array<std::uint16_t, kSize * kSize> actual{};
    reference.fill(initial);
    actual.fill(initial);
    REQUIRE(
        vector_v2::apply_incremental_operation({.tool = tool, .color = 0x001FU, .samples = samples},
                                               {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                .level_bounds = {0, 0, kSize, kSize},
                                                .pixels = reference,
                                                .stride = kSize}));

    std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
    const auto bytes = std::as_writable_bytes(std::span(storage));
    const auto batch = vector_v2::prepare_operation_chord_batch(samples, samples.size() - 1U,
                                                                vector_v2::ZoomLevel::k400Percent,
                                                                {0, 0, kSize, kSize}, bytes);
    REQUIRE(batch.has_value());
    REQUIRE(batch->chord_count == 1U);
    std::array<std::uint8_t, (kSize * kSize) / 8U> mask{};
    std::array<std::uint16_t, kSize> unset_rows{};
    std::array<std::uint32_t, kSize / 32U> saturated_rows{};
    vector_v2::MaskedRowSummary summary{unset_rows, saturated_rows};
    summary.reset(kSize, kSize);
    const vector_v2::RasterSurface surface{
        .zoom = vector_v2::ZoomLevel::k400Percent,
        .level_bounds = {0, 0, kSize, kSize},
        .pixels = actual,
        .stride = kSize,
    };
    int row = batch->clipped_bounds.y0;
    while (row < batch->clipped_bounds.y1) {
      vector_v2::OperationSweepSlice slice{};
      REQUIRE(vector_v2::apply_masked_operation_chord_rows({
          .tool = tool,
          .color = 0x001FU,
          .chord_storage = bytes,
          .batch = *batch,
          .first_row = row,
          .max_work_px = 16'000U,
          .surface = surface,
          .finalized_pixels = mask,
          .summary = &summary,
          .slice = slice,
      }));
      REQUIRE(slice.next_row > row);
      row = slice.next_row;
    }
    CHECK(actual == reference);
    bool mask_matches = true;
    bool summary_matches = true;
    for (int y = 0; y < kSize; ++y) {
      bool row_saturated = true;
      for (int x = 0; x < kSize; ++x) {
        const std::size_t pixel = static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x);
        const bool finalized = (mask[pixel >> 3U] & (1U << (pixel & 7U))) != 0U;
        mask_matches = mask_matches && finalized == (reference[pixel] == applied);
        row_saturated = row_saturated && finalized;
      }
      summary_matches = summary_matches && summary.row_saturated(y) == row_saturated;
    }
    CHECK(mask_matches);
    CHECK(summary_matches);
  }
}
