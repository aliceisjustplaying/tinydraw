#pragma once

#include <cstddef>
#include <cstdint>

#include "vector_v2_live_stroke_session.h"
#include "vector_v2_presenter.h"
#include "vector_v2_touch_sampler.h"

namespace tinydraw::esp32 {

struct PanMetrics {
  std::uint64_t compose_total_us = 0;
  std::uint64_t present_total_us = 0;
  std::uint64_t tear_wait_total_us = 0;
  std::uint32_t frames = 0;
  std::uint32_t reused_frames = 0;
  std::uint32_t compose_max_us = 0;
  std::uint32_t present_max_us = 0;
  std::uint32_t tear_wait_max_us = 0;
  std::uint32_t failures = 0;

  void include(const LivePresentationTiming& timing);
  void reset() { *this = {}; }
};

struct PendingStrokeReport {
  vector_v2::DocumentRevision revision{};
  vector_v2::PixelRect refresh_level_bounds{};
  LivePresentationTiming refresh{};
  LiveStrokeMetrics metrics{};
  TouchSamplerMetrics touch{};
  std::size_t operation_count = 0;
  std::size_t sample_count = 0;
  std::size_t free_psram = 0;
  std::size_t largest_psram = 0;
  std::int64_t detected_us = 0;
  std::int64_t finish_preview_us = 0;
  std::int64_t builder_finish_us = 0;
  std::int64_t append_us = 0;
  std::int64_t refresh_wall_us = 0;
  std::int64_t stroke_logging_us = 0;
  std::uint32_t id = 0;
  std::uint32_t poll_max_us = 0;
  bool authority_match = false;
  bool committed = false;
  bool commit_failed = false;
  bool pending = false;
};

[[nodiscard]] const char* zoom_name(vector_v2::ZoomLevel zoom);

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing);
void print_pan_baseline(const VectorV2Presenter& presenter, const PanMetrics& metrics);
void print_lift_baseline(const PendingStrokeReport& report, std::int64_t poll_started_us,
                         std::int64_t poll_completed_us, std::uint32_t reports_dropped);
void print_stroke(const PendingStrokeReport& report);

}  // namespace tinydraw::esp32
