#include "tinydraw/vector_v2/authority_journal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {
namespace {

constexpr std::uint32_t kTransactionMagic = 0x314A4454U;  // TDJ1
constexpr std::uint32_t kCommitMagic = 0x54494D43U;       // CMIT
constexpr std::uint16_t kFormatVersion = 1U;
constexpr std::size_t kOperationWireBytes = 16U;
constexpr std::size_t kCrcChunkBytes = 512U;

constexpr std::array<std::uint32_t, 256> make_crc32_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::size_t index = 0; index < table.size(); ++index) {
    std::uint32_t value = static_cast<std::uint32_t>(index);
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (value & 1U);
      value = (value >> 1U) ^ (0xEDB8'8320U & mask);
    }
    table[index] = value;
  }
  return table;
}

constexpr auto kCrc32Table = make_crc32_table();

std::uint8_t get_u8(std::span<const std::byte> bytes, std::size_t offset) {
  return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint16_t get_u16(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(get_u8(bytes, offset)) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(get_u8(bytes, offset + 1U)) << 8U);
}

std::uint32_t get_u32(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(get_u16(bytes, offset)) |
         static_cast<std::uint32_t>(static_cast<std::uint32_t>(get_u16(bytes, offset + 2U)) << 16U);
}

std::uint64_t get_u64(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint64_t>(get_u32(bytes, offset)) |
         static_cast<std::uint64_t>(get_u32(bytes, offset + 4U)) << 32U;
}

void put_u8(std::span<std::byte> bytes, std::size_t offset, std::uint8_t value) {
  bytes[offset] = static_cast<std::byte>(value);
}

void put_u16(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) {
  put_u8(bytes, offset, static_cast<std::uint8_t>(value & 0xFFU));
  put_u8(bytes, offset + 1U, static_cast<std::uint8_t>(value >> 8U));
}

void put_u32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  put_u16(bytes, offset, static_cast<std::uint16_t>(value & 0xFFFFU));
  put_u16(bytes, offset + 2U, static_cast<std::uint16_t>(value >> 16U));
}

