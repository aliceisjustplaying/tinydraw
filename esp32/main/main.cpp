#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

#include "firmware_canvas.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef TINYDRAW_QEMU_GRAPHICS
#include "qemu_display.h"
#endif
#include "tinydraw/geometry.h"
#include "tinydraw/platform/display_backend.h"
#include "tinydraw/graphics/coverage_tile.h"
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
constexpr std::size_t kMaximumPrimitives = 32U;

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

Bounds primitive_bounds(std::span<const tinydraw::RibbonPrimitive> primitives) {
  Bounds bounds;
  for (const auto& primitive : primitives) {
    if (primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
      include(bounds, primitive.center, primitive.radius);
      continue;
    }
    for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
      include(bounds, primitive.points[index]);
    }
  }
  return bounds;
}

std::uint32_t hash_pixel(std::uint32_t hash, std::uint16_t pixel) {
  constexpr std::uint32_t kFnvPrime = 16'777'619U;
  hash = (hash ^ static_cast<std::uint8_t>(pixel >> 8U)) * kFnvPrime;
  return (hash ^ static_cast<std::uint8_t>(pixel)) * kFnvPrime;
}

class NullDisplay final : public tinydraw::DisplayBackend {
 public:
  void push_rect(int, int, int, int, const std::uint16_t*) override {}
  [[nodiscard]] bool busy() const override { return false; }
};

struct RasterResult {
  std::uint32_t tiles_touched = 0;
  std::uint32_t checksum = 2'166'136'261U;
};

RasterResult raster_checksum(std::span<const tinydraw::RibbonPrimitive> primitives,
                             tinydraw::DisplayBackend* display) {
  static tinydraw::CoverageTile coverage(0, 0);
  static std::array<std::uint16_t, tinydraw::kTileSize * tinydraw::kTileSize> pixels{};
  RasterResult result;

  for (int tile_y = 0; tile_y < tinydraw::kCanvasHeight; tile_y += tinydraw::kTileSize) {
    for (int tile_x = 0; tile_x < tinydraw::kCanvasWidth; tile_x += tinydraw::kTileSize) {
      const int width = std::min(tinydraw::kTileSize, tinydraw::kCanvasWidth - tile_x);
      const int height = std::min(tinydraw::kTileSize, tinydraw::kCanvasHeight - tile_y);
      coverage.reset(tile_x, tile_y, width, height);
      for (const auto& primitive : primitives) {
        if (primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
          coverage.rasterize_circle(primitive.center, primitive.radius);
        } else {
          coverage.rasterize_convex(
              std::span(primitive.points.data(), primitive.point_count));
        }
      }

      const std::size_t pixel_count = static_cast<std::size_t>(width * height);
      std::fill_n(pixels.begin(), pixel_count, kBackground);
      tinydraw::composite_rgb565(coverage, kInk, std::span(pixels.data(), pixel_count));
      if (display != nullptr) {
        display->push_rect(tile_x, tile_y, width, height, pixels.data());
      }

      bool touched = false;
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          touched = touched || pixels[index] != kBackground;
          result.checksum = hash_pixel(result.checksum, pixels[index]);
        }
      }
      result.tiles_touched += touched ? 1U : 0U;
    }
  }
  return result;
}

}  // namespace

extern "C" void app_main() {
  tinydraw::InkStream ink;
  tinydraw::RibbonStream ribbon;
  static std::array<tinydraw::RibbonPrimitive, kMaximumPrimitives> primitives{};
  std::size_t primitive_count = 0U;

  const auto keep_committed = [&](const tinydraw::RibbonUpdate& update) {
    for (const auto& primitive : update.committed) {
      if (primitive_count >= primitives.size()) {
        std::printf("TINYDRAW_REPLAY_FAIL reason=primitive_capacity\n");
        return false;
      }
      primitives[primitive_count++] = primitive;
    }
    return true;
  };

  if (!keep_committed(ribbon.append(ink.begin(kFixture.front())))) {
    return;
  }
  for (std::size_t index = 1; index + 1U < kFixture.size(); ++index) {
    if (!keep_committed(ribbon.append(ink.update(kFixture[index])))) {
      return;
    }
  }
  if (!keep_committed(ribbon.finish(ink.finish(kFixture.back())))) {
    return;
  }

  NullDisplay null_display;
  tinydraw::DisplayBackend* display = &null_display;
#ifdef TINYDRAW_QEMU_GRAPHICS
  tinydraw::esp32::QemuDisplayBackend qemu_display;
  if (!qemu_display.ready()) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=qemu_display\n");
    return;
  }
  display = &qemu_display;
#endif
  tinydraw::esp32::FirmwareCanvas canvas(*display);
  if (!canvas.ready() || !canvas.capabilities_valid()) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=canvas_memory\n");
    return;
  }
  std::printf("TINYDRAW_MEMORY_OK committed=%u coverage=%u scratch=internal\n",
              static_cast<unsigned>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight * 2),
              static_cast<unsigned>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));

  const auto geometry = std::span(primitives.data(), primitive_count);
  const Bounds bounds = primitive_bounds(geometry);
  const RasterResult raster = raster_checksum(geometry, display);
#ifdef TINYDRAW_QEMU_GRAPHICS
  tinydraw::ToolbarState toolbar;
  toolbar.can_undo = true;
  auto framebuffer = qemu_display.framebuffer();
  tinydraw::draw_toolbar(framebuffer, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, toolbar);
  const auto background_corner = static_cast<std::size_t>(0);
  const auto stroke_center = static_cast<std::size_t>(40 * tinydraw::kCanvasWidth + 30);
  const auto color_center = static_cast<std::size_t>(410 * tinydraw::kCanvasWidth + 213);
  if (framebuffer[background_corner] != kBackground || framebuffer[stroke_center] != kInk ||
      framebuffer[color_center] != tinydraw::rgb565(toolbar.color) || !qemu_display.refresh()) {
    std::printf("TINYDRAW_REPLAY_FAIL reason=qemu_ui\n");
    return;
  }
  std::printf("TINYDRAW_UI_OK canvas=1 controls=6\n");
#endif
  std::printf(
      "TINYDRAW_REPLAY_OK accepted=%u primitives=%u tiles=%u "
      "bounds=%.2f,%.2f,%.2f,%.2f checksum=%08lx\n",
      static_cast<unsigned>(kFixture.size()), static_cast<unsigned>(primitive_count),
      static_cast<unsigned>(raster.tiles_touched), static_cast<double>(bounds.minimum_x),
      static_cast<double>(bounds.minimum_y), static_cast<double>(bounds.maximum_x),
      static_cast<double>(bounds.maximum_y), static_cast<unsigned long>(raster.checksum));
  vTaskDelay(1);
  std::printf("TINYDRAW_QEMU_DONE\n");
}
