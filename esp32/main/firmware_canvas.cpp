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
  undo_history_storage_ = heap_caps_malloc(sizeof(TileUndoHistory), kDmaInternalCaps);
  raster_storage_ = heap_caps_malloc(sizeof(StrokeRaster), kDmaInternalCaps);
  if (committed_ == nullptr || visible_ == nullptr || active_coverage_ == nullptr ||
      undo_storage_ == nullptr || undo_history_storage_ == nullptr || raster_storage_ == nullptr) {
    return;
  }

  std::fill_n(committed_, kPixelCount, kBackground);
  std::fill_n(visible_, kPixelCount, kBackground);
  undo_history_ = new (undo_history_storage_)
      TileUndoHistory(std::span(undo_storage_, TileUndoHistory::kRequiredPixels));
  raster_ = new (raster_storage_)
      StrokeRaster(std::span(committed_, kPixelCount), std::span(visible_, kPixelCount),
                   std::span(active_coverage_, kPixelCount), display);
}

FirmwareCanvas::~FirmwareCanvas() {
  if (raster_ != nullptr) {
    raster_->~StrokeRaster();
  }
  if (undo_history_ != nullptr) {
    undo_history_->~TileUndoHistory();
  }
  heap_caps_free(raster_storage_);
  heap_caps_free(undo_history_storage_);
  heap_caps_free(undo_storage_);
  heap_caps_free(active_coverage_);
  heap_caps_free(visible_);
  heap_caps_free(committed_);
}

bool FirmwareCanvas::capabilities_valid() const {
  return ready() && esp_ptr_external_ram(committed_) && esp_ptr_external_ram(visible_) &&
         esp_ptr_internal(active_coverage_) && esp_ptr_external_ram(undo_storage_) &&
         esp_ptr_internal(undo_history_storage_) && esp_ptr_dma_capable(undo_history_storage_) &&
         esp_ptr_internal(raster_storage_) && esp_ptr_dma_capable(raster_storage_);
}

std::span<std::uint16_t> FirmwareCanvas::committed() {
  return ready() ? std::span(committed_, kPixelCount) : std::span<std::uint16_t>{};
}

std::span<std::uint16_t> FirmwareCanvas::visible() {
  return ready() ? std::span(visible_, kPixelCount) : std::span<std::uint16_t>{};
}

}  // namespace tinydraw::esp32
