#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

// PROTOTYPE ONLY: measures how much 3x3 raster-prefetch runway remains while a
// low-priority vector renderer fills missing cache bands. This does not
// implement cache rebasing or production persistence.
class InteractivePanBenchmark;

// Optional access to panel transfer submit/completion sequence numbers so zoom
// endpoints can be measured at physical transfer completion rather than at
// queue submission. Sequences are 1-based and complete in submission order.
struct DisplayTransferTelemetry {
  std::uint32_t (*submit_count)(void* context) = nullptr;
  std::uint32_t (*complete_count)(void* context) = nullptr;
  // Returns the low 32 bits of the esp_timer completion time for `sequence`,
  // or -1 when it has not completed or has been evicted. Callers use modular
  // subtraction for sub-second benchmark intervals.
  std::int64_t (*complete_time_us)(void* context, std::uint32_t sequence) = nullptr;
  void* context = nullptr;
};

// Elapsed microseconds since the zoom input event for the latest transition to
// each zoom level. Zero means "not reached/recorded". Strips are pushed
// center-out; "first strip" is the first center-out strip, and "last visible"
// covers every strip of the physically presented region.
struct ZoomTransitionTiming {
  std::uint32_t cancel_done_us = 0;
  std::uint32_t first_strip_ready_us = 0;
  std::uint32_t first_strip_submit_us = 0;
  std::uint32_t first_strip_complete_us = 0;
  std::uint32_t last_visible_submit_us = 0;
  std::uint32_t last_visible_complete_us = 0;
  std::uint32_t fallback_ready_us = 0;
  std::uint32_t settled_us = 0;
};

[[nodiscard]] InteractivePanBenchmark* start_interactive_pan_benchmark(
    VectorDocument& document, WorldCanvas& world, std::span<std::uint16_t> materialization_storage,
    std::span<std::uint16_t> render_buffer, int presented_rows, DisplayBackend& display,
    void (*refinement_published)(void*) = nullptr, void* refinement_published_context = nullptr,
    DisplayTransferTelemetry transfer_telemetry = {});

// Prepares and physically presents the requested zoom level as center-out
// nearest-resampled strips, then records completion-based endpoints. Returns
// false when the transition cannot be published from valid source pixels.
[[nodiscard]] bool interactive_pan_benchmark_set_zoom(InteractivePanBenchmark& benchmark,
                                                      int zoom_percent);
[[nodiscard]] bool interactive_pan_benchmark_last_zoom_timing(
    const InteractivePanBenchmark& benchmark, int zoom_percent, ZoomTransitionTiming& timing);
// Records the first physical display presentation for pushes made outside
// interactive_pan_benchmark_set_zoom, such as the initial-atlas presentation.
void interactive_pan_benchmark_record_zoom_present(InteractivePanBenchmark& benchmark);

// Pauses and joins refinement before the document is mutated. Returns false
// rather than allowing live raster ink to diverge from vector authority.
[[nodiscard]] bool interactive_pan_benchmark_begin_stroke(InteractivePanBenchmark& benchmark);
[[nodiscard]] StrokeSample interactive_pan_benchmark_map_sample(
    const InteractivePanBenchmark& benchmark, Point screen_point, float screen_radius);
// `visible_raster_current` is true only when the live raster path has already
// merged the committed stroke into every pixel of the captured viewport.
void interactive_pan_benchmark_commit_stroke(InteractivePanBenchmark& benchmark,
                                             bool visible_raster_current);
void interactive_pan_benchmark_cancel_stroke(InteractivePanBenchmark& benchmark);
void interactive_pan_benchmark_record_draw_update(InteractivePanBenchmark& benchmark,
                                                  std::uint32_t elapsed_us);

void interactive_pan_benchmark_begin_pan(InteractivePanBenchmark& benchmark, ViewOrigin origin,
                                         std::uint32_t event_us);
// Called while holding the cache lock. Rejects views containing invalid bands
// rather than publishing checkerboard or unproven raster pixels.
[[nodiscard]] bool interactive_pan_benchmark_view_ready(const InteractivePanBenchmark& benchmark,
                                                        ViewOrigin origin);
[[nodiscard]] bool interactive_pan_benchmark_view_changed(InteractivePanBenchmark& benchmark,
                                                          ViewOrigin origin);
void interactive_pan_benchmark_record_frame(InteractivePanBenchmark& benchmark, ViewOrigin origin,
                                            std::uint32_t event_us, std::uint32_t direct_present_us,
                                            std::uint32_t event_to_present_us);
void interactive_pan_benchmark_end_pan(InteractivePanBenchmark& benchmark);

// Cache publication and the unchanged direct display push must share this lock.
void interactive_pan_benchmark_lock_cache(InteractivePanBenchmark& benchmark);
void interactive_pan_benchmark_unlock_cache(InteractivePanBenchmark& benchmark);

// Cancels background rendering, then persists the accumulated report to the
// final 8 KiB of the export partition. Intended for the XL size action.
[[nodiscard]] bool finish_interactive_pan_benchmark(InteractivePanBenchmark& benchmark);

}  // namespace tinydraw::esp32
