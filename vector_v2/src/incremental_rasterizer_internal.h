#ifndef TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_INTERNAL_H
#define TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_INTERNAL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace tinydraw::vector_v2::raster_internal {

inline constexpr std::uint16_t kBackground = 0xFFFFU;

struct Sample {
  float x = 0;
  float y = 0;
  float radius = 0;
};

struct Segment {
  Sample first{};
  Sample second{};
  float delta_x = 0;
  float delta_y = 0;
  float inverse_length_squared = 0;
};

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
  std::array<Segment, 3> segments{};
  std::size_t count = 0U;
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
[[nodiscard]] int first_covered_at_or_after(const Segment& segment, int first, int last,
                                            float pixel_y);
[[nodiscard]] int last_covered_at_or_before(const Segment& segment, int first, int last,
                                            float pixel_y);
[[nodiscard]] RowSeed make_row_seed(const Segment& segment);
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
[[nodiscard]] std::optional<CurveUnit> curved_unit(std::span<const CompactOperationSample> samples,
                                                   std::size_t endpoint, ZoomLevel zoom);
void paint_masked_curve_unit_warm(const CurveUnit& unit, std::uint16_t color,
                                  const RasterSurface& surface, std::span<std::uint8_t> finalized,
                                  MaskedRowSummary* summary);

}  // namespace tinydraw::vector_v2::raster_internal

#endif  // TINYDRAW_VECTOR_V2_INCREMENTAL_RASTERIZER_INTERNAL_H
