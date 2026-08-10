#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/graphics/world_canvas.h"

namespace tinydraw::esp32 {

// Starts an open "TinyDraw" access point and export page at
// http://192.168.4.1. The PNG endpoint reads the full raster world.
[[nodiscard]] bool start_wifi_export(const WorldCanvas& world,
                                     std::span<const std::uint16_t> viewport);

}  // namespace tinydraw::esp32
