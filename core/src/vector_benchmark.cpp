#include "tinydraw/document/vector_benchmark.h"

#include <cmath>

#include "tinydraw/geometry.h"

namespace tinydraw {
namespace {

std::size_t unique_samples(VectorBenchmarkPattern pattern) {
  switch (pattern) {
    case VectorBenchmarkPattern::kShortSparse:
    case VectorBenchmarkPattern::kManyOffscreen:
    case VectorBenchmarkPattern::kManyVisible:
      return 2U;
    case VectorBenchmarkPattern::kHandwriting:
      return 11U;
    case VectorBenchmarkPattern::kLongDense:
      return 63U;
  }
  return 0U;
}

StrokeSample benchmark_sample(std::size_t stroke, std::size_t sample,
                              VectorBenchmarkPattern pattern) {
  const float stroke_number = static_cast<float>(stroke);
  const float sample_number = static_cast<float>(sample);
  float x = static_cast<float>((stroke * 37U) % static_cast<std::size_t>(kCanvasWidth));
  float y = static_cast<float>((stroke * 53U) % static_cast<std::size_t>(kCanvasHeight));
  float radius = 2.0F + static_cast<float>(stroke % 4U);

  switch (pattern) {
    case VectorBenchmarkPattern::kShortSparse:
      x = static_cast<float>((stroke * 101U) % 1'600U) - 600.0F + sample_number * 7.0F;
      y = static_cast<float>((stroke * 67U) % 1'800U) - 700.0F + sample_number * 5.0F;
      break;
    case VectorBenchmarkPattern::kHandwriting:
      x += sample_number * 3.0F;
      y += std::sin(sample_number * 0.8F + stroke_number * 0.13F) * 12.0F;
      radius = 2.0F + 1.5F * std::sin(sample_number * 0.31F + 1.0F);
      break;
    case VectorBenchmarkPattern::kLongDense:
      x = 10.0F + sample_number * 5.5F;
      y = 224.0F + std::sin(sample_number * 0.22F + stroke_number * 0.17F) * 170.0F;
      radius = 4.0F + static_cast<float>(stroke % 3U);
      break;
    case VectorBenchmarkPattern::kManyOffscreen:
      x += 10'000.0F + sample_number * 6.0F;
      y += 10'000.0F + sample_number * 4.0F;
      break;
    case VectorBenchmarkPattern::kManyVisible:
      x = 4.0F + std::fmod(x, static_cast<float>(kCanvasWidth - 16)) + sample_number * 6.0F;
      y = 4.0F + std::fmod(y, static_cast<float>(kCanvasHeight - 16)) + sample_number * 4.0F;
      break;
  }
  return {.x = x, .y = y, .radius = radius};
}

}  // namespace

std::size_t vector_benchmark_samples_per_stroke(VectorBenchmarkPattern pattern) {
  return unique_samples(pattern) + 1U;
}

const char* vector_benchmark_pattern_name(VectorBenchmarkPattern pattern) {
  switch (pattern) {
    case VectorBenchmarkPattern::kShortSparse:
      return "sparse";
    case VectorBenchmarkPattern::kHandwriting:
      return "handwriting";
    case VectorBenchmarkPattern::kLongDense:
      return "dense";
    case VectorBenchmarkPattern::kManyOffscreen:
      return "offscreen";
    case VectorBenchmarkPattern::kManyVisible:
      return "visible";
  }
  return "unknown";
}

bool populate_vector_benchmark(VectorDocument& document, std::size_t stroke_count,
                               VectorBenchmarkPattern pattern,
                               VectorBenchmarkDocumentStats& stats) {
  document.clear();
  stats = {};
  const std::size_t unique = unique_samples(pattern);
  if (unique == 0U || stroke_count > document.stroke_capacity() ||
      stroke_count * (unique + 1U) > document.sample_capacity()) {
    return false;
  }

  for (std::size_t stroke = 0; stroke < stroke_count; ++stroke) {
    const StrokeSample first = benchmark_sample(stroke, 0U, pattern);
    if (!document.begin_stroke(static_cast<std::uint16_t>(0x001FU + stroke % 12U), VectorTool::kPen,
                               first)) {
      return false;
    }
    StrokeSample last = first;
    for (std::size_t sample = 1U; sample < unique; ++sample) {
      last = benchmark_sample(stroke, sample, pattern);
      if (!document.append(last)) {
        document.cancel_stroke();
        return false;
      }
    }
    if (!document.append(last) || !document.finish_stroke()) {
      document.cancel_stroke();
      return false;
    }
  }

  stats = {.strokes = document.stroke_count(), .samples = document.sample_count()};
  return true;
}

}  // namespace tinydraw
