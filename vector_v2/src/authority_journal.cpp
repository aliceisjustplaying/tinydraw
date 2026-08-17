#include "tinydraw/vector_v2/authority_journal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {
namespace {

constexpr std::uint32_t kTransactionMagic = 0x314A4454U;  // TDJ1
constexpr std::uint32_t kCommitMagic = 0x54494D43U;       // CMIT
constexpr std::uint16_t kFormatVersion = 1U;
constexpr std::size_t kOperationWireBytes = 16U;
constexpr std::size_t kStateOffset = 64U;
constexpr std::size_t kCrcChunkBytes = 512U;
constexpr int kMinimumTiledRadiusUnits = 24;

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

std::int32_t get_i32(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::int32_t>(get_u32(bytes, offset));
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

void put_i32(std::span<std::byte> bytes, std::size_t offset, std::int32_t value) {
  put_u32(bytes, offset, static_cast<std::uint32_t>(value));
}

std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::byte> bytes) {
  for (const std::byte value : bytes) {
    crc ^= std::to_integer<std::uint8_t>(value);
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB8'8320U & mask);
    }
  }
  return crc;
}

std::uint32_t crc32(std::span<const std::byte> bytes) {
  return ~crc32_update(0xFFFF'FFFFU, bytes);
}

bool valid_tool(ChromeTool tool) {
  switch (tool) {
    case ChromeTool::kDraw:
    case ChromeTool::kErase:
    case ChromeTool::kPan:
      return true;
  }
  return false;
}

bool valid_size(ChromeSize size) {
  switch (size) {
    case ChromeSize::kSmall:
    case ChromeSize::kMedium:
    case ChromeSize::kLarge:
    case ChromeSize::kExtraLarge:
      return true;
  }
  return false;
}

bool valid_state(const JournalState& state) {
  NavigationState navigation;
  return navigation.restore(state.navigation) && valid_tool(state.tool) && valid_size(state.size) &&
         state.palette_page < kPico8Palettes.size() && state.color_index < kPaletteColorCount &&
         state.next_stroke_id != 0U;
}

void encode_state(const JournalState& state, std::span<std::byte> header) {
  put_u8(header, kStateOffset, static_cast<std::uint8_t>(state.navigation.zoom));
  put_u8(header, kStateOffset + 1U, static_cast<std::uint8_t>(state.tool));
  put_u8(header, kStateOffset + 2U, static_cast<std::uint8_t>(state.size));
  put_u8(header, kStateOffset + 3U, state.palette_page);
  put_u8(header, kStateOffset + 4U, state.color_index);
  std::uint8_t remembered_valid = 0U;
  for (std::size_t index = 0; index < state.navigation.remembered_valid.size(); ++index) {
    remembered_valid |= state.navigation.remembered_valid[index]
                            ? static_cast<std::uint8_t>(1U << index)
                            : 0U;
  }
  put_u8(header, kStateOffset + 5U, remembered_valid);
  put_u16(header, kStateOffset + 6U, state.next_stroke_id);
  put_i32(header, kStateOffset + 8U, state.navigation.origin.x);
  put_i32(header, kStateOffset + 12U, state.navigation.origin.y);
  put_i32(header, kStateOffset + 16U, state.navigation.focus_quarter_world.x);
  put_i32(header, kStateOffset + 20U, state.navigation.focus_quarter_world.y);
  std::size_t offset = kStateOffset + 24U;
  for (const NavigationPoint point : state.navigation.remembered_origins) {
    put_i32(header, offset, point.x);
    put_i32(header, offset + 4U, point.y);
    offset += 8U;
  }
  for (const NavigationPoint point : state.navigation.remembered_focuses) {
    put_i32(header, offset, point.x);
    put_i32(header, offset + 4U, point.y);
    offset += 8U;
  }
}

std::optional<JournalState> decode_state(std::span<const std::byte> header) {
  JournalState state;
  state.navigation.zoom = static_cast<ZoomLevel>(get_u8(header, kStateOffset));
  state.tool = static_cast<ChromeTool>(get_u8(header, kStateOffset + 1U));
  state.size = static_cast<ChromeSize>(get_u8(header, kStateOffset + 2U));
  state.palette_page = get_u8(header, kStateOffset + 3U);
  state.color_index = get_u8(header, kStateOffset + 4U);
  const std::uint8_t remembered_valid = get_u8(header, kStateOffset + 5U);
  if ((remembered_valid & 0xF0U) != 0U) {
    return std::nullopt;
  }
  state.next_stroke_id = get_u16(header, kStateOffset + 6U);
  state.navigation.origin = {get_i32(header, kStateOffset + 8U),
                             get_i32(header, kStateOffset + 12U)};
  state.navigation.focus_quarter_world = {get_i32(header, kStateOffset + 16U),
                                          get_i32(header, kStateOffset + 20U)};
  std::size_t offset = kStateOffset + 24U;
  for (NavigationPoint& point : state.navigation.remembered_origins) {
    point = {get_i32(header, offset), get_i32(header, offset + 4U)};
    offset += 8U;
  }
  for (NavigationPoint& point : state.navigation.remembered_focuses) {
    point = {get_i32(header, offset), get_i32(header, offset + 4U)};
    offset += 8U;
  }
  for (std::size_t index = 0; index < state.navigation.remembered_valid.size(); ++index) {
    state.navigation.remembered_valid[index] = (remembered_valid & (1U << index)) != 0U;
  }
  return valid_state(state) ? std::optional{state} : std::nullopt;
}

struct ChangeDescription {
  AuthorityReadView view{};
  std::size_t first_operation = 0;
  std::size_t operation_count = 0;
  std::size_t retained_sample_count = 0;
  std::size_t payload_bytes = 0;
};

std::optional<ChangeDescription> describe_change(JournalChange change, const OperationLog& log) {
  if (!log.ready()) {
    return std::nullopt;
  }
  const AuthorityReadView view = log.read_view();
  std::size_t retained_samples = 0;
  for (std::size_t index = 0; index < view.retained_operation_count; ++index) {
    const auto operation = log.retained_operation(view, index);
    if (!operation.has_value() ||
        operation->samples.size() > std::numeric_limits<std::uint16_t>::max() ||
        retained_samples > std::numeric_limits<std::size_t>::max() - operation->samples.size()) {
      return std::nullopt;
    }
    retained_samples += operation->samples.size();
  }

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
    case JournalChangeKind::kHistory:
    case JournalChangeKind::kState:
      break;
    case JournalChangeKind::kReset:
      if (view.retained_operation_count != 0U) {
        return std::nullopt;
      }
      break;
  }

  const bool carries_operations = change.kind == JournalChangeKind::kCheckpoint ||
                                  change.kind == JournalChangeKind::kAppendStroke;
  std::size_t payload = carries_operations ? sizeof(std::uint32_t) : 0U;
  for (std::size_t offset = 0; offset < count; ++offset) {
    const std::size_t index = first + offset;
    const auto operation = change.kind == JournalChangeKind::kCheckpoint
                               ? log.retained_operation(view, index)
                               : log.operation(view, index);
    if (!operation.has_value() ||
        operation->samples.size() >
            (std::numeric_limits<std::size_t>::max() - payload - kOperationWireBytes) /
                sizeof(CompactOperationSample)) {
      return std::nullopt;
    }
    payload += kOperationWireBytes + operation->samples.size() * sizeof(CompactOperationSample);
  }
  return ChangeDescription{view, first, count, retained_samples, payload};
}

