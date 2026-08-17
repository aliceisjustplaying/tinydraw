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
              bytes_.begin() + static_cast<std::ptrdiff_t>(offset + output.size()), output.begin());
    return true;
  }

 private:
  std::span<const std::byte> bytes_;
};

bool append_encoded(std::vector<std::byte>& journal, vector_v2::JournalChange change,
                    const vector_v2::OperationLog& log, const vector_v2::JournalState& state,
                    std::uint64_t sequence) {
  const auto size = vector_v2::authority_journal_encoded_size(change, log);
  if (!size.has_value()) {
    return false;
  }
  const std::size_t offset = journal.size();
  journal.resize(offset + *size);
  return vector_v2::encode_authority_journal(change, log, state, sequence,
                                             std::span(journal).subspan(offset, *size));
}

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
  REQUIRE(destination.restore(
      {.epoch = recovered.state.epoch,
       .generation = recovered.state.generation,
       .active_operation_count = recovered.state.active_operation_count,
       .records = std::span(recovered_records).first(recovered.state.retained_operation_count),
       .samples = std::span(recovered_samples).first(recovered.retained_sample_count)}));

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

TEST_CASE("authority journal append replaces the persisted Redo branch") {
  std::array<vector_v2::OperationRecord, 4> source_records{};
  std::array<vector_v2::CompactOperationSample, 4> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const std::array first{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 160, .radius_256 = 256, .elapsed_ms = 0},
  };
  const std::array discarded{
      vector_v2::CompactOperationSample{
          .x_quarter = 320, .y_quarter = 320, .radius_256 = 256, .elapsed_ms = 0},
  };
  const std::array replacement{
      vector_v2::CompactOperationSample{
          .x_quarter = 480, .y_quarter = 480, .radius_256 = 256, .elapsed_ms = 0},
  };
  REQUIRE(source.append({.color = 0x001FU, .gesture_id = 1, .samples = first}));
  REQUIRE(source.append({.color = 0xF800U, .gesture_id = 2, .samples = discarded}));
  auto undo = source.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();

  const vector_v2::JournalState checkpoint_state = example_state();
  const vector_v2::JournalChange checkpoint{
      .kind = vector_v2::JournalChangeKind::kCheckpoint,
  };
  const auto checkpoint_size = vector_v2::authority_journal_encoded_size(checkpoint, source);
  REQUIRE(checkpoint_size.has_value());
  std::vector<std::byte> encoded(*checkpoint_size);
  REQUIRE(vector_v2::encode_authority_journal(checkpoint, source, checkpoint_state, 30U, encoded));

  REQUIRE(source.append({.color = 0x07E0U, .gesture_id = 3, .samples = replacement}));
  vector_v2::JournalState replacement_state = checkpoint_state;
  replacement_state.tool = vector_v2::ChromeTool::kDraw;
  replacement_state.next_stroke_id = 4;
  const vector_v2::JournalChange append{
      .kind = vector_v2::JournalChangeKind::kAppendStroke,
      .first_operation = 1U,
  };
  const auto append_size = vector_v2::authority_journal_encoded_size(append, source);
  REQUIRE(append_size.has_value());
  const std::size_t append_offset = encoded.size();
  encoded.resize(encoded.size() + *append_size);
  REQUIRE(
      vector_v2::encode_authority_journal(append, source, replacement_state, 31U,
                                          std::span(encoded).subspan(append_offset, *append_size)));

  std::array<vector_v2::OperationRecord, 4> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 4> recovered_samples{};
  vector_v2::JournalState recovered_state{};
  const MemoryJournalSource journal(encoded);
  const vector_v2::JournalRecovery recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples, recovered_state);

  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.transaction_count == 2U);
  CHECK(recovered.sequence == 31U);
  CHECK(recovered.state == source.read_view());
  CHECK(recovered.retained_sample_count == 2U);
  CHECK(recovered_state == replacement_state);
  REQUIRE(recovered.state.retained_operation_count == 2U);
  CHECK(recovered_records[1].color == 0x07E0U);
  CHECK(recovered_records[1].gesture_id == 3U);
  CHECK(recovered_samples[1].x_quarter == replacement.front().x_quarter);
}

