#include "vector_benchmark_runner.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <new>
#include <span>

#include "esp_cpu.h"
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

constexpr std::size_t kMaxStrokes = 5'000U;
constexpr std::size_t kMaxSamples = 15'000U;
constexpr std::size_t kReportBytes = 8'192U;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr esp_partition_subtype_t kExportSubtype = static_cast<esp_partition_subtype_t>(0x41);

struct BenchmarkCase {
  std::size_t strokes;
  VectorBenchmarkPattern pattern;
};

constexpr std::array kCases{
    BenchmarkCase{100U, VectorBenchmarkPattern::kHandwriting},
    BenchmarkCase{100U, VectorBenchmarkPattern::kLongDense},
    BenchmarkCase{1'000U, VectorBenchmarkPattern::kHandwriting},
    BenchmarkCase{1'000U, VectorBenchmarkPattern::kManyVisible},
    BenchmarkCase{5'000U, VectorBenchmarkPattern::kManyOffscreen},
    BenchmarkCase{5'000U, VectorBenchmarkPattern::kManyVisible},
};
constexpr std::array kZooms{0.25F, 0.5F, 1.0F, 2.0F};

void benchmark_yield(void*) { vTaskDelay(1U); }

// Renders lane 1 on the second core. Priority stays strictly below the touch
// task (priority 5, core 1) so rebuilds can never delay input capture; the
// worker merely soaks up idle core-1 time between touch polls.
constexpr UBaseType_t kWorkerPriority = 1U;

struct ParallelBridge {
  SemaphoreHandle_t start = nullptr;
  SemaphoreHandle_t done = nullptr;
  void (*work)(void*, int) = nullptr;
  void* work_context = nullptr;
  TaskHandle_t worker = nullptr;
};

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

std::uint32_t benchmark_cycles() { return static_cast<std::uint32_t>(esp_cpu_get_cycle_count()); }

constexpr std::uint64_t kCyclesPerMicrosecond = 240U;

std::uint64_t to_microseconds(std::uint64_t cycles) { return cycles / kCyclesPerMicrosecond; }

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

}  // namespace

void run_vector_benchmarks(std::span<std::uint16_t> destination) {
  const auto free_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const auto largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  auto* strokes = static_cast<VectorStroke*>(
      heap_caps_malloc(kMaxStrokes * sizeof(VectorStroke), kExternalCaps));
  auto* samples = static_cast<StrokeSample*>(
      heap_caps_malloc(kMaxSamples * sizeof(StrokeSample), kExternalCaps));
  auto* scratch =
      static_cast<std::uint8_t*>(heap_caps_malloc(ViewportRenderer::kScratchBytes, kExternalCaps));
  void* renderer_storage =
      heap_caps_malloc(sizeof(ViewportRenderer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  auto* report =
      static_cast<char*>(heap_caps_malloc(kReportBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const esp_partition_t* export_partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kExportSubtype, "export");
  if (strokes == nullptr || samples == nullptr || scratch == nullptr ||
      renderer_storage == nullptr || report == nullptr || export_partition == nullptr ||
      destination.size() < ViewportRenderer::kPixelCount) {
    std::printf(
        "TINYDRAW_VECTOR_BENCH_FAIL strokes=%u samples=%u scratch=%u renderer=%u "
        "report=%u partition=%u destination=%u free=%lu largest=%lu\n",
        strokes != nullptr, samples != nullptr, scratch != nullptr, renderer_storage != nullptr,
        report != nullptr, export_partition != nullptr,
        destination.size() >= ViewportRenderer::kPixelCount,
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    heap_caps_free(report);
    heap_caps_free(renderer_storage);
    heap_caps_free(scratch);
    heap_caps_free(samples);
    heap_caps_free(strokes);
    return;
  }

  const std::size_t report_offset = export_partition->size - kReportBytes;
  std::fill_n(report, kReportBytes, '\0');
  std::size_t report_size = 0;
  append_report(report, report_size,
                "TINYDRAW_VECTOR_BENCH_V1 reset_reason=%d free_before=%lu largest_before=%lu\n",
                static_cast<int>(esp_reset_reason()), static_cast<unsigned long>(free_before),
                static_cast<unsigned long>(largest_before));
  static_cast<void>(persist_report(export_partition, report_offset, report));

  ParallelBridge bridge;
  bridge.start = xSemaphoreCreateBinary();
  bridge.done = xSemaphoreCreateBinary();
  if (bridge.start == nullptr || bridge.done == nullptr ||
      xTaskCreatePinnedToCore(parallel_worker, "vector_lane1", 12'288U, &bridge, kWorkerPriority,
                              &bridge.worker, 1) != pdPASS) {
    std::printf("TINYDRAW_VECTOR_BENCH_FAIL worker=0\n");
    heap_caps_free(report);
    heap_caps_free(renderer_storage);
    heap_caps_free(scratch);
    heap_caps_free(samples);
    heap_caps_free(strokes);
    return;
  }

  VectorDocument document(std::span(strokes, kMaxStrokes), std::span(samples, kMaxSamples));
  auto* renderer =
      new (renderer_storage) ViewportRenderer(std::span(scratch, ViewportRenderer::kScratchBytes));
  std::printf(
      "TINYDRAW_VECTOR_BENCH_START free_before=%lu largest_before=%lu free_allocated=%lu "
      "largest_allocated=%lu arena_bytes=%lu\n",
      static_cast<unsigned long>(free_before), static_cast<unsigned long>(largest_before),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(kMaxStrokes * sizeof(VectorStroke) +
                                 kMaxSamples * sizeof(StrokeSample) +
                                 ViewportRenderer::kScratchBytes));

  for (const BenchmarkCase benchmark : kCases) {
    VectorBenchmarkDocumentStats document_stats;
    if (!populate_vector_benchmark(document, benchmark.strokes, benchmark.pattern,
                                   document_stats)) {
      append_report(report, report_size, "FAIL pattern=%s populate=0\n",
                    vector_benchmark_pattern_name(benchmark.pattern));
      continue;
    }
    for (const float zoom : kZooms) {
      const char* pattern_name = vector_benchmark_pattern_name(benchmark.pattern);
      std::printf("TINYDRAW_VECTOR_BENCH_CASE_START pattern=%s zoom=%u strokes=%lu\n", pattern_name,
                  static_cast<unsigned>(zoom * 100.0F),
                  static_cast<unsigned long>(benchmark.strokes));
      std::fflush(stdout);
      ViewportRenderOptions options;
      options.yield = benchmark_yield;
      options.yield_every_tiles = 16U;
      options.now = benchmark_cycles;
      options.execute = execute_parallel;
      options.execute_context = &bridge;
      const auto started = esp_timer_get_time();
      const auto stats = renderer->render(document, {.zoom = zoom}, destination, options);
      const auto elapsed = esp_timer_get_time() - started;
      const std::size_t document_bytes = document_stats.strokes * sizeof(VectorStroke) +
                                         document_stats.samples * sizeof(StrokeSample);
      const auto free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      const auto largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
      std::printf(
          "TINYDRAW_VECTOR_BENCH platform=esp32 pattern=%s zoom=%u strokes=%lu "
          "intersecting=%lu samples=%lu processed=%lu primitives=%lu visits=%lu tiles=%lu "
          "document_bytes=%lu elapsed_us=%lld free=%lu largest=%lu\n",
          pattern_name, static_cast<unsigned>(zoom * 100.0F),
          static_cast<unsigned long>(stats.strokes_tested),
          static_cast<unsigned long>(stats.strokes_intersecting),
          static_cast<unsigned long>(document_stats.samples),
          static_cast<unsigned long>(stats.samples_processed),
          static_cast<unsigned long>(stats.primitives_rasterized),
          static_cast<unsigned long>(stats.primitive_tile_visits),
          static_cast<unsigned long>(stats.tiles_composited),
          static_cast<unsigned long>(document_bytes), static_cast<long long>(elapsed),
          static_cast<unsigned long>(free), static_cast<unsigned long>(largest));
      append_report(
          report, report_size,
          "pattern=%s zoom=%u strokes=%lu intersecting=%lu samples=%lu processed=%lu "
          "primitives=%lu visits=%lu tiles=%lu bytes=%lu elapsed_us=%lld free=%lu largest=%lu "
          "clear_us=%llu geo_us=%llu ras_us=%llu cmp_us=%llu\n",
          pattern_name, static_cast<unsigned>(zoom * 100.0F),
          static_cast<unsigned long>(stats.strokes_tested),
          static_cast<unsigned long>(stats.strokes_intersecting),
          static_cast<unsigned long>(document_stats.samples),
          static_cast<unsigned long>(stats.samples_processed),
          static_cast<unsigned long>(stats.primitives_rasterized),
          static_cast<unsigned long>(stats.primitive_tile_visits),
          static_cast<unsigned long>(stats.tiles_composited),
          static_cast<unsigned long>(document_bytes), static_cast<long long>(elapsed),
          static_cast<unsigned long>(free), static_cast<unsigned long>(largest),
          static_cast<unsigned long long>(to_microseconds(stats.clear_ticks)),
          static_cast<unsigned long long>(to_microseconds(stats.geometry_ticks)),
          static_cast<unsigned long long>(to_microseconds(stats.raster_ticks)),
          static_cast<unsigned long long>(to_microseconds(stats.composite_ticks)));
      static_cast<void>(persist_report(export_partition, report_offset, report));
      vTaskDelay(1U);
    }
  }

  append_report(report, report_size, "DONE minimum=%lu\n",
                static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
  const bool report_result = persist_report(export_partition, report_offset, report);
  std::printf("TINYDRAW_VECTOR_BENCH_REPORT success=%u offset=0x%lx bytes=%lu\n", report_result,
              static_cast<unsigned long>(report_offset), static_cast<unsigned long>(report_size));

  vTaskDelete(bridge.worker);
  vSemaphoreDelete(bridge.start);
  vSemaphoreDelete(bridge.done);
  renderer->~ViewportRenderer();
  heap_caps_free(report);
  heap_caps_free(renderer_storage);
  heap_caps_free(scratch);
  heap_caps_free(samples);
  heap_caps_free(strokes);
  std::printf("TINYDRAW_VECTOR_BENCH_DONE free=%lu largest=%lu minimum=%lu\n",
              static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
              static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
              static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
}

}  // namespace tinydraw::esp32