void encode_operation(const StoredOperation& operation, std::span<std::byte> output) {
  put_u16(output, 0U, static_cast<std::uint16_t>(operation.samples.size()));
  put_u16(output, 2U, operation.color);
  put_u16(output, 4U, static_cast<std::uint16_t>(operation.world_bounds.x0));
  put_u16(output, 6U, static_cast<std::uint16_t>(operation.world_bounds.y0));
  put_u16(output, 8U, static_cast<std::uint16_t>(operation.world_bounds.x1));
  put_u16(output, 10U, static_cast<std::uint16_t>(operation.world_bounds.y1));
  put_u8(output, 12U, static_cast<std::uint8_t>(operation.tool));
  put_u8(output, 13U, 0U);
  put_u16(output, 14U, operation.gesture_id);
  std::size_t position = kOperationWireBytes;
  for (const CompactOperationSample sample : operation.samples) {
    put_u16(output, position, sample.x_quarter);
    put_u16(output, position + 2U, sample.y_quarter);
    put_u16(output, position + 4U, sample.radius_256);
    put_u16(output, position + 6U, sample.elapsed_ms);
    position += sizeof(CompactOperationSample);
  }
}

struct DecodedHeader {
  JournalChangeKind kind = JournalChangeKind::kState;
  std::uint32_t total_bytes = 0;
  std::uint32_t payload_bytes = 0;
  std::uint32_t payload_crc = 0;
  AuthorityReadView authority{};
  std::size_t retained_sample_count = 0;
  std::size_t base_active_count = 0;
  std::uint64_t sequence = 0;
  JournalState state{};
};

