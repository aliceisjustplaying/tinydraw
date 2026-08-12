#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"

namespace tinydraw {

// Allocation-free, error-bounded sample simplification used by the settled
// renderer. The first and last samples, direction reversals, and radius extrema
// are retained. Every removed center stays within `maximum_center_error` of the
// replacement chord, and its radius stays within `maximum_radius_error` of the
// chord-interpolated radius. The result is visual LOD, not canonical geometry.
//
// Returns an empty span when output cannot hold the required endpoints.
[[nodiscard]] std::span<StrokeSample> simplify_stroke_samples(std::span<const StrokeSample> input,
                                                              std::span<StrokeSample> output,
                                                              float maximum_center_error,
                                                              float maximum_radius_error);

}  // namespace tinydraw
