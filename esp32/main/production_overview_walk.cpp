#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_display.h"
#include "tinydraw/production/materialized_canvas.h"

namespace tinydraw::esp32 {
namespace {

using production::DocumentRevision;
using production::MaterializationQuality;
using production::MaterializedCanvas;
using production::MaterializedSlotStorage;
using production::PixelRect;
using production::ViewRequest;
using production::ZoomLevel;

constexpr int kStripRows = 22;
constexpr std::uint32_t kExpectedPushesPerFrame = 21U;
constexpr std::uint32_t kExpectedTotalPushes = 105U;
constexpr std::uint32_t kExpectedOverviewHash = 0xD76C09B1U;
constexpr std::uint32_t kExpectedFallbackHashes[]{0xD9E39425U, 0xA4CE26E5U, 0x1B5753A5U,
                                                  0x91F8B705U};
constexpr std::uint32_t kFnvOffset = 2'166'136'261U;
constexpr std::uint32_t kFnvPrime = 16'777'619U;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

std::uint32_t hash_pixels(std::uint32_t hash, std::span<const std::uint16_t> pixels) {
  for (const std::uint16_t pixel : pixels) {
    hash = (hash ^ static_cast<std::uint8_t>(pixel >> 8U)) * kFnvPrime;
    hash = (hash ^ static_cast<std::uint8_t>(pixel)) * kFnvPrime;
  }
  return hash;
}

std::uint16_t overview_pattern(int x, int y) {
  const std::uint16_t red = static_cast<std::uint16_t>((x / 12) & 0x1F);
  const std::uint16_t green = static_cast<std::uint16_t>((y / 7) & 0x3F);
  const std::uint16_t blue = static_cast<std::uint16_t>(((x + y) / 15) & 0x1F);
  const bool marker = x % 92 < 3 || y % 112 < 3;
  return marker ? 0xFFFFU : static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
}

bool wait_for_transfers(std::uint32_t target, std::int64_t timeout_us) {
  const std::int64_t started = esp_timer_get_time();
  while (physical_display_complete_count(nullptr) < target) {
    if (esp_timer_get_time() - started >= timeout_us) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

bool present_overview(PhysicalDisplay& display, const MaterializedCanvas& canvas) {
  const auto source = canvas.overview_pixels();
  const std::int64_t started = esp_timer_get_time();
  const std::uint32_t pushes_before = display.push_count();
  std::uint32_t hash = kFnvOffset;
  for (int y = 0; y < production::kOverviewHeight; y += kStripRows) {
    const int rows = std::min(kStripRows, production::kOverviewHeight - y);
    const auto offset = static_cast<std::size_t>(y * production::kOverviewWidth);
    const auto pixels =
        source.subspan(offset, static_cast<std::size_t>(rows * production::kOverviewWidth));
    hash = hash_pixels(hash, pixels);
    display.push_rect(0, y, production::kOverviewWidth, rows, pixels.data(),
                      production::kOverviewWidth);
  }
  const std::uint32_t target = physical_display_submit_count(nullptr);
  const std::uint32_t pushes = display.push_count() - pushes_before;
  const bool completed = wait_for_transfers(target, 2'000'000);
  const bool passed = completed && pushes == kExpectedPushesPerFrame &&
                      hash == kExpectedOverviewHash && display.rejected_push_count() == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_FRAME zoom=25 x=0 y=0 hash=%08lx pushes=%lu "
      "elapsed_us=%lld completed=%u submit=%lu complete=%lu\n",
      static_cast<unsigned long>(hash), static_cast<unsigned long>(pushes),
      static_cast<long long>(esp_timer_get_time() - started), completed,
      static_cast<unsigned long>(target),
      static_cast<unsigned long>(physical_display_complete_count(nullptr)));
  return passed;
}

bool present_fallback(PhysicalDisplay& display, MaterializedCanvas& canvas, int world_x,
                      int world_y, std::uint32_t expected_hash, std::span<std::uint16_t> strip) {
  const std::int64_t started = esp_timer_get_time();
  const std::uint32_t pushes_before = display.push_count();
  std::int64_t compose_us = 0;
  std::uint32_t hash = kFnvOffset;
  for (int panel_y = 0; panel_y < production::kOverviewHeight; panel_y += kStripRows) {
    const int rows = std::min(kStripRows, production::kOverviewHeight - panel_y);
    const auto destination =
        strip.first(static_cast<std::size_t>(rows * production::kOverviewWidth));
    const ViewRequest request{
        .zoom = ZoomLevel::k100Percent,
        .level_pixels = PixelRect{world_x, world_y + panel_y, world_x + production::kOverviewWidth,
                                  world_y + panel_y + rows},
    };
    const std::int64_t compose_started = esp_timer_get_time();
    const auto stats = canvas.compose_view(request, destination);
    compose_us += esp_timer_get_time() - compose_started;
    if (!stats.has_value() || stats->fallback_pixels != destination.size()) {
      std::printf("TINYDRAW_PRODUCTION_WALK_FAIL reason=compose x=%d y=%d panel_y=%d\n", world_x,
                  world_y, panel_y);
      return false;
    }
    hash = hash_pixels(hash, destination);
    display.push_rect(0, panel_y, production::kOverviewWidth, rows, destination.data(),
                      production::kOverviewWidth);
  }
  const std::uint32_t target = physical_display_submit_count(nullptr);
  const std::uint32_t pushes = display.push_count() - pushes_before;
  const bool completed = wait_for_transfers(target, 2'000'000);
  const bool passed = completed && pushes == kExpectedPushesPerFrame && hash == expected_hash &&
                      display.rejected_push_count() == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_FRAME zoom=100 x=%d y=%d hash=%08lx pushes=%lu "
      "compose_us=%lld elapsed_us=%lld completed=%u submit=%lu complete=%lu\n",
      world_x, world_y, static_cast<unsigned long>(hash), static_cast<unsigned long>(pushes),
      static_cast<long long>(compose_us), static_cast<long long>(esp_timer_get_time() - started),
      completed, static_cast<unsigned long>(target),
      static_cast<unsigned long>(physical_display_complete_count(nullptr)));
  return passed;
}

}  // namespace

void run_production_overview_walk() {
  auto* overview =
      static_cast<std::uint16_t*>(heap_caps_malloc(production::kOverviewBytes, kExternalCaps));
  auto* raw_slot = static_cast<MaterializedSlotStorage*>(
      heap_caps_malloc(sizeof(MaterializedSlotStorage), kExternalCaps));
  auto* tile_pixels =
      static_cast<std::uint16_t*>(heap_caps_malloc(production::kTileBytes, kExternalCaps));
  auto* strip = static_cast<std::uint16_t*>(heap_caps_malloc(
      static_cast<std::size_t>(production::kOverviewWidth * kStripRows) * sizeof(std::uint16_t),
      kExternalCaps));
  if (overview == nullptr || raw_slot == nullptr || tile_pixels == nullptr || strip == nullptr) {
    std::printf(
        "TINYDRAW_PRODUCTION_WALK_FAIL reason=allocation overview=%u slot=%u tile=%u strip=%u\n",
        overview != nullptr, raw_slot != nullptr, tile_pixels != nullptr, strip != nullptr);
    return;
  }
  MaterializedSlotStorage* slot = std::construct_at(raw_slot);
  for (int y = 0; y < production::kOverviewHeight; ++y) {
    for (int x = 0; x < production::kOverviewWidth; ++x) {
      overview[static_cast<std::size_t>(y * production::kOverviewWidth + x)] =
          overview_pattern(x, y);
    }
  }

  MaterializedCanvas canvas(std::span(overview, production::kOverviewPixels), std::span(slot, 1),
                            std::span(tile_pixels, production::kTilePixels), DocumentRevision{0});
  PhysicalDisplay display(false);
  std::unique_ptr<std::uint16_t[]> overview_source(new (std::nothrow)
                                                       std::uint16_t[production::kOverviewPixels]);
  if (overview_source == nullptr) {
    std::printf("TINYDRAW_PRODUCTION_WALK_FAIL reason=overview_source\n");
    return;
  }
  std::copy_n(overview, production::kOverviewPixels, overview_source.get());
  if (!canvas.ready() ||
      !canvas.publish_overview({0},
                               std::span(overview_source.get(), production::kOverviewPixels)) ||
      !display.ready()) {
    std::printf("TINYDRAW_PRODUCTION_WALK_FAIL reason=bootstrap canvas=%u display=%u\n",
                canvas.ready(), display.ready());
    return;
  }

  display.reset_timing();
  bool pass = present_overview(display, canvas);
  vTaskDelay(pdMS_TO_TICKS(500));
  const std::span strip_pixels(strip,
                               static_cast<std::size_t>(production::kOverviewWidth * kStripRows));
  constexpr PixelRect kOrigins[]{
      {0, 0, 0, 0}, {184, 224, 0, 0}, {552, 672, 0, 0}, {1104, 1344, 0, 0}};
  for (std::size_t index = 0; index < std::size(kOrigins); ++index) {
    const PixelRect origin = kOrigins[index];
    pass = present_fallback(display, canvas, origin.x0, origin.y0, kExpectedFallbackHashes[index],
                            strip_pixels) &&
           pass;
    vTaskDelay(pdMS_TO_TICKS(350));
  }
  const std::uint32_t submits = physical_display_submit_count(nullptr);
  const std::uint32_t completes = physical_display_complete_count(nullptr);
  const std::uint32_t rejects = display.rejected_push_count();
  pass = pass && display.push_count() == kExpectedTotalPushes && submits == kExpectedTotalPushes &&
         completes == kExpectedTotalPushes && rejects == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_DONE pass=%u panel_rejects=%lu prepare_us=%lld transfer_us=%lld "
      "pushes=%lu submit=%lu complete=%lu free_psram=%lu largest_psram=%lu\n",
      pass, static_cast<unsigned long>(rejects), static_cast<long long>(display.prepare_us()),
      static_cast<long long>(display.transfer_us()),
      static_cast<unsigned long>(display.push_count()), static_cast<unsigned long>(submits),
      static_cast<unsigned long>(completes),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
  std::fflush(stdout);
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_production_overview_walk(); }
