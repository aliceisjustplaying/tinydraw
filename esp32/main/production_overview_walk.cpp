#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>

#include "co5300_panel_transport.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#include "tinydraw/production/display_scheduler.h"
#include "tinydraw/production/incremental_document.h"
#include "tinydraw/production/incremental_rasterizer.h"
#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation_builder.h"
#include "tinydraw/production/operation_log.h"

namespace tinydraw::esp32 {
namespace {

using production::CompactOperationSample;
using production::DisplayScheduler;
using production::DisplayStrip;
using production::DocumentRevision;
using production::IncrementalDocumentWorkspace;
using production::MaterializationQuality;
using production::MaterializedCanvas;
using production::MaterializedSlotStorage;
using production::OperationBuilder;
using production::OperationLog;
using production::OperationPoint;
using production::OperationRecord;
using production::OperationTool;
using production::PixelRect;
using production::RasterSurface;
using production::TileKey;
using production::TileRevisionPublication;
using production::ViewRequest;
using production::ZoomLevel;

constexpr int kStripRows = 22;
constexpr std::uint32_t kExpectedPushesPerFrame = 21U;
constexpr std::uint32_t kExpectedTotalPushes = 168U;
constexpr std::uint32_t kBurstOperationCount = 30U;
constexpr std::uint32_t kExpectedOverviewHash = 0xD76C09B1U;
constexpr std::uint32_t kExpectedFallbackHashes[]{0xD9E39425U, 0xA4CE26E5U, 0x1B5753A5U,
                                                  0x91F8B705U};
constexpr std::uint32_t kExpectedPenHash = 0xE93CC976U;
constexpr std::uint32_t kExpectedEraserHash = 0x9C622475U;
constexpr std::uint32_t kExpectedBurstHash = 0xD4E162C4U;
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

bool wait_for_transfers(Co5300PanelTransport& display, std::uint32_t target,
                        std::int64_t timeout_us) {
  const std::int64_t started = esp_timer_get_time();
  while (display.complete_count() < target) {
    if (esp_timer_get_time() - started >= timeout_us) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

bool submit_strip(DisplayScheduler& scheduler, Co5300PanelTransport& display,
                  const DisplayStrip& strip) {
  const auto sequence = scheduler.schedule(strip);
  if (!sequence.has_value()) {
    return false;
  }
  const auto scheduled = scheduler.front();
  if (!scheduled.has_value() || scheduled->sequence != *sequence) {
    return false;
  }
  const std::uint32_t pushes_before = display.push_count();
  const PixelRect bounds = scheduled->strip.panel_bounds;
  display.push_rect(bounds.x0, bounds.y0, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0,
                    scheduled->strip.pixels.data(), scheduled->strip.stride);
  const bool staged = display.push_count() == pushes_before + 1U;
  if (!staged) {
    static_cast<void>(scheduler.abort(*sequence));
    return false;
  }
  return scheduler.complete(*sequence);
}

bool present_overview(Co5300PanelTransport& display, DisplayScheduler& scheduler,
                      const MaterializedCanvas& canvas) {
  const auto source = canvas.overview_pixels();
  const std::int64_t started = esp_timer_get_time();
  const std::uint32_t pushes_before = display.push_count();
  std::uint32_t hash = kFnvOffset;
  scheduler.require_revision(canvas.current_revision());
  for (int y = 0; y < production::kOverviewHeight; y += kStripRows) {
    const int rows = std::min(kStripRows, production::kOverviewHeight - y);
    const auto offset = static_cast<std::size_t>(y * production::kOverviewWidth);
    const auto pixels =
        source.subspan(offset, static_cast<std::size_t>(rows * production::kOverviewWidth));
    hash = hash_pixels(hash, pixels);
    if (!submit_strip(scheduler, display,
                      {.revision = canvas.current_revision(),
                       .panel_bounds = {0, y, production::kOverviewWidth, y + rows},
                       .pixels = pixels,
                       .stride = production::kOverviewWidth})) {
      return false;
    }
  }
  const std::uint32_t target = display.submit_count();
  const std::uint32_t pushes = display.push_count() - pushes_before;
  const bool completed = wait_for_transfers(display, target, 2'000'000);
  const bool passed = completed && pushes == kExpectedPushesPerFrame &&
                      hash == kExpectedOverviewHash && display.rejected_push_count() == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_FRAME zoom=25 x=0 y=0 hash=%08lx pushes=%lu "
      "elapsed_us=%lld completed=%u submit=%lu complete=%lu\n",
      static_cast<unsigned long>(hash), static_cast<unsigned long>(pushes),
      static_cast<long long>(esp_timer_get_time() - started), completed,
      static_cast<unsigned long>(target), static_cast<unsigned long>(display.complete_count()));
  return passed;
}

bool present_fallback(Co5300PanelTransport& display, DisplayScheduler& scheduler,
                      MaterializedCanvas& canvas, int world_x, int world_y,
                      std::uint32_t expected_hash, std::span<std::uint16_t> strip) {
  const std::int64_t started = esp_timer_get_time();
  const std::uint32_t pushes_before = display.push_count();
  std::int64_t compose_us = 0;
  std::uint32_t hash = kFnvOffset;
  scheduler.require_revision(canvas.current_revision());
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
    if (!submit_strip(scheduler, display,
                      {.revision = canvas.current_revision(),
                       .panel_bounds = {0, panel_y, production::kOverviewWidth, panel_y + rows},
                       .pixels = destination,
                       .stride = production::kOverviewWidth})) {
      return false;
    }
  }
  const std::uint32_t target = display.submit_count();
  const std::uint32_t pushes = display.push_count() - pushes_before;
  const bool completed = wait_for_transfers(display, target, 2'000'000);
  const bool passed = completed && pushes == kExpectedPushesPerFrame && hash == expected_hash &&
                      display.rejected_push_count() == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_FRAME zoom=100 x=%d y=%d hash=%08lx pushes=%lu "
      "compose_us=%lld elapsed_us=%lld completed=%u submit=%lu complete=%lu\n",
      world_x, world_y, static_cast<unsigned long>(hash), static_cast<unsigned long>(pushes),
      static_cast<long long>(compose_us), static_cast<long long>(esp_timer_get_time() - started),
      completed, static_cast<unsigned long>(target),
      static_cast<unsigned long>(display.complete_count()));
  return passed;
}

bool present_incremental(Co5300PanelTransport& display, DisplayScheduler& scheduler,
                         MaterializedCanvas& canvas, std::span<std::uint16_t> strip,
                         DocumentRevision revision, const char* phase,
                         std::uint32_t expected_hash) {
  const std::int64_t started = esp_timer_get_time();
  const std::uint32_t pushes_before = display.push_count();
  std::uint32_t hash = kFnvOffset;
  std::size_t tile_pixels = 0;
  std::size_t fallback_pixels = 0;
  scheduler.require_revision(revision);
  for (int panel_y = 0; panel_y < production::kOverviewHeight; panel_y += kStripRows) {
    const int rows = std::min(kStripRows, production::kOverviewHeight - panel_y);
    const auto destination =
        strip.first(static_cast<std::size_t>(rows * production::kOverviewWidth));
    const auto stats = canvas.compose_view(
        {.zoom = ZoomLevel::k100Percent,
         .level_pixels = {0, panel_y, production::kOverviewWidth, panel_y + rows}},
        destination);
    if (!stats.has_value() || stats->revision != revision) {
      std::printf("TINYDRAW_PRODUCTION_WALK_FAIL reason=incremental_compose phase=%s panel_y=%d\n",
                  phase, panel_y);
      return false;
    }
    tile_pixels += stats->tile_pixels;
    fallback_pixels += stats->fallback_pixels;
    hash = hash_pixels(hash, destination);
    if (!submit_strip(scheduler, display,
                      {.revision = revision,
                       .panel_bounds = {0, panel_y, production::kOverviewWidth, panel_y + rows},
                       .pixels = destination,
                       .stride = production::kOverviewWidth})) {
      return false;
    }
  }
  const std::uint32_t target = display.submit_count();
  const std::uint32_t pushes = display.push_count() - pushes_before;
  const bool completed = wait_for_transfers(display, target, 2'000'000);
  const bool passed =
      completed && pushes == kExpectedPushesPerFrame && hash == expected_hash &&
      tile_pixels == 2U * production::kTilePixels &&
      tile_pixels + fallback_pixels == static_cast<std::size_t>(production::kOverviewPixels) &&
      display.rejected_push_count() == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_INCREMENTAL phase=%s revision=%lu hash=%08lx pushes=%lu "
      "tile_pixels=%lu fallback_pixels=%lu elapsed_us=%lld completed=%u submit=%lu complete=%lu\n",
      phase, static_cast<unsigned long>(revision.value), static_cast<unsigned long>(hash),
      static_cast<unsigned long>(pushes), static_cast<unsigned long>(tile_pixels),
      static_cast<unsigned long>(fallback_pixels),
      static_cast<long long>(esp_timer_get_time() - started), completed,
      static_cast<unsigned long>(target), static_cast<unsigned long>(display.complete_count()));
  return passed;
}

