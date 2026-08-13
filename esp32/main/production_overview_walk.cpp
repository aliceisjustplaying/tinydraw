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
#include "tinydraw/production/incremental_rasterizer.h"
#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation_log.h"

namespace tinydraw::esp32 {
namespace {

using production::CompactOperationSample;
using production::DocumentRevision;
using production::IncrementalOperation;
using production::MaterializationQuality;
using production::MaterializedCanvas;
using production::MaterializedSlotStorage;
using production::OperationLog;
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
constexpr std::uint32_t kExpectedTotalPushes = 147U;
constexpr std::uint32_t kExpectedOverviewHash = 0xD76C09B1U;
constexpr std::uint32_t kExpectedFallbackHashes[]{0xD9E39425U, 0xA4CE26E5U, 0x1B5753A5U,
                                                  0x91F8B705U};
constexpr std::uint32_t kExpectedPenHash = 0xE93CC976U;
constexpr std::uint32_t kExpectedEraserHash = 0x9C622475U;
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

bool present_overview(Co5300PanelTransport& display, const MaterializedCanvas& canvas) {
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

bool present_fallback(Co5300PanelTransport& display, MaterializedCanvas& canvas, int world_x,
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

bool present_incremental(Co5300PanelTransport& display, MaterializedCanvas& canvas,
                         std::span<std::uint16_t> strip, DocumentRevision revision,
                         const char* phase, std::uint32_t expected_hash) {
  const std::int64_t started = esp_timer_get_time();
  const std::uint32_t pushes_before = display.push_count();
  std::uint32_t hash = kFnvOffset;
  std::size_t tile_pixels = 0;
  std::size_t fallback_pixels = 0;
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
    display.push_rect(0, panel_y, production::kOverviewWidth, rows, destination.data(),
                      production::kOverviewWidth);
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

bool commit_operation_probe(MaterializedCanvas& canvas, const IncrementalOperation& operation,
                            DocumentRevision revision, std::span<std::uint16_t> next_overview,
                            std::span<std::uint16_t> tile_scratch) {
  constexpr TileKey kAffected{ZoomLevel::k100Percent, 0, 0};
  constexpr TileKey kCarried{ZoomLevel::k100Percent, 1, 0};
  if (revision == DocumentRevision{1}) {
    fill_tile_from_overview(tile_scratch, canvas.overview_pixels(), kAffected);
    if (!canvas.publish_tile(kAffected, {0}, MaterializationQuality::kSettled, tile_scratch)) {
      return false;
    }
    fill_tile_from_overview(tile_scratch, canvas.overview_pixels(), kCarried);
    if (!canvas.publish_tile(kCarried, {0}, MaterializationQuality::kExact, tile_scratch)) {
      return false;
    }
  }
  const auto carried_before = canvas.lookup(kCarried);
  if (!carried_before.has_value()) {
    return false;
  }

  std::array<TileKey, 4> operation_tiles{};
  const auto affected_count =
      production::affected_tiles(operation, ZoomLevel::k100Percent, operation_tiles);
  if (!affected_count.has_value() || !affected_count->complete() || affected_count->written != 1U ||
      operation_tiles[0] != kAffected) {
    return false;
  }
  const auto prior_tile = canvas.compose_view(
      {.zoom = ZoomLevel::k100Percent, .level_pixels = production::tile_pixel_bounds(kAffected)},
      tile_scratch);
  if (!prior_tile.has_value() || prior_tile->tile_pixels != production::kTilePixels) {
    return false;
  }
  std::copy(canvas.overview_pixels().begin(), canvas.overview_pixels().end(),
            next_overview.begin());

  const std::int64_t render_started = esp_timer_get_time();
  const bool overview_rendered = production::apply_incremental_operation(
      operation,
      RasterSurface{.zoom = ZoomLevel::k25Percent,
                    .level_bounds = {0, 0, production::kOverviewWidth, production::kOverviewHeight},
                    .pixels = next_overview,
                    .stride = production::kOverviewWidth});
  const std::int64_t overview_us = esp_timer_get_time() - render_started;
  const std::int64_t tile_started = esp_timer_get_time();
  const bool tile_rendered = production::apply_incremental_operation(
      operation, RasterSurface{.zoom = ZoomLevel::k100Percent,
                               .level_bounds = production::tile_pixel_bounds(kAffected),
                               .pixels = tile_scratch,
                               .stride = production::kTileWidth});
  const std::int64_t tile_us = esp_timer_get_time() - tile_started;
  if (!overview_rendered || !tile_rendered) {
    return false;
  }

  const std::array publications{TileRevisionPublication{
      .key = kAffected,
      .quality = MaterializationQuality::kSettled,
      .pixels = tile_scratch,
  }};
  const std::int64_t commit_started = esp_timer_get_time();
  const auto world_bounds = production::operation_world_bounds(operation.samples);
  if (!world_bounds.has_value()) {
    return false;
  }
  const bool committed =
      canvas.commit_incremental_revision(revision, next_overview, *world_bounds, publications);
  const std::int64_t commit_us = esp_timer_get_time() - commit_started;
  std::printf(
      "TINYDRAW_PRODUCTION_WALK_OPERATION revision=%lu overview_us=%lld tile_us=%lld "
      "commit_us=%lld "
      "committed=%u\n",
      static_cast<unsigned long>(revision.value), static_cast<long long>(overview_us),
      static_cast<long long>(tile_us), static_cast<long long>(commit_us), committed);
  if (!committed) {
    return false;
  }
  const auto updated = canvas.lookup(kAffected);
  const auto carried = canvas.lookup(kCarried);
  return updated.has_value() && carried.has_value() && updated->identity.revision == revision &&
         updated->identity.quality == MaterializationQuality::kSettled &&
         carried->identity.revision == revision &&
         carried->identity.generation == carried_before->identity.generation;
}

}  // namespace

bool append_and_commit_probe(OperationLog& log, MaterializedCanvas& canvas,
                             const production::OperationAppend& append_request,
                             std::span<std::uint16_t> next_overview,
                             std::span<std::uint16_t> tile_scratch) {
  const auto identity = log.append(append_request);
  if (!identity.has_value()) {
    return false;
  }
  const auto stored = log.operation(identity->operation_index);
  if (!stored.has_value() || stored->identity != *identity) {
    return false;
  }
  const IncrementalOperation operation{
      .tool = stored->tool, .color = stored->color, .samples = stored->samples};
  return commit_operation_probe(canvas, operation, identity->revision, next_overview, tile_scratch);
}

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
      static_cast<OperationRecord*>(heap_caps_malloc(2U * sizeof(OperationRecord), kExternalCaps));
  auto* operation_samples = static_cast<CompactOperationSample*>(
      heap_caps_malloc(4U * sizeof(CompactOperationSample), kExternalCaps));
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

  OperationLog operation_log(std::span(operation_records, 2), std::span(operation_samples, 4));
  MaterializedCanvas canvas(std::span(overview, production::kOverviewPixels), std::span(slots, 2),
                            std::span(tile_pixels, 2U * production::kTilePixels),
                            DocumentRevision{0});
  Co5300PanelTransport display;
  auto* overview_source =
      static_cast<std::uint16_t*>(heap_caps_malloc(production::kOverviewBytes, kExternalCaps));
  if (overview_source == nullptr) {
    std::printf("TINYDRAW_PRODUCTION_WALK_FAIL reason=overview_source\n");
    return;
  }
  std::copy_n(overview, production::kOverviewPixels, overview_source);
  if (!canvas.ready() ||
      !canvas.publish_overview({0}, std::span(overview_source, production::kOverviewPixels)) ||
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
  const std::array pen_samples{
      CompactOperationSample{.x_quarter = 32, .y_quarter = 48, .radius_256 = 1280},
      CompactOperationSample{.x_quarter = 208, .y_quarter = 192, .radius_256 = 1280},
  };
  const IncrementalOperation pen{
      .tool = OperationTool::kPen, .color = 0x001FU, .samples = pen_samples};
  pass = append_and_commit_probe(operation_log, canvas,
                                 {.tool = pen.tool, .color = pen.color, .samples = pen.samples},
                                 std::span(overview_source, production::kOverviewPixels),
                                 std::span(tile_scratch, production::kTilePixels)) &&
         present_incremental(display, canvas, strip_pixels, {1}, "pen", kExpectedPenHash) && pass;
  vTaskDelay(pdMS_TO_TICKS(350));

  const std::array eraser_samples{
      CompactOperationSample{.x_quarter = 120, .y_quarter = 32, .radius_256 = 768},
      CompactOperationSample{.x_quarter = 120, .y_quarter = 220, .radius_256 = 768},
  };
  const IncrementalOperation eraser{.tool = OperationTool::kEraser, .samples = eraser_samples};
  pass = append_and_commit_probe(
             operation_log, canvas,
             {.tool = eraser.tool, .color = eraser.color, .samples = eraser.samples},
             std::span(overview_source, production::kOverviewPixels),
             std::span(tile_scratch, production::kTilePixels)) &&
         present_incremental(display, canvas, strip_pixels, {2}, "eraser", kExpectedEraserHash) &&
         pass;
  const std::uint32_t submits = display.submit_count();
  const std::uint32_t completes = display.complete_count();
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
