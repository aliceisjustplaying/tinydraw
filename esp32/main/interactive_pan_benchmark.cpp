#include "interactive_pan_benchmark.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <new>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/document/stroke_macrogrid.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/raster_materializer.h"
#include "tinydraw/graphics/settled_renderer.h"
#include "tinydraw/graphics/stroke_lod.h"
#include "tinydraw/graphics/viewport_renderer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::size_t kStrokeCount = 1'000U;
constexpr std::size_t kRequiredSampleCapacity = 24'576U;
constexpr std::size_t kSettledLodSampleCapacity = 12'288U;
constexpr std::uint32_t kWorkloadSeed = 7U;
constexpr int kCellColumns = 3;
constexpr int kCellRows = 3;
constexpr int kBandRows = 32;
constexpr int kBandsPerCell = kCanvasHeight / kBandRows;
constexpr int kCellCount = kCellColumns * kCellRows;
constexpr int kJobCount = kCellCount * kBandsPerCell;
constexpr int kCenterOriginX = kCanvasWidth;
constexpr int kCenterOriginY = kCanvasHeight;
constexpr std::size_t kTimingCapacity = 1'024U;
constexpr std::size_t kReportBytes = 8'192U;
constexpr std::array kZoomPercents{50, 100, 200};
constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr float kCanonicalHaloPixels = 1.75F;
constexpr std::uint8_t kInvalidReady = 0U;
constexpr std::uint8_t kDerivedReady = 1U;
constexpr std::uint8_t kSettledReady = 2U;
constexpr std::uint8_t kExactReady = 3U;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
// One 368-wide, 22-row strip is exactly one 8,192-pixel panel transaction, so
// each strip's transfer-completion callback is an honest physical endpoint.
constexpr int kStripRows = 22;
constexpr std::uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr esp_partition_subtype_t kExportSubtype = static_cast<esp_partition_subtype_t>(0x41);
constexpr std::size_t kIndexWords = (1'100U + 63U) / 64U;

struct TimingStorage {
  std::array<std::array<std::uint32_t, kTimingCapacity>, kZoomPercents.size()> direct{};
  std::array<std::array<std::uint32_t, kTimingCapacity>, kZoomPercents.size()> event{};
  std::array<std::uint32_t, kTimingCapacity> draw{};
};

struct ZoomMetrics {
  std::uint32_t attempts = 0;
  std::uint32_t failed_attempts = 0;
  std::uint32_t maximum_cancellation_us = 0;
  std::uint32_t gestures = 0;
  std::uint32_t frames = 0;
  std::uint32_t timing_samples = 0;
  std::uint32_t miss_frames = 0;
  std::uint32_t maximum_missing_pixels = 0;
  std::uint32_t maximum_velocity_px_s = 0;
  std::uint64_t direct_total_us = 0;
  std::uint64_t event_total_us = 0;
  std::uint32_t previous_event_us = 0;
  ViewOrigin previous_origin{};
};

struct JobRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
};