TEST_CASE("authority journal replays history state and New commits") {
  std::array<vector_v2::OperationRecord, 3> source_records{};
  std::array<vector_v2::CompactOperationSample, 3> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const std::array first{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 160, .radius_256 = 256, .elapsed_ms = 0},
  };
  const std::array second{
      vector_v2::CompactOperationSample{
          .x_quarter = 320, .y_quarter = 320, .radius_256 = 256, .elapsed_ms = 0},
  };
  REQUIRE(source.append({.gesture_id = 1, .samples = first}));
  REQUIRE(source.append({.gesture_id = 2, .samples = second}));
  vector_v2::JournalState state = example_state();
  std::vector<std::byte> encoded;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source,
                         state, 40U));

  auto undo = source.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  state.tool = vector_v2::ChromeTool::kPan;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kHistory}, source, state,
                         41U));
  state.size = vector_v2::ChromeSize::kSmall;
  state.color_index = 3;
  REQUIRE(
      append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kState}, source, state, 42U));

  std::array<vector_v2::OperationRecord, 3> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 3> recovered_samples{};
  vector_v2::JournalState recovered_state{};
  const MemoryJournalSource history_source(encoded);
  const auto history_recovery = vector_v2::recover_authority_journal(
      history_source, encoded.size(), recovered_records, recovered_samples, recovered_state);
  CHECK(history_recovery.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(history_recovery.transaction_count == 3U);
  CHECK(history_recovery.state == source.read_view());
  CHECK(history_recovery.state.active_operation_count == 1U);
  CHECK(history_recovery.state.retained_operation_count == 2U);
  CHECK(recovered_state == state);
  CHECK(recovered_records[1].gesture_id == 2U);

  REQUIRE(source.reset({source.current_revision().value + 1U}));
  state.next_stroke_id = 1U;
  REQUIRE(
      append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kReset}, source, state, 43U));
  const MemoryJournalSource reset_source(encoded);
  const auto reset_recovery = vector_v2::recover_authority_journal(
      reset_source, encoded.size(), recovered_records, recovered_samples, recovered_state);
  CHECK(reset_recovery.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(reset_recovery.transaction_count == 4U);
  CHECK(reset_recovery.state == source.read_view());
  CHECK(reset_recovery.retained_sample_count == 0U);
  CHECK(recovered_state == state);
}

TEST_CASE("authority journal accepts a blank first checkpoint") {
  std::array<vector_v2::OperationRecord, 1> source_records{};
  std::array<vector_v2::CompactOperationSample, 1> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const vector_v2::JournalState saved_state = example_state();
  std::vector<std::byte> encoded;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source,
                         saved_state, 1U));

  std::array<vector_v2::OperationRecord, 1> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 1> recovered_samples{};
  vector_v2::JournalState recovered_state{};
  const MemoryJournalSource journal(encoded);
  const auto recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples, recovered_state);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.transaction_count == 1U);
  CHECK(recovered.state == source.read_view());
  CHECK(recovered.retained_sample_count == 0U);
  CHECK(recovered_state == saved_state);
}

TEST_CASE("authority journal keeps the prior recovery point after every truncated tail byte") {
  std::array<vector_v2::OperationRecord, 2> source_records{};
  std::array<vector_v2::CompactOperationSample, 2> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const std::array sample{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 160, .radius_256 = 256, .elapsed_ms = 0},
  };
  REQUIRE(source.append({.gesture_id = 1, .samples = sample}));
  vector_v2::JournalState checkpoint_state = example_state();
  std::vector<std::byte> encoded;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source,
                         checkpoint_state, 50U));
  const std::size_t recovery_point_bytes = encoded.size();
  vector_v2::JournalState latest_state = checkpoint_state;
  latest_state.color_index = 2;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kState}, source,
                         latest_state, 51U));

  for (std::size_t cut = recovery_point_bytes + 1U; cut < encoded.size(); ++cut) {
    std::array<vector_v2::OperationRecord, 2> recovered_records{};
    std::array<vector_v2::CompactOperationSample, 2> recovered_samples{};
    vector_v2::JournalState recovered_state{};
    const MemoryJournalSource journal(std::span(encoded).first(cut));
    const auto recovered = vector_v2::recover_authority_journal(journal, cut, recovered_records,
                                                                recovered_samples, recovered_state);
    CAPTURE(cut);
    CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
    CHECK(recovered.sequence == 50U);
    CHECK(recovered.transaction_count == 1U);
    CHECK(recovered.bytes_consumed == recovery_point_bytes);
    CHECK(recovered.discarded_tail);
    CHECK(recovered_state == checkpoint_state);
  }
}