void fill_tile_from_overview(std::span<std::uint16_t> tile, std::span<const std::uint16_t> overview,
                             TileKey key) {
  const PixelRect bounds = production::tile_pixel_bounds(key);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    for (int x = bounds.x0; x < bounds.x1; ++x) {
      const std::size_t destination =
          static_cast<std::size_t>(y - bounds.y0) * production::kTileWidth +
          static_cast<std::size_t>(x - bounds.x0);
      const std::size_t source = static_cast<std::size_t>(y / 4) * production::kOverviewWidth +
                                 static_cast<std::size_t>(x / 4);
      tile[destination] = overview[source];
    }
  }
}

bool append_and_commit_probe(OperationLog& log, MaterializedCanvas& canvas,
                             const production::OperationAppend& append_request,
                             std::span<std::uint16_t> next_overview,
                             std::span<std::uint16_t> tile_scratch) {
  constexpr TileKey kAffected{ZoomLevel::k100Percent, 0, 0};
  constexpr TileKey kCarried{ZoomLevel::k100Percent, 1, 0};
  const auto carried_before = canvas.lookup(kCarried);
  if (!carried_before.has_value()) {
    return false;
  }
  std::array<TileRevisionPublication, 1> publications{};
  std::array<TileKey, 1> affected{};
  const std::int64_t started = esp_timer_get_time();
  const auto result = production::append_incrementally(
      log, canvas, append_request,
      IncrementalDocumentWorkspace{.overview_scratch = next_overview,
                                   .tile_scratch = tile_scratch,
                                   .publications = publications,
                                   .affected_keys = affected});
  const std::int64_t elapsed_us = esp_timer_get_time() - started;
  std::printf("TINYDRAW_PRODUCTION_WALK_OPERATION revision=%lu append_us=%lld committed=%u\n",
              static_cast<unsigned long>(canvas.current_revision().value),
              static_cast<long long>(elapsed_us), result.has_value());
  const auto updated = canvas.lookup(kAffected);
  const auto carried = canvas.lookup(kCarried);
  return result.has_value() && result->affected_resident_tiles == 1U && updated.has_value() &&
         carried.has_value() && updated->identity.revision == result->identity.revision &&
         updated->identity.quality == MaterializationQuality::kImmediate &&
         carried->identity.revision == result->identity.revision &&
         carried->identity.generation == carried_before->identity.generation;
}

}  // namespace

