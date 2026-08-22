#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_INTERNAL_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_INTERNAL_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace tinydraw::vector_v2::raster_internal {

inline constexpr std::uint16_t kBackground = 0xFFFFU;

// Xtensa lowers these to TRUNC.S plus one native comparison.  Keeping them
// inline avoids the flash-resident floorf/ceilf calls in both rasterizers.
[[nodiscard]] inline int fast_floor(float value) {
  const int truncated = static_cast<int>(value);
  return truncated - static_cast<int>(static_cast<float>(truncated) > value);
}

[[nodiscard]] inline int fast_ceil(float value) {
  const int truncated = static_cast<int>(value);
  return truncated + static_cast<int>(static_cast<float>(truncated) < value);
}

// GCC materializes 0.5F through a literal load and a GPR-to-FPR transfer in
// pixel loops. LX7 can create it directly in one FP instruction.
[[nodiscard, gnu::always_inline]] inline float pixel_center(int coordinate) {
#if defined(__XTENSA__)
  float centered;
  float half;
  asm("float.s %0, %2, 0\n"
      "const.s %1, 3\n"
      "add.s %0, %0, %1"
      : "=&f"(centered), "=&f"(half)
      : "r"(coordinate));
  return centered;
#else
  return static_cast<float>(coordinate) + 0.5F;
#endif
}

// These hot-path geometry records deliberately have no default member
// initialization. Their construction sites either value-initialize them
// explicitly or fill every field before exposing the record.
struct Sample {
  float x;
  float y;
  float radius;
};

struct Segment {
  Sample first;
  Sample second;
  float delta_x;
  float delta_y;
  float inverse_length_squared;
};

inline void copy_segment(const Segment& source, Segment& destination) {
  destination.first.x = source.first.x;
  destination.first.y = source.first.y;
  destination.first.radius = source.first.radius;
  destination.second.x = source.second.x;
  destination.second.y = source.second.y;
  destination.second.radius = source.second.radius;
  destination.delta_x = source.delta_x;
  destination.delta_y = source.delta_y;
  destination.inverse_length_squared = source.inverse_length_squared;
}

// Scalar copy of one segment's geometry specialized for a fixed pixel row.
// Keeping this record independent from the destination surface proves to the
// compiler that pixel stores cannot change the geometry mid-loop.  y_delta
// and radius_delta hoist only individually-rounded subtractions; the
// multiply-adds stay in the predicate so Xtensa keeps the exact MADD.S
// evaluation used by covers_pixel.
struct PixelCoverageRow {
  float first_x;
  float first_y;
  float first_radius;
  float delta_x;
  float delta_y;
  float inverse_length_squared;
  float pixel_y;
  float y_delta;
  float radius_delta;
};

[[nodiscard, gnu::always_inline]] inline PixelCoverageRow make_pixel_coverage_row(
    const Segment& segment, float pixel_y) {
  return {
      .first_x = segment.first.x,
      .first_y = segment.first.y,
      .first_radius = segment.first.radius,
      .delta_x = segment.delta_x,
      .delta_y = segment.delta_y,
      .inverse_length_squared = segment.inverse_length_squared,
      .pixel_y = pixel_y,
      .y_delta = pixel_y - segment.first.y,
      .radius_delta = segment.second.radius - segment.first.radius,
  };
}

