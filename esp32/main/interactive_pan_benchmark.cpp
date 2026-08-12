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
#include "tinydraw/document/stroke_macrogrid.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/raster_materializer.h"
#include "tinydraw/graphics/viewport_renderer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::size_t kStrokeCount = 1'000U;
constexpr std::size_t kSamplesPerStroke = 12U;
constexpr std::size_t kSampleCount = kStrokeCount * kSamplesPerStroke;
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

bool populate_coherent_handwriting(VectorDocument& document) {
  document.clear();
  for (std::size_t stroke = 0; stroke < kStrokeCount; ++stroke) {
    const float stroke_number = static_cast<float>(stroke);
    const float base_x =
        5.0F + static_cast<float>((stroke * 37U) % static_cast<std::size_t>(kCanvasWidth - 40));
    const float base_y =
        20.0F + static_cast<float>((stroke * 53U) % static_cast<std::size_t>(kCanvasHeight - 40));
    auto sample_at = [&](std::size_t sample) {
      const float sample_number = static_cast<float>(sample);
      return StrokeSample{
          .x = static_cast<float>(kCenterOriginX) + base_x + sample_number * 3.0F,
          .y = static_cast<float>(kCenterOriginY) + base_y +
               std::sin(sample_number * 0.8F + stroke_number * 0.13F) * 12.0F,
          .radius = 2.0F + 1.5F * std::sin(sample_number * 0.31F + 1.0F),
      };
    };
    StrokeSample last = sample_at(0U);
    if (!document.begin_stroke(static_cast<std::uint16_t>(0x001FU + stroke % 12U), VectorTool::kPen,
                               last)) {
      return false;
    }
    for (std::size_t sample = 1U; sample < kSamplesPerStroke - 1U; ++sample) {
      last = sample_at(sample);
      if (!document.append(last)) {
        document.cancel_stroke();
        return false;
      }
    }
    // Match the existing handwriting benchmark: the final stored sample
    // duplicates the last unique point to model the release-time endpoint.
    if (!document.append(last) || !document.finish_stroke()) {
      document.cancel_stroke();
      return false;
    }
  }
  return document.stroke_count() == kStrokeCount && document.sample_count() == kSampleCount;
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
  ViewportRenderer* renderer = nullptr;
  void* renderer_storage_ = nullptr;
  std::uint64_t* index_storage_ = nullptr;
  void (*refinement_published)(void*) = nullptr;
  void* refinement_published_context = nullptr;
  DisplayTransferTelemetry transfer_telemetry{};
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
  Camera fallback_source_atlas = atlas_camera(1.0F);
  std::array<std::uint8_t, kJobCount> fallback_source_ready{};
  std::array<std::uint32_t, kJobCount> fallback_source_job_revision{};
  std::uint32_t fallback_source_document_revision = 0U;
  std::atomic<int> view_x{kCenterOriginX};
  std::atomic<int> view_y{kCenterOriginY};
  // 0 = dirty, 1 = being cleared, 2 = clean and ready as a destination.
  std::atomic<std::uint8_t> materialization_state{2U};
  std::atomic<bool> rendering{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> zoom_presentation_pending{false};
  std::atomic<bool> stroke_mutation_active{false};
  bool has_complete_initial_atlas = false;
  std::atomic<std::uint32_t> document_revision{1U};
  std::atomic<bool> finished{false};
};

namespace {

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
  if (!benchmark.has_complete_initial_atlas) {
    return;
  }
  const JobRect rect = job_rect(job);
  const int view_x = benchmark.view_x.load();
  const int view_y = benchmark.view_y.load();
  const int x0 = std::max(rect.x0, view_x);
  const int y0 = std::max(rect.y0, view_y);
  const int x1 = std::min(rect.x1, view_x + kCanvasWidth);
  const int y1 = std::min(rect.y1, view_y + benchmark.presented_rows);
  if (x0 >= x1 || y0 >= y1) {
    return;
  }
  const auto offset = static_cast<std::size_t>(y0 * WorldCanvas::kWidth + x0);
  benchmark.display.push_rect(x0 - view_x, y0 - view_y, x1 - x0, y1 - y0,
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

    // Fill valid neighboring bands from the previous atlas after first-visible
    // presentation. This work is cancelable at band boundaries and makes pan
    // runway available before slower canonical refinement reaches it.
    std::uint8_t dirty = 0U;
    if (benchmark.materialization_state.compare_exchange_strong(dirty, 1U)) {
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
        if (benchmark.requested_generation.load() == generation) {
          if (active_atlas.zoom > source_atlas.zoom) {
            resample_bilinear_rgb565_region(
                benchmark.materialization_storage, WorldCanvas::kWidth, WorldCanvas::kHeight,
                source_atlas, benchmark.world.pixels(), WorldCanvas::kWidth, WorldCanvas::kHeight,
                WorldCanvas::kWidth, active_atlas, pixels, kBackground);
            benchmark.ready[slot].store(kSettledReady);
          } else {
            resample_valid_raster_region(
                benchmark.materialization_storage, WorldCanvas::kWidth, WorldCanvas::kHeight,
                source_atlas, benchmark.world.pixels(), WorldCanvas::kWidth, WorldCanvas::kHeight,
                WorldCanvas::kWidth, active_atlas, pixels, kBackground);
            benchmark.ready[slot].store(kDerivedReady);
          }
          benchmark.job_revision[slot].store(benchmark.document_revision.load());
          present_job(benchmark, job);
        }
        xSemaphoreGive(benchmark.cache_mutex);
        if (benchmark.refinement_published != nullptr &&
            benchmark.requested_generation.load() == generation) {
          benchmark.refinement_published(benchmark.refinement_published_context);
        }
      }
      std::fill_n(benchmark.materialization_storage.begin(), WorldCanvas::kRequiredPixels,
                  kBackground);
      benchmark.materialization_state.store(2U);
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
      options.candidate_strokes = benchmark.macrogrid.query(
          {.x0 = static_cast<float>(camera.x) - world_halo,
           .y0 = static_cast<float>(camera.y + local_y * inverse_zoom) - world_halo,
           .x1 = static_cast<float>(camera.x + kCanvasWidth * inverse_zoom) + world_halo,
           .y1 = static_cast<float>(camera.y + (local_y + kBandRows) * inverse_zoom) + world_halo});
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
      if (benchmark.refinement_published != nullptr &&
          benchmark.requested_generation.load() == generation) {
        benchmark.refinement_published(benchmark.refinement_published_context);
      }
      if (benchmark.center_ready_us[static_cast<std::size_t>(zoom)].load() == 0U &&
          center_ready(benchmark)) {
        const std::uint32_t elapsed = static_cast<std::uint32_t>(esp_timer_get_time()) -
                                      benchmark.generation_started_us.load();
        benchmark.center_ready_us[static_cast<std::size_t>(zoom)].store(elapsed);
      }
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
                "TINYDRAW_INTERACTIVE_PAN_V1 workload=periodic_handwriting1000 "
                "cache=3x3 band_rows=%d presented_rows=%d timing_capacity=%lu\n",
                kBandRows, benchmark.presented_rows, static_cast<unsigned long>(kTimingCapacity));
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
  renderer->~ViewportRenderer();
  benchmark.~InteractivePanBenchmark();
  heap_caps_free(renderer_storage);
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
      document.stroke_capacity() < kStrokeCount || document.sample_capacity() < kSampleCount ||
      presented_rows <= 0 || presented_rows > kCanvasHeight) {
    return nullptr;
  }
  auto* scratch =
      static_cast<std::uint8_t*>(heap_caps_malloc(ViewportRenderer::kScratchBytes, kExternalCaps));
  auto* timings =
      static_cast<TimingStorage*>(heap_caps_calloc(1U, sizeof(TimingStorage), kExternalCaps));
  auto* index_storage = static_cast<std::uint64_t*>(heap_caps_calloc(
      (StrokeMacrogrid::kCellCount + 1U) * kIndexWords, sizeof(std::uint64_t), kExternalCaps));
  void* renderer_storage = heap_caps_malloc(sizeof(ViewportRenderer), kInternalCaps);
  void* benchmark_storage = heap_caps_malloc(sizeof(InteractivePanBenchmark), kInternalCaps);
  if (scratch == nullptr || timings == nullptr || index_storage == nullptr ||
      renderer_storage == nullptr || benchmark_storage == nullptr) {
    heap_caps_free(benchmark_storage);
    heap_caps_free(renderer_storage);
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
  if (!populate_coherent_handwriting(document) || !benchmark->macrogrid.rebuild(document)) {
    destroy_benchmark(*benchmark);
    return nullptr;
  }
  std::fill_n(world.pixels().begin(), WorldCanvas::kRequiredPixels, kBackground);
  // materialization_state == 2 means the inactive arena is already white and
  // safe to reuse. Establish that invariant once before the first zoom; later
  // generations restore it in render_task_entry() before publishing state 2.
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
  while (benchmark.materialization_state.load() != 2U) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  const Rect visible{.x0 = kCenterOriginX,
                     .y0 = kCenterOriginY,
                     .x1 = kCenterOriginX + kCanvasWidth,
                     .y1 = kCenterOriginY + benchmark.presented_rows};
  const std::uint32_t old_document_revision = benchmark.document_revision.load();
  for (std::size_t job = 0; job < benchmark.ready.size(); ++job) {
    benchmark.fallback_source_ready[job] = benchmark.ready[job].load();
    benchmark.fallback_source_job_revision[job] = benchmark.job_revision[job].load();
  }
  benchmark.fallback_source_document_revision = old_document_revision;

  // Never publish a fallback assembled from missing raster content. Keep the
  // current zoom active until its background runway can support the request.
  // This is intentionally conservative; invalid source bands proven blank by
  // the vector document do not block the transition.
  std::array<bool, kJobCount> fallback_valid{};
  if (benchmark.has_complete_initial_atlas) {
    for (int job = 0; job < kJobCount; ++job) {
      const JobRect rect = job_rect(job);
      if (!intersects(rect, {visible.x0, visible.y0, visible.x1, visible.y1})) {
        continue;
      }
      const Rect pixels{rect.x0, rect.y0, rect.x1, rect.y1};
      const bool valid = source_region_materialized(
          benchmark, old_camera, new_camera, pixels, benchmark.fallback_source_ready,
          benchmark.fallback_source_job_revision, old_document_revision);
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

  // Exchange storage first: the inactive arena is already white because
  // set_zoom waited for materialization_state == 2, so visible strips can be
  // materialized into it and pushed as each becomes ready while the retired
  // atlas stays readable as the resample source.
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
  benchmark.fallback_source_atlas = old_camera;
  benchmark.requested_zoom_index.store(index);
  benchmark.active_zoom_index.store(index);
  benchmark.active_atlas = new_camera;
  benchmark.requested_generation.fetch_add(1U);
  benchmark.generation_started_us.store(event_started);
  for (std::size_t job = 0; job < benchmark.ready.size(); ++job) {
    const JobRect rect = job_rect(static_cast<int>(job));
    const bool visible_job = intersects(rect, {visible.x0, visible.y0, visible.x1, visible.y1});
    const bool valid = benchmark.has_complete_initial_atlas && visible_job && fallback_valid[job];
    benchmark.job_revision[job].store(benchmark.document_revision.load());
    // Nearest-resampled first previews are derived quality in both zoom
    // directions; settled output now requires a settled or exact pass.
    benchmark.ready[job].store(valid ? kDerivedReady : kInvalidReady);
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
    resample_valid_raster_region(benchmark.materialization_storage, WorldCanvas::kWidth,
                                 WorldCanvas::kHeight, old_camera, benchmark.world.pixels(),
                                 WorldCanvas::kWidth, WorldCanvas::kHeight, WorldCanvas::kWidth,
                                 new_camera, region, kBackground);
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
  const std::uint32_t fallback_elapsed =
      static_cast<std::uint32_t>(esp_timer_get_time()) - event_started;
  benchmark.fallback_ready_us[slot].store(fallback_elapsed);
  benchmark.materialization_state.store(0U);
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
      const std::uint32_t elapsed = static_cast<std::uint32_t>(last_completed) - event_started;
      benchmark.last_visible_complete_us[slot].store(elapsed);
      if (zoom_percent <= 100) {
        benchmark.settled_us[slot].store(elapsed);
      }
    }
  } else if (benchmark.has_complete_initial_atlas) {
    // Without completion telemetry, fall back to submit-time endpoints.
    benchmark.first_valid_us[slot].store(benchmark.first_strip_submit_us[slot].load());
    if (zoom_percent <= 100) {
      benchmark.settled_us[slot].store(benchmark.last_visible_submit_us[slot].load());
    }
  }
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
  if (benchmark.settled_us[static_cast<std::size_t>(index)].load() == 0U &&
      kZoomPercents[static_cast<std::size_t>(index)] <= 100) {
    benchmark.settled_us[static_cast<std::size_t>(index)].store(elapsed);
  }
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

void interactive_pan_benchmark_commit_stroke(InteractivePanBenchmark& benchmark) {
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
  if (committed_index >= strokes.size() ||
      !benchmark.macrogrid.append(committed_index, strokes[committed_index].bounds)) {
    std::printf("TINYDRAW_INTERACTIVE_PAN_FAIL index_append=0 stroke=%lu\n",
                static_cast<unsigned long>(committed_index));
  }
  const std::uint32_t next_revision = benchmark.document_revision.fetch_add(1U) + 1U;
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
    } else if (intersects(rect, visible)) {
      benchmark.job_revision[job].store(next_revision);
      benchmark.ready[job].store(kDerivedReady);
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
  const int index = benchmark.active_zoom_index.load();
  auto& metrics = benchmark.metrics[static_cast<std::size_t>(index)];
  ++metrics.gestures;
  metrics.previous_origin = origin;
  metrics.previous_event_us = event_us;
}

bool interactive_pan_benchmark_view_changed(InteractivePanBenchmark& benchmark, ViewOrigin origin) {
  if (missing_pixels(benchmark, origin) != 0U) {
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

void interactive_pan_benchmark_end_pan(InteractivePanBenchmark&) {}

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
