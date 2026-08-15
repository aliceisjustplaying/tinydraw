#ifndef TINYDRAW_VECTOR_V2_INK_TRACE_H
#define TINYDRAW_VECTOR_V2_INK_TRACE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tinydraw::vector_v2 {

inline constexpr std::string_view kInkTraceMagic = "TINYDRAW_INKTRACE";
inline constexpr std::uint32_t kInkTraceVersion = 1U;
inline constexpr std::uint16_t kInkTraceWidth = 368U;
inline constexpr std::uint16_t kInkTraceHeight = 448U;

enum class TraceSource : std::uint8_t {
  kRecorded,
  kSynthetic,
};

enum class TraceEventKind : std::uint8_t {
  kDown,
  kMove,
  kUp,
};

struct TraceHeader {
  std::string_view magic = kInkTraceMagic;
  std::uint32_t version = kInkTraceVersion;
  std::string_view name{};
  TraceSource source = TraceSource::kRecorded;
  std::string_view sample_rate_note{};
};

struct TraceEvent {
  std::uint64_t t_us = 0;
  TraceEventKind kind = TraceEventKind::kUp;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  bool operator==(const TraceEvent&) const = default;
};

enum class TraceValidationError : std::uint8_t {
  kNone,
  kInvalidHeader,
  kNoEvents,
  kTimestampNotMonotonic,
  kCoordinateOutOfBounds,
  kUnexpectedDown,
  kUnexpectedMove,
  kUnexpectedUp,
  kUnclosedStroke,
};

struct TraceValidationResult {
  TraceValidationError error = TraceValidationError::kNone;
  std::size_t event_index = 0;
  [[nodiscard]] bool ok() const { return error == TraceValidationError::kNone; }
};

// Header string views must remain valid for the duration of this call.
[[nodiscard]] TraceValidationResult validate_ink_trace(const TraceHeader& header,
                                                       std::span<const TraceEvent> events);

enum class TraceParseStatus : std::uint8_t {
  kOk,
  kInvalidCsv,
  kUnsupportedVersion,
  kOutputTooSmall,
  kInvalidTrace,
};

struct TraceParseResult {
  TraceParseStatus status = TraceParseStatus::kInvalidCsv;
  TraceHeader header{};
  std::size_t event_count = 0;
  std::size_t line = 0;
  TraceValidationResult validation{};
  [[nodiscard]] bool ok() const { return status == TraceParseStatus::kOk; }
};

// Parses the canonical CSV representation without allocating. Header strings
// refer into csv, and events are written into caller-owned storage.
[[nodiscard]] TraceParseResult parse_ink_trace_csv(std::string_view csv,
                                                   std::span<TraceEvent> event_storage);

enum class TraceSerializeStatus : std::uint8_t {
  kOk,
  kInvalidTrace,
  kOutputTooSmall,
};

struct TraceSerializeResult {
  TraceSerializeStatus status = TraceSerializeStatus::kInvalidTrace;
  std::size_t bytes_written = 0;
  [[nodiscard]] bool ok() const { return status == TraceSerializeStatus::kOk; }
};

// Serializes into caller-owned storage. The result excludes a terminating NUL.
[[nodiscard]] TraceSerializeResult serialize_ink_trace_csv(const TraceHeader& header,
                                                           std::span<const TraceEvent> events,
                                                           std::span<char> output);

struct InkSampleTiming {
  std::uint64_t t_event_us = 0;
  std::uint64_t t_consumed_us = 0;
  std::uint64_t t_geometry_ready_us = 0;
  std::uint64_t t_first_submit_us = 0;
  std::uint64_t t_dma_complete_us = 0;
};

struct InkLatencyPercentiles {
  std::uint64_t p50_us = 0;
  std::uint64_t p95_us = 0;
  std::uint64_t max_us = 0;
};

struct InkLatencySummary {
  InkLatencyPercentiles event_to_consumed{};
  InkLatencyPercentiles consumed_to_geometry_ready{};
  InkLatencyPercentiles geometry_ready_to_first_submit{};
  InkLatencyPercentiles first_submit_to_dma_complete{};
  InkLatencyPercentiles event_to_dma_complete{};
};

// Uses nearest-rank percentiles. scratch must provide at least timings.size()
// uint64_t entries. Returns nullopt for empty input, short scratch, or timestamps
// that run backward anywhere in the measured chain.
[[nodiscard]] std::optional<InkLatencySummary> summarize_ink_latency(
    std::span<const InkSampleTiming> timings, std::span<std::uint64_t> scratch);

struct InkStrokeCounters {
  std::uint32_t received_events = 0;
  std::uint32_t consumed_events = 0;
  std::uint32_t coalesced_events = 0;
  std::uint32_t trace_down_events = 0;
  std::uint32_t trace_up_events = 0;
  std::uint32_t consumed_down_events = 0;
  std::uint32_t consumed_up_events = 0;
  std::uint64_t max_consumed_sample_time_gap_us = 0;
  float max_consumed_sample_space_gap_px = 0.0F;

  [[nodiscard]] bool valid() const;
  [[nodiscard]] bool down_up_conserved() const;
  [[nodiscard]] double coalesced_ratio() const;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_INK_TRACE_H
