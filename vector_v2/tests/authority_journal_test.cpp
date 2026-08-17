#include "tinydraw/vector_v2/authority_journal.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/operation_log.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

class MemoryJournalSource final : public vector_v2::AuthorityJournalSource {
 public:
  explicit MemoryJournalSource(std::span<const std::byte> bytes) : bytes_(bytes) {}

  bool read(std::size_t offset, std::span<std::byte> output) const override {
    if (offset > bytes_.size() || output.size() > bytes_.size() - offset) {
      return false;
    }
    std::copy(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
              bytes_.begin() + static_cast<std::ptrdiff_t>(offset + output.size()),
              output.begin());
    return true;
  }

 private:
  std::span<const std::byte> bytes_;
};

vector_v2::JournalState example_state() {
  vector_v2::NavigationState navigation;
  static_cast<void>(navigation.set_zoom(vector_v2::ZoomLevel::k100Percent, {184, 186}));
  static_cast<void>(navigation.set_origin(321, 654, {184, 186}));
  static_cast<void>(navigation.set_zoom(vector_v2::ZoomLevel::k400Percent, {120, 200}));
  static_cast<void>(navigation.set_origin(2048, 4096, {120, 200}));
  return {
      .navigation = navigation.snapshot(),
      .tool = vector_v2::ChromeTool::kErase,
      .size = vector_v2::ChromeSize::kExtraLarge,
      .palette_page = 1,
      .color_index = 9,
      .next_stroke_id = 42,
  };
}

}  // namespace

TEST_CASE("authority journal checkpoint restores drawing history and session state") {
  std::array<vector_v2::OperationRecord, 4> source_records{};
  std::array<vector_v2::CompactOperationSample, 8> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const std::array first{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 320, .radius_256 = 512, .elapsed_ms = 0},
      vector_v2::CompactOperationSample{
          .x_quarter = 320, .y_quarter = 480, .radius_256 = 768, .elapsed_ms = 10},
  };
  const std::array second_a{
      vector_v2::CompactOperationSample{
          .x_quarter = 640, .y_quarter = 800, .radius_256 = 256, .elapsed_ms = 0},
  };
  const std::array second_b{
      vector_v2::CompactOperationSample{
          .x_quarter = 960, .y_quarter = 1120, .radius_256 = 384, .elapsed_ms = 20},
  };
  REQUIRE(source.append({.tool = vector_v2::OperationTool::kPen,
                         .color = 0x001FU,
                         .gesture_id = 7,
                         .samples = first}));
  REQUIRE(source.append({.tool = vector_v2::OperationTool::kEraser,
                         .color = 0xFFFFU,
                         .gesture_id = 8,
                         .samples = second_a}));
  REQUIRE(source.append({.tool = vector_v2::OperationTool::kEraser,
                         .color = 0xFFFFU,
                         .gesture_id = 8,
                         .samples = second_b}));
  auto undo = source.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  REQUIRE(source.read_view().active_operation_count == 1U);
  REQUIRE(source.read_view().retained_operation_count == 3U);

  const vector_v2::JournalState saved_state = example_state();
  const vector_v2::JournalChange change{.kind = vector_v2::JournalChangeKind::kCheckpoint};
  const auto encoded_size = vector_v2::authority_journal_encoded_size(change, source);
  REQUIRE(encoded_size.has_value());
  std::vector<std::byte> encoded(*encoded_size);
  REQUIRE(vector_v2::encode_authority_journal(change, source, saved_state, 17U, encoded));

  std::array<vector_v2::OperationRecord, 4> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 8> recovered_samples{};
  vector_v2::JournalState recovered_state{};
  const MemoryJournalSource journal(encoded);
  const vector_v2::JournalRecovery recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples, recovered_state);

  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.sequence == 17U);
  CHECK(recovered.transaction_count == 1U);
  CHECK(recovered.bytes_consumed == encoded.size());
  CHECK_FALSE(recovered.discarded_tail);
  CHECK(recovered.state == source.read_view());
  CHECK(recovered_state == saved_state);

  std::array<vector_v2::OperationRecord, 4> destination_records{};
  std::array<vector_v2::CompactOperationSample, 8> destination_samples{};
  vector_v2::OperationLog destination(destination_records, destination_samples);
  REQUIRE(destination.restore({.epoch = recovered.state.epoch,
                               .generation = recovered.state.generation,
                               .active_operation_count =
                                   recovered.state.active_operation_count,
                               .records = std::span(recovered_records)
                                              .first(recovered.state.retained_operation_count),
                               .samples = std::span(recovered_samples)
                                              .first(recovered.retained_sample_count)}));

  CHECK(destination.read_view() == source.read_view());
  for (std::size_t index = 0; index < source.read_view().retained_operation_count; ++index) {
    const auto expected = source.retained_operation(source.read_view(), index);
    const auto actual = destination.retained_operation(destination.read_view(), index);
    REQUIRE(expected.has_value());
    REQUIRE(actual.has_value());
    CHECK(actual->tool == expected->tool);
    CHECK(actual->color == expected->color);
    CHECK(actual->gesture_id == expected->gesture_id);
    CHECK(actual->world_bounds == expected->world_bounds);
    CHECK(std::equal(actual->samples.begin(), actual->samples.end(), expected->samples.begin(),
                     expected->samples.end()));
  }
}
