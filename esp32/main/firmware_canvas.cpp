#include "firmware_canvas.h"

#include <algorithm>
#include <new>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr std::uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr std::uint32_t kDmaInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;

}  // namespace

FirmwareCanvas::FirmwareCanvas(DisplayBackend& display) {
  committed_ = static_cast<std::uint16_t*>(
      heap_caps_malloc(kPixelCount * sizeof(std::uint16_t), kExternalCaps));
  visible_ = static_cast<std::uint16_t*>(
      heap_caps_malloc(kPixelCount * sizeof(std::uint16_t), kExternalCaps));
  active_coverage_ = static_cast<std::uint8_t*>(
      heap_caps_calloc(kPixelCount, sizeof(std::uint8_t), kInternalCaps));
  undo_storage_ = static_cast<std::uint16_t*>(
      heap_caps_malloc(TileUndoHistory::kRequiredPixels * sizeof(std::uint16_t), kExternalCaps));
  world_storage_ = static_cast<std::uint16_t*>(
      heap_caps_malloc(WorldCanvas::kRequiredPixels * sizeof(std::uint16_t), kExternalCaps));
  undo_history_storage_ = heap_caps_malloc(sizeof(TileUndoHistory), kDmaInternalCaps);
  world_object_storage_ = heap_caps_malloc(sizeof(WorldCanvas), kDmaInternalCaps);
  raster_storage_ = heap_caps_malloc(sizeof(StrokeRaster), kDmaInternalCaps);
  if (committed_ == nullptr || visible_ == nullptr || active_coverage_ == nullptr ||
      undo_storage_ == nullptr || world_storage_ == nullptr || undo_history_storage_ == nullptr ||
      world_object_storage_ == nullptr || raster_storage_ == nullptr) {
    return;
  }

  std::fill_n(committed_, kPixelCount, kBackground);
  std::fill_n(visible_, kPixelCount, kBackground);
  undo_history_ = new (undo_history_storage_)
      TileUndoHistory(std::span(undo_storage_, TileUndoHistory::kRequiredPixels));
  world_ = new (world_object_storage_)
      WorldCanvas(std::span(world_storage_, WorldCanvas::kRequiredPixels));
  raster_ = new (raster_storage_)
      StrokeRaster(std::span(committed_, kPixelCount), std::span(visible_, kPixelCount),
                   std::span(active_coverage_, kPixelCount), display);
}

FirmwareCanvas::~FirmwareCanvas() {
  if (raster_ != nullptr) {
    raster_->~StrokeRaster();
  }
  if (world_ != nullptr) {
    world_->~WorldCanvas();
  }
  if (undo_history_ != nullptr) {
    undo_history_->~TileUndoHistory();
  }
  heap_caps_free(raster_storage_);
  heap_caps_free(world_object_storage_);
  heap_caps_free(undo_history_storage_);
  heap_caps_free(world_storage_);
  heap_caps_free(undo_storage_);
  heap_caps_free(active_coverage_);
  heap_caps_free(visible_);
  heap_caps_free(committed_);
}

bool FirmwareCanvas::capabilities_valid() const {
  return ready() && world_ != nullptr && world_->valid() && esp_ptr_external_ram(committed_) &&
         esp_ptr_external_ram(visible_) && esp_ptr_internal(active_coverage_) &&
         esp_ptr_external_ram(undo_storage_) && esp_ptr_external_ram(world_storage_) &&
         esp_ptr_internal(undo_history_storage_) && esp_ptr_dma_capable(undo_history_storage_) &&
         esp_ptr_internal(world_object_storage_) && esp_ptr_dma_capable(world_object_storage_) &&
         esp_ptr_internal(raster_storage_) && esp_ptr_dma_capable(raster_storage_);
}

std::span<std::uint16_t> FirmwareCanvas::committed() {
  return ready() ? std::span(committed_, kPixelCount) : std::span<std::uint16_t>{};
}

std::span<std::uint16_t> FirmwareCanvas::visible() {
  return ready() ? std::span(visible_, kPixelCount) : std::span<std::uint16_t>{};
}

#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
std::span<std::uint16_t> FirmwareCanvas::prototype_materialization_storage() {
  // The decisive prototype deliberately defers raster Undo. Reuse its 3.28 MiB
  // arena as a second 3x3 materialization buffer rather than increasing PSRAM.
  return ready() && TileUndoHistory::kRequiredPixels >= WorldCanvas::kRequiredPixels
             ? std::span(undo_storage_, WorldCanvas::kRequiredPixels)
             : std::span<std::uint16_t>{};
}
#endif

}  // namespace tinydraw::esp32
