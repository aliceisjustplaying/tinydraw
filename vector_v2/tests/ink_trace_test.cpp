#include "tinydraw/vector_v2/ink_trace.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("ink trace CSV parses and serializes canonically without owned storage") {
  constexpr std::string_view csv =
      "magic,version,name,source,sample_rate_note\n"
      "TINYDRAW_INKTRACE,1,host-example,synthetic,SYNTHETIC placeholder 125Hz nominal\n"
      "t_us,kind,x,y\n"
      "0,Down,10,20\n"
      "8000,Move,30,40\n"
      "16000,Up,50,60\n";
  std::array<vector_v2::TraceEvent, 3> events{};

  const vector_v2::TraceParseResult parsed = vector_v2::parse_ink_trace_csv(csv, events);
  REQUIRE(parsed.ok());
  CHECK(parsed.header.magic == vector_v2::kInkTraceMagic);
  CHECK(parsed.header.version == vector_v2::kInkTraceVersion);
  CHECK(parsed.header.name == "host-example");
  CHECK(parsed.header.source == vector_v2::TraceSource::kSynthetic);
  CHECK(parsed.event_count == events.size());
  CHECK(events[1] == vector_v2::TraceEvent{8000, vector_v2::TraceEventKind::kMove, 30, 40});

  std::array<char, 512> serialized{};
  const vector_v2::TraceSerializeResult result =
      vector_v2::serialize_ink_trace_csv(parsed.header, events, serialized);
  REQUIRE(result.ok());
  CHECK(std::string_view(serialized.data(), result.bytes_written) == csv);
}

TEST_CASE("ink trace parsing reports caller capacity and malformed lifecycle") {
  constexpr std::string_view csv =
      "magic,version,name,source,sample_rate_note\n"
      "TINYDRAW_INKTRACE,1,capacity,recorded,125Hz touch sampler\n"
      "t_us,kind,x,y\n"
      "0,Down,10,20\n"
      "8,Up,10,20\n";
  std::array<vector_v2::TraceEvent, 1> short_storage{};
  const auto short_result = vector_v2::parse_ink_trace_csv(csv, short_storage);
  CHECK(short_result.status == vector_v2::TraceParseStatus::kOutputTooSmall);
  CHECK(short_result.event_count == 1U);
  CHECK(short_result.line == 5U);

  constexpr std::string_view unpaired =
      "magic,version,name,source,sample_rate_note\n"
      "TINYDRAW_INKTRACE,1,unpaired,synthetic,SYNTHETIC placeholder\n"
      "t_us,kind,x,y\n"
      "0,Move,10,20\n";
  std::array<vector_v2::TraceEvent, 2> storage{};
  const auto invalid = vector_v2::parse_ink_trace_csv(unpaired, storage);
  CHECK(invalid.status == vector_v2::TraceParseStatus::kInvalidTrace);
  CHECK(invalid.validation.error == vector_v2::TraceValidationError::kUnexpectedMove);
  CHECK(invalid.line == 4U);
}

TEST_CASE("ink trace validation reports malformed metadata on line two") {
  constexpr std::string_view csv =
      "magic,version,name,source,sample_rate_note\n"
      "WRONG,1,metadata,recorded,125Hz touch sampler\n"
      "t_us,kind,x,y\n"
      "0,Down,10,20\n"
      "8,Up,10,20\n";
  std::array<vector_v2::TraceEvent, 2> storage{};
  const auto invalid = vector_v2::parse_ink_trace_csv(csv, storage);
  CHECK(invalid.status == vector_v2::TraceParseStatus::kInvalidTrace);
  CHECK(invalid.validation.error == vector_v2::TraceValidationError::kInvalidHeader);
  CHECK(invalid.line == 2U);
}

TEST_CASE("ink trace validation enforces time coordinates and stroke edges") {
  const vector_v2::TraceHeader header{
      .name = "validation",
      .source = vector_v2::TraceSource::kSynthetic,
      .sample_rate_note = "SYNTHETIC placeholder",
  };
  std::array events{
      vector_v2::TraceEvent{0, vector_v2::TraceEventKind::kDown, 12, 14},
      vector_v2::TraceEvent{8, vector_v2::TraceEventKind::kMove, 20, 22},
      vector_v2::TraceEvent{16, vector_v2::TraceEventKind::kUp, 24, 26},
  };
  CHECK(vector_v2::validate_ink_trace(header, events).ok());

  events[1].t_us = 17;
  CHECK(vector_v2::validate_ink_trace(header, events).error ==
        vector_v2::TraceValidationError::kTimestampNotMonotonic);
  events[1].t_us = 8;
  events[1].x = vector_v2::kInkTraceWidth;
  CHECK(vector_v2::validate_ink_trace(header, events).error ==
        vector_v2::TraceValidationError::kCoordinateOutOfBounds);
  events[1].x = 20;
  events[2].kind = vector_v2::TraceEventKind::kDown;
  CHECK(vector_v2::validate_ink_trace(header, events).error ==
        vector_v2::TraceValidationError::kUnexpectedDown);
}

