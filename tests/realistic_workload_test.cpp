#include "tinydraw/document/realistic_workload.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "tinydraw/document/vector_document.h"

namespace {

constexpr std::size_t kStrokeCount = 1'000U;
constexpr std::size_t kStrokeCapacity = 1'100U;
constexpr std::size_t kSampleCapacity = 24'576U;
constexpr tinydraw::RectF kArea{.x0 = 368.0F, .y0 = 448.0F, .x1 = 736.0F, .y1 = 896.0F};

}  // namespace

TEST_CASE("realistic handwriting generation is deterministic per seed") {
  std::vector<tinydraw::VectorStroke> stroke_storage(kStrokeCapacity);
  std::vector<tinydraw::StrokeSample> sample_storage(kSampleCapacity);
  std::vector<tinydraw::VectorStroke> other_stroke_storage(kStrokeCapacity);
  std::vector<tinydraw::StrokeSample> other_sample_storage(kSampleCapacity);
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  tinydraw::VectorDocument other(other_stroke_storage, other_sample_storage);

  REQUIRE(tinydraw::populate_realistic_handwriting(document, 7U, kStrokeCount, kArea));
  REQUIRE(tinydraw::populate_realistic_handwriting(other, 7U, kStrokeCount, kArea));
  REQUIRE(document.stroke_count() == other.stroke_count());
  REQUIRE(document.sample_count() == other.sample_count());
  for (std::size_t index = 0; index < document.stroke_count(); ++index) {
    const auto& stroke = document.strokes()[index];
    const auto& other_stroke = other.strokes()[index];
    REQUIRE(stroke.sample_count == other_stroke.sample_count);
    const auto samples = document.samples(stroke);
    const auto other_samples = other.samples(other_stroke);
    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
      REQUIRE(samples[sample].x == other_samples[sample].x);
      REQUIRE(samples[sample].y == other_samples[sample].y);
      REQUIRE(samples[sample].radius == other_samples[sample].radius);
    }
  }

  REQUIRE(tinydraw::populate_realistic_handwriting(other, 8U, kStrokeCount, kArea));
  CHECK(document.sample_count() != other.sample_count());
}

TEST_CASE("realistic handwriting matches touch-cadence sample distribution") {
  std::vector<tinydraw::VectorStroke> stroke_storage(kStrokeCapacity);
  std::vector<tinydraw::StrokeSample> sample_storage(kSampleCapacity);
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  tinydraw::RealisticWorkloadStats stats;

  REQUIRE(tinydraw::populate_realistic_handwriting(document, 7U, kStrokeCount, kArea, &stats));
  CHECK(stats.strokes == kStrokeCount);
  CHECK(stats.samples == document.sample_count());

  // Mean per-stroke samples should reflect the bucket mixture, roughly
  // 0.6*10 + 0.3*25 + 0.09*58 + 0.01*160 = 20.3.
  const double mean = static_cast<double>(stats.samples) / static_cast<double>(stats.strokes);
  CHECK(mean > 16.0);
  CHECK(mean < 25.0);
  CHECK(stats.maximum_stroke_samples >= 36U);
  CHECK(stats.maximum_stroke_samples <= 200U);

  std::size_t minimum = kSampleCapacity;
  for (const auto& stroke : document.strokes()) {
    minimum = std::min(minimum, static_cast<std::size_t>(stroke.sample_count));
  }
  CHECK(minimum >= 6U);
}

TEST_CASE("realistic handwriting samples stay inside the requested area") {
  std::vector<tinydraw::VectorStroke> stroke_storage(kStrokeCapacity);
  std::vector<tinydraw::StrokeSample> sample_storage(kSampleCapacity);
  tinydraw::VectorDocument document(stroke_storage, sample_storage);

  REQUIRE(tinydraw::populate_realistic_handwriting(document, 3U, kStrokeCount, kArea));
  for (const auto& stroke : document.strokes()) {
    for (const auto& sample : document.samples(stroke)) {
      CHECK(sample.x >= kArea.x0);
      CHECK(sample.x <= kArea.x1);
      CHECK(sample.y >= kArea.y0);
      CHECK(sample.y <= kArea.y1);
      CHECK(sample.radius > 0.0F);
    }
  }
}

TEST_CASE("realistic handwriting rejects insufficient capacity and area") {
  std::array<tinydraw::VectorStroke, 8> small_strokes;
  std::array<tinydraw::StrokeSample, 32> small_samples;
  tinydraw::VectorDocument small(small_strokes, small_samples);
  CHECK_FALSE(tinydraw::populate_realistic_handwriting(small, 7U, kStrokeCount, kArea));

  std::vector<tinydraw::VectorStroke> stroke_storage(kStrokeCapacity);
  std::vector<tinydraw::StrokeSample> sample_storage(kSampleCapacity);
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  CHECK_FALSE(tinydraw::populate_realistic_handwriting(
      document, 7U, kStrokeCount, {.x0 = 0.0F, .y0 = 0.0F, .x1 = 40.0F, .y1 = 30.0F}));
  CHECK_FALSE(tinydraw::populate_realistic_handwriting(document, 7U, 0U, kArea));
}
