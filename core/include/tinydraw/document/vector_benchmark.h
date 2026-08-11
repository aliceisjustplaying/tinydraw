#pragma once

#include <cstddef>
#include <cstdint>

#include "tinydraw/document/vector_document.h"

namespace tinydraw {

enum class VectorBenchmarkPattern : std::uint8_t {
  kShortSparse,
  kHandwriting,
  kLongDense,
  kManyOffscreen,
  kManyVisible,
};

struct VectorBenchmarkDocumentStats {
  std::size_t strokes = 0;
  std::size_t samples = 0;
};

[[nodiscard]] std::size_t vector_benchmark_samples_per_stroke(VectorBenchmarkPattern pattern);
[[nodiscard]] const char* vector_benchmark_pattern_name(VectorBenchmarkPattern pattern);
[[nodiscard]] bool populate_vector_benchmark(VectorDocument& document, std::size_t stroke_count,
                                             VectorBenchmarkPattern pattern,
                                             VectorBenchmarkDocumentStats& stats);

}  // namespace tinydraw