int zoom_index(int percent) {
  for (std::size_t index = 0; index < kZoomPercents.size(); ++index) {
    if (kZoomPercents[index] == percent) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

JobRect job_rect(int job) {
  const int cell = job / kBandsPerCell;
  const int band = job % kBandsPerCell;
  const int cell_x = cell % kCellColumns;
  const int cell_y = cell / kCellColumns;
  return {
      .x0 = cell_x * kCanvasWidth,
      .y0 = cell_y * kCanvasHeight + band * kBandRows,
      .x1 = (cell_x + 1) * kCanvasWidth,
      .y1 = cell_y * kCanvasHeight + (band + 1) * kBandRows,
  };
}

bool intersects(JobRect left, JobRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

int interval_distance(int left0, int left1, int right0, int right1) {
  if (left1 <= right0) {
    return right0 - left1;
  }
  if (right1 <= left0) {
    return left0 - right1;
  }
  return 0;
}

void benchmark_yield(void*) { vTaskDelay(pdMS_TO_TICKS(1)); }

std::uint64_t benchmark_clock_us(void*) { return static_cast<std::uint64_t>(esp_timer_get_time()); }

// Maps presentation position 0..strip_count-1 onto strip indices center-out:
// center, center+1, center-1, center+2, ... Exactly one overflow can occur at
// the final position when strip_count is even; it mirrors to the uncovered
// opposite side.
int center_out_strip(int position, int strip_count) {
  const int center = strip_count / 2;
  const int offset = (position + 1) / 2;
  const int candidate = position % 2 == 1 ? center + offset : center - offset;
  if (candidate >= 0 && candidate < strip_count) {
    return candidate;
  }
  return position % 2 == 1 ? center - offset : center + offset;
}

Camera atlas_camera(float zoom) {
  const double focus_x = static_cast<double>(kCenterOriginX) + kCanvasWidth / 2.0;
  const double focus_y = static_cast<double>(kCenterOriginY) + kCanvasHeight / 2.0;
  return {
      .x = focus_x - static_cast<double>(WorldCanvas::kWidth) / (2.0 * zoom),
      .y = focus_y - static_cast<double>(WorldCanvas::kHeight) / (2.0 * zoom),
      .zoom = zoom,
  };
}

Camera job_camera(Camera atlas, int job) {
  const JobRect rect = job_rect(job);
  const int cell = job / kBandsPerCell;
  const int cell_y = cell / kCellColumns;
  return {
      .x = atlas.x + static_cast<double>(rect.x0) / atlas.zoom,
      .y = atlas.y + static_cast<double>(cell_y * kCanvasHeight) / atlas.zoom,
      .zoom = atlas.zoom,
  };
}

RectF camera_world_rect(Camera camera, Rect pixels) {
  const double inverse_zoom = 1.0 / static_cast<double>(camera.zoom);
  return {
      .x0 = static_cast<float>(camera.x + pixels.x0 * inverse_zoom),
      .y0 = static_cast<float>(camera.y + pixels.y0 * inverse_zoom),
      .x1 = static_cast<float>(camera.x + pixels.x1 * inverse_zoom),
      .y1 = static_cast<float>(camera.y + pixels.y1 * inverse_zoom),
  };
}

bool region_proven_by_source(const VectorDocument& document, Camera source_camera,
                             Camera destination_camera, Rect region) {
  const RectF source_world{
      .x0 = static_cast<float>(source_camera.x),
      .y0 = static_cast<float>(source_camera.y),
      .x1 = static_cast<float>(source_camera.x + WorldCanvas::kWidth / source_camera.zoom),
      .y1 = static_cast<float>(source_camera.y + WorldCanvas::kHeight / source_camera.zoom),
  };
  RectF destination_world = camera_world_rect(destination_camera, region);
  const float destination_halo = kCanonicalHaloPixels / destination_camera.zoom;
  destination_world.x0 -= destination_halo;
  destination_world.y0 -= destination_halo;
  destination_world.x1 += destination_halo;
  destination_world.y1 += destination_halo;
  const float source_halo = kCanonicalHaloPixels / source_camera.zoom;
  const RectF proven_source{
      .x0 = source_world.x0 + source_halo,
      .y0 = source_world.y0 + source_halo,
      .x1 = source_world.x1 - source_halo,
      .y1 = source_world.y1 - source_halo,
  };
  for (const VectorStroke& stroke : document.strokes()) {
    if (rects_intersect(stroke.bounds, destination_world) &&
        (stroke.bounds.x0 < proven_source.x0 || stroke.bounds.y0 < proven_source.y0 ||
         stroke.bounds.x1 > proven_source.x1 || stroke.bounds.y1 > proven_source.y1)) {
      return false;
    }
  }
  return true;
}

bool populate_benchmark_document(VectorDocument& document, RealisticWorkloadStats& stats) {
  const RectF area{
      .x0 = static_cast<float>(kCenterOriginX),
      .y0 = static_cast<float>(kCenterOriginY),
      .x1 = static_cast<float>(kCenterOriginX + kCanvasWidth),
      .y1 = static_cast<float>(kCenterOriginY + kCanvasHeight),
  };
  return populate_realistic_handwriting(document, kWorkloadSeed, kStrokeCount, area, &stats);
}

void append_report(char* report, std::size_t& size, const char* format, ...) {
  if (size + 1U >= kReportBytes) {
    return;
  }
  va_list arguments;
  va_start(arguments, format);
  const std::size_t remaining = kReportBytes - size;
  const int written = std::vsnprintf(report + size, remaining, format, arguments);
  va_end(arguments);
  if (written > 0) {
    size += std::min(static_cast<std::size_t>(written), remaining - 1U);
  }
}

std::uint32_t quantile(std::array<std::uint32_t, kTimingCapacity>& values, std::size_t count,
                       std::size_t numerator, std::size_t denominator) {
  if (count == 0U) {
    return 0U;
  }
  std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
  const std::size_t index = std::min(count - 1U, ((count - 1U) * numerator) / denominator);
  return values[index];
}

}  // namespace

class InteractivePanBenchmark {
 public:
  InteractivePanBenchmark(VectorDocument& document_value, WorldCanvas& world_value,
                          std::span<std::uint16_t> materialization_storage_value,
                          std::span<std::uint16_t> render_buffer_value, int presented_rows_value,
                          DisplayBackend& display_value, std::span<std::uint8_t> scratch_value,
                          TimingStorage& timings_value, std::span<std::uint64_t> index_cells_value,
                          std::span<std::uint64_t> index_query_value, void* renderer_storage,
                          void (*refinement_published_value)(void*),
                          void* refinement_published_context_value,
                          DisplayTransferTelemetry transfer_telemetry_value)
      : document(document_value),
        world(world_value),
        materialization_storage(materialization_storage_value),
        render_buffer(render_buffer_value),
        presented_rows(presented_rows_value),
        display(display_value),
        scratch(scratch_value),
        timings(timings_value),
        macrogrid(index_cells_value, index_query_value, document_value.stroke_capacity()),
        renderer(new (renderer_storage) ViewportRenderer(scratch_value)),
        renderer_storage_(renderer_storage),
        refinement_published(refinement_published_value),
        refinement_published_context(refinement_published_context_value),
        transfer_telemetry(transfer_telemetry_value) {
    for (auto& value : ready) {
      value.store(0U);
    }
    for (auto& value : center_ready_us) {
      value.store(0U);
    }
    for (auto& value : full_ready_us) {
      value.store(0U);
    }
  }

  VectorDocument& document;
  WorldCanvas& world;
  std::span<std::uint16_t> materialization_storage;
  std::span<std::uint16_t> render_buffer;
  int presented_rows = 0;
  DisplayBackend& display;
  std::span<std::uint8_t> scratch;
  TimingStorage& timings;
  StrokeMacrogrid macrogrid;
  bool macrogrid_valid = true;
  bool settled_lod_high_quality = false;
  ViewportRenderer* renderer = nullptr;
  void* renderer_storage_ = nullptr;
  std::uint64_t* index_storage_ = nullptr;
  std::span<std::uint8_t> settled_scratch{};
  std::uint8_t* settled_scratch_storage_ = nullptr;
  std::span<StrokeSample> settled_lod_samples{};
  std::span<std::uint32_t> settled_lod_first{};
  std::span<std::uint16_t> settled_lod_count{};
  StrokeSample* settled_lod_samples_storage_ = nullptr;
  std::uint32_t* settled_lod_first_storage_ = nullptr;
  std::uint16_t* settled_lod_count_storage_ = nullptr;
  void (*refinement_published)(void*) = nullptr;
  void* refinement_published_context = nullptr;
  DisplayTransferTelemetry transfer_telemetry{};
  RealisticWorkloadStats workload_stats{};
  SemaphoreHandle_t cache_mutex = nullptr;
  TaskHandle_t render_task = nullptr;
  std::array<std::atomic<std::uint8_t>, kJobCount> ready{};
  std::array<std::atomic<std::uint32_t>, kJobCount> job_revision{};
  std::array<ZoomMetrics, kZoomPercents.size()> metrics{};
  std::size_t draw_samples = 0U;
  std::uint32_t draw_updates = 0U;
  std::uint64_t draw_total_us = 0U;
  std::uint32_t draw_max_us = 0U;
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> center_ready_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> full_ready_us{};
  std::atomic<std::uint32_t> requested_generation{0U};
  std::atomic<std::uint32_t> generation_started_us{0U};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> first_valid_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> fallback_ready_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> settled_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> cancel_done_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> first_strip_ready_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> first_strip_submit_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> first_strip_complete_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> last_visible_submit_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> last_visible_complete_us{};
  std::atomic<int> requested_zoom_index{1};
  std::atomic<int> active_zoom_index{1};
  Camera active_atlas = atlas_camera(1.0F);
  // After the first post-initialization zoom, materialization_storage owns an
  // immutable, complete atlas. Keeping that provenance root intact lets later
  // transitions replace the active (other) arena in place even when settled or
  // exact work on an intermediate zoom was canceled.
  Camera fallback_source_atlas = atlas_camera(1.0F);
  std::array<std::uint8_t, kJobCount> fallback_source_ready{};
  std::array<std::uint32_t, kJobCount> fallback_source_job_revision{};
  std::array<bool, kJobCount> fallback_repair_pending{};
  std::uint32_t fallback_source_document_revision = 0U;
  std::uint32_t fallback_repair_target_revision = 0U;
  bool fallback_source_pinned = false;
  std::atomic<int> view_x{kCenterOriginX};
  std::atomic<int> view_y{kCenterOriginY};
  std::atomic<bool> rendering{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> zoom_presentation_pending{false};
  std::atomic<bool> stroke_mutation_active{false};
  std::atomic<bool> pan_active{false};
  std::uint32_t pan_rejected_views = 0U;
  std::uint32_t pan_maximum_missing_pixels = 0U;
  bool has_complete_initial_atlas = false;
  std::atomic<std::uint32_t> document_revision{1U};
  std::atomic<bool> finished{false};
};

namespace {

bool build_settled_lod(InteractivePanBenchmark& benchmark, bool high_quality) {
  const float center_error = high_quality ? 1.0F : 2.0F;
  const float radius_error = high_quality ? 0.375F : 0.75F;
  auto storage = std::span(benchmark.settled_lod_samples_storage_, kSettledLodSampleCapacity);
  std::size_t output_count = 0U;
  const auto strokes = benchmark.document.strokes();
  if (strokes.size() > benchmark.settled_lod_first.size() ||
      strokes.size() > benchmark.settled_lod_count.size()) {
    return false;
  }
  for (std::size_t stroke_index = 0; stroke_index < strokes.size(); ++stroke_index) {
    const auto input = benchmark.document.samples(strokes[stroke_index]);
    if (output_count > storage.size()) {
      return false;
    }
    const auto available = storage.subspan(output_count);
    const auto simplified = simplify_stroke_samples(input, available, center_error, radius_error);
    if (simplified.empty() || simplified.size() > UINT16_MAX) {
      return false;
    }
    benchmark.settled_lod_first[stroke_index] = static_cast<std::uint32_t>(output_count);
    benchmark.settled_lod_count[stroke_index] = static_cast<std::uint16_t>(simplified.size());
    output_count += simplified.size();
  }
  std::printf(
      "TINYDRAW_SETTLED_LOD quality=%s center_error_milli=%lu radius_error_milli=%lu "
      "input=%lu output=%lu bytes=%lu\n",
      high_quality ? "high" : "normal", static_cast<unsigned long>(center_error * 1'000.0F),
      static_cast<unsigned long>(radius_error * 1'000.0F),
      static_cast<unsigned long>(benchmark.document.sample_count()),
      static_cast<unsigned long>(output_count),
      static_cast<unsigned long>(output_count * sizeof(StrokeSample)));
  benchmark.settled_lod_samples = storage.first(output_count);
  benchmark.settled_lod_high_quality = high_quality;
  return true;
}

bool region_contains_ink(const VectorDocument& document, Camera camera, JobRect pixels) {
  const float halo = kCanonicalHaloPixels / camera.zoom;
  RectF world = camera_world_rect(camera, {pixels.x0, pixels.y0, pixels.x1, pixels.y1});
  world.x0 -= halo;
  world.y0 -= halo;
  world.x1 += halo;
  world.y1 += halo;
  for (const VectorStroke& stroke : document.strokes()) {
    if (rects_intersect(stroke.bounds, world)) {
      return true;
    }
  }
  return false;
}

bool source_region_materialized(const InteractivePanBenchmark& benchmark, Camera source_camera,
                                Camera destination_camera, Rect destination_region,
                                const std::array<std::uint8_t, kJobCount>& source_ready,
                                const std::array<std::uint32_t, kJobCount>& source_job_revision,
                                std::uint32_t source_document_revision) {
  if (!region_proven_by_source(benchmark.document, source_camera, destination_camera,
                               destination_region)) {
    return false;
  }
  const RectF world = camera_world_rect(destination_camera, destination_region);
  // Coordinates below are already in source pixels; bilinear filtering needs
  // one complete source-pixel halo regardless of source zoom.
  constexpr float sampling_halo = 1.0F;
  const int source_x0 = static_cast<int>(std::floor(
      (static_cast<double>(world.x0) - source_camera.x) * source_camera.zoom - sampling_halo));
  const int source_y0 = static_cast<int>(std::floor(
      (static_cast<double>(world.y0) - source_camera.y) * source_camera.zoom - sampling_halo));
  const int source_x1 = static_cast<int>(std::ceil(
      (static_cast<double>(world.x1) - source_camera.x) * source_camera.zoom + sampling_halo));
  const int source_y1 = static_cast<int>(std::ceil(
      (static_cast<double>(world.y1) - source_camera.y) * source_camera.zoom + sampling_halo));
  const JobRect source_pixels{
      .x0 = std::clamp(source_x0, 0, WorldCanvas::kWidth),
      .y0 = std::clamp(source_y0, 0, WorldCanvas::kHeight),
      .x1 = std::clamp(source_x1, 0, WorldCanvas::kWidth),
      .y1 = std::clamp(source_y1, 0, WorldCanvas::kHeight),
  };
  for (int job = 0; job < kJobCount; ++job) {
    const auto slot = static_cast<std::size_t>(job);
    const JobRect source_job = job_rect(job);
    if (!intersects(source_job, source_pixels)) {
      continue;
    }
    const bool current = source_ready[slot] != kInvalidReady &&
                         source_job_revision[slot] == source_document_revision;
    // Every inactive destination starts white. An unmaterialized source band
    // is therefore still usable when the vector document proves it is blank.
    if (!current && region_contains_ink(benchmark.document, source_camera, source_job)) {
      return false;
    }
  }
  return true;
}

bool center_ready(const InteractivePanBenchmark& benchmark) {
  constexpr int center_cell = 4;
  const int first = center_cell * kBandsPerCell;
  const std::uint32_t revision = benchmark.document_revision.load();
  for (int band = 0; band < kBandsPerCell; ++band) {
    const auto slot = static_cast<std::size_t>(first + band);
    if (benchmark.ready[slot].load() != kExactReady ||
        benchmark.job_revision[slot].load() != revision) {
      return false;
    }
  }
  return true;
}

int choose_job(const InteractivePanBenchmark& benchmark) {
  const JobRect view{
      .x0 = benchmark.view_x.load(),
      .y0 = benchmark.view_y.load(),
      .x1 = benchmark.view_x.load() + kCanvasWidth,
      .y1 = benchmark.view_y.load() + benchmark.presented_rows,
  };
  int best_job = -1;
  int best_score = 0;
  const std::uint32_t revision = benchmark.document_revision.load();
  for (int job = 0; job < kJobCount; ++job) {
    const auto slot = static_cast<std::size_t>(job);
    if (benchmark.ready[slot].load() == kExactReady &&
        benchmark.job_revision[slot].load() == revision) {
      continue;
    }
    const JobRect candidate = job_rect(job);
    const int distance = interval_distance(candidate.x0, candidate.x1, view.x0, view.x1) +
                         interval_distance(candidate.y0, candidate.y1, view.y0, view.y1);
    const int score = (intersects(candidate, view) ? 0 : 1'000'000) + distance;
    if (best_job < 0 || score < best_score) {
      best_job = job;
      best_score = score;
    }
  }
  return best_job;
}

void present_job(InteractivePanBenchmark& benchmark, int job) {
  if (!benchmark.has_complete_initial_atlas || benchmark.pan_active.load()) {
    return;
  }
  const JobRect rect = job_rect(job);
  const int view_x = benchmark.view_x.load();
  const int view_y = benchmark.view_y.load();
  const int intersect_y0 = std::max(rect.y0, view_y);
  const int intersect_y1 = std::min(rect.y1, view_y + benchmark.presented_rows);
  if (std::max(rect.x0, view_x) >= std::min(rect.x1, view_x + kCanvasWidth) ||
      intersect_y0 >= intersect_y1) {
    return;
  }

  // CO5300 QSPI publications must keep even transfer-window boundaries. A
  // clipped cache job can begin at an arbitrary screen x while panning; those
  // partial writes visibly skew later panel rows. Publish an even-aligned,
  // full-width horizontal band only after every cache job backing that band is
  // current. Zoom, pan, and settled presentation already use this safe shape.
  const int screen_y0 = std::max(0, (intersect_y0 - view_y) & ~1);
  const int screen_y1 = std::min(benchmark.presented_rows, (intersect_y1 - view_y + 1) & ~1);
  const JobRect publication{
      .x0 = view_x,
      .y0 = view_y + screen_y0,
      .x1 = view_x + kCanvasWidth,
      .y1 = view_y + screen_y1,
  };
  const std::uint32_t revision = benchmark.document_revision.load();
  for (int candidate = 0; candidate < kJobCount; ++candidate) {
    if (!intersects(job_rect(candidate), publication)) {
      continue;
    }
    const auto slot = static_cast<std::size_t>(candidate);
    if (benchmark.ready[slot].load() == kInvalidReady ||
        benchmark.job_revision[slot].load() != revision) {
      return;
    }
  }
  const auto offset = static_cast<std::size_t>((view_y + screen_y0) * WorldCanvas::kWidth + view_x);
  benchmark.display.push_rect(0, screen_y0, kCanvasWidth, screen_y1 - screen_y0,
                              benchmark.world.pixels().data() + offset, WorldCanvas::kWidth);
}

void copy_job_to_cache(InteractivePanBenchmark& benchmark, int job) {
  const JobRect rect = job_rect(job);
  const int local_y = (job % kBandsPerCell) * kBandRows;
  auto world = benchmark.world.pixels();
  for (int row = 0; row < kBandRows; ++row) {
    const auto* source = benchmark.render_buffer.data() +
                         static_cast<std::ptrdiff_t>((local_y + row) * kCanvasWidth);
    auto* destination =
        world.data() + static_cast<std::ptrdiff_t>((rect.y0 + row) * WorldCanvas::kWidth + rect.x0);
    std::copy_n(source, kCanvasWidth, destination);
  }
}

struct RenderCancellation {
  InteractivePanBenchmark* benchmark = nullptr;
  std::uint32_t generation = 0U;
};

bool render_cancelled(void* raw) {
  const auto& cancellation = *static_cast<RenderCancellation*>(raw);
  return cancellation.benchmark->requested_generation.load() != cancellation.generation ||
         cancellation.benchmark->paused.load();
}

void render_task_entry(void* raw) {
  auto& benchmark = *static_cast<InteractivePanBenchmark*>(raw);
  std::uint32_t handled_generation = 0U;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const std::uint32_t generation = benchmark.requested_generation.load();
    if (benchmark.paused.load() || benchmark.zoom_presentation_pending.load() ||
        generation == handled_generation) {
      benchmark.rendering.store(false);
      continue;
    }
    handled_generation = generation;
    benchmark.rendering.store(true);

    const int zoom = benchmark.requested_zoom_index.load();
    const Camera active_atlas = benchmark.active_atlas;
    if (benchmark.document.active()) {
      std::printf("TINYDRAW_INTERACTIVE_PAN_FAIL active_document=1 zoom=%d\n",
                  kZoomPercents[static_cast<std::size_t>(zoom)]);
      benchmark.rendering.store(false);
      continue;
    }

    RenderCancellation cancellation{.benchmark = &benchmark, .generation = generation};
    // Materialize a one-band pan runway before expensive settled work. This
    // keeps immediate small drags from depending on a 400–700 ms refinement.
    // Jobs are certified only after their complete 368×32 region is resampled.
    if (benchmark.fallback_source_pinned &&
        benchmark.fallback_source_document_revision == benchmark.document_revision.load()) {
      constexpr int kPanRunwayPixels = kBandRows;
      const JobRect runway_view{
          .x0 = std::max(benchmark.view_x.load() - kPanRunwayPixels, 0),
          .y0 = std::max(benchmark.view_y.load() - kPanRunwayPixels, 0),
          .x1 = std::min(benchmark.view_x.load() + kCanvasWidth + kPanRunwayPixels,
                         WorldCanvas::kWidth),
          .y1 = std::min(benchmark.view_y.load() + benchmark.presented_rows + kPanRunwayPixels,
                         WorldCanvas::kHeight),
      };
      for (int job = 0; job < kJobCount && benchmark.requested_generation.load() == generation &&
                        !benchmark.paused.load();
           ++job) {
        const auto slot = static_cast<std::size_t>(job);
        const JobRect rect = job_rect(job);
        if (!intersects(rect, runway_view) || benchmark.ready[slot].load() != kInvalidReady) {
          continue;
        }
        const Rect pixels{rect.x0, rect.y0, rect.x1, rect.y1};
        if (!source_region_materialized(benchmark, benchmark.fallback_source_atlas, active_atlas,
                                        pixels, benchmark.fallback_source_ready,
                                        benchmark.fallback_source_job_revision,
                                        benchmark.fallback_source_document_revision)) {
          continue;
        }
        xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
        if (benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
          resample_valid_raster_region(
              benchmark.materialization_storage, WorldCanvas::kWidth, WorldCanvas::kHeight,
              benchmark.fallback_source_atlas, benchmark.world.pixels(), WorldCanvas::kWidth,
              WorldCanvas::kHeight, WorldCanvas::kWidth, active_atlas, pixels, kBackground);
          benchmark.job_revision[slot].store(benchmark.document_revision.load());
          benchmark.ready[slot].store(kDerivedReady);
        }
        xSemaphoreGive(benchmark.cache_mutex);
      }
    }

    const bool high_quality_lod = active_atlas.zoom > 1.0F;
    if (benchmark.settled_lod_high_quality != high_quality_lod &&
        !build_settled_lod(benchmark, high_quality_lod)) {
      std::printf("TINYDRAW_INTERACTIVE_PAN_FAIL lod_rebuild=0 zoom=%d\n",
                  kZoomPercents[static_cast<std::size_t>(zoom)]);
      benchmark.rendering.store(false);
      continue;
    }

    std::uint64_t settled_clear_us = 0U;
    std::uint64_t settled_raster_us = 0U;
    std::uint64_t settled_composite_us = 0U;
    std::uint64_t settled_publish_us = 0U;
    std::uint32_t settled_segments = 0U;
    std::uint32_t settled_bands = 0U;
    bool settled_visible_complete = false;
    int settled_view_x = 0;
    int settled_view_y = 0;
    std::uint32_t settled_revision = 0U;

    // Settled pass: render all visible bands from one cache cell as one
    // geometry supertask. A centered viewport needs one traversal instead of
    // repeating the same 1,000-stroke traversal for twelve 32-row bands.
    while (benchmark.requested_generation.load() == generation && !benchmark.paused.load() &&
           !benchmark.pan_active.load() &&
           (!benchmark.fallback_source_pinned ||
            benchmark.fallback_source_document_revision == benchmark.document_revision.load())) {
      const int view_x = benchmark.view_x.load();
      const int view_y = benchmark.view_y.load();
      const JobRect view{
          .x0 = view_x,
          .y0 = view_y,
          .x1 = view_x + kCanvasWidth,
          .y1 = view_y + benchmark.presented_rows,
      };
      const std::uint32_t revision = benchmark.document_revision.load();
      int settled_job = -1;
      for (int job = 0; job < kJobCount; ++job) {
        const auto slot = static_cast<std::size_t>(job);
        if (!intersects(job_rect(job), view)) {
          continue;
        }
        if (benchmark.ready[slot].load() >= kSettledReady &&
            benchmark.job_revision[slot].load() == revision) {
          continue;
        }
        settled_job = job;
        break;
      }
      if (settled_job < 0) {
        settled_visible_complete = true;
        settled_view_x = view_x;
        settled_view_y = view_y;
        settled_revision = revision;
        break;
      }

      const int cell = settled_job / kBandsPerCell;
      int first_band = kBandsPerCell;
      int last_band = -1;
      for (int band = 0; band < kBandsPerCell; ++band) {
        const int job = cell * kBandsPerCell + band;
        if (intersects(job_rect(job), view)) {
          first_band = std::min(first_band, band);
          last_band = std::max(last_band, band);
        }
      }
      if (last_band < first_band) {
        break;
      }

      const int local_y0 = first_band * kBandRows;
      const int local_y1 = (last_band + 1) * kBandRows;
      const Camera camera = job_camera(active_atlas, cell * kBandsPerCell);
      const double inverse_zoom = 1.0 / static_cast<double>(camera.zoom);
      const float world_halo = kCanonicalHaloPixels / camera.zoom;
      SettledRenderOptions settled_options;
      settled_options.background = kBackground;
      settled_options.cancelled = render_cancelled;
      settled_options.cancelled_frequently = render_cancelled;
      settled_options.cancellation_context = &cancellation;
      settled_options.clock_us = benchmark_clock_us;
      // This temporary world-space LOD is bounded to 2 screen pixels at the
      // prototype's 200% maximum. Production 400/800% support needs tighter
      // zoom-bucketed geometry rather than reusing this map.
      settled_options.lod_samples = benchmark.settled_lod_samples;
      settled_options.lod_first_sample = benchmark.settled_lod_first;
      settled_options.lod_sample_count = benchmark.settled_lod_count;
      if (benchmark.macrogrid_valid) {
        settled_options.candidate_strokes = benchmark.macrogrid.query(
            {.x0 = static_cast<float>(camera.x) - world_halo,
             .y0 = static_cast<float>(camera.y + local_y0 * inverse_zoom) - world_halo,
             .x1 = static_cast<float>(camera.x + kCanvasWidth * inverse_zoom) + world_halo,
             .y1 = static_cast<float>(camera.y + local_y1 * inverse_zoom) + world_halo});
      }
      const SettledRenderStats settled_stats =
          settled_render_region(benchmark.document, camera, benchmark.render_buffer,
                                {.x0 = 0, .y0 = local_y0, .x1 = kCanvasWidth, .y1 = local_y1},
                                benchmark.settled_scratch, settled_options);
      if (!settled_stats.complete || benchmark.requested_generation.load() != generation ||
          benchmark.paused.load()) {
        break;
      }
      settled_clear_us += settled_stats.clear_us;
      settled_raster_us += settled_stats.raster_us;
      settled_composite_us += settled_stats.composite_us;
      settled_segments += settled_stats.segments_rendered;
      const std::uint64_t publish_started = benchmark_clock_us(nullptr);
      xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
      if (benchmark.requested_generation.load() == generation && !benchmark.paused.load() &&
          benchmark.document_revision.load() == revision && benchmark.view_x.load() == view_x &&
          benchmark.view_y.load() == view_y) {
        for (int band = first_band; band <= last_band; ++band) {
          const int job = cell * kBandsPerCell + band;
          const auto slot = static_cast<std::size_t>(job);
          if (benchmark.ready[slot].load() == kExactReady &&
              benchmark.job_revision[slot].load() == revision) {
            continue;
          }
          copy_job_to_cache(benchmark, job);
          benchmark.job_revision[slot].store(revision);
          benchmark.ready[slot].store(kSettledReady);
          ++settled_bands;
        }
      }
      xSemaphoreGive(benchmark.cache_mutex);
      settled_publish_us += benchmark_clock_us(nullptr) - publish_started;
    }

    // Present the visible settled set once. Per-band publication paid twelve
    // transfer setup costs and made the screen sharpen as horizontal stripes.
    if (settled_visible_complete && settled_bands != 0U &&
        benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
      const std::uint64_t publish_started = benchmark_clock_us(nullptr);
      const std::uint32_t revision = settled_revision;
      const int view_x = settled_view_x;
      const int view_y = settled_view_y;
      std::uint32_t last_sequence = 0U;
      bool submitted = false;
      xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
      if (benchmark.requested_generation.load() == generation && !benchmark.paused.load() &&
          benchmark.document_revision.load() == revision && benchmark.view_x.load() == view_x &&
          benchmark.view_y.load() == view_y) {
        benchmark.display.push_rect(
            0, 0, kCanvasWidth, benchmark.presented_rows,
            benchmark.world.pixels().data() +
                static_cast<std::ptrdiff_t>(view_y * WorldCanvas::kWidth + view_x),
            WorldCanvas::kWidth);
        submitted = true;
        if (benchmark.transfer_telemetry.submit_count != nullptr) {
          last_sequence =
              benchmark.transfer_telemetry.submit_count(benchmark.transfer_telemetry.context);
        }
      }
      xSemaphoreGive(benchmark.cache_mutex);
      settled_publish_us += benchmark_clock_us(nullptr) - publish_started;

      std::uint32_t physical_elapsed = 0U;
      bool physically_completed = false;
      if (submitted && last_sequence != 0U &&
          benchmark.transfer_telemetry.complete_count != nullptr &&
          benchmark.transfer_telemetry.complete_time_us != nullptr) {
        while (benchmark.transfer_telemetry.complete_count(benchmark.transfer_telemetry.context) <
                   last_sequence &&
               benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
          vTaskDelay(pdMS_TO_TICKS(1));
        }
        const std::int64_t completed = benchmark.transfer_telemetry.complete_time_us(
            benchmark.transfer_telemetry.context, last_sequence);
        if (completed >= 0) {
          physical_elapsed =
              static_cast<std::uint32_t>(completed) - benchmark.generation_started_us.load();
          physically_completed = true;
        }
      }
      if (submitted && physically_completed &&
          benchmark.requested_generation.load() == generation && !benchmark.paused.load() &&
          benchmark.document_revision.load() == revision && benchmark.view_x.load() == view_x &&
          benchmark.view_y.load() == view_y) {
        benchmark.settled_us[static_cast<std::size_t>(zoom)].store(physical_elapsed);
        if (benchmark.refinement_published != nullptr) {
          benchmark.refinement_published(benchmark.refinement_published_context);
        }
      }
    }

    if (settled_bands != 0U) {
      std::printf(
          "TINYDRAW_SETTLED_PROFILE zoom=%d generation=%lu bands=%lu segments=%lu "
          "clear_us=%llu raster_us=%llu composite_us=%llu publish_us=%llu complete=%u\n",
          kZoomPercents[static_cast<std::size_t>(zoom)], static_cast<unsigned long>(generation),
          static_cast<unsigned long>(settled_bands), static_cast<unsigned long>(settled_segments),
          static_cast<unsigned long long>(settled_clear_us),
          static_cast<unsigned long long>(settled_raster_us),
          static_cast<unsigned long long>(settled_composite_us),
          static_cast<unsigned long long>(settled_publish_us),
          benchmark.requested_generation.load() == generation && !benchmark.paused.load());
    }

    // Repair only pinned-source bands touched by appended vector mutations.
    // Until every affected band reaches the same revision, set_zoom refuses to
    // use this arena; partially repaired pixels can never become a source.
    if (benchmark.fallback_source_pinned &&
        benchmark.fallback_source_document_revision != benchmark.document_revision.load()) {
      const std::uint32_t repair_revision = benchmark.fallback_repair_target_revision;
      ViewportRenderOptions repair_options;
      repair_options.yield = benchmark_yield;
      repair_options.yield_every_tiles = 8U;
      repair_options.cancelled = render_cancelled;
      repair_options.cancellation_context = &cancellation;
      bool repair_complete = repair_revision == benchmark.document_revision.load();
      for (int job = 0;
           job < kJobCount && repair_complete &&
           benchmark.requested_generation.load() == generation && !benchmark.paused.load();
           ++job) {
        const auto slot = static_cast<std::size_t>(job);
        if (!benchmark.fallback_repair_pending[slot]) {
          continue;
        }
        const int local_y = (job % kBandsPerCell) * kBandRows;
        const Camera camera = job_camera(benchmark.fallback_source_atlas, job);
        const double inverse_zoom = 1.0 / static_cast<double>(camera.zoom);
        const float world_halo = kCanonicalHaloPixels / camera.zoom;
        if (benchmark.macrogrid_valid) {
          repair_options.candidate_strokes = benchmark.macrogrid.query(
              {.x0 = static_cast<float>(camera.x) - world_halo,
               .y0 = static_cast<float>(camera.y + local_y * inverse_zoom) - world_halo,
               .x1 = static_cast<float>(camera.x + kCanvasWidth * inverse_zoom) + world_halo,
               .y1 = static_cast<float>(camera.y + (local_y + kBandRows) * inverse_zoom) +
                     world_halo});
        }
        const ViewportRenderStats repair_stats = benchmark.renderer->render_region(
            benchmark.document, camera, benchmark.render_buffer,
            {.x0 = 0, .y0 = local_y, .x1 = kCanvasWidth, .y1 = local_y + kBandRows},
            repair_options);
        if (!repair_stats.complete || benchmark.requested_generation.load() != generation ||
            benchmark.paused.load() || benchmark.document_revision.load() != repair_revision) {
          repair_complete = false;
          break;
        }
        xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
        if (benchmark.requested_generation.load() == generation && !benchmark.paused.load() &&
            benchmark.document_revision.load() == repair_revision) {
          const JobRect rect = job_rect(job);
          auto destination = benchmark.materialization_storage;
          for (int row = 0; row < kBandRows; ++row) {
            const auto* source = benchmark.render_buffer.data() +
                                 static_cast<std::ptrdiff_t>((local_y + row) * kCanvasWidth);
            auto* output =
                destination.data() +
                static_cast<std::ptrdiff_t>((rect.y0 + row) * WorldCanvas::kWidth + rect.x0);
            std::copy_n(source, kCanvasWidth, output);
          }
          benchmark.fallback_source_ready[slot] = kExactReady;
          benchmark.fallback_source_job_revision[slot] = repair_revision;
          benchmark.fallback_repair_pending[slot] = false;
        } else {
          repair_complete = false;
        }
        xSemaphoreGive(benchmark.cache_mutex);
      }
      if (repair_complete && benchmark.requested_generation.load() == generation &&
          !benchmark.paused.load()) {
        xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
        if (benchmark.document_revision.load() == repair_revision &&
            benchmark.fallback_repair_target_revision == repair_revision &&
            std::none_of(benchmark.fallback_repair_pending.begin(),
                         benchmark.fallback_repair_pending.end(),
                         [](bool pending) { return pending; })) {
          std::fill(benchmark.fallback_source_job_revision.begin(),
                    benchmark.fallback_source_job_revision.end(), repair_revision);
          benchmark.fallback_source_document_revision = repair_revision;
          std::printf("TINYDRAW_FALLBACK_REPAIRED revision=%lu\n",
                      static_cast<unsigned long>(repair_revision));
          // Start a fresh generation now that zoom provenance is restored. It
          // will run the deferred visible settled pass; this generation exits
          // without doing lower-priority runway or exact work.
          benchmark.requested_generation.fetch_add(1U);
          xTaskNotifyGive(benchmark.render_task);
        }
        xSemaphoreGive(benchmark.cache_mutex);
      }
    }

    // Fill active-atlas runway from the immutable complete fallback after the
    // visible settled pass. Cancellation may leave this generation partial,
    // but it cannot damage the source needed by the next transition.
    if (benchmark.fallback_source_pinned &&
        benchmark.fallback_source_document_revision == benchmark.document_revision.load()) {
      const Camera source_atlas = benchmark.fallback_source_atlas;
      for (int job = 0; job < kJobCount && benchmark.requested_generation.load() == generation;
           ++job) {
        const auto slot = static_cast<std::size_t>(job);
        if (benchmark.ready[slot].load() != kInvalidReady) {
          continue;
        }
        const JobRect rect = job_rect(job);
        const Rect pixels{rect.x0, rect.y0, rect.x1, rect.y1};
        if (!source_region_materialized(benchmark, source_atlas, active_atlas, pixels,
                                        benchmark.fallback_source_ready,
                                        benchmark.fallback_source_job_revision,
                                        benchmark.fallback_source_document_revision)) {
          continue;
        }
        xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
        if (benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
          resample_valid_raster_region(benchmark.materialization_storage, WorldCanvas::kWidth,
                                       WorldCanvas::kHeight, source_atlas, benchmark.world.pixels(),
                                       WorldCanvas::kWidth, WorldCanvas::kHeight,
                                       WorldCanvas::kWidth, active_atlas, pixels, kBackground);
          benchmark.ready[slot].store(kDerivedReady);
          benchmark.job_revision[slot].store(benchmark.document_revision.load());
          present_job(benchmark, job);
        }
        xSemaphoreGive(benchmark.cache_mutex);
      }
    }
    if (benchmark.refinement_published != nullptr &&
        benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
      benchmark.refinement_published(benchmark.refinement_published_context);
    }

    ViewportRenderOptions options;
    options.yield = benchmark_yield;
    options.yield_every_tiles = 8U;
    options.cancelled = render_cancelled;
    options.cancellation_context = &cancellation;
    while (benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
      const int job = choose_job(benchmark);
      if (job < 0) {
        const std::uint32_t elapsed = static_cast<std::uint32_t>(esp_timer_get_time()) -
                                      benchmark.generation_started_us.load();
        benchmark.full_ready_us[static_cast<std::size_t>(zoom)].store(elapsed);
        break;
      }
      const int local_y = (job % kBandsPerCell) * kBandRows;
      const Camera camera = job_camera(active_atlas, job);
      const double inverse_zoom = 1.0 / static_cast<double>(camera.zoom);
      const float world_halo = kCanonicalHaloPixels / camera.zoom;
      if (benchmark.macrogrid_valid) {
        options.candidate_strokes = benchmark.macrogrid.query(
            {.x0 = static_cast<float>(camera.x) - world_halo,
             .y0 = static_cast<float>(camera.y + local_y * inverse_zoom) - world_halo,
             .x1 = static_cast<float>(camera.x + kCanvasWidth * inverse_zoom) + world_halo,
             .y1 =
                 static_cast<float>(camera.y + (local_y + kBandRows) * inverse_zoom) + world_halo});
      }
      const ViewportRenderStats render_stats = benchmark.renderer->render_region(
          benchmark.document, camera, benchmark.render_buffer,
          {.x0 = 0, .y0 = local_y, .x1 = kCanvasWidth, .y1 = local_y + kBandRows}, options);
      if (!render_stats.complete || benchmark.requested_generation.load() != generation ||
          benchmark.paused.load()) {
        break;
      }
      xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
      if (benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
        copy_job_to_cache(benchmark, job);
        benchmark.job_revision[static_cast<std::size_t>(job)].store(
            benchmark.document_revision.load());
        benchmark.ready[static_cast<std::size_t>(job)].store(kExactReady);
        present_job(benchmark, job);
      }
      xSemaphoreGive(benchmark.cache_mutex);
      if (benchmark.center_ready_us[static_cast<std::size_t>(zoom)].load() == 0U &&
          center_ready(benchmark)) {
        const std::uint32_t elapsed = static_cast<std::uint32_t>(esp_timer_get_time()) -
                                      benchmark.generation_started_us.load();
        benchmark.center_ready_us[static_cast<std::size_t>(zoom)].store(elapsed);
      }
    }
    if (benchmark.refinement_published != nullptr &&
        benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
      benchmark.refinement_published(benchmark.refinement_published_context);
    }
    benchmark.rendering.store(false);
  }
}

std::uint32_t missing_pixels(const InteractivePanBenchmark& benchmark, ViewOrigin origin) {
  const JobRect view{
      .x0 = origin.x,
      .y0 = origin.y,
      .x1 = origin.x + kCanvasWidth,
      .y1 = origin.y + benchmark.presented_rows,
  };
  std::uint32_t missing = 0U;
  for (int job = 0; job < kJobCount; ++job) {
    const auto slot = static_cast<std::size_t>(job);
    if (benchmark.ready[slot].load() != 0U &&
        benchmark.job_revision[slot].load() == benchmark.document_revision.load()) {
      continue;
    }
    const JobRect candidate = job_rect(job);
    const int width =
        std::max(0, std::min(view.x1, candidate.x1) - std::max(view.x0, candidate.x0));
    const int height =
        std::max(0, std::min(view.y1, candidate.y1) - std::max(view.y0, candidate.y0));
    missing += static_cast<std::uint32_t>(width * height);
  }
  return missing;
}

bool persist_report(InteractivePanBenchmark& benchmark) {
  const esp_partition_t* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kExportSubtype, "export");
  auto* report = static_cast<char*>(heap_caps_calloc(kReportBytes, 1U, kExternalCaps));
  if (partition == nullptr || report == nullptr) {
    heap_caps_free(report);
    return false;
  }
  std::size_t size = 0U;
  append_report(report, size,
                "TINYDRAW_INTERACTIVE_PAN_V1 workload=realistic_handwriting1000 "
                "cache=3x3 band_rows=%d presented_rows=%d timing_capacity=%lu "
                "strokes=%lu samples=%lu max_stroke_samples=%lu\n",
                kBandRows, benchmark.presented_rows, static_cast<unsigned long>(kTimingCapacity),
                static_cast<unsigned long>(benchmark.workload_stats.strokes),
                static_cast<unsigned long>(benchmark.workload_stats.samples),
                static_cast<unsigned long>(benchmark.workload_stats.maximum_stroke_samples));
  for (std::size_t index = 0; index < kZoomPercents.size(); ++index) {
    auto& metrics = benchmark.metrics[index];
    const std::size_t samples = metrics.timing_samples;
    const auto direct_median = quantile(benchmark.timings.direct[index], samples, 1U, 2U);
    const auto direct_p95 = quantile(benchmark.timings.direct[index], samples, 95U, 100U);
    const auto direct_max = samples == 0U ? 0U : benchmark.timings.direct[index][samples - 1U];
    const auto event_median = quantile(benchmark.timings.event[index], samples, 1U, 2U);
    const auto event_p95 = quantile(benchmark.timings.event[index], samples, 95U, 100U);
    const auto event_max = samples == 0U ? 0U : benchmark.timings.event[index][samples - 1U];
    append_report(report, size,
                  "ZOOM zoom=%d attempts=%lu failed_attempts=%lu max_cancel_us=%lu "
                  "gestures=%lu frames=%lu samples=%lu direct_avg_us=%llu "
                  "direct_median_us=%lu direct_p95_us=%lu direct_max_us=%lu event_avg_us=%llu "
                  "event_median_us=%lu event_p95_us=%lu event_max_us=%lu miss_frames=%lu "
                  "max_missing_pixels=%lu max_velocity_px_s=%lu first_valid_us=%lu "
                  "fallback_ready_us=%lu settled_us=%lu center_ready_us=%lu full_ready_us=%lu "
                  "cancel_done_us=%lu first_ready_us=%lu first_submit_us=%lu "
                  "first_complete_us=%lu last_submit_us=%lu last_complete_us=%lu\n",
                  kZoomPercents[index], static_cast<unsigned long>(metrics.attempts),
                  static_cast<unsigned long>(metrics.failed_attempts),
                  static_cast<unsigned long>(metrics.maximum_cancellation_us),
                  static_cast<unsigned long>(metrics.gestures),
                  static_cast<unsigned long>(metrics.frames), static_cast<unsigned long>(samples),
                  static_cast<unsigned long long>(
                      metrics.frames == 0U ? 0U : metrics.direct_total_us / metrics.frames),
                  static_cast<unsigned long>(direct_median), static_cast<unsigned long>(direct_p95),
                  static_cast<unsigned long>(direct_max),
                  static_cast<unsigned long long>(
                      metrics.frames == 0U ? 0U : metrics.event_total_us / metrics.frames),
                  static_cast<unsigned long>(event_median), static_cast<unsigned long>(event_p95),
                  static_cast<unsigned long>(event_max),
                  static_cast<unsigned long>(metrics.miss_frames),
                  static_cast<unsigned long>(metrics.maximum_missing_pixels),
                  static_cast<unsigned long>(metrics.maximum_velocity_px_s),
                  static_cast<unsigned long>(benchmark.first_valid_us[index].load()),
                  static_cast<unsigned long>(benchmark.fallback_ready_us[index].load()),
                  static_cast<unsigned long>(benchmark.settled_us[index].load()),
                  static_cast<unsigned long>(benchmark.center_ready_us[index].load()),
                  static_cast<unsigned long>(benchmark.full_ready_us[index].load()),
                  static_cast<unsigned long>(benchmark.cancel_done_us[index].load()),
                  static_cast<unsigned long>(benchmark.first_strip_ready_us[index].load()),
                  static_cast<unsigned long>(benchmark.first_strip_submit_us[index].load()),
                  static_cast<unsigned long>(benchmark.first_strip_complete_us[index].load()),
                  static_cast<unsigned long>(benchmark.last_visible_submit_us[index].load()),
                  static_cast<unsigned long>(benchmark.last_visible_complete_us[index].load()));
  }
  const auto draw_median = quantile(benchmark.timings.draw, benchmark.draw_samples, 1U, 2U);
  const auto draw_p95 = quantile(benchmark.timings.draw, benchmark.draw_samples, 95U, 100U);
  const auto draw_p99 = quantile(benchmark.timings.draw, benchmark.draw_samples, 99U, 100U);
  append_report(
      report, size,
      "DRAW samples=%lu average_us=%llu median_us=%lu p95_us=%lu p99_us=%lu "
      "max_us=%lu\n",
      static_cast<unsigned long>(benchmark.draw_samples),
      static_cast<unsigned long long>(
          benchmark.draw_updates == 0U ? 0U : benchmark.draw_total_us / benchmark.draw_updates),
      static_cast<unsigned long>(draw_median), static_cast<unsigned long>(draw_p95),
      static_cast<unsigned long>(draw_p99), static_cast<unsigned long>(benchmark.draw_max_us));
  append_report(report, size, "DONE\n");
  const std::size_t offset = partition->size - kReportBytes;
  const bool persisted = esp_partition_erase_range(partition, offset, kReportBytes) == ESP_OK &&
                         esp_partition_write(partition, offset, report, kReportBytes) == ESP_OK;
  heap_caps_free(report);
  return persisted;
}

void destroy_benchmark(InteractivePanBenchmark& benchmark) {
  benchmark.paused.store(true);
  benchmark.requested_generation.fetch_add(1U);
  if (benchmark.render_task != nullptr) {
    xTaskNotifyGive(benchmark.render_task);
    const std::uint32_t wait_started = static_cast<std::uint32_t>(esp_timer_get_time());
    while (benchmark.rendering.load() &&
           static_cast<std::uint32_t>(esp_timer_get_time()) - wait_started < 5'000'000U) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(benchmark.render_task);
  }
  if (benchmark.cache_mutex != nullptr) {
    vSemaphoreDelete(benchmark.cache_mutex);
  }
  auto* scratch = benchmark.scratch.data();
  auto* timings = &benchmark.timings;
  auto* renderer = benchmark.renderer;
  void* renderer_storage = benchmark.renderer_storage_;
  auto* index_storage = benchmark.index_storage_;
  auto* settled_scratch = benchmark.settled_scratch_storage_;
  auto* settled_lod_samples = benchmark.settled_lod_samples_storage_;
  auto* settled_lod_first = benchmark.settled_lod_first_storage_;
  auto* settled_lod_count = benchmark.settled_lod_count_storage_;
  renderer->~ViewportRenderer();
  benchmark.~InteractivePanBenchmark();
  heap_caps_free(renderer_storage);
  heap_caps_free(settled_lod_count);
  heap_caps_free(settled_lod_first);
  heap_caps_free(settled_lod_samples);
  heap_caps_free(settled_scratch);
  heap_caps_free(index_storage);
  heap_caps_free(timings);
  heap_caps_free(scratch);
  heap_caps_free(&benchmark);
}

}  // namespace

InteractivePanBenchmark* start_interactive_pan_benchmark(
    VectorDocument& document, WorldCanvas& world, std::span<std::uint16_t> materialization_storage,
    std::span<std::uint16_t> render_buffer, int presented_rows, DisplayBackend& display,
    void (*refinement_published)(void*), void* refinement_published_context,
    DisplayTransferTelemetry transfer_telemetry) {
  if (!world.valid() || materialization_storage.size() < WorldCanvas::kRequiredPixels ||
      render_buffer.size() < ViewportRenderer::kPixelCount ||
      document.stroke_capacity() < kStrokeCount ||
      document.sample_capacity() < kRequiredSampleCapacity || presented_rows <= 0 ||
      presented_rows > kCanvasHeight) {
    return nullptr;
  }
  auto* scratch =
      static_cast<std::uint8_t*>(heap_caps_malloc(ViewportRenderer::kScratchBytes, kExternalCaps));
  auto* timings =
      static_cast<TimingStorage*>(heap_caps_calloc(1U, sizeof(TimingStorage), kExternalCaps));
  auto* index_storage = static_cast<std::uint64_t*>(heap_caps_calloc(
      (StrokeMacrogrid::kCellCount + 1U) * kIndexWords, sizeof(std::uint64_t), kExternalCaps));
  // Settled and canonical rendering are serialized on the render task. Share
  // the full-cell canonical coverage arena instead of spending another 161 KB
  // of the device's remaining PSRAM.
  constexpr std::size_t kSettledScratchBytes = ViewportRenderer::kScratchBytes;
  auto* settled_lod_samples = static_cast<StrokeSample*>(
      heap_caps_malloc(kSettledLodSampleCapacity * sizeof(StrokeSample), kExternalCaps));
  auto* settled_lod_first = static_cast<std::uint32_t*>(
      heap_caps_calloc(document.stroke_capacity(), sizeof(std::uint32_t), kExternalCaps));
  auto* settled_lod_count = static_cast<std::uint16_t*>(
      heap_caps_calloc(document.stroke_capacity(), sizeof(std::uint16_t), kExternalCaps));
  void* renderer_storage = heap_caps_malloc(sizeof(ViewportRenderer), kInternalCaps);
  void* benchmark_storage = heap_caps_malloc(sizeof(InteractivePanBenchmark), kInternalCaps);
  if (scratch == nullptr || timings == nullptr || index_storage == nullptr ||
      settled_lod_samples == nullptr || settled_lod_first == nullptr ||
      settled_lod_count == nullptr || renderer_storage == nullptr || benchmark_storage == nullptr) {
    std::printf(
        "TINYDRAW_BENCH_ALLOC_FAIL scratch=%u timings=%u index=%u settled=%u lod=%u first=%u "
        "count=%u renderer=%u benchmark=%u free=%lu largest=%lu\n",
        scratch != nullptr, timings != nullptr, index_storage != nullptr, scratch != nullptr,
        settled_lod_samples != nullptr, settled_lod_first != nullptr, settled_lod_count != nullptr,
        renderer_storage != nullptr, benchmark_storage != nullptr,
        static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
    heap_caps_free(benchmark_storage);
    heap_caps_free(renderer_storage);
    heap_caps_free(settled_lod_count);
    heap_caps_free(settled_lod_first);
    heap_caps_free(settled_lod_samples);
    heap_caps_free(index_storage);
    heap_caps_free(timings);
    heap_caps_free(scratch);
    return nullptr;
  }
  auto* benchmark = new (benchmark_storage) InteractivePanBenchmark(
      document, world, materialization_storage, render_buffer, presented_rows, display,
      std::span(scratch, ViewportRenderer::kScratchBytes), *timings,
      std::span(index_storage, StrokeMacrogrid::kCellCount * kIndexWords),
      std::span(index_storage + StrokeMacrogrid::kCellCount * kIndexWords, kIndexWords),
      renderer_storage, refinement_published, refinement_published_context, transfer_telemetry);
  benchmark->index_storage_ = index_storage;
  benchmark->settled_scratch = std::span(scratch, kSettledScratchBytes);
  benchmark->settled_scratch_storage_ = nullptr;
  benchmark->settled_lod_samples = std::span(settled_lod_samples, kSettledLodSampleCapacity);
  benchmark->settled_lod_first = std::span(settled_lod_first, document.stroke_capacity());
  benchmark->settled_lod_count = std::span(settled_lod_count, document.stroke_capacity());
  benchmark->settled_lod_samples_storage_ = settled_lod_samples;
  benchmark->settled_lod_first_storage_ = settled_lod_first;
  benchmark->settled_lod_count_storage_ = settled_lod_count;
  const bool populated = populate_benchmark_document(document, benchmark->workload_stats);
  const bool indexed = populated && benchmark->macrogrid.rebuild(document);
  benchmark->macrogrid_valid = indexed;
  const bool lod_built = indexed && build_settled_lod(*benchmark, false);
  if (!lod_built) {
    std::printf("TINYDRAW_BENCH_SETUP_FAIL populated=%u indexed=%u lod=%u samples=%lu\n", populated,
                indexed, lod_built, static_cast<unsigned long>(document.sample_count()));
    destroy_benchmark(*benchmark);
    return nullptr;
  }
  std::fill_n(world.pixels().begin(), WorldCanvas::kRequiredPixels, kBackground);
  std::fill_n(materialization_storage.begin(), WorldCanvas::kRequiredPixels, kBackground);
  benchmark->cache_mutex = xSemaphoreCreateMutex();
  if (benchmark->cache_mutex == nullptr ||
      xTaskCreatePinnedToCore(render_task_entry, "interactive_pan_render", 16'384U, benchmark, 1U,
                              &benchmark->render_task, 1) != pdPASS) {
    destroy_benchmark(*benchmark);
    return nullptr;
  }
  if (!interactive_pan_benchmark_set_zoom(*benchmark, 100)) {
    destroy_benchmark(*benchmark);
    return nullptr;
  }
  benchmark->zoom_presentation_pending.store(false);
  xTaskNotifyGive(benchmark->render_task);
  // Initial setup is not an interaction measurement. Do not expose any
  // synthetic-document band until the entire first atlas belongs to it.
  const std::uint32_t initial_wait_started = static_cast<std::uint32_t>(esp_timer_get_time());
  while (benchmark->full_ready_us[1].load() == 0U &&
         static_cast<std::uint32_t>(esp_timer_get_time()) - initial_wait_started < 30'000'000U) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (benchmark->full_ready_us[1].load() == 0U) {
    destroy_benchmark(*benchmark);
    return nullptr;
  }
  benchmark->has_complete_initial_atlas = true;
  std::printf(
      "TINYDRAW_BENCH_MEMORY free=%lu largest=%lu minimum=%lu lod_capacity_bytes=%lu "
      "lod_used_bytes=%lu\n",
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_minimum_free_size(kExternalCaps)),
      static_cast<unsigned long>(kSettledLodSampleCapacity * sizeof(StrokeSample)),
      static_cast<unsigned long>(benchmark->settled_lod_samples.size() * sizeof(StrokeSample)));
  return benchmark;
}

bool interactive_pan_benchmark_set_zoom(InteractivePanBenchmark& benchmark, int zoom_percent) {
  const int index = zoom_index(zoom_percent);
  if (index < 0 || benchmark.finished.load()) {
    return false;
  }
  auto& attempt_metrics = benchmark.metrics[static_cast<std::size_t>(index)];
  ++attempt_metrics.attempts;
  const auto event_started = static_cast<std::uint32_t>(esp_timer_get_time());
  benchmark.zoom_presentation_pending.store(true);
  benchmark.paused.store(true);
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
  const std::uint32_t cancellation_started = static_cast<std::uint32_t>(esp_timer_get_time());
  while (benchmark.rendering.load() &&
         static_cast<std::uint32_t>(esp_timer_get_time()) - cancellation_started < 2'000'000U) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  const std::uint32_t cancellation_us =
      static_cast<std::uint32_t>(esp_timer_get_time()) - cancellation_started;
  attempt_metrics.maximum_cancellation_us =
      std::max(attempt_metrics.maximum_cancellation_us, cancellation_us);
  const std::uint32_t cancel_done_elapsed =
      static_cast<std::uint32_t>(esp_timer_get_time()) - event_started;
  if (benchmark.rendering.load()) {
    ++attempt_metrics.failed_attempts;
    benchmark.zoom_presentation_pending.store(false);
    benchmark.paused.store(false);
    xTaskNotifyGive(benchmark.render_task);
    return false;
  }

  benchmark.center_ready_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.full_ready_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.first_valid_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.fallback_ready_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.settled_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.cancel_done_us[static_cast<std::size_t>(index)].store(cancel_done_elapsed);
  benchmark.first_strip_ready_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.first_strip_submit_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.first_strip_complete_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.last_visible_submit_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.last_visible_complete_us[static_cast<std::size_t>(index)].store(0U);

  xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
  const float new_zoom = static_cast<float>(zoom_percent) / 100.0F;
  const Camera old_camera = benchmark.active_atlas;
  const double focus_x =
      old_camera.x +
      (static_cast<double>(benchmark.view_x.load()) + kCanvasWidth / 2.0) / old_camera.zoom;
  const double focus_y = old_camera.y + (static_cast<double>(benchmark.view_y.load()) +
                                         benchmark.presented_rows / 2.0) /
                                            old_camera.zoom;
  const Camera new_camera{
      .x = focus_x - (static_cast<double>(kCenterOriginX) + kCanvasWidth / 2.0) / new_zoom,
      .y = focus_y -
           (static_cast<double>(kCenterOriginY) + benchmark.presented_rows / 2.0) / new_zoom,
      .zoom = new_zoom,
  };
  const Rect visible{.x0 = kCenterOriginX,
                     .y0 = kCenterOriginY,
                     .x1 = kCenterOriginX + kCanvasWidth,
                     .y1 = kCenterOriginY + benchmark.presented_rows};
  const std::uint32_t old_document_revision = benchmark.document_revision.load();
  Camera source_camera = old_camera;
  std::span<const std::uint16_t> source_pixels = benchmark.world.pixels();
  std::array<std::uint8_t, kJobCount> source_ready{};
  std::array<std::uint32_t, kJobCount> source_job_revision{};
  for (std::size_t job = 0; job < benchmark.ready.size(); ++job) {
    source_ready[job] = benchmark.ready[job].load();
    source_job_revision[job] = benchmark.job_revision[job].load();
  }
  std::uint32_t source_document_revision = old_document_revision;

  if (benchmark.has_complete_initial_atlas && benchmark.fallback_source_pinned) {
    // Ownership remains pinned across mutations. Until incremental repair has
    // atomically advanced the whole source to this document revision, neither
    // the stale pinned arena nor the partial active arena is a legal source.
    if (benchmark.fallback_source_document_revision != old_document_revision) {
      xSemaphoreGive(benchmark.cache_mutex);
      ++attempt_metrics.failed_attempts;
      benchmark.zoom_presentation_pending.store(false);
      benchmark.paused.store(false);
      benchmark.requested_generation.fetch_add(1U);
      xTaskNotifyGive(benchmark.render_task);
      return false;
    }
    source_camera = benchmark.fallback_source_atlas;
    source_pixels = benchmark.materialization_storage;
    source_ready = benchmark.fallback_source_ready;
    source_job_revision = benchmark.fallback_source_job_revision;
    source_document_revision = benchmark.fallback_source_document_revision;
  }

  // Never publish a fallback assembled from missing raster content. A pinned
  // complete source avoids making transition success depend on whether runway
  // work for the current active atlas happened to finish before cancellation.
  std::array<bool, kJobCount> fallback_valid{};
  if (benchmark.has_complete_initial_atlas) {
    for (int job = 0; job < kJobCount; ++job) {
      const JobRect rect = job_rect(job);
      if (!intersects(rect, {visible.x0, visible.y0, visible.x1, visible.y1})) {
        continue;
      }
      const Rect pixels{rect.x0, rect.y0, rect.x1, rect.y1};
      const bool valid =
          source_region_materialized(benchmark, source_camera, new_camera, pixels, source_ready,
                                     source_job_revision, source_document_revision);
      fallback_valid[static_cast<std::size_t>(job)] = valid;
      if (!valid) {
        xSemaphoreGive(benchmark.cache_mutex);
        ++attempt_metrics.failed_attempts;
        benchmark.zoom_presentation_pending.store(false);
        benchmark.paused.store(false);
        benchmark.requested_generation.fetch_add(1U);
        xTaskNotifyGive(benchmark.render_task);
        return false;
      }
    }
  }

  // The initial full atlas becomes the immutable fallback on the first
  // interactive zoom. Thereafter the active arena is rewritten in place from
  // that pinned source, so no third 2.97 MiB buffer is needed.
  if (benchmark.has_complete_initial_atlas && !benchmark.fallback_source_pinned) {
    // Only the initialization generation may establish the provenance root.
    // After a document mutation the root remains pinned but stale, so this
    // branch is unreachable and a partial active atlas can never be promoted.
    const auto old_storage = benchmark.world.exchange_storage(benchmark.materialization_storage);
    if (old_storage.empty()) {
      xSemaphoreGive(benchmark.cache_mutex);
      ++attempt_metrics.failed_attempts;
      benchmark.zoom_presentation_pending.store(false);
      benchmark.paused.store(false);
      xTaskNotifyGive(benchmark.render_task);
      return false;
    }
    benchmark.materialization_storage = old_storage;
    benchmark.fallback_source_atlas = source_camera;
    benchmark.fallback_source_ready = source_ready;
    benchmark.fallback_source_job_revision = source_job_revision;
    benchmark.fallback_source_document_revision = source_document_revision;
    benchmark.fallback_source_pinned = true;
    source_pixels = benchmark.materialization_storage;
  }
  benchmark.requested_zoom_index.store(index);
  benchmark.active_zoom_index.store(index);
  benchmark.active_atlas = new_camera;
  benchmark.requested_generation.fetch_add(1U);
  benchmark.generation_started_us.store(event_started);
  for (std::size_t job = 0; job < benchmark.ready.size(); ++job) {
    benchmark.job_revision[job].store(benchmark.document_revision.load());
    benchmark.ready[job].store(kInvalidReady);
  }
  benchmark.view_x.store(kCenterOriginX);
  benchmark.view_y.store(kCenterOriginY);
  static_cast<void>(benchmark.world.move_to({kCenterOriginX, kCenterOriginY}));

  // Materialize and physically submit the visible region as center-out
  // nearest-resampled strips, pipelining resampling against DMA transfers.
  const DisplayTransferTelemetry& telemetry = benchmark.transfer_telemetry;
  const bool telemetry_available = telemetry.submit_count != nullptr &&
                                   telemetry.complete_count != nullptr &&
                                   telemetry.complete_time_us != nullptr;
  const auto slot = static_cast<std::size_t>(index);
  std::uint32_t first_submit_sequence = 0U;
  std::uint32_t last_submit_sequence = 0U;
  const int strip_count = (benchmark.presented_rows + kStripRows - 1) / kStripRows;
  for (int position = 0; position < strip_count; ++position) {
    const int strip = center_out_strip(position, strip_count);
    const int row0 = strip * kStripRows;
    const int row1 = std::min(row0 + kStripRows, benchmark.presented_rows);
    const Rect region{visible.x0, visible.y0 + row0, visible.x1, visible.y0 + row1};
    resample_valid_raster_region(source_pixels, WorldCanvas::kWidth, WorldCanvas::kHeight,
                                 source_camera, benchmark.world.pixels(), WorldCanvas::kWidth,
                                 WorldCanvas::kHeight, WorldCanvas::kWidth, new_camera, region,
                                 kBackground);
    if (position == 0) {
      benchmark.first_strip_ready_us[slot].store(static_cast<std::uint32_t>(esp_timer_get_time()) -
                                                 event_started);
    }
    if (!benchmark.has_complete_initial_atlas) {
      continue;
    }
    const auto offset = static_cast<std::size_t>(region.y0 * WorldCanvas::kWidth + region.x0);
    benchmark.display.push_rect(0, row0, kCanvasWidth, row1 - row0,
                                benchmark.world.pixels().data() + offset, WorldCanvas::kWidth);
    const std::uint32_t submitted =
        static_cast<std::uint32_t>(esp_timer_get_time()) - event_started;
    if (telemetry_available) {
      const std::uint32_t sequence = telemetry.submit_count(telemetry.context);
      if (first_submit_sequence == 0U) {
        first_submit_sequence = sequence;
      }
      last_submit_sequence = sequence;
    }
    if (position == 0) {
      benchmark.first_strip_submit_us[slot].store(submitted);
    }
    benchmark.last_visible_submit_us[slot].store(submitted);
  }
  // The visible height is not band-aligned. Finish any intersecting edge band
  // before certifying it derived, so a small pan cannot expose rows left from
  // the previous camera. Only the visible rows are submitted to the panel.
  for (int job = 0; job < kJobCount; ++job) {
    const auto job_slot = static_cast<std::size_t>(job);
    const JobRect rect = job_rect(job);
    if (!intersects(rect, {visible.x0, visible.y0, visible.x1, visible.y1}) ||
        !fallback_valid[job_slot]) {
      continue;
    }
    const bool fully_materialized = rect.x0 >= visible.x0 && rect.y0 >= visible.y0 &&
                                    rect.x1 <= visible.x1 && rect.y1 <= visible.y1;
    if (!fully_materialized) {
      resample_valid_raster_region(source_pixels, WorldCanvas::kWidth, WorldCanvas::kHeight,
                                   source_camera, benchmark.world.pixels(), WorldCanvas::kWidth,
                                   WorldCanvas::kHeight, WorldCanvas::kWidth, new_camera,
                                   {rect.x0, rect.y0, rect.x1, rect.y1}, kBackground);
    }
    benchmark.ready[job_slot].store(kDerivedReady);
  }
  const std::uint32_t fallback_elapsed =
      static_cast<std::uint32_t>(esp_timer_get_time()) - event_started;
  benchmark.fallback_ready_us[slot].store(fallback_elapsed);
  benchmark.zoom_presentation_pending.store(false);
  benchmark.paused.store(false);
  xSemaphoreGive(benchmark.cache_mutex);
  xTaskNotifyGive(benchmark.render_task);

  // Outside the cache lock: wait briefly for the queued strip transfers to
  // physically complete, then record completion-based endpoints.
  if (telemetry_available && last_submit_sequence != 0U) {
    const std::uint32_t wait_started = static_cast<std::uint32_t>(esp_timer_get_time());
    while (telemetry.complete_count(telemetry.context) < last_submit_sequence &&
           static_cast<std::uint32_t>(esp_timer_get_time()) - wait_started < 1'000'000U) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    const std::int64_t first_completed =
        telemetry.complete_time_us(telemetry.context, first_submit_sequence);
    const std::int64_t last_completed =
        telemetry.complete_time_us(telemetry.context, last_submit_sequence);
    if (first_completed >= 0) {
      const std::uint32_t elapsed = static_cast<std::uint32_t>(first_completed) - event_started;
      benchmark.first_strip_complete_us[slot].store(elapsed);
      benchmark.first_valid_us[slot].store(elapsed);
    }
    if (last_completed >= 0) {
      benchmark.last_visible_complete_us[slot].store(static_cast<std::uint32_t>(last_completed) -
                                                     event_started);
    }
  } else if (benchmark.has_complete_initial_atlas) {
    // Without completion telemetry, fall back to submit-time endpoints.
    benchmark.first_valid_us[slot].store(benchmark.first_strip_submit_us[slot].load());
  }
  // settled_us is recorded by the background settled pass once every visible
  // band has actual-resolution settled-renderer output.
  return true;
}

bool interactive_pan_benchmark_last_zoom_timing(const InteractivePanBenchmark& benchmark,
                                                int zoom_percent, ZoomTransitionTiming& timing) {
  const int index = zoom_index(zoom_percent);
  if (index < 0) {
    return false;
  }
  const auto slot = static_cast<std::size_t>(index);
  timing.cancel_done_us = benchmark.cancel_done_us[slot].load();
  timing.first_strip_ready_us = benchmark.first_strip_ready_us[slot].load();
  timing.first_strip_submit_us = benchmark.first_strip_submit_us[slot].load();
  timing.first_strip_complete_us = benchmark.first_strip_complete_us[slot].load();
  timing.last_visible_submit_us = benchmark.last_visible_submit_us[slot].load();
  timing.last_visible_complete_us = benchmark.last_visible_complete_us[slot].load();
  timing.fallback_ready_us = benchmark.fallback_ready_us[slot].load();
  timing.settled_us = benchmark.settled_us[slot].load();
  return true;
}

void interactive_pan_benchmark_record_zoom_present(InteractivePanBenchmark& benchmark) {
  const int index = benchmark.active_zoom_index.load();
  std::uint32_t expected = 0U;
  const std::uint32_t elapsed =
      static_cast<std::uint32_t>(esp_timer_get_time()) - benchmark.generation_started_us.load();
  static_cast<void>(
      benchmark.first_valid_us[static_cast<std::size_t>(index)].compare_exchange_strong(expected,
                                                                                        elapsed));
  benchmark.zoom_presentation_pending.store(false);
  xTaskNotifyGive(benchmark.render_task);
}

bool interactive_pan_benchmark_begin_stroke(InteractivePanBenchmark& benchmark) {
  if (benchmark.finished.load() || benchmark.stroke_mutation_active.exchange(true)) {
    return false;
  }
  benchmark.paused.store(true);
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
  const std::uint32_t started = static_cast<std::uint32_t>(esp_timer_get_time());
  while (benchmark.rendering.load() &&
         static_cast<std::uint32_t>(esp_timer_get_time()) - started < 2'000'000U) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (benchmark.rendering.load()) {
    benchmark.stroke_mutation_active.store(false);
    benchmark.paused.store(false);
    benchmark.requested_generation.fetch_add(1U);
    xTaskNotifyGive(benchmark.render_task);
    return false;
  }
  return true;
}

StrokeSample interactive_pan_benchmark_map_sample(const InteractivePanBenchmark& benchmark,
                                                  Point screen_point, float screen_radius) {
  const int zoom_index_value = benchmark.active_zoom_index.load();
  const float zoom =
      static_cast<float>(kZoomPercents[static_cast<std::size_t>(zoom_index_value)]) / 100.0F;
  const Camera atlas = benchmark.active_atlas;
  return {
      .x = static_cast<float>(
          atlas.x + (static_cast<double>(benchmark.view_x.load()) + screen_point.x) / atlas.zoom),
      .y = static_cast<float>(
          atlas.y + (static_cast<double>(benchmark.view_y.load()) + screen_point.y) / atlas.zoom),
      .radius = screen_radius / zoom,
  };
}

void interactive_pan_benchmark_commit_stroke(InteractivePanBenchmark& benchmark,
                                             bool visible_raster_current) {
  if (!benchmark.stroke_mutation_active.exchange(false)) {
    return;
  }
  const auto strokes = benchmark.document.strokes();
  if (strokes.empty()) {
    benchmark.paused.store(false);
    benchmark.requested_generation.fetch_add(1U);
    xTaskNotifyGive(benchmark.render_task);
    return;
  }
  const std::size_t committed_index = strokes.size() - 1U;
  if (benchmark.macrogrid_valid &&
      !benchmark.macrogrid.append(committed_index, strokes[committed_index].bounds)) {
    // An incomplete candidate index can omit ink while a tile is certified.
    // Disable candidate filtering for every renderer until a full rebuild.
    benchmark.macrogrid_valid = false;
    std::printf("TINYDRAW_INTERACTIVE_PAN_WARN index_disabled=1 stroke=%lu\n",
                static_cast<unsigned long>(committed_index));
  }
  // Append one LOD segment without rebuilding the existing document geometry.
  const auto committed_samples = benchmark.document.samples(strokes[committed_index]);
  benchmark.settled_lod_count[committed_index] = 0U;
  const std::size_t lod_used = benchmark.settled_lod_samples.size();
  auto lod_storage = std::span(benchmark.settled_lod_samples_storage_, kSettledLodSampleCapacity);
  if (lod_used <= lod_storage.size()) {
    const float center_error = benchmark.settled_lod_high_quality ? 1.0F : 2.0F;
    const float radius_error = benchmark.settled_lod_high_quality ? 0.375F : 0.75F;
    const auto simplified = simplify_stroke_samples(
        committed_samples, lod_storage.subspan(lod_used), center_error, radius_error);
    if (!simplified.empty() && committed_index < benchmark.settled_lod_first.size()) {
      benchmark.settled_lod_first[committed_index] = static_cast<std::uint32_t>(lod_used);
      benchmark.settled_lod_count[committed_index] = static_cast<std::uint16_t>(simplified.size());
      benchmark.settled_lod_samples = lod_storage.first(lod_used + simplified.size());
    }
  }
  const std::uint32_t next_revision = benchmark.document_revision.fetch_add(1U) + 1U;
  // Keep immutable fallback ownership fixed and repair only source bands whose
  // world regions overlap the appended stroke. Pending work is cumulative, so
  // cancellation or another mutation cannot accidentally certify an old band.
  RectF fallback_changed_bounds = strokes[committed_index].bounds;
  const float fallback_halo = kCanonicalHaloPixels / benchmark.fallback_source_atlas.zoom;
  fallback_changed_bounds.x0 -= fallback_halo;
  fallback_changed_bounds.y0 -= fallback_halo;
  fallback_changed_bounds.x1 += fallback_halo;
  fallback_changed_bounds.y1 += fallback_halo;
  if (benchmark.fallback_source_pinned) {
    benchmark.fallback_repair_target_revision = next_revision;
    for (int job = 0; job < kJobCount; ++job) {
      const JobRect rect = job_rect(job);
      const RectF source_world =
          camera_world_rect(benchmark.fallback_source_atlas, {rect.x0, rect.y0, rect.x1, rect.y1});
      if (rects_intersect(fallback_changed_bounds, source_world)) {
        benchmark.fallback_repair_pending[static_cast<std::size_t>(job)] = true;
      }
    }
    if (std::none_of(benchmark.fallback_repair_pending.begin(),
                     benchmark.fallback_repair_pending.end(),
                     [](bool pending) { return pending; })) {
      std::fill(benchmark.fallback_source_job_revision.begin(),
                benchmark.fallback_source_job_revision.end(), next_revision);
      benchmark.fallback_source_document_revision = next_revision;
    }
  }
  const int zoom = benchmark.active_zoom_index.load();
  benchmark.center_ready_us[static_cast<std::size_t>(zoom)].store(0U);
  benchmark.full_ready_us[static_cast<std::size_t>(zoom)].store(0U);
  // A mutation invalidates only bands touched by the new stroke. The live
  // raster path already merged those pixels into the displayed viewport;
  // unaffected cached bands remain valid across the document revision.
  const JobRect visible{
      .x0 = benchmark.view_x.load(),
      .y0 = benchmark.view_y.load(),
      .x1 = benchmark.view_x.load() + kCanvasWidth,
      .y1 = benchmark.view_y.load() + benchmark.presented_rows,
  };
  RectF changed_bounds = strokes[committed_index].bounds;
  const float world_halo = kCanonicalHaloPixels / benchmark.active_atlas.zoom;
  changed_bounds.x0 -= world_halo;
  changed_bounds.y0 -= world_halo;
  changed_bounds.x1 += world_halo;
  changed_bounds.y1 += world_halo;
  for (std::size_t job = 0; job < benchmark.ready.size(); ++job) {
    const JobRect rect = job_rect(static_cast<int>(job));
    const RectF world_bounds =
        camera_world_rect(benchmark.active_atlas, {rect.x0, rect.y0, rect.x1, rect.y1});
    if (!rects_intersect(changed_bounds, world_bounds)) {
      benchmark.job_revision[job].store(next_revision);
    } else if (visible_raster_current && rect.x0 >= visible.x0 && rect.y0 >= visible.y0 &&
               rect.x1 <= visible.x1 && rect.y1 <= visible.y1) {
      benchmark.job_revision[job].store(next_revision);
      // Only a band fully captured from the live viewport is current. Edge
      // bands with any off-viewport pixels stay invalid until vector redraw.
      const std::uint8_t previous_quality = benchmark.ready[job].load();
      benchmark.ready[job].store(previous_quality >= kSettledReady ? kSettledReady : kDerivedReady);
    } else {
      benchmark.job_revision[job].store(next_revision);
      benchmark.ready[job].store(kInvalidReady);
    }
  }
  benchmark.generation_started_us.store(static_cast<std::uint32_t>(esp_timer_get_time()));
  benchmark.paused.store(false);
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
}

void interactive_pan_benchmark_cancel_stroke(InteractivePanBenchmark& benchmark) {
  if (!benchmark.stroke_mutation_active.exchange(false)) {
    return;
  }
  benchmark.paused.store(false);
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
}

void interactive_pan_benchmark_record_draw_update(InteractivePanBenchmark& benchmark,
                                                  std::uint32_t elapsed_us) {
  if (benchmark.draw_samples < kTimingCapacity) {
    benchmark.timings.draw[benchmark.draw_samples++] = elapsed_us;
  }
  ++benchmark.draw_updates;
  benchmark.draw_total_us += elapsed_us;
  benchmark.draw_max_us = std::max(benchmark.draw_max_us, elapsed_us);
}

void interactive_pan_benchmark_begin_pan(InteractivePanBenchmark& benchmark, ViewOrigin origin,
                                         std::uint32_t event_us) {
  // Cancel settled rendering, but keep the render task runnable. It can repair
  // pinned-source provenance and fill cache runway between foreground pan
  // frames; pausing it here starved repair for as long as the user kept moving.
  benchmark.pan_active.store(true);
  benchmark.pan_rejected_views = 0U;
  benchmark.pan_maximum_missing_pixels = 0U;
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
  const int index = benchmark.active_zoom_index.load();
  auto& metrics = benchmark.metrics[static_cast<std::size_t>(index)];
  ++metrics.gestures;
  metrics.previous_origin = origin;
  metrics.previous_event_us = event_us;
}

bool interactive_pan_benchmark_view_ready(const InteractivePanBenchmark& benchmark,
                                          ViewOrigin origin) {
  return missing_pixels(benchmark, origin) == 0U;
}

bool interactive_pan_benchmark_view_changed(InteractivePanBenchmark& benchmark, ViewOrigin origin) {
  const std::uint32_t missing = missing_pixels(benchmark, origin);
  if (missing != 0U) {
    ++benchmark.pan_rejected_views;
    benchmark.pan_maximum_missing_pixels = std::max(benchmark.pan_maximum_missing_pixels, missing);
    return false;
  }
  benchmark.view_x.store(origin.x);
  benchmark.view_y.store(origin.y);
  return true;
}

void interactive_pan_benchmark_record_frame(InteractivePanBenchmark& benchmark, ViewOrigin origin,
                                            std::uint32_t event_us, std::uint32_t direct_present_us,
                                            std::uint32_t event_to_present_us) {
  const int index = benchmark.active_zoom_index.load();
  auto& metrics = benchmark.metrics[static_cast<std::size_t>(index)];
  if (metrics.timing_samples < kTimingCapacity) {
    benchmark.timings.direct[static_cast<std::size_t>(index)][metrics.timing_samples] =
        direct_present_us;
    benchmark.timings.event[static_cast<std::size_t>(index)][metrics.timing_samples] =
        event_to_present_us;
    ++metrics.timing_samples;
  }
  ++metrics.frames;
  metrics.direct_total_us += direct_present_us;
  metrics.event_total_us += event_to_present_us;
  const std::uint32_t missing = missing_pixels(benchmark, origin);
  if (missing != 0U) {
    ++metrics.miss_frames;
    metrics.maximum_missing_pixels = std::max(metrics.maximum_missing_pixels, missing);
  }
  const std::uint32_t interval = event_us - metrics.previous_event_us;
  if (interval != 0U) {
    const int delta_x = std::abs(origin.x - metrics.previous_origin.x);
    const int delta_y = std::abs(origin.y - metrics.previous_origin.y);
    const auto velocity = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(std::max(delta_x, delta_y)) * 1'000'000ULL / interval);
    metrics.maximum_velocity_px_s = std::max(metrics.maximum_velocity_px_s, velocity);
  }
  metrics.previous_origin = origin;
  metrics.previous_event_us = event_us;
}

void interactive_pan_benchmark_end_pan(InteractivePanBenchmark& benchmark) {
  benchmark.pan_active.store(false);
  const ViewOrigin origin{benchmark.view_x.load(), benchmark.view_y.load()};
  std::size_t visible_ink = 0U;
  std::uint32_t visible_hash = 2166136261U;
  xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
  for (int y = 0; y < benchmark.presented_rows; ++y) {
    const auto row = static_cast<std::size_t>((origin.y + y) * WorldCanvas::kWidth + origin.x);
    for (int x = 0; x < kCanvasWidth; ++x) {
      const std::uint16_t pixel = benchmark.world.pixels()[row + static_cast<std::size_t>(x)];
      visible_ink += pixel != kBackground;
      visible_hash = (visible_hash ^ pixel) * 16777619U;
    }
  }
  xSemaphoreGive(benchmark.cache_mutex);
  std::printf(
      "TINYDRAW_PAN_CONTENT zoom=%d origin=%d,%d visible_ink=%lu visible_hash=%08lx "
      "rejected_views=%lu max_missing_pixels=%lu\n",
      kZoomPercents[static_cast<std::size_t>(benchmark.active_zoom_index.load())], origin.x,
      origin.y, static_cast<unsigned long>(visible_ink), static_cast<unsigned long>(visible_hash),
      static_cast<unsigned long>(benchmark.pan_rejected_views),
      static_cast<unsigned long>(benchmark.pan_maximum_missing_pixels));
  benchmark.generation_started_us.store(static_cast<std::uint32_t>(esp_timer_get_time()));
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
}

void interactive_pan_benchmark_lock_cache(InteractivePanBenchmark& benchmark) {
  xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
}

void interactive_pan_benchmark_unlock_cache(InteractivePanBenchmark& benchmark) {
  xSemaphoreGive(benchmark.cache_mutex);
}

bool finish_interactive_pan_benchmark(InteractivePanBenchmark& benchmark) {
  if (benchmark.finished.exchange(true)) {
    return true;
  }
  benchmark.paused.store(true);
  benchmark.requested_generation.fetch_add(1U);
  xTaskNotifyGive(benchmark.render_task);
  const std::uint32_t wait_started = static_cast<std::uint32_t>(esp_timer_get_time());
  while (benchmark.rendering.load() &&
         static_cast<std::uint32_t>(esp_timer_get_time()) - wait_started < 5'000'000U) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  const bool persisted = persist_report(benchmark);
  std::printf("TINYDRAW_INTERACTIVE_PAN_DONE persisted=%u\n", persisted);
  return persisted;
}

}  // namespace tinydraw::esp32
