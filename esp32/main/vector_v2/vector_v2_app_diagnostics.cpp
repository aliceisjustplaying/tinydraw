#include "vector_v2_app_diagnostics.h"

#include <cstdio>

#include "co5300_panel_transport.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {
namespace {

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

}  // namespace

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

}  // namespace tinydraw::esp32
