#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "firmware_canvas.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef TINYDRAW_QEMU_GRAPHICS
#include "qemu_display.h"
#endif
#include "tinydraw/geometry.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kInk = 0x001FU;
constexpr std::array<tinydraw::TouchPoint, 7> kFixture{{
    {.x = 30.0F, .y = 40.0F, .timestamp_us = 1'000'000U},
    {.x = 80.0F, .y = 90.0F, .timestamp_us = 1'008'000U},
    {.x = 150.0F, .y = 50.0F, .timestamp_us = 1'020'000U},
    {.x = 220.0F, .y = 180.0F, .timestamp_us = 1'024'000U},
    {.x = 180.0F, .y = 300.0F, .timestamp_us = 1'039'000U},
    {.x = 280.0F, .y = 350.0F, .timestamp_us = 1'046'000U},
    {.x = 340.0F, .y = 410.0F, .timestamp_us = 1'055'000U},
}};

struct Bounds {
  float minimum_x = std::numeric_limits<float>::max();
  float minimum_y = std::numeric_limits<float>::max();
  float maximum_x = std::numeric_limits<float>::lowest();
  float maximum_y = std::numeric_limits<float>::lowest();
};

void include(Bounds& bounds, tinydraw::Point point, float padding = 0.0F) {
  bounds.minimum_x = std::min(bounds.minimum_x, point.x - padding);
  bounds.minimum_y = std::min(bounds.minimum_y, point.y - padding);
  bounds.maximum_x = std::max(bounds.maximum_x, point.x + padding);
  bounds.maximum_y = std::max(bounds.maximum_y, point.y + padding);
}

void include(Bounds& bounds, const tinydraw::RibbonPrimitiveBatch& primitives) {
  for (const auto& primitive : primitives) {
    if (primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
      include(bounds, primitive.center, primitive.radius);
      continue;
    }
    for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
      include(bounds, primitive.points[index]);
    }
  }
}

std::uint32_t hash_pixel(std::uint32_t hash, std::uint16_t pixel) {
  constexpr std::uint32_t kFnvPrime = 16'777'619U;
  hash = (hash ^ static_cast<std::uint8_t>(pixel >> 8U)) * kFnvPrime;
  return (hash ^ static_cast<std::uint8_t>(pixel)) * kFnvPrime;
}

std::uint32_t canvas_checksum(std::span<const std::uint16_t> pixels) {
  std::uint32_t hash = 2'166'136'261U;
  for (const auto pixel : pixels) {
    hash = hash_pixel(hash, pixel);
  }
  return hash;
}

std::uint32_t touched_tiles(std::span<const std::uint16_t> pixels) {
  std::uint32_t count = 0U;
  for (int tile_y = 0; tile_y < tinydraw::kCanvasHeight; tile_y += tinydraw::kTileSize) {
    for (int tile_x = 0; tile_x < tinydraw::kCanvasWidth; tile_x += tinydraw::kTileSize) {
      const int width = std::min(tinydraw::kTileSize, tinydraw::kCanvasWidth - tile_x);
      const int height = std::min(tinydraw::kTileSize, tinydraw::kCanvasHeight - tile_y);
      bool touched = false;
      for (int y = 0; y < height && !touched; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto index =
              static_cast<std::size_t>((tile_y + y) * tinydraw::kCanvasWidth + tile_x + x);
          if (pixels[index] != kBackground) {
            touched = true;
            break;
          }
        }
      }
      count += touched ? 1U : 0U;
    }
  }
  return count;
}

class NullDisplay final : public tinydraw::DisplayBackend {
 public:
  void push_rect(int, int, int, int, const std::uint16_t*, int = 0) override { ++pushes; }

  std::uint32_t pushes = 0U;
};

struct ReplayStats {
  Bounds bounds;
  std::uint32_t primitives = 0U;
  std::uint32_t tiles_updated = 0U;
  std::uint32_t primitive_tile_visits = 0U;
  std::uint32_t pixels_composited = 0U;
  std::uint32_t display_bytes = 0U;
  std::uint32_t psram_bytes_read = 0U;
  std::uint32_t psram_bytes_written = 0U;
  std::uint32_t maximum_tiles_per_update = 0U;
  std::uint32_t maximum_visits_per_update = 0U;
  std::uint32_t finish_tiles = 0U;
  std::uint32_t finish_visits = 0U;
  std::uint32_t maximum_psram_read_per_frame = 0U;
  std::uint32_t maximum_psram_write_per_frame = 0U;
};

