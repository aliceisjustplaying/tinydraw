#include "tinydraw/document/vector_benchmark.h"

#include <doctest.h>

#include <vector>

TEST_CASE("synthetic vector benchmark documents have deterministic sizes") {
  constexpr std::size_t strokes = 100U;
  constexpr auto pattern = tinydraw::VectorBenchmarkPattern::kHandwriting;
  const std::size_t expected_samples =
      strokes * tinydraw::vector_benchmark_samples_per_stroke(pattern);
  std::vector<tinydraw::VectorStroke> stroke_storage(strokes);
  std::vector<tinydraw::StrokeSample> sample_storage(expected_samples);
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  tinydraw::VectorBenchmarkDocumentStats stats;

  REQUIRE(tinydraw::populate_vector_benchmark(document, strokes, pattern, stats));
  CHECK(stats.strokes == strokes);
  CHECK(stats.samples == expected_samples);
  CHECK(document.stroke_count() == strokes);
  CHECK(document.sample_count() == expected_samples);
  CHECK(document.strokes().front().bounds.x1 > document.strokes().front().bounds.x0);
}

TEST_CASE("synthetic vector benchmark respects bounded arenas") {
  std::vector<tinydraw::VectorStroke> stroke_storage(9U);
  std::vector<tinydraw::StrokeSample> sample_storage(30U);
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  tinydraw::VectorBenchmarkDocumentStats stats;

  CHECK_FALSE(tinydraw::populate_vector_benchmark(
      document, 10U, tinydraw::VectorBenchmarkPattern::kManyVisible, stats));
  CHECK(document.stroke_count() == 0U);
}
