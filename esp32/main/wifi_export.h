#pragma once

#include <cstdint>
#include <span>

namespace tinydraw::esp32 {

// Starts an open "TinyDraw" access point and captive export page at
// http://192.168.4.1. The PNG endpoint reads the current raster viewport.
[[nodiscard]] bool start_wifi_export(std::span<const std::uint16_t> canvas);

}  // namespace tinydraw::esp32
