#include "tinydraw/document/vector_document.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tinydraw {

VectorDocument::VectorDocument(std::span<VectorStroke> strokes, std::span<StrokeSample> samples)
    : strokes_(strokes), samples_(samples) {}

bool VectorDocument::begin_stroke(std::uint16_t color, VectorTool tool, StrokeSample first) {
  if (active_ || stroke_count_ >= strokes_.size() || !valid(first) ||
      sample_count_ >= samples_.size() ||
      sample_count_ > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }

  active_first_sample_ = sample_count_;
  samples_[sample_count_++] = first;
  strokes_[stroke_count_] = {
      .first_sample = static_cast<std::uint32_t>(active_first_sample_),
      .sample_count = 1,
      .bounds = {.x0 = first.x - first.radius,
                 .y0 = first.y - first.radius,
                 .x1 = first.x + first.radius,
                 .y1 = first.y + first.radius},
      .color = color,
      .tool = tool,
  };
  active_ = true;
  return true;
}

bool VectorDocument::append(StrokeSample sample) {
  if (!active_ || !valid(sample) || sample_count_ >= samples_.size() ||
      strokes_[stroke_count_].sample_count == std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  samples_[sample_count_++] = sample;
  ++strokes_[stroke_count_].sample_count;
  include(strokes_[stroke_count_].bounds, sample);
  return true;
}

bool VectorDocument::finish_stroke() {
  if (!active_) {
    return false;
  }
  ++stroke_count_;
  active_ = false;
  return true;
}

void VectorDocument::cancel_stroke() {
  if (!active_) {
    return;
  }
  sample_count_ = active_first_sample_;
  strokes_[stroke_count_] = {};
  active_ = false;
}

void VectorDocument::clear() {
  stroke_count_ = 0;
  sample_count_ = 0;
  active_first_sample_ = 0;
  active_ = false;
}

std::span<const VectorStroke> VectorDocument::strokes() const {
  return std::span<const VectorStroke>(strokes_.data(), stroke_count_);
}

std::span<const StrokeSample> VectorDocument::samples(const VectorStroke& stroke) const {
  const std::size_t first = stroke.first_sample;
  const std::size_t count = stroke.sample_count;
  if (first > sample_count_ || count > sample_count_ - first) {
    return {};
  }
  return std::span<const StrokeSample>(samples_.data() + first, count);
}

bool VectorDocument::valid(StrokeSample sample) {
  return std::isfinite(sample.x) && std::isfinite(sample.y) && std::isfinite(sample.radius) &&
         sample.radius > 0.0F;
}

void VectorDocument::include(RectF& bounds, StrokeSample sample) {
  bounds.x0 = std::min(bounds.x0, sample.x - sample.radius);
  bounds.y0 = std::min(bounds.y0, sample.y - sample.radius);
  bounds.x1 = std::max(bounds.x1, sample.x + sample.radius);
  bounds.y1 = std::max(bounds.y1, sample.y + sample.radius);
}

}  // namespace tinydraw
