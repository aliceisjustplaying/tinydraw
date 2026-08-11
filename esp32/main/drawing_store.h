#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/world_canvas.h"

namespace tinydraw::esp32 {

// Debounced, tile-granular persistence for the physical ESP32 build.
class DrawingStore {
 public:
  DrawingStore();
  ~DrawingStore();

  DrawingStore(const DrawingStore&) = delete;
  DrawingStore& operator=(const DrawingStore&) = delete;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool restore(WorldCanvas& world, std::span<std::uint16_t> committed,
                             std::span<std::uint16_t> visible);

  void activity();
  void suspend();
  void include_segment(Point from, Point to, float radius, ViewOrigin origin);
  void save_stroke(WorldCanvas& world, std::span<const std::uint16_t> viewport);
  void save_viewport(WorldCanvas& world, std::span<const std::uint16_t> viewport);
  void save_all(WorldCanvas& world);
  void save_origin(const WorldCanvas& world);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace tinydraw::esp32
