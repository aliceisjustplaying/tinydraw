#include "tinydraw/document/realistic_workload.h"

#include <algorithm>
#include <cmath>

namespace tinydraw {
namespace {

// Deterministic 32-bit xorshift so every platform generates identical
// documents from the same seed.
class Xorshift32 {
 public:
  explicit Xorshift32(std::uint32_t seed) : state_(seed != 0U ? seed : 0x9E3779B9U) {}

  std::uint32_t next() {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 17U;
    state_ ^= state_ << 5U;
    return state_;
  }

  // Uniform integer in [low, high].
  std::uint32_t range(std::uint32_t low, std::uint32_t high) {
    return low + next() % (high - low + 1U);
  }

  // Uniform float in [low, high).
  float range_f(float low, float high) {
    return low + static_cast<float>(next() >> 8U) * (high - low) / 16'777'216.0F;
  }

 private:
  std::uint32_t state_;
};

constexpr float kLineHeight = 34.0F;
constexpr float kLetterAdvance = 19.0F;
constexpr float kMargin = 10.0F;
constexpr float kMaximumRadius = 3.5F;

// Sample-count buckets modeling ~15 ms touch reports over human stroke
// durations: 60% quick letter strokes, 30% connectors, 9% flourishes, 1% long
// strokes such as underlines or cross-outs.
std::size_t stroke_sample_count(Xorshift32& random) {
  const std::uint32_t bucket = random.range(0U, 99U);
  if (bucket < 60U) {
    return random.range(6U, 14U);
  }
  if (bucket < 90U) {
    return random.range(15U, 35U);
  }
  if (bucket < 99U) {
    return random.range(36U, 80U);
  }
  return random.range(120U, 200U);
}

}  // namespace

bool populate_realistic_handwriting(VectorDocument& document, std::uint32_t seed,
                                    std::size_t stroke_count, RectF area,
                                    RealisticWorkloadStats* stats) {
  const float writable_width = area.x1 - area.x0 - 2.0F * kMargin;
  const float writable_height = area.y1 - area.y0 - 2.0F * kMargin;
  if (stroke_count == 0U || writable_width < 4.0F * kLetterAdvance ||
      writable_height < 2.0F * kLineHeight) {
    return false;
  }

  document.clear();
  Xorshift32 random(seed);
  RealisticWorkloadStats local_stats;
  float cursor_x = area.x0 + kMargin;
  float cursor_y = area.y0 + kMargin + kLineHeight * 0.5F;
  float page_offset = 0.0F;

  const auto clamp_sample = [&](StrokeSample sample) {
    sample.x = std::clamp(sample.x, area.x0 + kMaximumRadius, area.x1 - kMaximumRadius);
    sample.y = std::clamp(sample.y, area.y0 + kMaximumRadius, area.y1 - kMaximumRadius);
    return sample;
  };

  for (std::size_t stroke = 0; stroke < stroke_count; ++stroke) {
    const std::size_t samples = stroke_sample_count(random);
    const bool long_stroke = samples >= 120U;

    // Letter strokes stay inside one letter cell; long strokes span the line.
    const float span_x = long_stroke ? writable_width * random.range_f(0.6F, 0.95F)
                                     : kLetterAdvance * random.range_f(0.5F, 1.1F);
    const float start_x = long_stroke ? area.x0 + kMargin : cursor_x + random.range_f(-2.0F, 2.0F);
    const float start_y = cursor_y + random.range_f(-4.0F, 4.0F);
    const float advance_x = span_x / static_cast<float>(samples);
    const float advance_y = random.range_f(-6.0F, 6.0F) / static_cast<float>(samples);
    const float amplitude_x = random.range_f(1.0F, 5.0F);
    const float amplitude_y = random.range_f(4.0F, long_stroke ? 10.0F : 14.0F);
    const float frequency_x = random.range_f(0.25F, 0.9F);
    const float frequency_y = random.range_f(0.3F, 1.0F);
    const float phase_x = random.range_f(0.0F, 6.28F);
    const float phase_y = random.range_f(0.0F, 6.28F);
    const float pressure_phase = random.range_f(0.0F, 6.28F);
    const std::uint16_t color = static_cast<std::uint16_t>(0x001FU + stroke % 12U);

    const auto sample_at = [&](std::size_t index) {
      const float t = static_cast<float>(index);
      return clamp_sample({
          .x = start_x + t * advance_x + amplitude_x * std::sin(frequency_x * t + phase_x),
          .y = start_y + t * advance_y + amplitude_y * std::sin(frequency_y * t + phase_y),
          .radius = 2.0F + 1.5F * std::sin(0.31F * t + pressure_phase),
      });
    };

    if (!document.begin_stroke(color, VectorTool::kPen, sample_at(0U))) {
      return false;
    }
    for (std::size_t index = 1U; index < samples; ++index) {
      if (!document.append(sample_at(index))) {
        document.cancel_stroke();
        return false;
      }
    }
    if (!document.finish_stroke()) {
      document.cancel_stroke();
      return false;
    }
    local_stats.samples += samples;
    local_stats.maximum_stroke_samples = std::max(local_stats.maximum_stroke_samples, samples);

    // Advance the writing cursor; wrap lines, then wrap pages with an offset
    // so overlapping pages do not stack samples exactly.
    cursor_x += kLetterAdvance * random.range_f(0.8F, 1.2F);
    if (long_stroke || cursor_x > area.x1 - kMargin - kLetterAdvance) {
      cursor_x = area.x0 + kMargin;
      cursor_y += kLineHeight;
      if (cursor_y > area.y1 - kMargin - kLineHeight * 0.5F) {
        page_offset = std::fmod(page_offset + 7.0F, kLineHeight);
        cursor_y = area.y0 + kMargin + kLineHeight * 0.5F + page_offset;
      }
    }
  }

  local_stats.strokes = stroke_count;
  if (stats != nullptr) {
    *stats = local_stats;
  }
  return document.stroke_count() == stroke_count;
}

}  // namespace tinydraw
