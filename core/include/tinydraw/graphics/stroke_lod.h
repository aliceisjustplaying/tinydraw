#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"

namespace tinydraw {

// Allocation-free sample simplification used by the settled renderer. Samples
// are emitted in document order; the first and last input sample are retained.
// Interior samples are dropped when their center is within `minimum_distance`
// of the last retained center and their radius differs by no more than
// `maximum_radius_delta`. The result is a visual LOD, not canonical geometry.
//
// Returns an empty span when output cannot hold the required endpoints.
[[nodiscard]] std::span<StrokeSample> simplify_stroke_samples(std::span<const StrokeSample> input,
                                                              std::span<StrokeSample> output,
                                                              float minimum_distance,
                                                              float maximum_radius_delta);

}  // namespace tinydraw
