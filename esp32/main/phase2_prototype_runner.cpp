#include "phase2_prototype_runner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/document/vector_benchmark.h"
#include "tinydraw/graphics/viewport_renderer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::size_t kMaxStrokes = 1'000U;
constexpr std::size_t kMaxSamples = 12'000U;
constexpr std::size_t kReportBytes = 8'192U;
constexpr int kPanPixels = 32;
constexpr int kRefineRows = 32;
constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr esp_partition_subtype_t kExportSubtype = static_cast<esp_partition_subtype_t>(0x41);

struct PrototypeCase {
  const char* name;
  std::size_t strokes;
  VectorBenchmarkPattern pattern;
};

constexpr std::array kCases{
    PrototypeCase{"visible1000", 1'000U, VectorBenchmarkPattern::kManyVisible},
    PrototypeCase{"handwriting1000", 1'000U, VectorBenchmarkPattern::kHandwriting},
    PrototypeCase{"dense100", 100U, VectorBenchmarkPattern::kLongDense},
};

struct ParallelBridge {
  SemaphoreHandle_t start = nullptr;
  SemaphoreHandle_t done = nullptr;
  void (*work)(void*, int) = nullptr;
  void* work_context = nullptr;
  TaskHandle_t worker = nullptr;
};

void benchmark_yield(void*) { vTaskDelay(1U); }

void parallel_worker(void* raw) {
  auto* bridge = static_cast<ParallelBridge*>(raw);
  while (true) {
    xSemaphoreTake(bridge->start, portMAX_DELAY);
    bridge->work(bridge->work_context, 1);
    xSemaphoreGive(bridge->done);
  }
}

void execute_parallel(void* raw, void (*work)(void*, int), void* work_context) {
  auto* bridge = static_cast<ParallelBridge*>(raw);
  bridge->work = work;
  bridge->work_context = work_context;
  xSemaphoreGive(bridge->start);
  work(work_context, 0);
  xSemaphoreTake(bridge->done, portMAX_DELAY);
}

ViewportRenderOptions render_options(ParallelBridge& bridge) {
  return {
      .background = kBackground,
      .yield = benchmark_yield,
      .yield_every_tiles = 16U,
      .execute = execute_parallel,
      .execute_context = &bridge,
  };
}

bool persist_report(const esp_partition_t* partition, std::size_t offset, const char* report) {
  return esp_partition_erase_range(partition, offset, kReportBytes) == ESP_OK &&
         esp_partition_write(partition, offset, report, kReportBytes) == ESP_OK;
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

std::uint32_t timestamp_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

void shift_for_camera_delta(std::span<std::uint16_t> pixels, int delta_x, int delta_y) {
  const int destination_x = std::max(0, -delta_x);
  const int source_x = std::max(0, delta_x);
  const int columns = kCanvasWidth - std::abs(delta_x);
  const int first_y = delta_y >= 0 ? 0 : kCanvasHeight - 1;
  const int last_y = delta_y >= 0 ? kCanvasHeight - delta_y : -delta_y - 1;
  const int step_y = delta_y >= 0 ? 1 : -1;
  for (int y = first_y; y != last_y; y += step_y) {
    const int source_y = y + delta_y;
    auto* destination =
        pixels.data() + static_cast<std::ptrdiff_t>(y * kCanvasWidth + destination_x);
    const auto* source =
        pixels.data() + static_cast<std::ptrdiff_t>(source_y * kCanvasWidth + source_x);
    std::memmove(destination, source, static_cast<std::size_t>(columns) * sizeof(std::uint16_t));
  }
}

Camera camera_after_pan(Camera camera, int delta_x, int delta_y) {
  camera.x += static_cast<double>(delta_x) / static_cast<double>(camera.zoom);
  camera.y += static_cast<double>(delta_y) / static_cast<double>(camera.zoom);
  return camera;
}

Camera camera_zoomed_about_center(Camera camera, float zoom) {
  const double center_x = camera.x + static_cast<double>(kCanvasWidth) / (2.0 * camera.zoom);
  const double center_y = camera.y + static_cast<double>(kCanvasHeight) / (2.0 * camera.zoom);
  return {
      .x = center_x - static_cast<double>(kCanvasWidth) / (2.0 * zoom),
      .y = center_y - static_cast<double>(kCanvasHeight) / (2.0 * zoom),
      .zoom = zoom,
  };
}

void build_zoom_preview(std::span<const std::uint16_t> source, Camera source_camera,
                        std::span<std::uint16_t> destination, Camera destination_camera) {
  // The mapping is affine. Avoid software double division and rounding per
  // pixel by calculating one 16.16 start and step per axis.
  constexpr std::int64_t kFixedOne = 1LL << 16;
  constexpr std::int64_t kFixedHalf = kFixedOne / 2;
  const double source_step =
      static_cast<double>(source_camera.zoom) / static_cast<double>(destination_camera.zoom);
  const double source_start_x =
      (destination_camera.x - source_camera.x) * source_camera.zoom + source_step * 0.5 - 0.5;
  const double source_start_y =
      (destination_camera.y - source_camera.y) * source_camera.zoom + source_step * 0.5 - 0.5;
  const auto step = static_cast<std::int64_t>(std::llround(source_step * kFixedOne));
  const auto start_x = static_cast<std::int64_t>(std::llround(source_start_x * kFixedOne));
  std::int64_t source_y_fixed = static_cast<std::int64_t>(std::llround(source_start_y * kFixedOne));
  for (int y = 0; y < kCanvasHeight; ++y) {
    const int source_y = static_cast<int>((source_y_fixed + kFixedHalf) >> 16);
    std::int64_t source_x_fixed = start_x;
    for (int x = 0; x < kCanvasWidth; ++x) {
      const int source_x = static_cast<int>((source_x_fixed + kFixedHalf) >> 16);
      const auto destination_index = static_cast<std::size_t>(y * kCanvasWidth + x);
      if (source_x >= 0 && source_x < kCanvasWidth && source_y >= 0 && source_y < kCanvasHeight) {
        destination[destination_index] =
            source[static_cast<std::size_t>(source_y * kCanvasWidth + source_x)];
      } else {
        destination[destination_index] = kBackground;
      }
      source_x_fixed += step;
    }
    source_y_fixed += step;
  }
}

std::size_t pixel_differences(std::span<const std::uint16_t> left,
                              std::span<const std::uint16_t> right) {
  std::size_t differences = 0;
  for (std::size_t index = 0; index < left.size(); ++index) {
    differences += left[index] != right[index] ? 1U : 0U;
  }
  return differences;
}

Phase2TouchPollStats measured_render(Phase2TouchProbe& probe, std::int64_t& elapsed_us,
                                     const auto& operation) {
  probe.begin(timestamp_us());
  const auto started = esp_timer_get_time();
  operation();
  elapsed_us = esp_timer_get_time() - started;
  return probe.finish();
}

void append_touch(char* report, std::size_t& report_size, Phase2TouchPollStats touch) {
  append_report(report, report_size, " polls=%lu touch_avg_us=%lu touch_max_us=%lu",
                static_cast<unsigned long>(touch.polls),
                static_cast<unsigned long>(touch.average_interval_us),
                static_cast<unsigned long>(touch.maximum_interval_us));
}

void run_pan_case(const PrototypeCase& prototype, int delta_x, int delta_y,
                  const VectorDocument& document, ViewportRenderer& renderer,
                  std::span<std::uint16_t> cache, std::span<std::uint16_t> reference,
                  DisplayBackend& display, ParallelBridge& bridge, Phase2TouchProbe& touch_probe,
                  char* report, std::size_t& report_size) {
  const Camera old_camera{.x = 32.0, .y = 32.0, .zoom = 1.0F};
  const Camera new_camera = camera_after_pan(old_camera, delta_x, delta_y);
  auto options = render_options(bridge);
  static_cast<void>(renderer.render(document, old_camera, cache, options));

  ViewportRenderStats strip_stats;
  std::int64_t incremental_us = 0;
  const auto touch = measured_render(touch_probe, incremental_us, [&] {
    shift_for_camera_delta(cache, delta_x, delta_y);
    if (delta_x > 0) {
      strip_stats = renderer.render_region(
          document, new_camera, cache,
          {.x0 = kCanvasWidth - delta_x, .y0 = 0, .x1 = kCanvasWidth, .y1 = kCanvasHeight},
          options);
    } else if (delta_x < 0) {
      strip_stats =
          renderer.render_region(document, new_camera, cache,
                                 {.x0 = 0, .y0 = 0, .x1 = -delta_x, .y1 = kCanvasHeight}, options);
    } else if (delta_y > 0) {
      strip_stats = renderer.render_region(
          document, new_camera, cache,
          {.x0 = 0, .y0 = kCanvasHeight - delta_y, .x1 = kCanvasWidth, .y1 = kCanvasHeight},
          options);
    } else {
      strip_stats =
          renderer.render_region(document, new_camera, cache,
                                 {.x0 = 0, .y0 = 0, .x1 = kCanvasWidth, .y1 = -delta_y}, options);
    }
  });

  const auto present_started = esp_timer_get_time();
  display.push_rect(0, 0, kCanvasWidth, kCanvasHeight, cache.data(), kCanvasWidth);
  const auto present_us = esp_timer_get_time() - present_started;
  const auto full_started = esp_timer_get_time();
  const auto full_stats = renderer.render(document, new_camera, reference, options);
  const auto full_us = esp_timer_get_time() - full_started;
  const std::size_t differences = pixel_differences(cache, reference);

  append_report(report, report_size,
                "PAN case=%s dx=%d dy=%d strip_us=%lld present_us=%lld full_us=%lld exact=%u "
                "diff_pixels=%lu complete=%u strip_strokes=%lu strip_tiles=%lu full_strokes=%lu",
                prototype.name, delta_x, delta_y, static_cast<long long>(incremental_us),
                static_cast<long long>(present_us), static_cast<long long>(full_us),
                differences == 0U, static_cast<unsigned long>(differences),
                strip_stats.complete && full_stats.complete,
                static_cast<unsigned long>(strip_stats.strokes_intersecting),
                static_cast<unsigned long>(strip_stats.tiles_composited),
                static_cast<unsigned long>(full_stats.strokes_intersecting));
  append_touch(report, report_size, touch);
  append_report(report, report_size, "\n");
}

void run_zoom_case(const PrototypeCase& prototype, float zoom, const VectorDocument& document,
                   ViewportRenderer& renderer, std::span<std::uint16_t> cache,
                   std::span<std::uint16_t> preview, DisplayBackend& display,
                   ParallelBridge& bridge, Phase2TouchProbe& touch_probe, char* report,
                   std::size_t& report_size) {
  const Camera old_camera{.x = 32.0, .y = 32.0, .zoom = 1.0F};
  const Camera new_camera = camera_zoomed_about_center(old_camera, zoom);
  auto options = render_options(bridge);
  static_cast<void>(renderer.render(document, old_camera, cache, options));

  std::int64_t preview_us = 0;
  const auto preview_touch = measured_render(
      touch_probe, preview_us, [&] { build_zoom_preview(cache, old_camera, preview, new_camera); });
  const auto preview_present_started = esp_timer_get_time();
  display.push_rect(0, 0, kCanvasWidth, kCanvasHeight, preview.data(), kCanvasWidth);
  const auto preview_present_us = esp_timer_get_time() - preview_present_started;

  std::int64_t refine_us = 0;
  std::int64_t first_band_us = 0;
  bool complete = true;
  touch_probe.begin(timestamp_us());
  const auto refine_started = esp_timer_get_time();
  for (int y = 0; y < kCanvasHeight; y += kRefineRows) {
    const int bottom = std::min(y + kRefineRows, kCanvasHeight);
    const auto band_started = esp_timer_get_time();
    const auto stats =
        renderer.render_region(document, new_camera, preview,
                               {.x0 = 0, .y0 = y, .x1 = kCanvasWidth, .y1 = bottom}, options);
    display.push_rect(0, y, kCanvasWidth, bottom - y,
                      preview.data() + static_cast<std::ptrdiff_t>(y * kCanvasWidth), kCanvasWidth);
    if (y == 0) {
      first_band_us = esp_timer_get_time() - band_started;
    }
    complete = complete && stats.complete;
  }
  refine_us = esp_timer_get_time() - refine_started;
  const auto refine_touch = touch_probe.finish();

  const auto full_stats = renderer.render(document, new_camera, cache, options);
  const bool exact = pixel_differences(preview, cache) == 0U;
  append_report(report, report_size,
                "ZOOM case=%s zoom=%u preview_us=%lld preview_present_us=%lld "
                "first_band_us=%lld refine_us=%lld exact=%u complete=%u",
                prototype.name, static_cast<unsigned>(zoom * 100.0F),
                static_cast<long long>(preview_us), static_cast<long long>(preview_present_us),
                static_cast<long long>(first_band_us), static_cast<long long>(refine_us), exact,
                complete && full_stats.complete);
  append_report(report, report_size,
                " preview_polls=%lu preview_touch_avg_us=%lu preview_touch_max_us=%lu",
                static_cast<unsigned long>(preview_touch.polls),
                static_cast<unsigned long>(preview_touch.average_interval_us),
                static_cast<unsigned long>(preview_touch.maximum_interval_us));
  append_touch(report, report_size, refine_touch);
  append_report(report, report_size, "\n");
}

}  // namespace

void Phase2TouchProbe::begin(std::uint32_t now_us) {
  active_.store(false);
  polls_.store(0U);
  total_interval_us_.store(0U);
  maximum_interval_us_.store(0U);
  last_us_.store(now_us);
  active_.store(true);
}

void Phase2TouchProbe::record(std::uint32_t now_us) {
  if (!active_.load()) {
    return;
  }
  const std::uint32_t previous = last_us_.exchange(now_us);
  const std::uint32_t interval = now_us - previous;
  polls_.fetch_add(1U);
  total_interval_us_.fetch_add(interval);
  std::uint32_t maximum = maximum_interval_us_.load();
  while (maximum < interval && !maximum_interval_us_.compare_exchange_weak(maximum, interval)) {
  }
}

Phase2TouchPollStats Phase2TouchProbe::finish() {
  active_.store(false);
  const std::uint32_t polls = polls_.load();
  return {
      .polls = polls,
      .average_interval_us = polls == 0U ? 0U : total_interval_us_.load() / polls,
      .maximum_interval_us = maximum_interval_us_.load(),
  };
}

void run_phase2_prototype(std::span<std::uint16_t> cache, std::span<std::uint16_t> reference,
                          DisplayBackend& display, Phase2TouchProbe& touch_probe) {
  const auto free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const auto largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  const esp_partition_t* export_partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kExportSubtype, "export");
  auto* report =
      static_cast<char*>(heap_caps_malloc(kReportBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (report == nullptr || export_partition == nullptr) {
    std::printf("TINYDRAW_PHASE2_FAIL report=%u partition=%u\n", report != nullptr,
                export_partition != nullptr);
    heap_caps_free(report);
    return;
  }

  const std::size_t report_offset = export_partition->size - kReportBytes;
  std::fill_n(report, kReportBytes, '\0');
  std::size_t report_size = 0;
  append_report(report, report_size,
                "TINYDRAW_PHASE2_V1 reset_reason=%d free_before=%lu largest_before=%lu\n",
                static_cast<int>(esp_reset_reason()), static_cast<unsigned long>(free_before),
                static_cast<unsigned long>(largest_before));
  static_cast<void>(persist_report(export_partition, report_offset, report));

  auto* strokes = static_cast<VectorStroke*>(
      heap_caps_malloc(kMaxStrokes * sizeof(VectorStroke), kExternalCaps));
  auto* samples = static_cast<StrokeSample*>(
      heap_caps_malloc(kMaxSamples * sizeof(StrokeSample), kExternalCaps));
  auto* scratch =
      static_cast<std::uint8_t*>(heap_caps_malloc(ViewportRenderer::kScratchBytes, kExternalCaps));
  void* renderer_storage =
      heap_caps_malloc(sizeof(ViewportRenderer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (strokes == nullptr || samples == nullptr || scratch == nullptr ||
      renderer_storage == nullptr || cache.size() < ViewportRenderer::kPixelCount ||
      reference.size() < ViewportRenderer::kPixelCount) {
    append_report(report, report_size,
                  "FAIL allocation strokes=%u samples=%u scratch=%u renderer=%u cache=%u "
                  "reference=%u free=%lu largest=%lu\n",
                  strokes != nullptr, samples != nullptr, scratch != nullptr,
                  renderer_storage != nullptr, cache.size() >= ViewportRenderer::kPixelCount,
                  reference.size() >= ViewportRenderer::kPixelCount,
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    static_cast<void>(persist_report(export_partition, report_offset, report));
    heap_caps_free(renderer_storage);
    heap_caps_free(scratch);
    heap_caps_free(samples);
    heap_caps_free(strokes);
    heap_caps_free(report);
    return;
  }

  ParallelBridge bridge;
  bridge.start = xSemaphoreCreateBinary();
  bridge.done = xSemaphoreCreateBinary();
  if (bridge.start == nullptr || bridge.done == nullptr ||
      xTaskCreatePinnedToCore(parallel_worker, "phase2_lane1", 6'144U, &bridge, 1U, &bridge.worker,
                              1) != pdPASS) {
    append_report(
        report, report_size, "FAIL worker=0 free_internal=%lu largest_internal=%lu\n",
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    static_cast<void>(persist_report(export_partition, report_offset, report));
    if (bridge.start != nullptr) {
      vSemaphoreDelete(bridge.start);
    }
    if (bridge.done != nullptr) {
      vSemaphoreDelete(bridge.done);
    }
    heap_caps_free(renderer_storage);
    heap_caps_free(scratch);
    heap_caps_free(samples);
    heap_caps_free(strokes);
    heap_caps_free(report);
    return;
  }
  append_report(report, report_size,
                "ALLOC free=%lu largest=%lu free_internal=%lu largest_internal=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
  static_cast<void>(persist_report(export_partition, report_offset, report));

  VectorDocument document(std::span(strokes, kMaxStrokes), std::span(samples, kMaxSamples));
  auto* renderer =
      new (renderer_storage) ViewportRenderer(std::span(scratch, ViewportRenderer::kScratchBytes));
  for (const auto& prototype : kCases) {
    VectorBenchmarkDocumentStats document_stats;
    if (!populate_vector_benchmark(document, prototype.strokes, prototype.pattern,
                                   document_stats)) {
      append_report(report, report_size, "FAIL case=%s populate=0\n", prototype.name);
      static_cast<void>(persist_report(export_partition, report_offset, report));
      continue;
    }
    run_pan_case(prototype, -kPanPixels, 0, document, *renderer, cache, reference, display, bridge,
                 touch_probe, report, report_size);
    run_pan_case(prototype, 0, -kPanPixels, document, *renderer, cache, reference, display, bridge,
                 touch_probe, report, report_size);
    run_zoom_case(prototype, 0.5F, document, *renderer, cache, reference, display, bridge,
                  touch_probe, report, report_size);
    run_zoom_case(prototype, 2.0F, document, *renderer, cache, reference, display, bridge,
                  touch_probe, report, report_size);
    append_report(report, report_size, "MEM case=%s free=%lu largest=%lu minimum=%lu\n",
                  prototype.name,
                  static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
    static_cast<void>(persist_report(export_partition, report_offset, report));
  }

  append_report(report, report_size, "DONE free=%lu largest=%lu minimum=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
  static_cast<void>(persist_report(export_partition, report_offset, report));

  vTaskDelete(bridge.worker);
  vSemaphoreDelete(bridge.start);
  vSemaphoreDelete(bridge.done);
  renderer->~ViewportRenderer();
  heap_caps_free(renderer_storage);
  heap_caps_free(scratch);
  heap_caps_free(samples);
  heap_caps_free(strokes);
  heap_caps_free(report);
}

}  // namespace tinydraw::esp32
