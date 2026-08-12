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
#include "tinydraw/geometry.h"
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
constexpr std::size_t kTimingCapacity = 256U;
constexpr std::size_t kReportBytes = 8'192U;
constexpr std::array kZoomPercents{50, 100, 200};
constexpr std::uint16_t kMissA = 0xF81FU;
constexpr std::uint16_t kMissB = 0xFFE0U;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr std::uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr esp_partition_subtype_t kExportSubtype = static_cast<esp_partition_subtype_t>(0x41);

struct TimingStorage {
  std::array<std::array<std::uint32_t, kTimingCapacity>, kZoomPercents.size()> direct{};
  std::array<std::array<std::uint32_t, kTimingCapacity>, kZoomPercents.size()> event{};
};

struct ZoomMetrics {
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

void fill_miss_pattern(WorldCanvas& world) {
  auto pixels = world.pixels();
  for (int y = 0; y < WorldCanvas::kHeight; ++y) {
    for (int x = 0; x < WorldCanvas::kWidth; ++x) {
      const bool alternate = ((x / 16) + (y / 16)) % 2 == 0;
      pixels[static_cast<std::size_t>(y * WorldCanvas::kWidth + x)] = alternate ? kMissA : kMissB;
    }
  }
}

bool populate_periodic_handwriting(VectorDocument& document, float zoom) {
  document.clear();
  for (std::size_t stroke = 0; stroke < kStrokeCount; ++stroke) {
    const float stroke_number = static_cast<float>(stroke);
    const float base_x =
        static_cast<float>((stroke * 37U) % static_cast<std::size_t>(kCanvasWidth));
    const float base_y =
        static_cast<float>((stroke * 53U) % static_cast<std::size_t>(kCanvasHeight));
    auto sample_at = [&](std::size_t sample) {
      const float sample_number = static_cast<float>(sample);
      return StrokeSample{
          .x = (base_x + sample_number * 3.0F) / zoom,
          .y = (base_y + std::sin(sample_number * 0.8F + stroke_number * 0.13F) * 12.0F) / zoom,
          .radius = (2.0F + 1.5F * std::sin(sample_number * 0.31F + 1.0F)) / zoom,
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
                          std::span<std::uint16_t> render_buffer_value, int presented_rows_value,
                          std::span<std::uint8_t> scratch_value, TimingStorage& timings_value,
                          void* renderer_storage)
      : document(document_value),
        world(world_value),
        render_buffer(render_buffer_value),
        presented_rows(presented_rows_value),
        scratch(scratch_value),
        timings(timings_value),
        renderer(new (renderer_storage) ViewportRenderer(scratch_value)),
        renderer_storage_(renderer_storage) {
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
  std::span<std::uint16_t> render_buffer;
  int presented_rows = 0;
  std::span<std::uint8_t> scratch;
  TimingStorage& timings;
  ViewportRenderer* renderer = nullptr;
  void* renderer_storage_ = nullptr;
  SemaphoreHandle_t cache_mutex = nullptr;
  TaskHandle_t render_task = nullptr;
  std::array<std::atomic<std::uint8_t>, kJobCount> ready{};
  std::array<ZoomMetrics, kZoomPercents.size()> metrics{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> center_ready_us{};
  std::array<std::atomic<std::uint32_t>, kZoomPercents.size()> full_ready_us{};
  std::atomic<std::uint32_t> requested_generation{0U};
  std::atomic<std::uint32_t> generation_started_us{0U};
  std::atomic<int> requested_zoom_index{1};
  std::atomic<int> active_zoom_index{1};
  std::atomic<int> view_x{kCenterOriginX};
  std::atomic<int> view_y{kCenterOriginY};
  std::atomic<bool> rendering{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> finished{false};
};

namespace {

bool center_ready(const InteractivePanBenchmark& benchmark) {
  constexpr int center_cell = 4;
  const int first = center_cell * kBandsPerCell;
  for (int band = 0; band < kBandsPerCell; ++band) {
    if (benchmark.ready[static_cast<std::size_t>(first + band)].load() == 0U) {
      return false;
    }
  }
  return true;
}

int choose_job(const InteractivePanBenchmark& benchmark) {
  constexpr int center_first = 4 * kBandsPerCell;
  for (int band = 0; band < kBandsPerCell; ++band) {
    const int job = center_first + band;
    if (benchmark.ready[static_cast<std::size_t>(job)].load() == 0U) {
      return job;
    }
  }
  const JobRect view{
      .x0 = benchmark.view_x.load(),
      .y0 = benchmark.view_y.load(),
      .x1 = benchmark.view_x.load() + kCanvasWidth,
      .y1 = benchmark.view_y.load() + benchmark.presented_rows,
  };
  int best_job = -1;
  int best_score = 0;
  for (int job = 0; job < kJobCount; ++job) {
    if (benchmark.ready[static_cast<std::size_t>(job)].load() != 0U) {
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
    if (benchmark.paused.load() || generation == handled_generation) {
      benchmark.rendering.store(false);
      continue;
    }
    handled_generation = generation;
    benchmark.rendering.store(true);
    const int zoom = benchmark.requested_zoom_index.load();
    const float zoom_value =
        static_cast<float>(kZoomPercents[static_cast<std::size_t>(zoom)]) / 100.0F;
    if (!populate_periodic_handwriting(benchmark.document, zoom_value)) {
      std::printf("TINYDRAW_INTERACTIVE_PAN_FAIL populate=0 zoom=%d\n",
                  kZoomPercents[static_cast<std::size_t>(zoom)]);
      benchmark.rendering.store(false);
      continue;
    }

    RenderCancellation cancellation{.benchmark = &benchmark, .generation = generation};
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
      const ViewportRenderStats render_stats = benchmark.renderer->render_region(
          benchmark.document, {.zoom = zoom_value}, benchmark.render_buffer,
          {.x0 = 0, .y0 = local_y, .x1 = kCanvasWidth, .y1 = local_y + kBandRows}, options);
      if (!render_stats.complete || benchmark.requested_generation.load() != generation ||
          benchmark.paused.load()) {
        break;
      }
      xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
      if (benchmark.requested_generation.load() == generation && !benchmark.paused.load()) {
        copy_job_to_cache(benchmark, job);
        benchmark.ready[static_cast<std::size_t>(job)].store(1U);
      }
      xSemaphoreGive(benchmark.cache_mutex);
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
    if (benchmark.ready[static_cast<std::size_t>(job)].load() != 0U) {
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
    append_report(
        report, size,
        "ZOOM zoom=%d gestures=%lu frames=%lu samples=%lu direct_avg_us=%llu "
        "direct_median_us=%lu direct_p95_us=%lu direct_max_us=%lu event_avg_us=%llu "
        "event_median_us=%lu event_p95_us=%lu event_max_us=%lu miss_frames=%lu "
        "max_missing_pixels=%lu max_velocity_px_s=%lu center_ready_us=%lu full_ready_us=%lu\n",
        kZoomPercents[index], static_cast<unsigned long>(metrics.gestures),
        static_cast<unsigned long>(metrics.frames), static_cast<unsigned long>(samples),
        static_cast<unsigned long long>(
            metrics.frames == 0U ? 0U : metrics.direct_total_us / metrics.frames),
        static_cast<unsigned long>(direct_median), static_cast<unsigned long>(direct_p95),
        static_cast<unsigned long>(direct_max),
        static_cast<unsigned long long>(
            metrics.frames == 0U ? 0U : metrics.event_total_us / metrics.frames),
        static_cast<unsigned long>(event_median), static_cast<unsigned long>(event_p95),
        static_cast<unsigned long>(event_max), static_cast<unsigned long>(metrics.miss_frames),
        static_cast<unsigned long>(metrics.maximum_missing_pixels),
        static_cast<unsigned long>(metrics.maximum_velocity_px_s),
        static_cast<unsigned long>(benchmark.center_ready_us[index].load()),
        static_cast<unsigned long>(benchmark.full_ready_us[index].load()));
  }
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
  renderer->~ViewportRenderer();
  benchmark.~InteractivePanBenchmark();
  heap_caps_free(renderer_storage);
  heap_caps_free(timings);
  heap_caps_free(scratch);
  heap_caps_free(&benchmark);
}

}  // namespace

InteractivePanBenchmark* start_interactive_pan_benchmark(VectorDocument& document,
                                                         WorldCanvas& world,
                                                         std::span<std::uint16_t> render_buffer,
                                                         int presented_rows) {
  if (!world.valid() || render_buffer.size() < ViewportRenderer::kPixelCount ||
      document.stroke_capacity() < kStrokeCount || document.sample_capacity() < kSampleCount ||
      presented_rows <= 0 || presented_rows > kCanvasHeight) {
    return nullptr;
  }
  auto* scratch =
      static_cast<std::uint8_t*>(heap_caps_malloc(ViewportRenderer::kScratchBytes, kExternalCaps));
  auto* timings =
      static_cast<TimingStorage*>(heap_caps_calloc(1U, sizeof(TimingStorage), kExternalCaps));
  void* renderer_storage = heap_caps_malloc(sizeof(ViewportRenderer), kInternalCaps);
  void* benchmark_storage = heap_caps_malloc(sizeof(InteractivePanBenchmark), kInternalCaps);
  if (scratch == nullptr || timings == nullptr || renderer_storage == nullptr ||
      benchmark_storage == nullptr) {
    heap_caps_free(benchmark_storage);
    heap_caps_free(renderer_storage);
    heap_caps_free(timings);
    heap_caps_free(scratch);
    return nullptr;
  }
  auto* benchmark = new (benchmark_storage) InteractivePanBenchmark(
      document, world, render_buffer, presented_rows,
      std::span(scratch, ViewportRenderer::kScratchBytes), *timings, renderer_storage);
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
  return benchmark;
}

bool interactive_pan_benchmark_set_zoom(InteractivePanBenchmark& benchmark, int zoom_percent) {
  const int index = zoom_index(zoom_percent);
  if (index < 0 || benchmark.finished.load()) {
    return false;
  }
  benchmark.paused.store(false);
  benchmark.center_ready_us[static_cast<std::size_t>(index)].store(0U);
  benchmark.full_ready_us[static_cast<std::size_t>(index)].store(0U);

  // Cache publication uses this same mutex. Keeping the new generation,
  // readiness reset, and replacement pixels in one critical section prevents
  // an old renderer from marking freshly replaced checkerboard pixels ready.
  xSemaphoreTake(benchmark.cache_mutex, portMAX_DELAY);
  benchmark.requested_zoom_index.store(index);
  benchmark.active_zoom_index.store(index);
  const std::uint32_t generation = benchmark.requested_generation.fetch_add(1U) + 1U;
  benchmark.generation_started_us.store(static_cast<std::uint32_t>(esp_timer_get_time()));
  for (auto& value : benchmark.ready) {
    value.store(0U);
  }
  benchmark.view_x.store(kCenterOriginX);
  benchmark.view_y.store(kCenterOriginY);
  fill_miss_pattern(benchmark.world);
  static_cast<void>(benchmark.world.move_to({kCenterOriginX, kCenterOriginY}));
  xSemaphoreGive(benchmark.cache_mutex);
  xTaskNotifyGive(benchmark.render_task);

  const std::uint32_t wait_started = static_cast<std::uint32_t>(esp_timer_get_time());
  while (benchmark.center_ready_us[static_cast<std::size_t>(index)].load() == 0U) {
    if (benchmark.requested_generation.load() != generation ||
        static_cast<std::uint32_t>(esp_timer_get_time()) - wait_started > 30'000'000U) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return true;
}

void interactive_pan_benchmark_begin_pan(InteractivePanBenchmark& benchmark, ViewOrigin origin,
                                         std::uint32_t event_us) {
  const int index = benchmark.active_zoom_index.load();
  auto& metrics = benchmark.metrics[static_cast<std::size_t>(index)];
  ++metrics.gestures;
  metrics.previous_origin = origin;
  metrics.previous_event_us = event_us;
  interactive_pan_benchmark_view_changed(benchmark, origin);
}

void interactive_pan_benchmark_view_changed(InteractivePanBenchmark& benchmark, ViewOrigin origin) {
  benchmark.view_x.store(origin.x);
  benchmark.view_y.store(origin.y);
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