std::optional<DecodedHeader> decode_header(std::array<std::byte, kAuthorityJournalHeaderBytes> raw) {
  const auto bytes = std::span<const std::byte>(raw);
  if (get_u32(bytes, 0U) != kTransactionMagic || get_u16(bytes, 4U) != kFormatVersion ||
      get_u32(bytes, 8U) != kAuthorityJournalHeaderBytes) {
    return std::nullopt;
  }
  const auto kind = static_cast<JournalChangeKind>(get_u16(bytes, 6U));
  if (kind != JournalChangeKind::kCheckpoint && kind != JournalChangeKind::kAppendStroke &&
      kind != JournalChangeKind::kHistory && kind != JournalChangeKind::kReset &&
      kind != JournalChangeKind::kState) {
    return std::nullopt;
  }
  const std::uint32_t expected_header_crc = get_u32(bytes, 20U);
  put_u32(raw, 20U, 0U);
  if (crc32(raw) != expected_header_crc) {
    return std::nullopt;
  }
  const auto state = decode_state(bytes);
  if (!state.has_value()) {
    return std::nullopt;
  }
  const std::uint32_t total_bytes = get_u32(bytes, 12U);
  const std::uint32_t payload_bytes = get_u32(bytes, 16U);
  if (total_bytes != kAuthorityJournalHeaderBytes + payload_bytes +
                         kAuthorityJournalCommitMarkerBytes) {
    return std::nullopt;
  }
  return DecodedHeader{
      .kind = kind,
      .total_bytes = total_bytes,
      .payload_bytes = payload_bytes,
      .payload_crc = get_u32(bytes, 24U),
      .authority = {.epoch = {get_u64(bytes, 32U)},
                    .generation = {get_u32(bytes, 28U)},
                    .active_operation_count = get_u32(bytes, 52U),
                    .retained_operation_count = get_u32(bytes, 56U)},
      .retained_sample_count = get_u32(bytes, 60U),
      .base_active_count = get_u32(bytes, 48U),
      .sequence = get_u64(bytes, 40U),
      .state = *state,
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

    int minimum_x = kWorldWidth * kSampleUnitsPerWorldUnit;
    int minimum_y = kWorldHeight * kSampleUnitsPerWorldUnit;
    int maximum_x = 0;
    int maximum_y = 0;
    std::uint16_t previous_elapsed = 0;
    bool first_sample = true;
    std::size_t sample_cursor = 0;
    while (sample_cursor < operation_sample_bytes) {
      const std::size_t read_count = std::min(operation_sample_bytes - sample_cursor,
                                              sample_bytes.size());
      if (!source.read(payload_offset + cursor + sample_cursor,
                       std::span(sample_bytes).first(read_count))) {
        return std::nullopt;
      }
      for (std::size_t offset = 0; offset < read_count; offset += sizeof(CompactOperationSample)) {
        const std::uint16_t x = get_u16(sample_bytes, offset);
        const std::uint16_t y = get_u16(sample_bytes, offset + 2U);
        const std::uint16_t radius = get_u16(sample_bytes, offset + 4U);
        const std::uint16_t elapsed = get_u16(sample_bytes, offset + 6U);
        if (x > kWorldWidth * kSampleUnitsPerWorldUnit ||
            y > kWorldHeight * kSampleUnitsPerWorldUnit || radius == 0U ||
            (!first_sample && elapsed < previous_elapsed)) {
          return std::nullopt;
        }
        const int radius_units =
            std::max((static_cast<int>(radius) + 15) / 16, kMinimumTiledRadiusUnits);
        minimum_x = std::min(minimum_x, static_cast<int>(x) - radius_units);
        minimum_y = std::min(minimum_y, static_cast<int>(y) - radius_units);
        maximum_x = std::max(maximum_x, static_cast<int>(x) + radius_units);
        maximum_y = std::max(maximum_y, static_cast<int>(y) + radius_units);
        previous_elapsed = elapsed;
        first_sample = false;
      }
      sample_cursor += read_count;
    }
    const PixelRect calculated{
        .x0 = std::max(0, minimum_x) / kSampleUnitsPerWorldUnit,
        .y0 = std::max(0, minimum_y) / kSampleUnitsPerWorldUnit,
        .x1 = std::min(kWorldWidth,
                       (maximum_x + kSampleUnitsPerWorldUnit - 1) / kSampleUnitsPerWorldUnit),
        .y1 = std::min(kWorldHeight,
                       (maximum_y + kSampleUnitsPerWorldUnit - 1) / kSampleUnitsPerWorldUnit),
    };
    const PixelRect encoded{get_u16(metadata, 4U), get_u16(metadata, 6U),
                            get_u16(metadata, 8U), get_u16(metadata, 10U)};
    if (calculated != encoded) {
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
      const std::size_t read_count = std::min(operation_sample_bytes - sample_cursor,
                                              sample_bytes.size());
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

std::optional<std::size_t> authority_journal_encoded_size(JournalChange change,
                                                          const OperationLog& log) {
  const auto description = describe_change(change, log);
  if (!description.has_value() ||
      description->payload_bytes > std::numeric_limits<std::uint32_t>::max() ||
      description->payload_bytes >
          std::numeric_limits<std::size_t>::max() - kAuthorityJournalHeaderBytes -
              kAuthorityJournalCommitMarkerBytes) {
    return std::nullopt;
  }
  return kAuthorityJournalHeaderBytes + description->payload_bytes +
         kAuthorityJournalCommitMarkerBytes;
}

bool encode_authority_journal(JournalChange change, const OperationLog& log,
                              const JournalState& state, std::uint64_t sequence,
                              std::span<std::byte> output) {
  const auto description = describe_change(change, log);
  const auto size = authority_journal_encoded_size(change, log);
  if (!description.has_value() || !size.has_value() || output.size() != *size || sequence == 0U ||
      !valid_state(state)) {
    return false;
  }
  std::fill(output.begin(), output.end(), std::byte{0});
  auto header = output.first(kAuthorityJournalHeaderBytes);
  auto payload = output.subspan(kAuthorityJournalHeaderBytes, description->payload_bytes);
  put_u32(header, 0U, kTransactionMagic);
  put_u16(header, 4U, kFormatVersion);
  put_u16(header, 6U, static_cast<std::uint16_t>(change.kind));
  put_u32(header, 8U, static_cast<std::uint32_t>(kAuthorityJournalHeaderBytes));
  put_u32(header, 12U, static_cast<std::uint32_t>(output.size()));
  put_u32(header, 16U, static_cast<std::uint32_t>(payload.size()));
  put_u32(header, 28U, description->view.generation.value);
  put_u64(header, 32U, description->view.epoch.value);
  put_u64(header, 40U, sequence);
  put_u32(header, 48U, static_cast<std::uint32_t>(description->first_operation));
  put_u32(header, 52U,
          static_cast<std::uint32_t>(description->view.active_operation_count));
  put_u32(header, 56U,
          static_cast<std::uint32_t>(description->view.retained_operation_count));
  put_u32(header, 60U, static_cast<std::uint32_t>(description->retained_sample_count));
  encode_state(state, header);

  if (!payload.empty()) {
    put_u32(payload, 0U, static_cast<std::uint32_t>(description->operation_count));
    std::size_t position = sizeof(std::uint32_t);
    for (std::size_t offset = 0; offset < description->operation_count; ++offset) {
      const std::size_t index = description->first_operation + offset;
      const auto operation = change.kind == JournalChangeKind::kCheckpoint
                                 ? log.retained_operation(description->view, index)
                                 : log.operation(description->view, index);
      if (!operation.has_value()) {
        return false;
      }
      const std::size_t bytes =
          kOperationWireBytes + operation->samples.size() * sizeof(CompactOperationSample);
      encode_operation(*operation, payload.subspan(position, bytes));
      position += bytes;
    }
  }
  put_u32(header, 24U, crc32(payload));
  put_u32(header, 20U, 0U);
  put_u32(header, 20U, crc32(header));
  std::uint32_t transaction_crc = crc32_update(0xFFFF'FFFFU, header);
  transaction_crc = ~crc32_update(transaction_crc, payload);
  auto marker = output.last(kAuthorityJournalCommitMarkerBytes);
  put_u32(marker, 0U, kCommitMagic);
  put_u32(marker, 4U, transaction_crc);
  put_u64(marker, 8U, sequence);
  return log.unchanged(description->view);
}

JournalRecovery recover_authority_journal(const AuthorityJournalSource& source, std::size_t bytes,
                                           std::span<OperationRecord> records,
                                           std::span<CompactOperationSample> samples,
                                           JournalState& state) {
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
    if (std::all_of(magic.begin(), magic.end(), [](std::byte value) {
          return value == std::byte{0xFF};
        })) {
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
        (result.transaction_count == 0U && header->kind != JournalChangeKind::kCheckpoint)) {
      return failed_recovery(JournalRecoveryStatus::kCorrupt, result, true);
    }
    const std::size_t payload_offset = offset + kAuthorityJournalHeaderBytes;
    const auto payload_crc = source_crc(source, payload_offset, header->payload_bytes,
                                        0xFFFF'FFFFU);
    if (!payload_crc.has_value() || ~*payload_crc != header->payload_crc) {
      return failed_recovery(payload_crc.has_value() ? JournalRecoveryStatus::kCorrupt
                                                     : JournalRecoveryStatus::kIoError,
                             result, payload_crc.has_value());
    }
    std::array<std::byte, kAuthorityJournalCommitMarkerBytes> marker{};
    if (!source.read(payload_offset + header->payload_bytes, marker)) {
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
    const std::size_t current_samples = result.retained_sample_count;
    std::size_t destination_operation = 0U;
    std::size_t destination_sample = 0U;
    bool semantic_valid = true;
    switch (header->kind) {
      case JournalChangeKind::kCheckpoint:
        semantic_valid = payload.has_value() &&
                         payload->operation_count == header->authority.retained_operation_count &&
                         payload->sample_count == header->retained_sample_count &&
                         header->authority.active_operation_count <=
                             header->authority.retained_operation_count;
        break;
      case JournalChangeKind::kAppendStroke:
        semantic_valid = payload.has_value() && header->base_active_count ==
                                                    result.state.active_operation_count &&
                         payload->operation_count ==
                             header->authority.retained_operation_count -
                                 header->base_active_count &&
                         header->authority.active_operation_count ==
                             header->authority.retained_operation_count;
        destination_operation = header->base_active_count;
        destination_sample = sample_count_for_prefix(records, destination_operation);
        semantic_valid = semantic_valid &&
                         destination_sample + payload->sample_count ==
                             header->retained_sample_count;
        break;
      case JournalChangeKind::kHistory:
      case JournalChangeKind::kState:
        semantic_valid = header->payload_bytes == 0U &&
                         header->authority.retained_operation_count == current_retained &&
                         header->retained_sample_count == current_samples &&
                         header->authority.active_operation_count <= current_retained &&
                         valid_active_boundary(records.first(current_retained),
                                               header->authority.active_operation_count);
        break;
      case JournalChangeKind::kReset:
        semantic_valid = header->payload_bytes == 0U &&
                         header->authority.active_operation_count == 0U &&
                         header->authority.retained_operation_count == 0U &&
                         header->retained_sample_count == 0U;
        break;
    }
    if (header->authority.generation.value < header->authority.active_operation_count ||
        header->authority.retained_operation_count > records.size() ||
        header->retained_sample_count > samples.size()) {
      semantic_valid = false;
    }
    if (!semantic_valid) {
      const bool insufficient = header->authority.retained_operation_count > records.size() ||
                                header->retained_sample_count > samples.size();
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
    result.retained_sample_count = header->retained_sample_count;
    result.bytes_consumed = offset + header->total_bytes;
    ++result.transaction_count;
    result.sequence = header->sequence;
    result.discarded_tail = false;
    state = header->state;
    offset = result.bytes_consumed;
  }
  return result;
}

}  // namespace tinydraw::vector_v2
