#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tinydraw/vector_v2/ink_trace.h"

namespace {

using tinydraw::vector_v2::TraceEvent;
using tinydraw::vector_v2::TraceEventKind;
using tinydraw::vector_v2::TraceParseStatus;
using tinydraw::vector_v2::TraceSource;
using tinydraw::vector_v2::TraceValidationError;

std::string_view parse_status_text(TraceParseStatus status) {
  switch (status) {
    case TraceParseStatus::kOk:
      return "ok";
    case TraceParseStatus::kInvalidCsv:
      return "invalid CSV";
    case TraceParseStatus::kUnsupportedVersion:
      return "unsupported version";
    case TraceParseStatus::kOutputTooSmall:
      return "event storage exhausted";
    case TraceParseStatus::kInvalidTrace:
      return "invalid event sequence";
  }
  return "unknown parse error";
}

std::string_view validation_error_text(TraceValidationError error) {
  switch (error) {
    case TraceValidationError::kNone:
      return "none";
    case TraceValidationError::kInvalidHeader:
      return "invalid header";
    case TraceValidationError::kNoEvents:
      return "no events";
    case TraceValidationError::kTimestampNotMonotonic:
      return "timestamp is not monotonic";
    case TraceValidationError::kCoordinateOutOfBounds:
      return "coordinate is out of bounds";
    case TraceValidationError::kUnexpectedDown:
      return "unexpected Down";
    case TraceValidationError::kUnexpectedMove:
      return "unexpected Move";
    case TraceValidationError::kUnexpectedUp:
      return "unexpected Up";
    case TraceValidationError::kUnclosedStroke:
      return "trace ends with an open stroke";
  }
  return "unknown validation error";
}

std::string_view source_text(TraceSource source) {
  return source == TraceSource::kRecorded ? "recorded" : "synthetic";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: tinydraw_ink_trace_check TRACE.csv\n";
    return 2;
  }

  std::ifstream input(argv[1], std::ios::binary);
  if (!input.is_open()) {
    std::cerr << "invalid ink trace: cannot open " << argv[1] << '\n';
    return 2;
  }
  const std::string csv{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (input.bad()) {
    std::cerr << "invalid ink trace: cannot read " << argv[1] << '\n';
    return 2;
  }

  const auto maximum_events = static_cast<std::size_t>(std::count(csv.begin(), csv.end(), '\n'));
  std::vector<TraceEvent> events(maximum_events);
  const auto parsed = tinydraw::vector_v2::parse_ink_trace_csv(csv, events);
  if (!parsed.ok()) {
    std::cerr << "invalid ink trace: " << parse_status_text(parsed.status) << " at line "
              << parsed.line;
    if (parsed.status == TraceParseStatus::kInvalidTrace) {
      std::cerr << " (" << validation_error_text(parsed.validation.error) << ')';
    }
    std::cerr << '\n';
    return 2;
  }

  const auto trace_events = std::span<const TraceEvent>{events}.first(parsed.event_count);
  const auto validation = tinydraw::vector_v2::validate_ink_trace(parsed.header, trace_events);
  if (!validation.ok()) {
    std::cerr << "invalid ink trace: " << validation_error_text(validation.error) << " at event "
              << validation.event_index << '\n';
    return 2;
  }

  std::size_t strokes = 0;
  std::uint64_t maximum_time_gap = 0;
  double maximum_space_gap = 0.0;
  std::optional<std::pair<std::uint16_t, std::uint16_t>> previous_sample;
  for (std::size_t index = 0; index < trace_events.size(); ++index) {
    const TraceEvent& event = trace_events[index];
    if (index != 0U) {
      maximum_time_gap = std::max(maximum_time_gap, event.t_us - trace_events[index - 1U].t_us);
    }
    if (event.kind == TraceEventKind::kDown) {
      ++strokes;
      previous_sample = {event.x, event.y};
      continue;
    }
    const double dx = static_cast<double>(event.x) - static_cast<double>(previous_sample->first);
    const double dy = static_cast<double>(event.y) - static_cast<double>(previous_sample->second);
    maximum_space_gap = std::max(maximum_space_gap, std::hypot(dx, dy));
    previous_sample = event.kind == TraceEventKind::kUp
                          ? std::nullopt
                          : std::optional{std::pair{event.x, event.y}};
  }

  std::cout << "valid name=" << parsed.header.name
            << " source=" << source_text(parsed.header.source) << " events=" << trace_events.size()
            << " strokes=" << strokes << " duration_us=" << trace_events.back().t_us
            << " max_time_gap_us=" << maximum_time_gap << " max_space_gap_px=" << std::fixed
            << std::setprecision(2) << maximum_space_gap << '\n';
  return 0;
}
