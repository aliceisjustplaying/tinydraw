#ifndef TINYDRAW_VECTOR_V2_TILE_UNIFORM_H
#define TINYDRAW_VECTOR_V2_TILE_UNIFORM_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {

// Returns the shared color only when every pixel in a valid strided tile
// window matches. Nonuniform and malformed payloads both return nullopt.
[[nodiscard]] std::optional<std::uint16_t> tile_uniform_color(std::span<const std::uint16_t> pixels,
                                                              int width, int height,
                                                              std::size_t source_stride);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_TILE_UNIFORM_H
