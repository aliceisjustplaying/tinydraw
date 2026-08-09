#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/ink/ink_stream.h"

namespace tinydraw {

enum class RibbonPrimitiveKind { kConvex, kCircle };

struct RibbonPrimitive {
  RibbonPrimitiveKind kind = RibbonPrimitiveKind::kConvex;
  std::array<Point, 4> points{};
  std::uint8_t point_count = 0;
  Point center{};
  float radius = 0.0F;
};

class RibbonPrimitiveBatch {
 public:
  [[nodiscard]] const RibbonPrimitive* begin() const { return primitives_.data(); }
  [[nodiscard]] const RibbonPrimitive* end() const { return begin() + count_; }
  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0U; }

 private:
  friend class RibbonStream;
  void push_back(RibbonPrimitive primitive);

  // One update can stabilize a split span and join, then finalize another split span and cap.
  std::array<RibbonPrimitive, 7> primitives_{};
  std::size_t count_ = 0;
};

struct RibbonUpdate {
  RibbonPrimitiveBatch committed;
  RibbonPrimitiveBatch provisional;
};

// Emits geometry that has become append-stable plus a replacement tail for display.
// InkPoint radii are immutable inputs, so initial pressure never revises older geometry.
class RibbonStream {
 public:
  [[nodiscard]] RibbonUpdate append(InkPoint point);
  [[nodiscard]] RibbonUpdate finish(InkPoint point);
  void reset();

  [[nodiscard]] bool active() const { return point_count_ != 0U; }

 private:
  InkPoint first_{};
  InkPoint prior_{};
  InkPoint last_{};
  Point tail_left_{};
  Point tail_right_{};
  std::size_t point_count_ = 0;
  bool first_cap_pending_ = false;
};

// Build simple unionable pieces rather than one self-intersecting outline.
// The stream has already supplied dt-adapted positions, pressure, and radii.
[[nodiscard]] std::vector<RibbonPrimitive> build_pf_ribbon(std::span<const InkPoint> points);

}  // namespace tinydraw
