#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/graphics/stroke_raster.h"
#include "tinydraw/graphics/tile_undo_history.h"
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

class FirmwareCanvas {
 public:
  explicit FirmwareCanvas(DisplayBackend& display);
  ~FirmwareCanvas();

  FirmwareCanvas(const FirmwareCanvas&) = delete;
  FirmwareCanvas& operator=(const FirmwareCanvas&) = delete;

  [[nodiscard]] bool ready() const { return raster_ != nullptr; }
  [[nodiscard]] bool capabilities_valid() const;
  [[nodiscard]] std::span<std::uint16_t> committed();
  [[nodiscard]] std::span<std::uint16_t> visible();
  [[nodiscard]] StrokeRaster& raster() { return *raster_; }
  [[nodiscard]] TileUndoHistory& undo_history() { return *undo_history_; }
  [[nodiscard]] WorldCanvas& world() { return *world_; }
 private:
  static constexpr std::size_t kPixelCount = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);

  std::uint16_t* committed_ = nullptr;
  std::uint16_t* visible_ = nullptr;
  std::uint8_t* active_coverage_ = nullptr;
  std::uint16_t* undo_storage_ = nullptr;
  std::uint16_t* world_storage_ = nullptr;
  void* undo_history_storage_ = nullptr;
  TileUndoHistory* undo_history_ = nullptr;
  void* world_object_storage_ = nullptr;
  WorldCanvas* world_ = nullptr;
  void* raster_storage_ = nullptr;
  StrokeRaster* raster_ = nullptr;
};

}  // namespace tinydraw::esp32
