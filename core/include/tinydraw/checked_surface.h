#pragma once

#include <cstddef>
#include <limits>
#include <optional>

namespace tinydraw {

// Returns the number of elements needed to address a width x height window
// whose row starts are stride elements apart. Rejects zero dimensions, short
// strides, and multiplication/addition overflow before callers index rows.
[[nodiscard]] constexpr std::optional<std::size_t> checked_surface_extent(std::size_t width,
                                                                          std::size_t height,
                                                                          std::size_t stride) {
  if (width == 0U || height == 0U || stride < width) {
    return std::nullopt;
  }
  if (height - 1U > (std::numeric_limits<std::size_t>::max() - width) / stride) {
    return std::nullopt;
  }
  return (height - 1U) * stride + width;
}

}  // namespace tinydraw
