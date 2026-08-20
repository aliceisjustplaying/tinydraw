#include "vector_v2_app_diagnostics.h"

#include <algorithm>
#include <cstdio>

#include "co5300_panel_transport.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {
const char* zoom_name(vector_v2::ZoomLevel zoom) {
  switch (zoom) {
    case vector_v2::ZoomLevel::k25Percent:
      return "25";
    case vector_v2::ZoomLevel::k50Percent:
      return "50";
    case vector_v2::ZoomLevel::k100Percent:
      return "100";
    case vector_v2::ZoomLevel::k200Percent:
      return "200";
    case vector_v2::ZoomLevel::k400Percent:
      return "400";
  }
  return "unknown";
}

void PanMetrics::include(const LivePresentationTiming& timing) {
  if (timing.compose_pending) {
    return;
  }
  if (!timing.passed) {
    ++failures;
    return;
  }
  const auto compose = static_cast<std::uint32_t>(timing.compose_us);
  const auto present = static_cast<std::uint32_t>(timing.complete_us);
  const auto tear_wait = static_cast<std::uint32_t>(timing.tear_wait_us);
  compose_total_us += compose;
  present_total_us += present;
  tear_wait_total_us += tear_wait;
  ++frames;
  reused_frames += timing.frame_reused;
  compose_max_us = std::max(compose_max_us, compose);
  present_max_us = std::max(present_max_us, present);
  tear_wait_max_us = std::max(tear_wait_max_us, tear_wait);
}

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld scroll_us=%lld "
      "exposed_compose_us=%lld chrome_us=%lld chrome_prepare_us=%lld chrome_stage_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld "
      "transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu pushes=%lu tear_wait_us=%lld tear_edge_isr_to_resume_us=%lu "
      "tear_edge_observed=%u tear_edge_wait_resumed=%u tear_edge_timeout=%u "
      "tear_heal_attempted=%u "
      "tear_heal_command_sent=%u presentation_experiment=%s te_edge=%s "
      "clock_mhz=%d "
      "frame_reused=%u pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.scroll_us),
      static_cast<long long>(timing.exposed_compose_us), static_cast<long long>(timing.chrome_us),
      static_cast<long long>(timing.chrome_prepare_us),
      static_cast<long long>(timing.chrome_stage_us),
      static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.tile_pixels),
      static_cast<unsigned long>(timing.uniform_pixels),
      static_cast<unsigned long>(timing.overview_pixels),
      static_cast<unsigned long>(timing.fallback_pixels),
      static_cast<unsigned long>(timing.resident_tiles),
      static_cast<unsigned long>(timing.fallback_tiles), static_cast<unsigned long>(timing.pushes),
      static_cast<long long>(timing.tear_wait_us),
      static_cast<unsigned long>(timing.tear_edge_isr_to_resume_us), timing.tear_edge_observed,
      timing.tear_edge_wait_resumed, timing.tear_edge_timed_out, timing.tear_heal_attempted,
      timing.tear_heal_command_sent, presentation_experiment_name(), selected_tear_edge_name(),
      kCo5300ClockMHz, timing.frame_reused, timing.passed);
}

void print_pan_baseline(const VectorV2Presenter& presenter, const PanMetrics& metrics) {
  std::printf(
      "TINYDRAW_PAN_BASELINE zoom=%s x=%d y=%d frames=%lu reused=%lu "
      "compose_avg_us=%llu compose_max_us=%lu present_avg_us=%llu present_max_us=%lu "
      "tear_wait_avg_us=%llu tear_wait_max_us=%lu failures=%lu\n",
      zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<unsigned long>(metrics.frames), static_cast<unsigned long>(metrics.reused_frames),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.compose_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.compose_max_us),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.present_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.present_max_us),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.tear_wait_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.tear_wait_max_us),
      static_cast<unsigned long>(metrics.failures));
}