TEST_CASE("ink latency summary computes nearest-rank p50 p95 and max for every stage") {
  std::array<vector_v2::InkSampleTiming, 20> timings{};
  for (std::size_t index = 0; index < timings.size(); ++index) {
    const std::uint64_t step = index + 1U;
    timings[index] = {
        .t_event_us = 1'000,
        .t_consumed_us = 1'000 + step,
        .t_geometry_ready_us = 1'000 + 3U * step,
        .t_first_submit_us = 1'000 + 6U * step,
        .t_dma_complete_us = 1'000 + 10U * step,
    };
  }
  std::array<std::uint64_t, 20> scratch{};

  const auto summary = vector_v2::summarize_ink_latency(timings, scratch);
  REQUIRE(summary.has_value());
  CHECK(summary->event_to_consumed.p50_us == 10U);
  CHECK(summary->event_to_consumed.p95_us == 19U);
  CHECK(summary->event_to_consumed.max_us == 20U);
  CHECK(summary->consumed_to_geometry_ready.p50_us == 20U);
  CHECK(summary->consumed_to_geometry_ready.p95_us == 38U);
  CHECK(summary->consumed_to_geometry_ready.max_us == 40U);
  CHECK(summary->geometry_ready_to_first_submit.p50_us == 30U);
  CHECK(summary->geometry_ready_to_first_submit.p95_us == 57U);
  CHECK(summary->geometry_ready_to_first_submit.max_us == 60U);
  CHECK(summary->first_submit_to_dma_complete.p50_us == 40U);
  CHECK(summary->first_submit_to_dma_complete.p95_us == 76U);
  CHECK(summary->first_submit_to_dma_complete.max_us == 80U);
  CHECK(summary->event_to_dma_complete.p50_us == 100U);
  CHECK(summary->event_to_dma_complete.p95_us == 190U);
  CHECK(summary->event_to_dma_complete.max_us == 200U);
}

TEST_CASE("ink latency summary rejects incomplete storage and backward stage timestamps") {
  std::array timings{
      vector_v2::InkSampleTiming{.t_event_us = 10,
                                 .t_consumed_us = 20,
                                 .t_geometry_ready_us = 30,
                                 .t_first_submit_us = 40,
                                 .t_dma_complete_us = 50},
      vector_v2::InkSampleTiming{.t_event_us = 60,
                                 .t_consumed_us = 70,
                                 .t_geometry_ready_us = 80,
                                 .t_first_submit_us = 90,
                                 .t_dma_complete_us = 100},
  };
  std::array<std::uint64_t, 1> short_scratch{};
  CHECK_FALSE(vector_v2::summarize_ink_latency(timings, short_scratch));

  std::array<std::uint64_t, 2> scratch{};
  timings[1].t_geometry_ready_us = 69;
  CHECK_FALSE(vector_v2::summarize_ink_latency(timings, scratch));
}

TEST_CASE("ink stroke counters expose coalescing and edge conservation") {
  vector_v2::InkStrokeCounters counters{
      .received_events = 10,
      .consumed_events = 8,
      .coalesced_events = 2,
      .trace_down_events = 1,
      .trace_up_events = 1,
      .consumed_down_events = 1,
      .consumed_up_events = 1,
      .max_consumed_sample_time_gap_us = 8'000,
      .max_consumed_sample_space_gap_px = 12.5F,
  };
  CHECK(counters.valid());
  CHECK(counters.down_up_conserved());
  CHECK(counters.coalesced_ratio() == doctest::Approx(0.2));

  counters.consumed_up_events = 0;
  CHECK_FALSE(counters.down_up_conserved());
  counters.coalesced_events = 11;
  CHECK_FALSE(counters.valid());
  CHECK(vector_v2::InkStrokeCounters{}.coalesced_ratio() == 0.0);
}
