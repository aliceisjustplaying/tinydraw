#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

// Worst case is CurvedRibbonStream::finish on a sharp turn with every span
// split: initial cap (1) + split two-span curve (4) + sharp-turn joint (1) +
// split provisional tail (2) + final cap (1) = 9, plus one slot of margin.
inline constexpr std::size_t kRibbonPrimitiveBatchCapacity = 10;

class RibbonPrimitiveBatch {
 public:
  [[nodiscard]] const RibbonPrimitive* begin() const { return primitives_.data(); }
  [[nodiscard]] const RibbonPrimitive* end() const { return begin() + count_; }
  [[nodiscard]] std::size_t size() const { return count_; }
  [[nodiscard]] bool empty() const { return count_ == 0U; }
  // True when a push was dropped because the batch was full. Release builds
  // fail closed (drop + flag) instead of writing out of bounds; callers that
  // see an overflowed batch should request a full refresh of the stroke.
  [[nodiscard]] bool overflowed() const { return overflowed_; }

 private:
  friend class RibbonStream;
  friend class CurvedRibbonStream;
  void push_back(RibbonPrimitive primitive);

  std::array<RibbonPrimitive, kRibbonPrimitiveBatchCapacity> primitives_{};
  std::size_t count_ = 0;
  bool overflowed_ = false;
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

// Reconstructs a smooth midpoint-quadratic path from sparse controller reports.
// Stable curves lag by one input point; a replaceable straight tail still reaches the finger.
class CurvedRibbonStream {
 public:
  // provisional_needed=false skips building the replaceable display tail for
  // offline replay. A visual_endpoint lets that replaceable tail reach an
  // unsmoothed touch position while all stream state and committed geometry
  // remain derived from the smoothed InkPoint.
  [[nodiscard]] RibbonUpdate append(InkPoint point, bool provisional_needed = true,
                                    std::optional<Point> visual_endpoint = std::nullopt);
  [[nodiscard]] RibbonUpdate finish(InkPoint point);
  void reset();

  [[nodiscard]] bool active() const { return point_count_ != 0U; }

 private:
  InkPoint first_{};
  InkPoint stable_{};
  InkPoint last_{};
  std::size_t point_count_ = 0;
  bool first_cap_pending_ = false;
};

// Build simple unionable pieces rather than one self-intersecting outline.
// The stream has already supplied dt-adapted positions, pressure, and radii.
[[nodiscard]] std::vector<RibbonPrimitive> build_pf_ribbon(std::span<const InkPoint> points);

}  // namespace tinydraw
