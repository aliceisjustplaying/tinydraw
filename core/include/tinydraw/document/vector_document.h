#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"

namespace tinydraw {

struct StrokeSample {
  float x = 0.0F;
  float y = 0.0F;
  float radius = 0.0F;
};

enum class VectorTool : std::uint8_t {
  kPen,
  kEraser,
};

struct VectorStroke {
  std::uint32_t first_sample = 0;
  std::uint32_t sample_count = 0;
  RectF bounds{};
  std::uint16_t color = 0;
  VectorTool tool = VectorTool::kPen;
};

// A bounded, allocation-free stroke document. Callers choose the storage arena,
// allowing firmware to place it in PSRAM and tests to use ordinary vectors.
class VectorDocument {
 public:
  VectorDocument(std::span<VectorStroke> strokes, std::span<StrokeSample> samples);

  [[nodiscard]] bool begin_stroke(std::uint16_t color, VectorTool tool, StrokeSample first);
  [[nodiscard]] bool append(StrokeSample sample);
  [[nodiscard]] bool finish_stroke();
  void cancel_stroke();
  void clear();

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] std::size_t stroke_count() const { return stroke_count_; }
  [[nodiscard]] std::size_t sample_count() const { return sample_count_; }
  [[nodiscard]] std::size_t stroke_capacity() const { return strokes_.size(); }
  [[nodiscard]] std::size_t sample_capacity() const { return samples_.size(); }
  [[nodiscard]] std::span<const VectorStroke> strokes() const;
  [[nodiscard]] std::span<const StrokeSample> samples(const VectorStroke& stroke) const;

 private:
  [[nodiscard]] static bool valid(StrokeSample sample);
  static void include(RectF& bounds, StrokeSample sample);

  std::span<VectorStroke> strokes_;
  std::span<StrokeSample> samples_;
  std::size_t stroke_count_ = 0;
  std::size_t sample_count_ = 0;
  std::size_t active_first_sample_ = 0;
  bool active_ = false;
};

}  // namespace tinydraw
