#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"
#include "tinydraw/graphics/world_canvas.h"

namespace tinydraw::esp32 {

// PROTOTYPE ONLY: measures how much 3x3 raster-prefetch runway remains while a
// low-priority vector renderer fills missing cache bands. This does not
// implement cache rebasing or production persistence.
class InteractivePanBenchmark;

[[nodiscard]] InteractivePanBenchmark* start_interactive_pan_benchmark(
    VectorDocument& document, WorldCanvas& world, std::span<std::uint16_t> materialization_storage,
    std::span<std::uint16_t> render_buffer, int presented_rows);

[[nodiscard]] bool interactive_pan_benchmark_set_zoom(InteractivePanBenchmark& benchmark,
                                                      int zoom_percent);
// Records the first physical display presentation after a zoom request.
void interactive_pan_benchmark_record_zoom_present(InteractivePanBenchmark& benchmark);

// Pauses and joins refinement before the document is mutated. Returns false
// rather than allowing live raster ink to diverge from vector authority.
[[nodiscard]] bool interactive_pan_benchmark_begin_stroke(InteractivePanBenchmark& benchmark);
[[nodiscard]] StrokeSample interactive_pan_benchmark_map_sample(
    const InteractivePanBenchmark& benchmark, Point screen_point, float screen_radius);
void interactive_pan_benchmark_commit_stroke(InteractivePanBenchmark& benchmark);
void interactive_pan_benchmark_cancel_stroke(InteractivePanBenchmark& benchmark);
void interactive_pan_benchmark_record_draw_update(InteractivePanBenchmark& benchmark,
                                                  std::uint32_t elapsed_us);

void interactive_pan_benchmark_begin_pan(InteractivePanBenchmark& benchmark, ViewOrigin origin,
                                         std::uint32_t event_us);
void interactive_pan_benchmark_view_changed(InteractivePanBenchmark& benchmark, ViewOrigin origin);
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
