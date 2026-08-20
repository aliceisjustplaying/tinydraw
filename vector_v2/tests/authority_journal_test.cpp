#include "tinydraw/vector_v2/authority_journal.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/memory_layout.h"
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
                    const vector_v2::OperationLog& log, std::uint64_t sequence) {
  const auto plan = vector_v2::prepare_authority_journal(change, log);
  if (!plan.has_value()) {
    return false;
  }
  const std::size_t offset = journal.size();
  journal.resize(offset + plan->encoded_bytes);
  return vector_v2::encode_authority_journal(
      *plan, log, sequence, std::span(journal).subspan(offset, plan->encoded_bytes));
}

}  // namespace

TEST_CASE("authority journal checkpoint restores drawing history") {
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
      second_a.front(),
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

  const vector_v2::JournalChange change{.kind = vector_v2::JournalChangeKind::kCheckpoint};
  const auto plan = vector_v2::prepare_authority_journal(change, source);
  REQUIRE(plan.has_value());
  std::vector<std::byte> encoded(plan->encoded_bytes);
  REQUIRE(vector_v2::encode_authority_journal(*plan, source, 17U, encoded));

  std::array<vector_v2::OperationRecord, 4> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 8> recovered_samples{};
  const MemoryJournalSource journal(encoded);
  const vector_v2::JournalRecovery recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples);

  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.sequence == 17U);
  CHECK(recovered.transaction_count == 1U);
  CHECK(recovered.bytes_consumed == encoded.size());
  CHECK_FALSE(recovered.discarded_tail);
  CHECK(recovered.state == source.read_view());

  std::array<vector_v2::OperationRecord, 4> destination_records{};
  std::array<vector_v2::CompactOperationSample, 8> destination_samples{};
  vector_v2::OperationLog destination(destination_records, destination_samples);
  REQUIRE(destination.restore(
      {.epoch = recovered.state.epoch,
       .generation = recovered.state.generation,
       .active_operation_count = recovered.state.active_operation_count,
       .records = std::span(recovered_records).first(recovered.state.retained_operation_count),
       .samples = std::span(recovered_samples).first(recovered.state.retained_sample_count)}));

  CHECK(destination.read_view() == source.read_view());
  for (std::size_t index = 0; index < source.read_view().retained_operation_count; ++index) {
    const auto expected = source.retained_operation(index);
    const auto actual = destination.retained_operation(index);
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

  const vector_v2::JournalChange checkpoint{
      .kind = vector_v2::JournalChangeKind::kCheckpoint,
  };
  const auto checkpoint_plan = vector_v2::prepare_authority_journal(checkpoint, source);
  REQUIRE(checkpoint_plan.has_value());
  std::vector<std::byte> encoded(checkpoint_plan->encoded_bytes);
  REQUIRE(vector_v2::encode_authority_journal(*checkpoint_plan, source, 30U, encoded));

  REQUIRE(source.append({.color = 0x07E0U, .gesture_id = 3, .samples = replacement}));
  const vector_v2::JournalChange append{
      .kind = vector_v2::JournalChangeKind::kAppendStroke,
      .first_operation = 1U,
  };
  const auto append_plan = vector_v2::prepare_authority_journal(append, source);
  REQUIRE(append_plan.has_value());
  const std::size_t append_offset = encoded.size();
  encoded.resize(encoded.size() + append_plan->encoded_bytes);
  REQUIRE(vector_v2::encode_authority_journal(
      *append_plan, source, 31U,
      std::span(encoded).subspan(append_offset, append_plan->encoded_bytes)));

  std::array<vector_v2::OperationRecord, 4> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 4> recovered_samples{};
  const MemoryJournalSource journal(encoded);
  const vector_v2::JournalRecovery recovered = vector_v2::recover_authority_journal(
      journal, encoded.size(), recovered_records, recovered_samples);

  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.transaction_count == 2U);
  CHECK(recovered.sequence == 31U);
  CHECK(recovered.state == source.read_view());
  CHECK(recovered.state.retained_sample_count == 2U);
  REQUIRE(recovered.state.retained_operation_count == 2U);
  CHECK(recovered_records[1].color == 0x07E0U);
  CHECK(recovered_records[1].gesture_id == 3U);
  CHECK(recovered_samples[1].x_quarter == replacement.front().x_quarter);
}

