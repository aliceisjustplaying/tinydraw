#ifndef TINYDRAW_ESP32_VECTOR_V2_TILE_CENSUS_H
#define TINYDRAW_ESP32_VECTOR_V2_TILE_CENSUS_H

#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace tinydraw::esp32 {

// Exclusive hardware measurement. Produces and classifies every tile identity
// for the producer's current document without changing the storage policy.
[[nodiscard]] bool run_vector_v2_tile_census(vector_v2::TileProducer& producer,
                                             vector_v2::MaterializedCanvas& canvas,
                                             std::span<std::uint16_t> packed_scratch);

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_TILE_CENSUS_H