void put_u64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) {
  put_u32(bytes, offset, static_cast<std::uint32_t>(value & 0xFFFF'FFFFULL));
  put_u32(bytes, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
}

std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::byte> bytes) {
  for (const std::byte value : bytes) {
    const std::uint8_t index =
        static_cast<std::uint8_t>(crc ^ std::to_integer<std::uint8_t>(value));
    crc = (crc >> 8U) ^ kCrc32Table[index];
  }
  return crc;
}

std::uint32_t crc32(std::span<const std::byte> bytes) { return ~crc32_update(0xFFFF'FFFFU, bytes); }

std::optional<AuthorityJournalPlan> describe_change(JournalChange change, const OperationLog& log) {
  if (!log.ready()) {
    return std::nullopt;
  }
  const AuthorityReadView view = log.read_view();

  std::size_t first = 0;
  std::size_t count = 0;
  switch (change.kind) {
    case JournalChangeKind::kCheckpoint:
      count = view.retained_operation_count;
      break;
    case JournalChangeKind::kAppendStroke:
      if (change.first_operation > view.active_operation_count ||
          view.active_operation_count != view.retained_operation_count) {
        return std::nullopt;
      }
      first = change.first_operation;
      count = view.active_operation_count - first;
      if (count == 0U) {
        return std::nullopt;
      }
      break;
    case JournalChangeKind::kUpdate:
      break;
  }

  const bool carries_operations = change.kind == JournalChangeKind::kCheckpoint ||
                                  change.kind == JournalChangeKind::kAppendStroke;
  std::size_t payload = carries_operations ? sizeof(std::uint32_t) : 0U;
  for (std::size_t offset = 0; offset < count; ++offset) {
    const std::size_t index = first + offset;
    const auto operation = change.kind == JournalChangeKind::kCheckpoint
                               ? log.retained_operation(index)
                               : log.operation(index);
    if (!operation.has_value() ||
        operation->samples.size() >
            (std::numeric_limits<std::size_t>::max() - payload - kOperationWireBytes) /
                sizeof(CompactOperationSample)) {
      return std::nullopt;
    }
    payload += kOperationWireBytes + operation->samples.size() * sizeof(CompactOperationSample);
  }
  if (payload > std::numeric_limits<std::uint32_t>::max() ||
      payload > std::numeric_limits<std::size_t>::max() - kAuthorityJournalHeaderBytes -
                    kAuthorityJournalCommitMarkerBytes) {
    return std::nullopt;
  }
  return AuthorityJournalPlan{
      .change = change,
      .authority = view,
      .first_operation = first,
      .operation_count = count,
      .payload_bytes = payload,
      .encoded_bytes = kAuthorityJournalHeaderBytes + payload + kAuthorityJournalCommitMarkerBytes,
  };
}

void encode_operation_header(const StoredOperation& operation, std::span<std::byte> output) {
  put_u16(output, 0U, static_cast<std::uint16_t>(operation.samples.size()));
  put_u16(output, 2U, operation.color);
  put_u16(output, 4U, static_cast<std::uint16_t>(operation.world_bounds.x0));
  put_u16(output, 6U, static_cast<std::uint16_t>(operation.world_bounds.y0));
  put_u16(output, 8U, static_cast<std::uint16_t>(operation.world_bounds.x1));
  put_u16(output, 10U, static_cast<std::uint16_t>(operation.world_bounds.y1));
  put_u8(output, 12U, static_cast<std::uint8_t>(operation.tool));
  put_u8(output, 13U, 0U);
  put_u16(output, 14U, operation.gesture_id);
}

void encode_samples(std::span<const CompactOperationSample> samples, std::span<std::byte> output) {
  if constexpr (std::endian::native == std::endian::little) {
    static_assert(sizeof(CompactOperationSample) == 4U * sizeof(std::uint16_t));
    std::memcpy(output.data(), samples.data(), samples.size_bytes());
    return;
  }
  std::size_t position = 0U;
  for (const CompactOperationSample sample : samples) {
    put_u16(output, position, sample.x_quarter);
    put_u16(output, position + 2U, sample.y_quarter);
    put_u16(output, position + 4U, sample.radius_256);
    put_u16(output, position + 6U, sample.elapsed_ms);
    position += sizeof(CompactOperationSample);
  }
}

struct DecodedHeader {
  std::uint16_t wire_kind = 0;
  std::uint32_t total_bytes = 0;
  std::uint32_t payload_bytes = 0;
  AuthorityReadView authority{};
  std::size_t base_active_count = 0;
  std::uint64_t sequence = 0;
};

std::optional<DecodedHeader> decode_header(
    std::array<std::byte, kAuthorityJournalHeaderBytes> raw) {
  const auto bytes = std::span<const std::byte>(raw);
  if (get_u32(bytes, 0U) != kTransactionMagic || get_u16(bytes, 4U) != kFormatVersion ||
      get_u32(bytes, 8U) != kAuthorityJournalHeaderBytes) {
    return std::nullopt;
  }
  const std::uint16_t wire_kind = get_u16(bytes, 6U);
  if (wire_kind < 1U || wire_kind > 5U) {
    return std::nullopt;
  }
  const std::uint32_t expected_header_crc = get_u32(bytes, 20U);
  put_u32(raw, 20U, 0U);
  if (crc32(raw) != expected_header_crc) {
    return std::nullopt;
  }
  const std::uint32_t total_bytes = get_u32(bytes, 12U);
  const std::uint32_t payload_bytes = get_u32(bytes, 16U);
  if (total_bytes <
      kAuthorityJournalHeaderBytes + payload_bytes + kAuthorityJournalCommitMarkerBytes) {
    return std::nullopt;
  }
  return DecodedHeader{
      .wire_kind = wire_kind,
      .total_bytes = total_bytes,
      .payload_bytes = payload_bytes,
      .authority = {.epoch = {get_u64(bytes, 32U)},
                    .generation = {get_u32(bytes, 28U)},
                    .active_operation_count = get_u32(bytes, 52U),
                    .retained_operation_count = get_u32(bytes, 56U),
                    .retained_sample_count = get_u32(bytes, 60U)},
      .base_active_count = get_u32(bytes, 48U),
      .sequence = get_u64(bytes, 40U),
  };
}

std::optional<std::uint32_t> source_crc(const AuthorityJournalSource& source, std::size_t offset,
                                        std::size_t count, std::uint32_t initial) {
  std::array<std::byte, kCrcChunkBytes> chunk{};
  std::uint32_t crc = initial;
  while (count != 0U) {
    const std::size_t read_count = std::min(count, chunk.size());
    if (!source.read(offset, std::span(chunk).first(read_count))) {
      return std::nullopt;
    }
    crc = crc32_update(crc, std::span<const std::byte>(chunk).first(read_count));
    offset += read_count;
    count -= read_count;
  }
  return crc;
}

struct PayloadValidation {
  std::size_t operation_count = 0;
  std::size_t sample_count = 0;
  std::uint16_t before_active_gesture = 0;
  std::uint16_t at_active_gesture = 0;
};

std::optional<PayloadValidation> validate_payload(const AuthorityJournalSource& source,
                                                  std::size_t payload_offset,
                                                  std::size_t payload_bytes,
                                                  std::size_t active_count) {
  if (payload_bytes < sizeof(std::uint32_t)) {
    return std::nullopt;
  }
  std::array<std::byte, 4> count_bytes{};
  if (!source.read(payload_offset, count_bytes)) {
    return std::nullopt;
  }
  const std::size_t operation_count = get_u32(count_bytes, 0U);
  std::size_t cursor = sizeof(std::uint32_t);
  std::size_t samples_seen = 0;
  PayloadValidation validation{.operation_count = operation_count};
  std::array<std::byte, kOperationWireBytes> metadata{};
  std::array<std::byte, kCrcChunkBytes> sample_bytes{};
  for (std::size_t operation_index = 0; operation_index < operation_count; ++operation_index) {
    if (cursor > payload_bytes || kOperationWireBytes > payload_bytes - cursor ||
        !source.read(payload_offset + cursor, metadata)) {
      return std::nullopt;
    }
    const std::size_t sample_count = get_u16(metadata, 0U);
    const OperationTool tool = static_cast<OperationTool>(get_u8(metadata, 12U));
    if (sample_count == 0U || get_u8(metadata, 13U) != 0U ||
        (tool != OperationTool::kPen && tool != OperationTool::kEraser)) {
      return std::nullopt;
    }
    const std::uint16_t gesture = get_u16(metadata, 14U);
    if (operation_index + 1U == active_count) {
      validation.before_active_gesture = gesture;
    }
    if (operation_index == active_count) {
      validation.at_active_gesture = gesture;
    }
    cursor += kOperationWireBytes;
    const std::size_t operation_sample_bytes = sample_count * sizeof(CompactOperationSample);
    if (cursor > payload_bytes || operation_sample_bytes > payload_bytes - cursor) {
      return std::nullopt;
    }

    std::optional<PixelRect> calculated;
    std::uint16_t previous_elapsed = 0;
    bool first_sample = true;
    std::size_t sample_cursor = 0;
    while (sample_cursor < operation_sample_bytes) {
      const std::size_t read_count =
          std::min(operation_sample_bytes - sample_cursor, sample_bytes.size());
      if (!source.read(payload_offset + cursor + sample_cursor,
                       std::span(sample_bytes).first(read_count))) {
        return std::nullopt;
      }
      for (std::size_t offset = 0; offset < read_count; offset += sizeof(CompactOperationSample)) {
        const CompactOperationSample sample{
            .x_quarter = get_u16(sample_bytes, offset),
            .y_quarter = get_u16(sample_bytes, offset + 2U),
            .radius_256 = get_u16(sample_bytes, offset + 4U),
            .elapsed_ms = get_u16(sample_bytes, offset + 6U),
        };
        const auto sample_bounds = operation_sample_world_bounds(sample);
        if (!sample_bounds.has_value() || (!first_sample && sample.elapsed_ms < previous_elapsed)) {
          return std::nullopt;
        }
        if (!calculated.has_value()) {
          calculated = sample_bounds;
        } else {
          calculated->x0 = std::min(calculated->x0, sample_bounds->x0);
          calculated->y0 = std::min(calculated->y0, sample_bounds->y0);
          calculated->x1 = std::max(calculated->x1, sample_bounds->x1);
          calculated->y1 = std::max(calculated->y1, sample_bounds->y1);
        }
        previous_elapsed = sample.elapsed_ms;
        first_sample = false;
      }
      sample_cursor += read_count;
    }
    const PixelRect encoded{get_u16(metadata, 4U), get_u16(metadata, 6U), get_u16(metadata, 8U),
                            get_u16(metadata, 10U)};
    if (!calculated.has_value() || *calculated != encoded) {
      return std::nullopt;
    }
    cursor += operation_sample_bytes;
    samples_seen += sample_count;
  }
  if (cursor != payload_bytes) {
    return std::nullopt;
  }
  validation.sample_count = samples_seen;
  return validation;
}

std::size_t sample_count_for_prefix(std::span<const OperationRecord> records,
                                    std::size_t operation_count) {
  if (operation_count == 0U) {
    return 0U;
  }
  const OperationRecord& final = records[operation_count - 1U];
  return static_cast<std::size_t>(final.first_sample) + final.sample_count;
}

bool valid_active_boundary(std::span<const OperationRecord> records, std::size_t active_count) {
  if (active_count == 0U || active_count == records.size()) {
    return true;
  }
  const std::uint16_t previous = records[active_count - 1U].gesture_id;
  return previous == 0U || previous != records[active_count].gesture_id;
}

bool copy_payload(const AuthorityJournalSource& source, std::size_t payload_offset,
                  std::size_t payload_bytes, std::size_t destination_operation,
                  std::size_t destination_sample, std::span<OperationRecord> records,
                  std::span<CompactOperationSample> samples) {
  std::array<std::byte, 4> count_bytes{};
  if (!source.read(payload_offset, count_bytes)) {
    return false;
  }
  const std::size_t operation_count = get_u32(count_bytes, 0U);
  std::size_t cursor = sizeof(std::uint32_t);
  std::array<std::byte, kOperationWireBytes> metadata{};
  std::array<std::byte, kCrcChunkBytes> sample_bytes{};
  for (std::size_t operation_offset = 0; operation_offset < operation_count; ++operation_offset) {
    if (!source.read(payload_offset + cursor, metadata)) {
      return false;
    }
    const std::size_t sample_count = get_u16(metadata, 0U);
    records[destination_operation + operation_offset] = {
        .first_sample = static_cast<std::uint32_t>(destination_sample),
        .sample_count = static_cast<std::uint16_t>(sample_count),
        .color = get_u16(metadata, 2U),
        .bounds_x0 = get_u16(metadata, 4U),
        .bounds_y0 = get_u16(metadata, 6U),
        .bounds_x1 = get_u16(metadata, 8U),
        .bounds_y1 = get_u16(metadata, 10U),
        .tool = static_cast<OperationTool>(get_u8(metadata, 12U)),
        .flags = 0U,
        .gesture_id = get_u16(metadata, 14U),
    };
    cursor += kOperationWireBytes;
    const std::size_t operation_sample_bytes = sample_count * sizeof(CompactOperationSample);
    std::size_t sample_cursor = 0;
    while (sample_cursor < operation_sample_bytes) {
      const std::size_t read_count =
          std::min(operation_sample_bytes - sample_cursor, sample_bytes.size());
      if (!source.read(payload_offset + cursor + sample_cursor,
                       std::span(sample_bytes).first(read_count))) {
        return false;
      }
      for (std::size_t offset = 0; offset < read_count; offset += sizeof(CompactOperationSample)) {
        samples[destination_sample++] = {
            .x_quarter = get_u16(sample_bytes, offset),
            .y_quarter = get_u16(sample_bytes, offset + 2U),
            .radius_256 = get_u16(sample_bytes, offset + 4U),
            .elapsed_ms = get_u16(sample_bytes, offset + 6U),
        };
      }
      sample_cursor += read_count;
    }
    cursor += operation_sample_bytes;
  }
  return cursor == payload_bytes;
}

JournalRecovery failed_recovery(JournalRecoveryStatus status, const JournalRecovery& previous,
                                bool discardable) {
  if (previous.transaction_count != 0U && discardable) {
    JournalRecovery recovered = previous;
    recovered.status = JournalRecoveryStatus::kRecovered;
    recovered.discarded_tail = true;
    return recovered;
  }
  JournalRecovery failed = previous;
  failed.status = status;
  return failed;
}

}  // namespace

std::optional<AuthorityJournalPlan> prepare_authority_journal(JournalChange change,
                                                              const OperationLog& log) {
  return describe_change(change, log);
}

bool AuthorityJournalStager::start(const AuthorityJournalPlan& plan, const OperationLog& log,
                                   std::uint64_t sequence, std::span<std::byte> output) {
  reset();
  if (log.read_view() != plan.authority ||
      output.size() < kAuthorityJournalHeaderBytes + kAuthorityJournalCommitMarkerBytes ||
      output.size() < plan.encoded_bytes ||
      output.size() > std::numeric_limits<std::uint32_t>::max() || sequence == 0U ||
      plan.payload_bytes >
          output.size() - kAuthorityJournalHeaderBytes - kAuthorityJournalCommitMarkerBytes) {
    return false;
  }
  auto header = output.first(kAuthorityJournalHeaderBytes);
  auto payload = output.subspan(kAuthorityJournalHeaderBytes, plan.payload_bytes);
  std::fill(header.begin(), header.end(), std::byte{0xFF});
  std::fill(output.last(kAuthorityJournalCommitMarkerBytes).begin(), output.end(), std::byte{0xFF});
  put_u32(header, 0U, kTransactionMagic);
  put_u16(header, 4U, kFormatVersion);
  put_u16(header, 6U, static_cast<std::uint16_t>(plan.change.kind));
  put_u32(header, 8U, static_cast<std::uint32_t>(kAuthorityJournalHeaderBytes));
  put_u32(header, 12U, static_cast<std::uint32_t>(output.size()));
  put_u32(header, 16U, static_cast<std::uint32_t>(payload.size()));
  put_u32(header, 28U, plan.authority.generation.value);
  put_u64(header, 32U, plan.authority.epoch.value);
  put_u64(header, 40U, sequence);
  put_u32(header, 48U, static_cast<std::uint32_t>(plan.first_operation));
  put_u32(header, 52U, static_cast<std::uint32_t>(plan.authority.active_operation_count));
  put_u32(header, 56U, static_cast<std::uint32_t>(plan.authority.retained_operation_count));
  put_u32(header, 60U, static_cast<std::uint32_t>(plan.authority.retained_sample_count));
  if (!payload.empty()) {
    if (payload.size() < sizeof(std::uint32_t)) {
      return false;
    }
    put_u32(payload, 0U, static_cast<std::uint32_t>(plan.operation_count));
    payload_position_ = sizeof(std::uint32_t);
  }
  put_u32(header, 20U, 0U);
  put_u32(header, 20U, crc32(header));
  plan_ = plan;
  output_ = output;
  active_ = true;
  complete_ = plan.operation_count == 0U;
  return true;
}

AuthorityJournalStageResult AuthorityJournalStager::resume(const OperationLog& log,
                                                           std::size_t maximum_payload_bytes) {
  if (!active_ || maximum_payload_bytes < kOperationWireBytes) {
    return AuthorityJournalStageResult::kError;
  }
  if (log.read_view() != plan_.authority) {
    reset();
    return AuthorityJournalStageResult::kStale;
  }
  if (complete_) {
    return AuthorityJournalStageResult::kComplete;
  }
  auto payload = output_.subspan(kAuthorityJournalHeaderBytes, plan_.payload_bytes);
  std::size_t spent = 0U;
  while (operation_offset_ < plan_.operation_count) {
    const std::size_t index = plan_.first_operation + operation_offset_;
    const auto operation = plan_.change.kind == JournalChangeKind::kCheckpoint
                               ? log.retained_operation(index)
                               : log.operation(index);
    if (!operation.has_value()) {
      reset();
      return AuthorityJournalStageResult::kError;
    }
    if (!operation_header_staged_) {
      if (maximum_payload_bytes - spent < kOperationWireBytes) {
        return AuthorityJournalStageResult::kProgress;
      }
      if (payload_position_ > payload.size() ||
          kOperationWireBytes > payload.size() - payload_position_) {
        reset();
        return AuthorityJournalStageResult::kError;
      }
      encode_operation_header(*operation, payload.subspan(payload_position_, kOperationWireBytes));
      payload_position_ += kOperationWireBytes;
      spent += kOperationWireBytes;
      operation_header_staged_ = true;
    }
    const std::size_t remaining_samples = operation->samples.size() - sample_offset_;
    const std::size_t available_samples =
        (maximum_payload_bytes - spent) / sizeof(CompactOperationSample);
    const std::size_t copied_samples = std::min(remaining_samples, available_samples);
    const std::size_t copied_bytes = copied_samples * sizeof(CompactOperationSample);
    if (copied_samples != 0U) {
      if (payload_position_ > payload.size() - copied_bytes) {
        reset();
        return AuthorityJournalStageResult::kError;
      }
      encode_samples(operation->samples.subspan(sample_offset_, copied_samples),
                     payload.subspan(payload_position_, copied_bytes));
      payload_position_ += copied_bytes;
      sample_offset_ += copied_samples;
      spent += copied_bytes;
    }
    if (sample_offset_ != operation->samples.size()) {
      return AuthorityJournalStageResult::kProgress;
    }
    ++operation_offset_;
    sample_offset_ = 0U;
    operation_header_staged_ = false;
    if (spent == maximum_payload_bytes && operation_offset_ < plan_.operation_count) {
      return AuthorityJournalStageResult::kProgress;
    }
  }
  if (payload_position_ != plan_.payload_bytes) {
    reset();
    return AuthorityJournalStageResult::kError;
  }
  complete_ = true;
  return AuthorityJournalStageResult::kComplete;
}

void AuthorityJournalStager::reset() {
  plan_ = {};
  output_ = {};
  operation_offset_ = 0U;
  sample_offset_ = 0U;
  payload_position_ = 0U;
  operation_header_staged_ = false;
  active_ = false;
  complete_ = false;
}

bool stage_authority_journal(const AuthorityJournalPlan& plan, const OperationLog& log,
                             std::uint64_t sequence, std::span<std::byte> output) {
  AuthorityJournalStager stager;
  if (!stager.start(plan, log, sequence, output)) {
    return false;
  }
  while (!stager.complete()) {
    if (stager.resume(log, std::numeric_limits<std::size_t>::max()) !=
        AuthorityJournalStageResult::kComplete) {
      return false;
    }
  }
  return true;
}

bool seal_authority_journal(std::span<std::byte> output) {
  if (output.size() < kAuthorityJournalHeaderBytes + kAuthorityJournalCommitMarkerBytes ||
      output.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  auto header = output.first(kAuthorityJournalHeaderBytes);
  const std::size_t payload_bytes = get_u32(header, 16U);
  const std::uint64_t sequence = get_u64(header, 40U);
  if (get_u32(header, 0U) != kTransactionMagic || get_u16(header, 4U) != kFormatVersion ||
      get_u32(header, 8U) != kAuthorityJournalHeaderBytes ||
      get_u32(header, 12U) != output.size() ||
      payload_bytes >
          output.size() - kAuthorityJournalHeaderBytes - kAuthorityJournalCommitMarkerBytes ||
      sequence == 0U) {
    return false;
  }
  const std::uint32_t expected_header_crc = get_u32(header, 20U);
  put_u32(header, 20U, 0U);
  const std::uint32_t actual_header_crc = crc32(header);
  put_u32(header, 20U, expected_header_crc);
  if (actual_header_crc != expected_header_crc) {
    return false;
  }
  auto payload = output.subspan(kAuthorityJournalHeaderBytes, payload_bytes);
  const std::size_t padding_offset = kAuthorityJournalHeaderBytes + payload_bytes;
  const std::size_t padding_bytes =
      output.size() - padding_offset - kAuthorityJournalCommitMarkerBytes;
  std::fill(output.begin() + static_cast<std::ptrdiff_t>(padding_offset),
            output.begin() + static_cast<std::ptrdiff_t>(padding_offset + padding_bytes),
            std::byte{0xFF});
  std::uint32_t transaction_crc = crc32_update(0xFFFF'FFFFU, header);
  transaction_crc = ~crc32_update(transaction_crc, payload);
  auto marker = output.last(kAuthorityJournalCommitMarkerBytes);
  put_u32(marker, 0U, kCommitMagic);
  put_u32(marker, 4U, transaction_crc);
  put_u64(marker, 8U, sequence);
  return true;
}

bool encode_authority_journal(const AuthorityJournalPlan& plan, const OperationLog& log,
                              std::uint64_t sequence, std::span<std::byte> output) {
  return stage_authority_journal(plan, log, sequence, output) && seal_authority_journal(output);
}

JournalRecovery recover_authority_journal(const AuthorityJournalSource& source, std::size_t bytes,
                                          std::span<OperationRecord> records,
                                          std::span<CompactOperationSample> samples) {
  JournalRecovery result;
  std::size_t offset = 0U;
  std::array<std::byte, 4> magic{};
  while (offset < bytes) {
    if (bytes - offset < magic.size()) {
      return failed_recovery(JournalRecoveryStatus::kCorrupt, result, true);
    }
    if (!source.read(offset, magic)) {
      return failed_recovery(JournalRecoveryStatus::kIoError, result, false);
    }
    if (std::all_of(magic.begin(), magic.end(),
                    [](std::byte value) { return value == std::byte{0xFF}; })) {
      break;
    }
    if (bytes - offset < kAuthorityJournalHeaderBytes + kAuthorityJournalCommitMarkerBytes) {
      return failed_recovery(JournalRecoveryStatus::kCorrupt, result, true);
    }
    std::array<std::byte, kAuthorityJournalHeaderBytes> raw_header{};
    if (!source.read(offset, raw_header)) {
      return failed_recovery(JournalRecoveryStatus::kIoError, result, false);
    }
    const auto header = decode_header(raw_header);
    if (!header.has_value() || header->total_bytes > bytes - offset || header->sequence == 0U ||
        (result.transaction_count != 0U && header->sequence <= result.sequence) ||
        (result.transaction_count == 0U && header->wire_kind != 1U)) {
      return failed_recovery(JournalRecoveryStatus::kCorrupt, result, true);
    }
    const std::size_t payload_offset = offset + kAuthorityJournalHeaderBytes;
    std::array<std::byte, kAuthorityJournalCommitMarkerBytes> marker{};
    const std::size_t marker_offset = offset + header->total_bytes - marker.size();
    if (!source.read(marker_offset, marker)) {
      return failed_recovery(JournalRecoveryStatus::kIoError, result, false);
    }
    std::uint32_t transaction_crc = crc32_update(0xFFFF'FFFFU, raw_header);
    const auto continued_crc =
        source_crc(source, payload_offset, header->payload_bytes, transaction_crc);
    if (!continued_crc.has_value() || get_u32(marker, 0U) != kCommitMagic ||
        get_u64(marker, 8U) != header->sequence || get_u32(marker, 4U) != ~*continued_crc) {
      return failed_recovery(continued_crc.has_value() ? JournalRecoveryStatus::kCorrupt
                                                       : JournalRecoveryStatus::kIoError,
                             result, continued_crc.has_value());
    }

    std::optional<PayloadValidation> payload;
    if (header->payload_bytes != 0U) {
      payload = validate_payload(source, payload_offset, header->payload_bytes,
                                 header->authority.active_operation_count);
      if (!payload.has_value()) {
        return failed_recovery(JournalRecoveryStatus::kCorrupt, result, true);
      }
    }
    const std::size_t current_retained = result.state.retained_operation_count;
    const std::size_t current_samples = result.state.retained_sample_count;
    std::size_t destination_operation = 0U;
    std::size_t destination_sample = 0U;
    bool semantic_valid = true;
    switch (header->wire_kind) {
      case 1U:
        semantic_valid =
            payload.has_value() &&
            payload->operation_count == header->authority.retained_operation_count &&
            payload->sample_count == header->authority.retained_sample_count &&
            header->authority.active_operation_count <= header->authority.retained_operation_count;
        break;
      case 2U:
        semantic_valid =
            payload.has_value() &&
            header->base_active_count == result.state.active_operation_count &&
            payload->operation_count ==
                header->authority.retained_operation_count - header->base_active_count &&
            header->authority.active_operation_count == header->authority.retained_operation_count;
        destination_operation = header->base_active_count;
        destination_sample = sample_count_for_prefix(records, destination_operation);
        semantic_valid = semantic_valid && destination_sample + payload->sample_count ==
                                               header->authority.retained_sample_count;
        break;
      case 3U:
      case 5U:
        semantic_valid = header->payload_bytes == 0U &&
                         header->authority.retained_operation_count == current_retained &&
                         header->authority.retained_sample_count == current_samples &&
                         header->authority.active_operation_count <= current_retained &&
                         valid_active_boundary(records.first(current_retained),
                                               header->authority.active_operation_count);
        break;
      case 4U:
        semantic_valid = header->payload_bytes == 0U &&
                         header->authority.active_operation_count == 0U &&
                         header->authority.retained_operation_count == 0U &&
                         header->authority.retained_sample_count == 0U;
        break;
      default:
        semantic_valid = false;
        break;
    }
    if (header->authority.generation.value < header->authority.active_operation_count ||
        header->authority.retained_operation_count > records.size() ||
        header->authority.retained_sample_count > samples.size()) {
      semantic_valid = false;
    }
    if (!semantic_valid) {
      const bool insufficient = header->authority.retained_operation_count > records.size() ||
                                header->authority.retained_sample_count > samples.size();
      return failed_recovery(insufficient ? JournalRecoveryStatus::kInsufficientStorage
                                          : JournalRecoveryStatus::kCorrupt,
                             result, !insufficient);
    }
    if (payload.has_value() &&
        !copy_payload(source, payload_offset, header->payload_bytes, destination_operation,
                      destination_sample, records, samples)) {
      return failed_recovery(JournalRecoveryStatus::kIoError, result, false);
    }
    if (!valid_active_boundary(records.first(header->authority.retained_operation_count),
                               header->authority.active_operation_count)) {
      return failed_recovery(JournalRecoveryStatus::kCorrupt, result, true);
    }

    result.status = JournalRecoveryStatus::kRecovered;
    result.state = header->authority;
    result.bytes_consumed = offset + header->total_bytes;
    ++result.transaction_count;
    result.sequence = header->sequence;
    result.discarded_tail = false;
    offset = result.bytes_consumed;
  }
  return result;
}

}  // namespace tinydraw::vector_v2