TEST_CASE("authority journal replays history updates and empty checkpoints") {
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
  std::vector<std::byte> encoded;
  REQUIRE(
      append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source, 40U));

  auto undo = source.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kUpdate}, source, 41U));

  std::array<vector_v2::OperationRecord, 3> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 3> recovered_samples{};
  const MemoryJournalSource history_source(encoded);
  const auto history_recovery = vector_v2::recover_authority_journal(
      history_source, encoded.size(), recovered_records, recovered_samples);
  CHECK(history_recovery.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(history_recovery.transaction_count == 2U);
  CHECK(history_recovery.state == source.read_view());
  CHECK(history_recovery.state.active_operation_count == 1U);
  CHECK(history_recovery.state.retained_operation_count == 2U);
  CHECK(recovered_records[1].gesture_id == 2U);

  REQUIRE(source.reset({source.current_revision().value + 1U}));
  REQUIRE(
      append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source, 42U));
  const MemoryJournalSource reset_source(encoded);
  const auto reset_recovery = vector_v2::recover_authority_journal(
      reset_source, encoded.size(), recovered_records, recovered_samples);
  CHECK(reset_recovery.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(reset_recovery.transaction_count == 3U);
  CHECK(reset_recovery.state == source.read_view());
  CHECK(reset_recovery.state.retained_sample_count == 0U);
}

TEST_CASE("authority journal accepts a blank first checkpoint") {
  std::array<vector_v2::OperationRecord, 1> source_records{};
  std::array<vector_v2::CompactOperationSample, 1> source_samples{};
  vector_v2::OperationLog source(source_records, source_samples);
  std::vector<std::byte> encoded;
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source, 1U));

  std::array<vector_v2::OperationRecord, 1> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 1> recovered_samples{};
  const MemoryJournalSource journal(encoded);
  const auto recovered = vector_v2::recover_authority_journal(journal, encoded.size(),
                                                              recovered_records, recovered_samples);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.transaction_count == 1U);
  CHECK(recovered.state == source.read_view());
  CHECK(recovered.state.retained_sample_count == 0U);
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
  std::vector<std::byte> encoded;
  REQUIRE(
      append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source, 50U));
  const std::size_t recovery_point_bytes = encoded.size();
  REQUIRE(append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kUpdate}, source, 51U));

  for (std::size_t cut = recovery_point_bytes + 1U; cut < encoded.size(); ++cut) {
    std::array<vector_v2::OperationRecord, 2> recovered_records{};
    std::array<vector_v2::CompactOperationSample, 2> recovered_samples{};
    const MemoryJournalSource journal(std::span(encoded).first(cut));
    const auto recovered =
        vector_v2::recover_authority_journal(journal, cut, recovered_records, recovered_samples);
    CAPTURE(cut);
    CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
    CHECK(recovered.sequence == 50U);
    CHECK(recovered.transaction_count == 1U);
    CHECK(recovered.bytes_consumed == recovery_point_bytes);
    CHECK(recovered.discarded_tail);
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
  std::vector<std::byte> encoded;
  REQUIRE(
      append_encoded(encoded, {.kind = vector_v2::JournalChangeKind::kCheckpoint}, source, 60U));
  const std::size_t recovery_point_bytes = encoded.size();
  REQUIRE(source.append({.color = 0xF800U, .gesture_id = 2, .samples = second}));
  REQUIRE(append_encoded(
      encoded, {.kind = vector_v2::JournalChangeKind::kAppendStroke, .first_operation = 1U}, source,
      61U));

  for (std::size_t corrupt = recovery_point_bytes; corrupt < encoded.size(); ++corrupt) {
    std::vector<std::byte> damaged = encoded;
    damaged[corrupt] ^= std::byte{0x01};
    std::array<vector_v2::OperationRecord, 3> recovered_records{};
    std::array<vector_v2::CompactOperationSample, 3> recovered_samples{};
    const MemoryJournalSource journal(damaged);
    const auto recovered = vector_v2::recover_authority_journal(
        journal, damaged.size(), recovered_records, recovered_samples);
    CAPTURE(corrupt);
    CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
    CHECK(recovered.sequence == 60U);
    CHECK(recovered.transaction_count == 1U);
    CHECK(recovered.bytes_consumed == recovery_point_bytes);
    CHECK(recovered.discarded_tail);
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
  const auto plan = vector_v2::prepare_authority_journal(checkpoint, source);
  REQUIRE(plan.has_value());
  REQUIRE(plan->encoded_bytes < 4096U);
  std::vector<std::byte> encoded(4096U);
  REQUIRE(vector_v2::encode_authority_journal(*plan, source, 70U, encoded));

  std::array<vector_v2::OperationRecord, 1> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 1> recovered_samples{};
  const MemoryJournalSource journal(encoded);
  const auto recovered = vector_v2::recover_authority_journal(journal, encoded.size(),
                                                              recovered_records, recovered_samples);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.sequence == 70U);
  CHECK(recovered.bytes_consumed == 4096U);
  CHECK(recovered.state == source.read_view());
}

