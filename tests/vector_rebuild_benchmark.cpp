#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "tinydraw/document/vector_benchmark.h"
#include "tinydraw/graphics/viewport_renderer.h"

namespace {

struct BenchmarkCase {
  std::size_t strokes;
  tinydraw::VectorBenchmarkPattern pattern;
};

constexpr std::array kCases{
    BenchmarkCase{100U, tinydraw::VectorBenchmarkPattern::kShortSparse},
    BenchmarkCase{100U, tinydraw::VectorBenchmarkPattern::kHandwriting},
    BenchmarkCase{100U, tinydraw::VectorBenchmarkPattern::kLongDense},
    BenchmarkCase{100U, tinydraw::VectorBenchmarkPattern::kManyOffscreen},
    BenchmarkCase{100U, tinydraw::VectorBenchmarkPattern::kManyVisible},
    BenchmarkCase{1'000U, tinydraw::VectorBenchmarkPattern::kShortSparse},
    BenchmarkCase{1'000U, tinydraw::VectorBenchmarkPattern::kHandwriting},
    BenchmarkCase{1'000U, tinydraw::VectorBenchmarkPattern::kManyOffscreen},
    BenchmarkCase{1'000U, tinydraw::VectorBenchmarkPattern::kManyVisible},
    BenchmarkCase{5'000U, tinydraw::VectorBenchmarkPattern::kShortSparse},
    BenchmarkCase{5'000U, tinydraw::VectorBenchmarkPattern::kManyOffscreen},
    BenchmarkCase{5'000U, tinydraw::VectorBenchmarkPattern::kManyVisible},
};
constexpr std::array kZooms{0.25F, 0.5F, 1.0F, 2.0F};

}  // namespace

int main() {
  std::vector<std::uint8_t> scratch(tinydraw::ViewportRenderer::kScratchBytes);
  std::vector<std::uint16_t> destination(tinydraw::ViewportRenderer::kPixelCount);
  tinydraw::ViewportRenderer renderer(scratch);
  bool passed = true;

  for (const auto benchmark : kCases) {
    const std::size_t sample_capacity =
        benchmark.strokes * tinydraw::vector_benchmark_samples_per_stroke(benchmark.pattern);
    std::vector<tinydraw::VectorStroke> strokes(benchmark.strokes);
    std::vector<tinydraw::StrokeSample> samples(sample_capacity);
    tinydraw::VectorDocument document(strokes, samples);
    tinydraw::VectorBenchmarkDocumentStats document_stats;
    if (!tinydraw::populate_vector_benchmark(document, benchmark.strokes, benchmark.pattern,
                                             document_stats)) {
      passed = false;
      continue;
    }

    for (const float zoom : kZooms) {
      const auto started = std::chrono::steady_clock::now();
      const auto stats = renderer.render(document, {.zoom = zoom}, destination);
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      const std::size_t document_bytes = strokes.size() * sizeof(tinydraw::VectorStroke) +
                                         samples.size() * sizeof(tinydraw::StrokeSample);
      std::printf(
          "TINYDRAW_VECTOR_BENCH platform=host pattern=%s zoom=%u strokes=%lu "
          "intersecting=%lu samples=%lu processed=%lu primitives=%lu visits=%lu tiles=%lu "
          "document_bytes=%lu elapsed_us=%lld\n",
          tinydraw::vector_benchmark_pattern_name(benchmark.pattern),
          static_cast<unsigned>(zoom * 100.0F), static_cast<unsigned long>(stats.strokes_tested),
          static_cast<unsigned long>(stats.strokes_intersecting),
          static_cast<unsigned long>(document_stats.samples),
          static_cast<unsigned long>(stats.samples_processed),
          static_cast<unsigned long>(stats.primitives_rasterized),
          static_cast<unsigned long>(stats.primitive_tile_visits),
          static_cast<unsigned long>(stats.tiles_composited),
          static_cast<unsigned long>(document_bytes), static_cast<long long>(elapsed));
    }
  }
  return passed ? 0 : 1;
}