void print_lift_baseline(const PendingStrokeReport& report, std::int64_t poll_started_us,
                         std::int64_t poll_completed_us, std::uint32_t reports_dropped) {
  const std::int64_t measured_phase_us = report.finish_preview_us + report.builder_finish_us +
                                         report.append_us + report.refresh_wall_us +
                                         report.stroke_logging_us;
  const std::int64_t detected_to_poll_us = poll_started_us - report.detected_us;
  std::printf(
      "TINYDRAW_LIFT_BASELINE id=%lu finish_preview_us=%lld builder_finish_us=%lld "
      "append_us=%lld publication=operation refresh_wall_us=%lld refresh_x0=%d refresh_y0=%d "
      "refresh_x1=%d refresh_y1=%d refresh_compose_us=%lld "
      "refresh_first_submit_us=%lld refresh_first_complete_us=%lld "
      "refresh_transfer_wait_us=%lld stroke_logging_us=%lld "
      "detected_to_poll_start_us=%lld detected_to_poll_complete_us=%lld poll_read_us=%lld "
      "unattributed_tail_us=%lld reports_dropped=%lu committed=%u refresh=%u "
      "commit_failed=%u\n",
      static_cast<unsigned long>(report.id), static_cast<long long>(report.finish_preview_us),
      static_cast<long long>(report.builder_finish_us), static_cast<long long>(report.append_us),
      static_cast<long long>(report.refresh_wall_us), report.refresh_level_bounds.x0,
      report.refresh_level_bounds.y0, report.refresh_level_bounds.x1,
      report.refresh_level_bounds.y1, static_cast<long long>(report.refresh.compose_us),
      static_cast<long long>(report.refresh.first_submit_us),
      static_cast<long long>(report.refresh.first_complete_us),
      static_cast<long long>(report.refresh.complete_us),
      static_cast<long long>(report.stroke_logging_us), static_cast<long long>(detected_to_poll_us),
      static_cast<long long>(poll_completed_us - report.detected_us),
      static_cast<long long>(poll_completed_us - poll_started_us),
      static_cast<long long>(detected_to_poll_us - measured_phase_us),
      static_cast<unsigned long>(reports_dropped), report.committed, report.refresh.passed,
      report.commit_failed);
}

void print_stroke(const PendingStrokeReport& report) {
  std::printf(
      "TINYDRAW_LIVE_STROKE revision=%lu operations=%lu samples=%lu append_us=%lld "
      "publication=operation refresh_compose_us=%lld refresh_complete_us=%lld ink_samples=%lu "
      "read_submit_avg_us=%llu read_submit_max_us=%lu read_complete_avg_us=%llu "
      "read_complete_max_us=%lu submit_over_16ms=%lu complete_over_33ms=%lu "
      "presentation_failures=%lu poll_max_us=%lu touch_errors=%lu touch_overflows=%lu "
      "touch_resyncs=%lu touch_moves_coalesced=%lu touch_events=%lu touch_down=%lu touch_up=%lu "
      "touch_events_ge_8ms=%lu touch_event_age_max_us=%lu free_psram=%lu largest_psram=%lu "
      "authority_match=%u\n",
      static_cast<unsigned long>(report.revision.value),
      static_cast<unsigned long>(report.operation_count),
      static_cast<unsigned long>(report.sample_count), static_cast<long long>(report.append_us),
      static_cast<long long>(report.refresh.compose_us),
      static_cast<long long>(report.refresh.complete_us),
      static_cast<unsigned long>(report.metrics.samples),
      static_cast<unsigned long long>(
          report.metrics.samples == 0U ? 0U
                                       : report.metrics.submit_total_us / report.metrics.samples),
      static_cast<unsigned long>(report.metrics.submit_max_us),
      static_cast<unsigned long long>(
          report.metrics.samples == 0U ? 0U
                                       : report.metrics.complete_total_us / report.metrics.samples),
      static_cast<unsigned long>(report.metrics.complete_max_us),
      static_cast<unsigned long>(report.metrics.submit_over_16ms),
      static_cast<unsigned long>(report.metrics.complete_over_33ms),
      static_cast<unsigned long>(report.metrics.failures),
      static_cast<unsigned long>(report.poll_max_us),
      static_cast<unsigned long>(report.touch.errors),
      static_cast<unsigned long>(report.touch.queue_overflows),
      static_cast<unsigned long>(report.touch.queue_resyncs),
      static_cast<unsigned long>(report.touch.moves_coalesced),
      static_cast<unsigned long>(report.touch.events_consumed),
      static_cast<unsigned long>(report.touch.down_events),
      static_cast<unsigned long>(report.touch.up_events),
      static_cast<unsigned long>(report.touch.events_at_least_8ms_old),
      static_cast<unsigned long>(report.touch.maximum_event_age_us),
      static_cast<unsigned long>(report.free_psram),
      static_cast<unsigned long>(report.largest_psram), report.authority_match);
}

}  // namespace tinydraw::esp32