void accumulate(ReplayStats& total, const tinydraw::RibbonUpdate& update,
                const tinydraw::StrokeRasterStats& frame, bool final) {
  include(total.bounds, update.committed);
  total.primitives += static_cast<std::uint32_t>(update.committed.size());
  total.tiles_updated += frame.tiles_updated;
  total.primitive_tile_visits += frame.primitive_tile_visits;
  total.pixels_composited += frame.pixels_composited;
  total.display_bytes += frame.display_bytes;
  const auto psram_read = frame.committed_bytes_read + frame.coverage_bytes_read;
  const auto psram_written =
      frame.committed_bytes_written + frame.coverage_bytes_written + frame.history_bytes_written;
  total.psram_bytes_read += psram_read;
  total.psram_bytes_written += psram_written;
  if (final) {
    total.finish_tiles = frame.tiles_updated;
    total.finish_visits = frame.primitive_tile_visits;
  } else {
    total.maximum_tiles_per_update = std::max(total.maximum_tiles_per_update, frame.tiles_updated);
    total.maximum_visits_per_update =
        std::max(total.maximum_visits_per_update, frame.primitive_tile_visits);
  }
  total.maximum_psram_read_per_frame = std::max(total.maximum_psram_read_per_frame, psram_read);
  total.maximum_psram_write_per_frame =
      std::max(total.maximum_psram_write_per_frame, psram_written);
}

}  // namespace

#ifndef TINYDRAW_QEMU
void run_hardware_app();
#endif

extern "C" void app_main() {
#ifndef TINYDRAW_QEMU
  run_hardware_app();
  return;
#endif
  NullDisplay null_display;
  tinydraw::DisplayBackend* display = &null_display;
#ifdef TINYDRAW_QEMU_GRAPHICS
  tinydraw::esp32::QemuDisplayBackend qemu_display;
  if (!qemu_display.ready()) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=qemu_display\n");
    return;
  }
  display = &qemu_display;
  std::fill(qemu_display.framebuffer().begin(), qemu_display.framebuffer().end(), kBackground);
  tinydraw::ToolbarState toolbar;
  toolbar.can_undo = true;
  tinydraw::draw_toolbar(qemu_display.framebuffer(), tinydraw::kCanvasWidth,
                         tinydraw::kCanvasHeight, toolbar);
