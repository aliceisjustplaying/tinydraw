#include "tinydraw/vector_v2/ink_trace.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace tinydraw::vector_v2 {
namespace {

constexpr std::string_view kHeaderColumns = "magic,version,name,source,sample_rate_note";
constexpr std::string_view kEventColumns = "t_us,kind,x,y";

struct LineReader {
  std::string_view input;
  std::size_t cursor = 0;

  [[nodiscard]] std::optional<std::string_view> next() {
    if (cursor >= input.size()) {
      return std::nullopt;
    }
    const std::size_t end = input.find('\n', cursor);
    const std::size_t line_end = end == std::string_view::npos ? input.size() : end;
    std::string_view line = input.substr(cursor, line_end - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    cursor = end == std::string_view::npos ? input.size() : end + 1U;
    return line;
  }
};

template <std::size_t FieldCount>
bool split_fields(std::string_view line, std::array<std::string_view, FieldCount>& fields) {
  std::size_t start = 0;
  for (std::size_t index = 0; index < FieldCount; ++index) {
    const std::size_t comma = line.find(',', start);
    if (index + 1U == FieldCount) {
      if (comma != std::string_view::npos) {
        return false;
      }
      fields[index] = line.substr(start);
      return true;
    }
    if (comma == std::string_view::npos) {
      return false;
    }
    fields[index] = line.substr(start, comma - start);
    start = comma + 1U;
  }
  return false;
}

template <typename Integer>
bool parse_integer(std::string_view text, Integer& value) {
  if (text.empty()) {
    return false;
  }
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

std::optional<TraceSource> parse_source(std::string_view text) {
  if (text == "recorded") {
    return TraceSource::kRecorded;
  }
  if (text == "synthetic") {
    return TraceSource::kSynthetic;
  }
  return std::nullopt;
}

std::optional<TraceEventKind> parse_kind(std::string_view text) {
  if (text == "Down") {
    return TraceEventKind::kDown;
  }
  if (text == "Move") {
    return TraceEventKind::kMove;
  }
  if (text == "Up") {
    return TraceEventKind::kUp;
  }
  return std::nullopt;
}

std::string_view source_text(TraceSource source) {
  switch (source) {
    case TraceSource::kRecorded:
      return "recorded";
    case TraceSource::kSynthetic:
      return "synthetic";
  }
  return {};
}

std::string_view kind_text(TraceEventKind kind) {
  switch (kind) {
    case TraceEventKind::kDown:
      return "Down";
    case TraceEventKind::kMove:
      return "Move";
    case TraceEventKind::kUp:
      return "Up";
  }
  return {};
}

bool valid_csv_scalar(std::string_view text) {
  return !text.empty() && text.find_first_of(",\r\n") == std::string_view::npos;
}

class CsvWriter {
 public:
  explicit CsvWriter(std::span<char> output) : output_(output) {}

  bool append(std::string_view text) {
    if (text.size() > output_.size() - size_) {
      return false;
    }
    std::copy(text.begin(), text.end(), output_.begin() + static_cast<std::ptrdiff_t>(size_));
    size_ += text.size();
    return true;
  }

  template <typename Integer>
  bool append_integer(Integer value) {
    if (size_ == output_.size()) {
      return false;
    }
    char* const begin = output_.data() + size_;
    char* const end = output_.data() + output_.size();
    const auto result = std::to_chars(begin, end, value);
    if (result.ec != std::errc{}) {
      return false;
    }
    size_ = static_cast<std::size_t>(result.ptr - output_.data());
    return true;
  }

  [[nodiscard]] std::size_t size() const { return size_; }

 private:
  std::span<char> output_;
  std::size_t size_ = 0;
};

bool valid_timing(const InkSampleTiming& timing) {
  return timing.t_event_us <= timing.t_consumed_us &&
         timing.t_consumed_us <= timing.t_geometry_ready_us &&
         timing.t_geometry_ready_us <= timing.t_first_submit_us &&
         timing.t_first_submit_us <= timing.t_dma_complete_us;
}

enum class LatencyDelta : std::uint8_t {
  kEventToConsumed,
  kConsumedToGeometry,
  kGeometryToSubmit,
  kSubmitToDma,
  kEventToDma,
};

std::uint64_t latency_delta(const InkSampleTiming& timing, LatencyDelta delta) {
  switch (delta) {
    case LatencyDelta::kEventToConsumed:
      return timing.t_consumed_us - timing.t_event_us;
    case LatencyDelta::kConsumedToGeometry:
      return timing.t_geometry_ready_us - timing.t_consumed_us;
    case LatencyDelta::kGeometryToSubmit:
      return timing.t_first_submit_us - timing.t_geometry_ready_us;
    case LatencyDelta::kSubmitToDma:
      return timing.t_dma_complete_us - timing.t_first_submit_us;
    case LatencyDelta::kEventToDma:
      return timing.t_dma_complete_us - timing.t_event_us;
  }
  return 0;
}

std::size_t nearest_rank(std::size_t count, std::size_t percentile) {
  const std::size_t rank =
      (count / 100U) * percentile + (((count % 100U) * percentile + 99U) / 100U);
  return rank - 1U;
}

InkLatencyPercentiles summarize_delta(std::span<const InkSampleTiming> timings,
                                      std::span<std::uint64_t> scratch, LatencyDelta delta) {
  for (std::size_t index = 0; index < timings.size(); ++index) {
    scratch[index] = latency_delta(timings[index], delta);
  }
  auto values = scratch.first(timings.size());
  std::sort(values.begin(), values.end());
  return {
      .p50_us = values[nearest_rank(values.size(), 50U)],
      .p95_us = values[nearest_rank(values.size(), 95U)],
      .max_us = values.back(),
  };
}

}  // namespace

TraceValidationResult validate_ink_trace(const TraceHeader& header,
                                         std::span<const TraceEvent> events) {
  if (header.magic != kInkTraceMagic || header.version != kInkTraceVersion ||
      !valid_csv_scalar(header.name) || !valid_csv_scalar(header.sample_rate_note) ||
      source_text(header.source).empty()) {
    return {.error = TraceValidationError::kInvalidHeader};
  }
  if (events.empty()) {
    return {.error = TraceValidationError::kNoEvents};
  }

  bool stroke_open = false;
  std::uint64_t previous_timestamp = 0;
  for (std::size_t index = 0; index < events.size(); ++index) {
    const TraceEvent& event = events[index];
    if (index != 0U && event.t_us < previous_timestamp) {
      return {.error = TraceValidationError::kTimestampNotMonotonic, .event_index = index};
    }
    if (event.x >= kInkTraceWidth || event.y >= kInkTraceHeight) {
      return {.error = TraceValidationError::kCoordinateOutOfBounds, .event_index = index};
    }
    switch (event.kind) {
      case TraceEventKind::kDown:
        if (stroke_open) {
          return {.error = TraceValidationError::kUnexpectedDown, .event_index = index};
        }
        stroke_open = true;
        break;
      case TraceEventKind::kMove:
        if (!stroke_open) {
          return {.error = TraceValidationError::kUnexpectedMove, .event_index = index};
        }
        break;
      case TraceEventKind::kUp:
        if (!stroke_open) {
          return {.error = TraceValidationError::kUnexpectedUp, .event_index = index};
        }
        stroke_open = false;
        break;
      default:
        return {.error = TraceValidationError::kUnexpectedMove, .event_index = index};
    }
    previous_timestamp = event.t_us;
  }
  if (stroke_open) {
    return {.error = TraceValidationError::kUnclosedStroke, .event_index = events.size() - 1U};
  }
  return {};
}

TraceParseResult parse_ink_trace_csv(std::string_view csv, std::span<TraceEvent> event_storage) {
  LineReader lines{csv};
  std::size_t line_number = 1U;
  const auto columns = lines.next();
  if (!columns.has_value() || *columns != kHeaderColumns) {
    return {.status = TraceParseStatus::kInvalidCsv, .line = line_number};
  }

  ++line_number;
  const auto metadata = lines.next();
  std::array<std::string_view, 5> header_fields{};
  if (!metadata.has_value() || !split_fields(*metadata, header_fields)) {
    return {.status = TraceParseStatus::kInvalidCsv, .line = line_number};
  }
  std::uint32_t version = 0;
  const auto source = parse_source(header_fields[3]);
  if (!parse_integer(header_fields[1], version) || !source.has_value()) {
    return {.status = TraceParseStatus::kInvalidCsv, .line = line_number};
  }
  TraceHeader header{
      .magic = header_fields[0],
      .version = version,
      .name = header_fields[2],
      .source = *source,
      .sample_rate_note = header_fields[4],
  };
  if (version != kInkTraceVersion) {
    return {.status = TraceParseStatus::kUnsupportedVersion, .header = header, .line = line_number};
  }

  ++line_number;
  const auto event_columns = lines.next();
  if (!event_columns.has_value() || *event_columns != kEventColumns) {
    return {.status = TraceParseStatus::kInvalidCsv, .header = header, .line = line_number};
  }

  std::size_t event_count = 0;
  while (const auto line = lines.next()) {
    ++line_number;
    if (event_count == event_storage.size()) {
      return {.status = TraceParseStatus::kOutputTooSmall,
              .header = header,
              .event_count = event_count,
              .line = line_number};
    }
    std::array<std::string_view, 4> fields{};
    std::uint64_t timestamp = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    if (!split_fields(*line, fields) || !parse_integer(fields[0], timestamp) ||
        !parse_integer(fields[2], x) || !parse_integer(fields[3], y)) {
      return {.status = TraceParseStatus::kInvalidCsv,
              .header = header,
              .event_count = event_count,
              .line = line_number};
    }
    const auto kind = parse_kind(fields[1]);
    if (!kind.has_value()) {
      return {.status = TraceParseStatus::kInvalidCsv,
              .header = header,
              .event_count = event_count,
              .line = line_number};
    }
    event_storage[event_count++] = {.t_us = timestamp, .kind = *kind, .x = x, .y = y};
  }

  const TraceValidationResult validation =
      validate_ink_trace(header, event_storage.first(event_count));
  if (!validation.ok()) {
    return {.status = TraceParseStatus::kInvalidTrace,
            .header = header,
            .event_count = event_count,
            .line = 4U + validation.event_index,
            .validation = validation};
  }
  return {.status = TraceParseStatus::kOk,
          .header = header,
          .event_count = event_count,
          .line = line_number,
          .validation = validation};
}

TraceSerializeResult serialize_ink_trace_csv(const TraceHeader& header,
                                             std::span<const TraceEvent> events,
                                             std::span<char> output) {
  if (!validate_ink_trace(header, events).ok()) {
    return {.status = TraceSerializeStatus::kInvalidTrace};
  }
  CsvWriter writer(output);
  const std::string_view source = source_text(header.source);
  const auto append_header = [&]() {
    return writer.append(kHeaderColumns) && writer.append("\n") && writer.append(header.magic) &&
           writer.append(",") && writer.append_integer(header.version) && writer.append(",") &&
           writer.append(header.name) && writer.append(",") && writer.append(source) &&
           writer.append(",") && writer.append(header.sample_rate_note) && writer.append("\n") &&
           writer.append(kEventColumns) && writer.append("\n");
  };
  if (!append_header()) {
    return {.status = TraceSerializeStatus::kOutputTooSmall};
  }
  for (const TraceEvent& event : events) {
    if (!writer.append_integer(event.t_us) || !writer.append(",") ||
        !writer.append(kind_text(event.kind)) || !writer.append(",") ||
        !writer.append_integer(event.x) || !writer.append(",") || !writer.append_integer(event.y) ||
        !writer.append("\n")) {
      return {.status = TraceSerializeStatus::kOutputTooSmall};
    }
  }
  return {.status = TraceSerializeStatus::kOk, .bytes_written = writer.size()};
}

std::optional<InkLatencySummary> summarize_ink_latency(std::span<const InkSampleTiming> timings,
                                                       std::span<std::uint64_t> scratch) {
  if (timings.empty() || scratch.size() < timings.size() ||
      !std::all_of(timings.begin(), timings.end(), valid_timing)) {
    return std::nullopt;
  }
  return InkLatencySummary{
      .event_to_consumed = summarize_delta(timings, scratch, LatencyDelta::kEventToConsumed),
      .consumed_to_geometry_ready =
          summarize_delta(timings, scratch, LatencyDelta::kConsumedToGeometry),
      .geometry_ready_to_first_submit =
          summarize_delta(timings, scratch, LatencyDelta::kGeometryToSubmit),
      .first_submit_to_dma_complete = summarize_delta(timings, scratch, LatencyDelta::kSubmitToDma),
      .event_to_dma_complete = summarize_delta(timings, scratch, LatencyDelta::kEventToDma),
  };
}

bool InkStrokeCounters::valid() const {
  return consumed_events <= received_events && coalesced_events <= received_events &&
         consumed_down_events <= consumed_events && consumed_up_events <= consumed_events &&
         std::isfinite(max_consumed_sample_space_gap_px) &&
         max_consumed_sample_space_gap_px >= 0.0F;
}

bool InkStrokeCounters::down_up_conserved() const {
  return trace_down_events != 0U && trace_down_events == trace_up_events &&
         consumed_down_events == trace_down_events && consumed_up_events == trace_up_events;
}

double InkStrokeCounters::coalesced_ratio() const {
  if (received_events == 0U) {
    return 0.0;
  }
  return static_cast<double>(coalesced_events) / static_cast<double>(received_events);
}

}  // namespace tinydraw::vector_v2
