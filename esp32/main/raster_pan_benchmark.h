#pragma once

#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/platform/display_backend.h"

namespace tinydraw::esp32 {

void run_raster_pan_benchmark(WorldCanvas& world, DisplayBackend& display, int bottom);

}  // namespace tinydraw::esp32
