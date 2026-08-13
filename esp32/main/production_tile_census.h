#ifndef TINYDRAW_ESP32_PRODUCTION_TILE_CENSUS_H
#define TINYDRAW_ESP32_PRODUCTION_TILE_CENSUS_H

#include <cstdint>
#include <span>

#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/tile_producer.h"

namespace tinydraw::esp32 {

// Exclusive hardware measurement. Produces and classifies every tile identity
// for the producer's current document without changing the storage policy.
[[nodiscard]] bool run_production_tile_census(production::TileProducer& producer,
                                              production::MaterializedCanvas& canvas,
                                              std::span<std::uint16_t> packed_scratch);

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_PRODUCTION_TILE_CENSUS_H
