#include "raster_pan_benchmark.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "tinydraw/geometry.h"

namespace tinydraw::esp32 {
namespace {

constexpr int kTransferPixels = 8'192;
constexpr int kPanPixels = 32;
constexpr std::size_t kWarmups = 4U;
constexpr std::size_t kFrames = 30U;
constexpr std::size_t kReportBytes = 8'192U;
constexpr esp_partition_subtype_t kExportSubtype = static_cast<esp_partition_subtype_t>(0x41);

void push_world(DisplayBackend& display, std::span<const std::uint16_t> world, ViewOrigin origin,
                int bottom) {
  constexpr int rows_per_transfer = (kTransferPixels / kCanvasWidth) & ~1;
  for (int y = 0; y < bottom; y += rows_per_transfer) {
    const int height = std::min(rows_per_transfer, bottom - y);
    const auto offset = static_cast<std::size_t>((origin.y + y) * WorldCanvas::kWidth + origin.x);
    display.push_rect(0, y, kCanvasWidth, height, world.data() + offset, WorldCanvas::kWidth);
  }
}

}  // namespace

void run_raster_pan_benchmark(WorldCanvas& world, DisplayBackend& display, int bottom) {
  const ViewOrigin initial = world.origin();
  std::array<std::int64_t, kFrames> elapsed{};
  for (std::size_t frame = 0; frame < kWarmups + kFrames; ++frame) {
    const bool shifted = frame % 2U == 0U;
    const ViewOrigin target{.x = initial.x + (shifted ? kPanPixels : 0), .y = initial.y};
    const auto started = esp_timer_get_time();
    static_cast<void>(world.move_to(target));
    push_world(display, world.pixels(), world.origin(), bottom);
    const auto frame_us = esp_timer_get_time() - started;
    if (frame >= kWarmups) {
      elapsed[frame - kWarmups] = frame_us;
    }
  }
  static_cast<void>(world.move_to(initial));
  push_world(display, world.pixels(), world.origin(), bottom);

  auto sorted = elapsed;
  std::sort(sorted.begin(), sorted.end());
  std::int64_t total = 0;
  for (const auto value : elapsed) {
    total += value;
  }

  const auto minimum = sorted.front();
  const auto median = sorted[sorted.size() / 2U];
  const auto maximum = sorted.back();
  const auto average = total / static_cast<std::int64_t>(elapsed.size());

  const esp_partition_t* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kExportSubtype, "export");
  auto* report =
      static_cast<char*>(heap_caps_calloc(kReportBytes, 1U, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const int size =
      report == nullptr
          ? -1
          : std::snprintf(report, kReportBytes,
                          "TINYDRAW_RASTER_PAN_V1 frames=%lu pixels=%d rows=%d delta=%d "
                          "min_us=%lld median_us=%lld average_us=%lld max_us=%lld\nDONE\n",
                          static_cast<unsigned long>(kFrames), kCanvasWidth, bottom, kPanPixels,
                          static_cast<long long>(minimum), static_cast<long long>(median),
                          static_cast<long long>(average), static_cast<long long>(maximum));
  bool persisted = false;
  if (partition != nullptr && size > 0) {
    const std::size_t offset = partition->size - kReportBytes;
    persisted = esp_partition_erase_range(partition, offset, kReportBytes) == ESP_OK &&
                esp_partition_write(partition, offset, report, kReportBytes) == ESP_OK;
  }
  heap_caps_free(report);
  std::printf(
      "TINYDRAW_RASTER_PAN frames=%lu min_us=%lld median_us=%lld average_us=%lld max_us=%lld "
      "persisted=%u\n",
      static_cast<unsigned long>(kFrames), static_cast<long long>(minimum),
      static_cast<long long>(median), static_cast<long long>(average),
      static_cast<long long>(maximum), persisted);
}

}  // namespace tinydraw::esp32
