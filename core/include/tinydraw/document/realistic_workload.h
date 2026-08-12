#pragma once

#include <cstddef>
#include <cstdint>

#include "tinydraw/document/vector_document.h"
#include "tinydraw/geometry.h"

namespace tinydraw {

struct RealisticWorkloadStats {
  std::size_t strokes = 0;
  std::size_t samples = 0;
  std::size_t maximum_stroke_samples = 0;
};

// Populates `document` with a deterministic handwriting-like workload.
//
// Unlike the fixed 12-samples-per-stroke periodic workload, per-stroke sample
// counts here model the touch controller's measured ~15 ms changed-coordinate
// cadence over human stroke durations: mostly short letter strokes of
// 6-14 samples, connectors of 15-35, occasional 36-80 sample flourishes, and
// rare 120-200 sample strokes. Strokes advance along writing lines and wrap,
// so later pages overlap earlier ones the way a full page of ink does.
//
// The same seed always produces identical samples. Every sample stays inside
// `area`. Returns false when the document cannot hold the workload.
[[nodiscard]] bool populate_realistic_handwriting(VectorDocument& document, std::uint32_t seed,
                                                  std::size_t stroke_count, RectF area,
                                                  RealisticWorkloadStats* stats = nullptr);

}  // namespace tinydraw