#endif

  tinydraw::esp32::FirmwareCanvas canvas(*display);
  if (!canvas.ready() || !canvas.capabilities_valid()) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=canvas_memory\n");
    return;
  }
  std::printf(
      "TINYDRAW_MEMORY_OK committed=%u coverage=%u history=%u world=%u scratch=dma_internal\n",
      static_cast<unsigned>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight * 2),
      static_cast<unsigned>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight),
      static_cast<unsigned>(tinydraw::TileUndoHistory::kRequiredPixels * 2U),
      static_cast<unsigned>(tinydraw::WorldCanvas::kRequiredPixels * 2U));

  tinydraw::InkStream ink;
  tinydraw::RibbonStream ribbon;
  ReplayStats total;

  const auto process_frame = [&](const tinydraw::RibbonUpdate& update, bool final) {
    const auto stats = final ? canvas.raster().finish(update, kInk, &canvas.undo_history())
                             : canvas.raster().update(update, kInk);
    accumulate(total, update, stats, final);
#ifdef TINYDRAW_QEMU_GRAPHICS
    tinydraw::draw_toolbar(qemu_display.framebuffer(), tinydraw::kCanvasWidth,
                           tinydraw::kCanvasHeight, toolbar);
    return qemu_display.refresh();
#else
    return true;
#endif
  };

  if (!process_frame(ribbon.append(ink.begin(kFixture.front())), false)) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=frame_refresh\n");
    return;
  }
  for (std::size_t index = 1; index + 1U < kFixture.size(); ++index) {
    if (!process_frame(ribbon.append(ink.update(kFixture[index])), false)) {
      std::printf("TINYDRAW_REPLAY_FAIL reason=frame_refresh\n");
      return;
    }
  }
  if (!process_frame(ribbon.finish(ink.finish(kFixture.back())), true)) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=frame_refresh\n");
    return;
  }

  const auto committed = canvas.committed();
  const auto final_tiles = touched_tiles(committed);
  const auto checksum = canvas_checksum(committed);
  if (total.maximum_tiles_per_update > 20U || total.maximum_visits_per_update > 80U ||
      total.finish_tiles > 48U || total.finish_visits > 96U ||
      total.maximum_psram_read_per_frame > 512U * 1024U ||
      total.maximum_psram_write_per_frame > 512U * 1024U ||
      total.display_bytes != total.pixels_composited * 2U) {
    std::printf(
        "TINYDRAW_REPLAY_FAIL reason=unbounded_work max_tiles=%lu max_visits=%lu "
        "finish_tiles=%lu finish_visits=%lu bytes=%lu pixels=%lu\n",
        static_cast<unsigned long>(total.maximum_tiles_per_update),
        static_cast<unsigned long>(total.maximum_visits_per_update),
        static_cast<unsigned long>(total.finish_tiles),
        static_cast<unsigned long>(total.finish_visits),
        static_cast<unsigned long>(total.display_bytes),
        static_cast<unsigned long>(total.pixels_composited));
    return;
  }
#ifdef TINYDRAW_QEMU_GRAPHICS
  const auto framebuffer = qemu_display.framebuffer();
  const auto background_corner = static_cast<std::size_t>(0);
  const auto stroke_center = static_cast<std::size_t>(40 * tinydraw::kCanvasWidth + 30);
  const auto color_center = static_cast<std::size_t>(410 * tinydraw::kCanvasWidth + 213);
  if (framebuffer[background_corner] != kBackground || framebuffer[stroke_center] != kInk ||
      framebuffer[color_center] != tinydraw::rgb565(toolbar.color) ||
      qemu_display.refresh_count() != kFixture.size() ||
      qemu_display.push_count() != kFixture.size()) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=qemu_ui\n");
    return;
  }
  std::printf("TINYDRAW_UI_OK canvas=1 controls=6 pushes=%lu\n",
              static_cast<unsigned long>(qemu_display.push_count()));
#endif
  std::printf(
      "TINYDRAW_REPLAY_OK accepted=%u primitives=%u tiles=%u "
      "bounds=%.2f,%.2f,%.2f,%.2f checksum=%08lx frames=%u dirty=%u max_tiles=%u visits=%u "
      "max_visits=%u finish_tiles=%u finish_visits=%u display=%u psram_read=%u "
      "psram_write=%u max_psram_read=%u max_psram_write=%u\n",
      static_cast<unsigned>(kFixture.size()), static_cast<unsigned>(total.primitives),
      static_cast<unsigned>(final_tiles), static_cast<double>(total.bounds.minimum_x),
      static_cast<double>(total.bounds.minimum_y), static_cast<double>(total.bounds.maximum_x),
      static_cast<double>(total.bounds.maximum_y), static_cast<unsigned long>(checksum),
      static_cast<unsigned>(kFixture.size()), static_cast<unsigned>(total.tiles_updated),
      static_cast<unsigned>(total.maximum_tiles_per_update),
      static_cast<unsigned>(total.primitive_tile_visits),
      static_cast<unsigned>(total.maximum_visits_per_update),
      static_cast<unsigned>(total.finish_tiles), static_cast<unsigned>(total.finish_visits),
      static_cast<unsigned>(total.display_bytes), static_cast<unsigned>(total.psram_bytes_read),
      static_cast<unsigned>(total.psram_bytes_written),
      static_cast<unsigned>(total.maximum_psram_read_per_frame),
      static_cast<unsigned>(total.maximum_psram_write_per_frame));
  vTaskDelay(1);
  std::printf("TINYDRAW_QEMU_DONE\n");
}