void run_production_overview_walk() {
  auto* overview =
      static_cast<std::uint16_t*>(heap_caps_malloc(production::kOverviewBytes, kExternalCaps));
  auto* raw_slots = static_cast<MaterializedSlotStorage*>(
      heap_caps_malloc(2U * sizeof(MaterializedSlotStorage), kExternalCaps));
  auto* tile_pixels =
      static_cast<std::uint16_t*>(heap_caps_malloc(2U * production::kTileBytes, kExternalCaps));
  auto* tile_scratch =
      static_cast<std::uint16_t*>(heap_caps_malloc(production::kTileBytes, kExternalCaps));
  auto* operation_records =
      static_cast<OperationRecord*>(heap_caps_malloc(32U * sizeof(OperationRecord), kExternalCaps));
  auto* operation_samples = static_cast<CompactOperationSample*>(
      heap_caps_malloc(64U * sizeof(CompactOperationSample), kExternalCaps));
  auto* strip = static_cast<std::uint16_t*>(heap_caps_malloc(
      static_cast<std::size_t>(production::kOverviewWidth * kStripRows) * sizeof(std::uint16_t),
      kExternalCaps));
  if (overview == nullptr || raw_slots == nullptr || tile_pixels == nullptr ||
      tile_scratch == nullptr || operation_records == nullptr || operation_samples == nullptr ||
      strip == nullptr) {
    std::printf(
        "TINYDRAW_PRODUCTION_WALK_FAIL reason=allocation overview=%u slots=%u tile=%u "
        "tile_scratch=%u records=%u samples=%u strip=%u\n",
        overview != nullptr, raw_slots != nullptr, tile_pixels != nullptr, tile_scratch != nullptr,
        operation_records != nullptr, operation_samples != nullptr, strip != nullptr);
    return;
  }
  MaterializedSlotStorage* slots = std::construct_at(raw_slots);
  std::construct_at(raw_slots + 1);
  for (int y = 0; y < production::kOverviewHeight; ++y) {
    for (int x = 0; x < production::kOverviewWidth; ++x) {
      overview[static_cast<std::size_t>(y * production::kOverviewWidth + x)] =
          overview_pattern(x, y);
    }
  }

  OperationLog operation_log(std::span(operation_records, 32), std::span(operation_samples, 64));
  std::array<DisplayStrip, 3> display_queue{};
  DisplayScheduler scheduler(display_queue);
  MaterializedCanvas canvas(std::span(overview, production::kOverviewPixels), std::span(slots, 2),
                            std::span(tile_pixels, 2U * production::kTilePixels),
                            DocumentRevision{0});
  Co5300PanelTransport display;
  PhysicalTouch touch;
  auto* overview_source =
      static_cast<std::uint16_t*>(heap_caps_malloc(production::kOverviewBytes, kExternalCaps));
  if (overview_source == nullptr) {
    std::printf("TINYDRAW_PRODUCTION_WALK_FAIL reason=overview_source\n");
    return;
  }
  std::copy_n(overview, production::kOverviewPixels, overview_source);
  if (!canvas.ready() ||
      !canvas.publish_overview({0}, std::span(overview_source, production::kOverviewPixels)) ||
      !scheduler.ready() || !display.ready() || !touch.ready()) {
    std::printf(
        "TINYDRAW_PRODUCTION_WALK_FAIL reason=bootstrap canvas=%u scheduler=%u display=%u "
        "touch=%u\n",
        canvas.ready(), scheduler.ready(), display.ready(), touch.ready());
    return;
  }

  display.reset_timing();
  bool pass = present_overview(display, scheduler, canvas);
  vTaskDelay(pdMS_TO_TICKS(500));
  const std::span strip_pixels(strip,
                               static_cast<std::size_t>(production::kOverviewWidth * kStripRows));
  constexpr PixelRect kOrigins[]{
      {0, 0, 0, 0}, {184, 224, 0, 0}, {552, 672, 0, 0}, {1104, 1344, 0, 0}};
  for (std::size_t index = 0; index < std::size(kOrigins); ++index) {
    const PixelRect origin = kOrigins[index];
    pass = present_fallback(display, scheduler, canvas, origin.x0, origin.y0,
                            kExpectedFallbackHashes[index], strip_pixels) &&
           pass;
    vTaskDelay(pdMS_TO_TICKS(350));
  }
  constexpr TileKey kAffected{ZoomLevel::k100Percent, 0, 0};
  constexpr TileKey kCarried{ZoomLevel::k100Percent, 1, 0};
  fill_tile_from_overview(std::span(tile_scratch, production::kTilePixels),
                          canvas.overview_pixels(), kAffected);
  pass = canvas
             .publish_tile(kAffected, {0}, MaterializationQuality::kSettled,
                           std::span(tile_scratch, production::kTilePixels))
             .has_value() &&
         pass;
  fill_tile_from_overview(std::span(tile_scratch, production::kTilePixels),
                          canvas.overview_pixels(), kCarried);
  pass = canvas
             .publish_tile(kCarried, {0}, MaterializationQuality::kExact,
                           std::span(tile_scratch, production::kTilePixels))
             .has_value() &&
         pass;

  std::array<CompactOperationSample, 2> input_samples{};
  OperationBuilder input_operation(input_samples);
  pass = input_operation.begin(
             OperationTool::kPen, 0x001FU,
             OperationPoint{
                 .world_x = 8.0F, .world_y = 12.0F, .radius = 5.0F, .timestamp_us = 1'000U}) &&
         pass;
  const auto pen = input_operation.finish(
      OperationPoint{.world_x = 52.0F, .world_y = 48.0F, .radius = 5.0F, .timestamp_us = 5'000U});
  pass =
      pen.has_value() &&
      append_and_commit_probe(operation_log, canvas, *pen,
                              std::span(overview_source, production::kOverviewPixels),
                              std::span(tile_scratch, production::kTilePixels)) &&
      present_incremental(display, scheduler, canvas, strip_pixels, {1}, "pen", kExpectedPenHash) &&
      pass;
  vTaskDelay(pdMS_TO_TICKS(350));

  pass = input_operation.begin(
             OperationTool::kEraser, 0,
             OperationPoint{
                 .world_x = 30.0F, .world_y = 8.0F, .radius = 3.0F, .timestamp_us = 10'000U}) &&
         pass;
  const auto eraser = input_operation.finish(
      OperationPoint{.world_x = 30.0F, .world_y = 55.0F, .radius = 3.0F, .timestamp_us = 14'000U});
  pass = eraser.has_value() &&
         append_and_commit_probe(operation_log, canvas, *eraser,
                                 std::span(overview_source, production::kOverviewPixels),
                                 std::span(tile_scratch, production::kTilePixels)) &&
         present_incremental(display, scheduler, canvas, strip_pixels, {2}, "eraser",
                             kExpectedEraserHash) &&
         pass;
  std::int64_t burst_us = 0;
  for (std::uint32_t index = 0; index < kBurstOperationCount; ++index) {
    const std::uint16_t x = static_cast<std::uint16_t>(32U + (index % 6U) * 32U);
    const std::uint16_t y = static_cast<std::uint16_t>(32U + (index / 6U) * 32U);
    const std::array burst_samples{
        CompactOperationSample{.x_quarter = x, .y_quarter = y, .radius_256 = 512},
        CompactOperationSample{.x_quarter = static_cast<std::uint16_t>(x + 24U),
                               .y_quarter = static_cast<std::uint16_t>(y + 16U),
                               .radius_256 = 512},
    };
    const std::int64_t append_started = esp_timer_get_time();
    pass = append_and_commit_probe(
               operation_log, canvas,
               {.tool = index % 5U == 4U ? OperationTool::kEraser : OperationTool::kPen,
                .color = static_cast<std::uint16_t>(0x0200U + index),
                .samples = burst_samples},
               std::span(overview_source, production::kOverviewPixels),
               std::span(tile_scratch, production::kTilePixels)) &&
           pass;
    burst_us += esp_timer_get_time() - append_started;
  }
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_BURST operations=%lu revision=%lu total_us=%lld average_us=%lld "
      "log_operations=%lu log_samples=%lu\n",
      static_cast<unsigned long>(kBurstOperationCount),
      static_cast<unsigned long>(canvas.current_revision().value), static_cast<long long>(burst_us),
      static_cast<long long>(burst_us / kBurstOperationCount),
      static_cast<unsigned long>(operation_log.operation_count()),
      static_cast<unsigned long>(operation_log.sample_count()));
  pass = operation_log.current_revision() == canvas.current_revision() &&
         canvas.current_revision() == DocumentRevision{kBurstOperationCount + 2U} &&
         operation_log.operation_count() == kBurstOperationCount + 2U && pass;
  pass = present_incremental(display, scheduler, canvas, strip_pixels, {kBurstOperationCount + 2U},
                             "burst", kExpectedBurstHash) &&
         pass;
  const std::int64_t touch_probe_started = esp_timer_get_time();
  std::uint32_t touch_points = 0;
  std::uint32_t touch_errors = 0;
  std::uint32_t touch_changes = 0;
  Point last_touch{};
  bool have_touch = false;
  while (esp_timer_get_time() - touch_probe_started < 5'000'000) {
    Point point{};
    const TouchRead read = touch.read(point);
    if (read == TouchRead::kPoint) {
      ++touch_points;
      if (!have_touch || point.x != last_touch.x || point.y != last_touch.y) {
        ++touch_changes;
        last_touch = point;
      }
      have_touch = true;
    } else if (read == TouchRead::kError) {
      ++touch_errors;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  std::printf(
      "TINYDRAW_PRODUCTION_TOUCH_PROBE ready=%u points=%lu changes=%lu errors=%lu last_x=%.0f "
      "last_y=%.0f elapsed_us=%lld\n",
      touch.ready(), static_cast<unsigned long>(touch_points),
      static_cast<unsigned long>(touch_changes), static_cast<unsigned long>(touch_errors),
      static_cast<double>(last_touch.x), static_cast<double>(last_touch.y),
      static_cast<long long>(esp_timer_get_time() - touch_probe_started));
  pass = touch_errors == 0U && pass;
  const std::uint32_t submits = display.submit_count();
  const std::uint32_t completes = display.complete_count();
  const std::uint32_t rejects = display.rejected_push_count();
  const production::DisplaySchedulerStats scheduler_stats = scheduler.stats();
  pass = pass && display.push_count() == kExpectedTotalPushes && submits == kExpectedTotalPushes &&
         completes == kExpectedTotalPushes && rejects == 0U &&
         scheduler_stats.accepted == kExpectedTotalPushes &&
         scheduler_stats.completed == kExpectedTotalPushes && scheduler_stats.rejected == 0U &&
         scheduler_stats.stale_rejected == 0U && scheduler_stats.queued == 0U;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_DONE pass=%u panel_rejects=%lu scheduler_accepted=%lu "
      "scheduler_completed=%lu scheduler_rejected=%lu scheduler_stale=%lu scheduler_queued=%lu "
      "prepare_us=%lld transfer_us=%lld pushes=%lu submit=%lu complete=%lu free_psram=%lu "
      "largest_psram=%lu\n",
      pass, static_cast<unsigned long>(rejects),
      static_cast<unsigned long>(scheduler_stats.accepted),
      static_cast<unsigned long>(scheduler_stats.completed),
      static_cast<unsigned long>(scheduler_stats.rejected),
      static_cast<unsigned long>(scheduler_stats.stale_rejected),
      static_cast<unsigned long>(scheduler_stats.queued),
      static_cast<long long>(display.prepare_us()), static_cast<long long>(display.transfer_us()),
      static_cast<unsigned long>(display.push_count()), static_cast<unsigned long>(submits),
      static_cast<unsigned long>(completes),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
  std::fflush(stdout);
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_production_overview_walk(); }