[[nodiscard, gnu::always_inline]] inline bool covers_pixel(const PixelCoverageRow& row,
                                                           float pixel_x) {
  const float projection = ((pixel_x - row.first_x) * row.delta_x + row.y_delta * row.delta_y) *
                           row.inverse_length_squared;
  const float amount = std::clamp(projection, 0.0F, 1.0F);
  const float center_x = row.first_x + amount * row.delta_x;
  const float center_y = row.first_y + amount * row.delta_y;
  const float radius = row.first_radius + amount * row.radius_delta;
  const float distance_x = pixel_x - center_x;
  const float distance_y = row.pixel_y - center_y;
  return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

struct ScanSpan {
  int first = 0;
  int last = -1;

  [[nodiscard]] bool empty() const { return last < first; }
};

struct TaperedSpanTable {
  float origin_low = 0;
  float delta_low = 0;
  float inverse_low = 0;
  float origin_high = 0;
  float delta_high = 0;
  float inverse_high = 0;
  float left_origin = 0;
  float left_delta = 0;
  float right_origin = 0;
  float right_delta = 0;
};

struct UnsetWindow {
  std::size_t first = 0;
  std::size_t last = 0;
};

struct RowSeed {
  bool circular = false;
  float center_x = 0.0F;
  float center_y = 0.0F;
  float radius = 0.0F;
  TaperedSpanTable table{};
};

struct CurveUnit {
  std::array<Segment, 3> segments;
  std::size_t count;
};

// Rolling scaled samples for callers that visit adjacent curve endpoints.
// The zoom scale is resolved once and each subsequent endpoint converts only
// the one CompactOperationSample entering the three-sample window.
struct CurveSampleWindow {
  Sample prior;
  Sample control;
  Sample current;
  float scale;
};

struct MaskedRowTarget {
  int y;
  std::size_t row;
  int window_first;
  int window_last;
  std::uint16_t color;
  const RasterSurface& surface;
  std::span<std::uint8_t> finalized;
};

[[nodiscard]] Sample scaled_sample(CompactOperationSample sample, ZoomLevel zoom);
[[nodiscard]] bool valid_surface(const RasterSurface& surface);
[[nodiscard]] PixelRect segment_bounds(const Segment& segment, PixelRect clip);
[[nodiscard]] bool covers_pixel(const Segment& segment, float pixel_x, float pixel_y);
[[nodiscard]] std::optional<UnsetWindow> mask_unset_window(std::span<const std::uint8_t> finalized,
                                                           std::size_t first, std::size_t last);
[[nodiscard]] bool mask_range_all_set(std::span<const std::uint8_t> finalized, std::size_t first,
                                      std::size_t last);
[[nodiscard]] int paint_masked_exact_span(int first_covered, int last_covered, std::size_t row,
                                          std::uint16_t color, const RasterSurface& surface,
                                          std::span<std::uint8_t> finalized);
[[nodiscard]] int first_covered_at_or_after(const Segment& segment, int first, int last,
                                            float pixel_y);
[[nodiscard]] int last_covered_at_or_before(const Segment& segment, int first, int last,
                                            float pixel_y);
void make_row_seed(const Segment& segment, RowSeed& seed);
[[nodiscard]] ScanSpan conservative_row_span(const RowSeed& seed, PixelRect bounds, float pixel_y);
[[nodiscard]] int paint_masked_const_row(const Segment& segment, const RowSeed& seed,
                                         PixelRect bounds, const MaskedRowTarget& target);
[[nodiscard]] int paint_masked_tapered_row(const Segment& segment, int y, ScanSpan row_span,
                                           std::uint16_t color, const RasterSurface& surface,
                                           std::span<std::uint8_t> finalized);
void paint_segment(const Sample& start, const Sample& end, std::uint16_t color,
                   const RasterSurface& surface);
void paint_masked_segment(const Sample& start, const Sample& end, std::uint16_t color,
                          const RasterSurface& surface, std::span<std::uint8_t> finalized,
                          MaskedRowSummary* summary);
[[nodiscard]] bool curved_unit(std::span<const CompactOperationSample> samples,
                               std::size_t endpoint, ZoomLevel zoom, CurveUnit& unit);
void initialize_curve_sample_window(std::span<const CompactOperationSample> samples,
                                    std::size_t endpoint, ZoomLevel zoom, CurveSampleWindow& window,
                                    CurveUnit& unit);
void advance_curve_sample_window(CurveSampleWindow& window, CompactOperationSample next, bool last,
                                 CurveUnit& unit);
void retreat_curve_sample_window(CurveSampleWindow& window, CompactOperationSample prior,
                                 bool first, CurveUnit& unit);
void paint_masked_curve_unit_warm(const CurveUnit& unit, std::uint16_t color,
                                  const RasterSurface& surface, std::span<std::uint8_t> finalized,
                                  MaskedRowSummary* summary);

}  // namespace tinydraw::vector_v2::raster_internal

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_INTERNAL_H
