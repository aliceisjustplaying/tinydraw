#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/document/vector_document.h"
#include "tinydraw/graphics/stroke_lod.h"
#include "tinydraw/production/operation_lod_store.h"

namespace {

constexpr std::size_t kStrokeCount = 1'000U;
constexpr std::size_t kStrokeCapacity = 1'100U;
constexpr std::size_t kSampleCapacity = 24'576U;
constexpr tinydraw::RectF kArea{.x0 = 0.0F, .y0 = 0.0F, .x1 = 1472.0F, .y1 = 1792.0F};

struct Policy {
  const char* name;
  float maximum_screen_center_error;
  float maximum_screen_radius_error;
};

constexpr std::array kPolicies{
    Policy{"strict", 0.25F, 0.125F},
    Policy{"balanced", 0.5F, 0.25F},
    Policy{"loose", 0.75F, 0.375F},
};

struct Result {
  std::size_t samples = 0;
  std::size_t maximum_stroke_samples = 0;
  std::size_t failures = 0;
};

Result characterize(const tinydraw::VectorDocument& document, float scale, const Policy& policy,
                    std::span<tinydraw::StrokeSample> scratch) {
  Result result;
  for (const tinydraw::VectorStroke& stroke : document.strokes()) {
    const auto input = document.samples(stroke);
    const auto simplified = tinydraw::simplify_stroke_samples(
        input, scratch, policy.maximum_screen_center_error / scale,
        policy.maximum_screen_radius_error / scale);
    if (simplified.empty()) {
      ++result.failures;
      continue;
    }
    result.samples += simplified.size();
    result.maximum_stroke_samples = std::max(result.maximum_stroke_samples, simplified.size());
  }
  return result;
}

}  // namespace

int main() {
  std::vector<tinydraw::VectorStroke> strokes(kStrokeCapacity);
  std::vector<tinydraw::StrokeSample> samples(kSampleCapacity);
  tinydraw::VectorDocument document(strokes, samples);
  tinydraw::RealisticWorkloadStats source_stats;
  if (!tinydraw::populate_realistic_handwriting(document, 7U, kStrokeCount, kArea, &source_stats)) {
    std::fprintf(stderr, "production LOD characterization setup failed\n");
    return 1;
  }
  std::vector<tinydraw::StrokeSample> scratch(source_stats.maximum_stroke_samples);
  std::printf("TINYDRAW_LOD_SOURCE strokes=%zu samples=%zu maximum_stroke=%zu\n",
              source_stats.strokes, source_stats.samples, source_stats.maximum_stroke_samples);

  for (const Policy& policy : kPolicies) {
    std::size_t all_zoom_samples = 0;
    for (const tinydraw::production::ZoomLevel zoom : tinydraw::production::kLodZooms) {
      const int percent = tinydraw::production::zoom_percent(zoom);
      const Result result =
          characterize(document, static_cast<float>(percent) / 100.0F, policy, scratch);
      all_zoom_samples += result.samples;
      const double retained =
          100.0 * static_cast<double>(result.samples) / static_cast<double>(source_stats.samples);
      std::printf(
          "TINYDRAW_LOD_POLICY name=%s zoom=%d center_screen=%.3f radius_screen=%.3f "
          "samples=%zu retained_percent=%.3f maximum_stroke=%zu failures=%zu\n",
          policy.name, percent, static_cast<double>(policy.maximum_screen_center_error),
          static_cast<double>(policy.maximum_screen_radius_error), result.samples, retained,
          result.maximum_stroke_samples, result.failures);
    }
    std::printf("TINYDRAW_LOD_TOTAL name=%s samples=%zu capacity_percent=%.3f\n", policy.name,
                all_zoom_samples, 100.0 * static_cast<double>(all_zoom_samples) / 90'000.0);
  }
  return 0;
}