TEST_CASE("staged authority journal stays coherent after live authority mutates") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 2> samples{};
  vector_v2::OperationLog log(records, samples);
  const std::array first{vector_v2::CompactOperationSample{
      .x_quarter = 160, .y_quarter = 320, .radius_256 = 256, .elapsed_ms = 0}};
  const std::array second{vector_v2::CompactOperationSample{
      .x_quarter = 640, .y_quarter = 800, .radius_256 = 512, .elapsed_ms = 0}};
  REQUIRE(log.append({.color = 0x001FU, .gesture_id = 1U, .samples = first}));
  const auto plan = vector_v2::prepare_authority_journal(
      {.kind = vector_v2::JournalChangeKind::kCheckpoint}, log);
  REQUIRE(plan.has_value());
  const vector_v2::AuthorityReadView staged_view = plan->authority;
  std::vector<std::byte> transaction(plan->encoded_bytes);
  REQUIRE(vector_v2::stage_authority_journal(*plan, log, 80U, transaction));
  CHECK(std::all_of(transaction.end() - vector_v2::kAuthorityJournalCommitMarkerBytes,
                    transaction.end(), [](std::byte value) { return value == std::byte{0xFF}; }));

  REQUIRE(log.append({.color = 0xF800U, .gesture_id = 2U, .samples = second}));
  REQUIRE(vector_v2::seal_authority_journal(transaction));
  std::array<vector_v2::OperationRecord, 2> recovered_records{};
  std::array<vector_v2::CompactOperationSample, 2> recovered_samples{};
  const MemoryJournalSource source(transaction);
  const auto recovered = vector_v2::recover_authority_journal(source, transaction.size(),
                                                              recovered_records, recovered_samples);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.state == staged_view);
  CHECK(recovered_records[0].color == 0x001FU);
  CHECK(recovered_samples[0].x_quarter == first.front().x_quarter);
}

TEST_CASE("authority journal staging resumes after a header-only slice and rejects stale history") {
  std::array<vector_v2::OperationRecord, 1> records{};
  std::array<vector_v2::CompactOperationSample, 2> samples{};
  vector_v2::OperationLog log(records, samples);
  const std::array operation_samples{
      vector_v2::CompactOperationSample{
          .x_quarter = 160, .y_quarter = 320, .radius_256 = 256, .elapsed_ms = 0},
      vector_v2::CompactOperationSample{
          .x_quarter = 320, .y_quarter = 480, .radius_256 = 256, .elapsed_ms = 1},
  };
  REQUIRE(log.append({.gesture_id = 1U, .samples = operation_samples}));
  const auto plan = vector_v2::prepare_authority_journal(
      {.kind = vector_v2::JournalChangeKind::kCheckpoint}, log);
  REQUIRE(plan.has_value());
  std::vector<std::byte> transaction(plan->encoded_bytes);
  vector_v2::AuthorityJournalStager stager;
  REQUIRE(stager.start(*plan, log, 81U, transaction));
  CHECK(stager.resume(log, 16U) == vector_v2::AuthorityJournalStageResult::kProgress);
  CHECK(stager.resume(log, 16U) == vector_v2::AuthorityJournalStageResult::kComplete);
  CHECK(stager.complete());
  REQUIRE(vector_v2::seal_authority_journal(transaction));

  vector_v2::AuthorityJournalStager stale_stager;
  REQUIRE(stale_stager.start(*plan, log, 82U, transaction));
  CHECK(stale_stager.resume(log, 16U) == vector_v2::AuthorityJournalStageResult::kProgress);
  auto undo = log.prepare_undo();
  REQUIRE(undo.has_value());
  undo->publish();
  CHECK(stale_stager.resume(log, 16U) == vector_v2::AuthorityJournalStageResult::kStale);
  CHECK_FALSE(stale_stager.active());
}

TEST_CASE("authority journal staging bounds one maximum-sized operation") {
  constexpr std::size_t kMaximumSamples = std::numeric_limits<std::uint16_t>::max();
  std::array<vector_v2::OperationRecord, 1> records{};
  std::vector<vector_v2::CompactOperationSample> storage(kMaximumSamples);
  std::vector<vector_v2::CompactOperationSample> source_samples(kMaximumSamples,
                                                                vector_v2::CompactOperationSample{
                                                                    .x_quarter = 400,
                                                                    .y_quarter = 800,
                                                                    .radius_256 = 256,
                                                                    .elapsed_ms = 0,
                                                                });
  vector_v2::OperationLog log(records, storage);
  REQUIRE(log.append({.gesture_id = 1U, .samples = source_samples}));
  const auto plan = vector_v2::prepare_authority_journal(
      {.kind = vector_v2::JournalChangeKind::kCheckpoint}, log);
  REQUIRE(plan.has_value());
  std::vector<std::byte> transaction(plan->encoded_bytes);
  vector_v2::AuthorityJournalStager stager;
  REQUIRE(stager.start(*plan, log, 83U, transaction));
  std::size_t slices = 0U;
  vector_v2::AuthorityJournalStageResult result = vector_v2::AuthorityJournalStageResult::kProgress;
  while (result == vector_v2::AuthorityJournalStageResult::kProgress) {
    result = stager.resume(log, 1'024U);
    ++slices;
  }
  CHECK(result == vector_v2::AuthorityJournalStageResult::kComplete);
  CHECK(slices >= 512U);
  REQUIRE(vector_v2::seal_authority_journal(transaction));
  std::array<vector_v2::OperationRecord, 1> recovered_records{};
  std::vector<vector_v2::CompactOperationSample> recovered_samples(kMaximumSamples);
  const MemoryJournalSource source(transaction);
  const auto recovered = vector_v2::recover_authority_journal(source, transaction.size(),
                                                              recovered_records, recovered_samples);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.state == log.read_view());
  CHECK(recovered_samples.front() == source_samples.front());
  CHECK(recovered_samples.back() == source_samples.back());
}

