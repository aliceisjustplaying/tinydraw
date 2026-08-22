#ifndef TINYDRAW_VECTOR_V2_BUFFER_INITIALIZATION_INTERNAL_H
#define TINYDRAW_VECTOR_V2_BUFFER_INITIALIZATION_INTERNAL_H

#include <cstdint>
#include <span>

namespace tinydraw::vector_v2::buffer_initialization_internal {

// Initializes the painter surface and its optional finalized-pixel mask in
// one call. The ESP32-S3 implementation sends each aligned 16-byte interior
// through PIE; host and emulator builds use the exact scalar implementation.
void initialize_raster_buffers(std::span<std::uint16_t> pixels, std::uint16_t color,
                               std::span<std::uint8_t> finalized_pixels);

}  // namespace tinydraw::vector_v2::buffer_initialization_internal

#endif  // TINYDRAW_VECTOR_V2_BUFFER_INITIALIZATION_INTERNAL_H