TEST_CASE("authority journal rejects every single-byte corruption in a later commit") {
  std::array<vector_v2::OperationRecord, 3> source_records{};
  std::array<vector_v2::CompactOperationSample, 3> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const std::array first{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 160, .radius_256 = 256, .elapsed_ms = 0},
  };
  const std::array second{
      vector_v2::CompactOperationSample{
          .x_quarter = 320, .y_quarter = 320, .radius_256 = 256, .elapsed_ms = 0},
  };
  REQUIRE(source.append({.color = 0x001FU, .gesture_id = 1, .samples = first}));
  const vector_v2::JournalState checkpoint_state = example_state();
  std::vector<std::byte> encoded;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source,
                         checkpoint_state, 60U));
  const std::size_t recovery_point_bytes = encoded.size();
  REQUIRE(source.append({.color = 0xF800U, .gesture_id = 2, .samples = second}));
  vector_v2::JournalState latest_state = checkpoint_state;
  latest_state.next_stroke_id = 3;
  REQUIRE(append_encoded(
      encoded, {.kind = vector_v2::JournalChangeKind::kAppendStroke, .first_operation = 1U}, source,
      latest_state, 61U));

  for (std::size_t corrupt = recovery_point_bytes; corrupt < encoded.size(); ++corrupt) {
    std::vector<std::byte> damaged = encoded;
    damaged[corrupt] ^= std::byte{0x01};
    std::array<vector_v2::OperationRecord, 3> recovered_records{};
    std::array<vector_v2::CompactOperationSample, 3> recovered_samples{};
    vector_v2::JournalState recovered_state{};
    const MemoryJournalSource journal(damaged);
    const auto recovered = vector_v2::recover_authority_journal(
        journal, damaged.size(), recovered_records, recovered_samples, recovered_state);
    CAPTURE(corrupt);
    CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
    CHECK(recovered.sequence == 60U);
    CHECK(recovered.transaction_count == 1U);
    CHECK(recovered.bytes_consumed == recovery_point_bytes);
    CHECK(recovered.discarded_tail);
    CHECK(recovered_state == checkpoint_state);
    CHECK(recovered_records[0].color == 0x001FU);
    CHECK(recovered_samples[0].x_quarter == first.front().x_quarter);
  }
}

TEST_CASE("authority journal supports sector-aligned committed transactions") {
  std::array<vector_v2::OperationRecord, 1> source_records{};
  std::array<vector_v2::CompactOperationSample, 1> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  const std::array sample{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 160, .radius_256 = 256, .elapsed_ms = 0},
  };
  REQUIRE(source.append({.gesture_id = 1, .samples = sample}));
  const vector_v2::JournalChange checkpoint{
      .kind = vector_v2::JournalChangeKind::kCheckpoint,
  };
  const auto minimum = vector_v2::authority_journal_encoded_size(checkpoint, source);
  REQUIRE(minimum.has_value());
  REQUIRE(*minimum < 4096U);
  std::vector<std::byte> encoded(4096U);
  REQUIRE(vector_v2::encode_authority_journal(checkpoint, source, example_state(), 70U, encoded));

  std::array<vector_v2::OperationRecord, 1> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 1> recovered_samples{};
  vector_v2::JournalState recovered_state{};
  const MemoryJournalSource journal(encoded);
  const auto recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples, recovered_state);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.sequence == 70U);
  CHECK(recovered.bytes_consumed == 4096U);
  CHECK(recovered.state == source.read_view());
}

TEST_CASE("authority journal permits replacing an uncommitted queued state transaction") {
  std::array<vector_v2::OperationRecord, 1> source_records{};
  std::array<vector_v2::CompactOperationSample, 1> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  std::vector<std::byte> encoded(2U * 4096U);

  REQUIRE(vector_v2::encode_authority_journal({.kind = vector_v2::JournalChangeKind::kCheckpoint},
                                              source, example_state(), 1U,
                                              std::span(encoded).first(4096U)));
  vector_v2::JournalState superseded = example_state();
  superseded.tool = vector_v2::ChromeTool::kPan;
  REQUIRE(vector_v2::encode_authority_journal({.kind = vector_v2::JournalChangeKind::kState},
                                              source, superseded, 2U,
                                              std::span(encoded).subspan(4096U)));

  vector_v2::JournalState latest = superseded;
  latest.tool = vector_v2::ChromeTool::kDraw;
  latest.color_index = 4U;
  REQUIRE(vector_v2::encode_authority_journal({.kind = vector_v2::JournalChangeKind::kState},
                                              source, latest, 2U,
                                              std::span(encoded).subspan(4096U)));

  std::array<vector_v2::OperationRecord, 1> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 1> recovered_samples{};
  vector_v2::JournalState recovered_state{};
  const MemoryJournalSource journal(encoded);
  const auto recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples, recovered_state);

  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.transaction_count == 2U);
  CHECK(recovered.sequence == 2U);
  CHECK(recovered.bytes_consumed == encoded.size());
  CHECK(recovered_state == latest);
}