TEST_CASE("full-capacity staged journal recovers and preserves its prior point after corruption") {
  constexpr std::size_t kSectorBytes = 4096U;
  constexpr std::size_t kSamplesPerOperation =
      vector_v2::kOperationSampleCapacity / vector_v2::kOperationCapacity;
  std::array<vector_v2::OperationRecord, 1> blank_records{};
  std::array<vector_v2::CompactOperationSample, 1> blank_samples{};
  vector_v2::OperationLog blank(blank_records, blank_samples);
  const auto blank_plan = vector_v2::prepare_authority_journal(
      {.kind = vector_v2::JournalChangeKind::kCheckpoint}, blank);
  REQUIRE(blank_plan.has_value());
  std::vector<std::byte> journal(kSectorBytes);
  REQUIRE(vector_v2::encode_authority_journal(*blank_plan, blank, 90U, journal));
  const std::size_t recovery_point_bytes = journal.size();

  std::vector<vector_v2::OperationRecord> records(vector_v2::kOperationCapacity);
  std::vector<vector_v2::CompactOperationSample> samples(vector_v2::kOperationSampleCapacity);
  vector_v2::OperationLog log(records, samples);
  std::array<vector_v2::CompactOperationSample, kSamplesPerOperation> operation_samples{};
  for (std::size_t sample = 0; sample < operation_samples.size(); ++sample) {
    operation_samples[sample] = {
        .x_quarter = static_cast<std::uint16_t>(400U + sample),
        .y_quarter = static_cast<std::uint16_t>(800U + sample),
        .radius_256 = 256U,
        .elapsed_ms = static_cast<std::uint16_t>(sample),
    };
  }
  for (std::size_t operation = 0; operation < vector_v2::kOperationCapacity; ++operation) {
    REQUIRE(log.append({.color = static_cast<std::uint16_t>(operation),
                        .gesture_id = static_cast<std::uint16_t>(operation + 1U),
                        .samples = operation_samples}));
  }
  const auto full_plan = vector_v2::prepare_authority_journal(
      {.kind = vector_v2::JournalChangeKind::kCheckpoint}, log);
  REQUIRE(full_plan.has_value());
  const std::size_t full_bytes =
      (full_plan->encoded_bytes + kSectorBytes - 1U) / kSectorBytes * kSectorBytes;
  journal.resize(recovery_point_bytes + full_bytes);
  auto full_transaction = std::span(journal).subspan(recovery_point_bytes, full_bytes);
  REQUIRE(vector_v2::stage_authority_journal(*full_plan, log, 91U, full_transaction));
  REQUIRE(vector_v2::seal_authority_journal(full_transaction));

  std::vector<vector_v2::OperationRecord> recovered_records(vector_v2::kOperationCapacity);
  std::vector<vector_v2::CompactOperationSample> recovered_samples(
      vector_v2::kOperationSampleCapacity);
  const MemoryJournalSource source(journal);
  const auto recovered = vector_v2::recover_authority_journal(source, journal.size(),
                                                              recovered_records, recovered_samples);
  CHECK(recovered.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(recovered.sequence == 91U);
  CHECK(recovered.transaction_count == 2U);
  CHECK(recovered.state == log.read_view());
  CHECK(recovered_samples.back() == operation_samples.back());

  std::vector<std::byte> damaged = journal;
  damaged[recovery_point_bytes + vector_v2::kAuthorityJournalHeaderBytes + 4U] ^= std::byte{0x01};
  const MemoryJournalSource damaged_source(damaged);
  const auto prior = vector_v2::recover_authority_journal(damaged_source, damaged.size(),
                                                          recovered_records, recovered_samples);
  CHECK(prior.status == vector_v2::JournalRecoveryStatus::kRecovered);
  CHECK(prior.sequence == 90U);
  CHECK(prior.transaction_count == 1U);
  CHECK(prior.bytes_consumed == recovery_point_bytes);
  CHECK(prior.discarded_tail);
  CHECK(prior.state == blank.read_view());
}
