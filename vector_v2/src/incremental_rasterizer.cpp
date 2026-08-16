#include "tinydraw/vector_v2/incremental_rasterizer.h"

#include <algorithm>
#include <array>
#include <bit>
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS) && !defined(__XTENSA__)
#include <chrono>
#endif
#include <cmath>

#include "tinydraw/checked_surface.h"
#include "tinydraw/vector_v2/raster_census.h"
#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
RasterCensus g_raster_census{};
#if !defined(__XTENSA__)
std::uint32_t raster_census_now() {
  return static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif
#endif

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
// A center on a pixel-grid intersection is sqrt(0.5) pixels from the nearest
// pixel center. Keep thin projected operations above that distance so stroke
// presence survives every committed zoom.
constexpr float kMinimumScreenRadius = 0.75F;

struct Sample {
  float x = 0;
  float y = 0;
  float radius = 0;
};

// floor/ceil for in-range values without the floorf/ceilf libcalls the
// Xtensa toolchain emits: TRUNC.S plus one native compare. Bit-identical to
// std::floor/std::ceil for every |value| < 2^31.
int fast_floor(float value) {
  const int truncated = static_cast<int>(value);
  return truncated - static_cast<int>(static_cast<float>(truncated) > value);
}

int fast_ceil(float value) {
  const int truncated = static_cast<int>(value);
  return truncated + static_cast<int>(static_cast<float>(truncated) < value);
}

// Conservative upper bound on sqrt(value) from the classic reciprocal-sqrt
// bit seed with one Newton step (relative error under 0.2%), padded by a
// 0.5% margin. Native FPU only — the toolchain's sqrtf is a library call.
// Callers use it solely to widen probe seeds, never as geometry authority.
float conservative_sqrt_upper(float value) {
  if (value <= 0.0F) {
    return 0.0F;
  }
  const auto bits = std::bit_cast<std::uint32_t>(value);
  float estimate = std::bit_cast<float>(0x5F3759DFU - (bits >> 1U));
  estimate = estimate * (1.5F - 0.5F * value * estimate * estimate);
  return value * estimate * 1.005F;
}

// Committed zoom scales are exact binary fractions, so this lookup is
// bit-identical to the historical zoom_percent(zoom) / 100.0F division while
// avoiding the per-sample __divsf3 library call.
float zoom_scale(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return 0.25F;
    case ZoomLevel::k50Percent:
      return 0.5F;
    case ZoomLevel::k100Percent:
      return 1.0F;
    case ZoomLevel::k200Percent:
      return 2.0F;
    case ZoomLevel::k400Percent:
      return 4.0F;
  }
  return static_cast<float>(zoom_percent(zoom)) / 100.0F;
}

struct Segment {
  Sample first{};
  Sample second{};
  float delta_x = 0;
  float delta_y = 0;
  float inverse_length_squared = 0;
};

Segment make_segment(const Sample& first, const Sample& second) {
  const float delta_x = second.x - first.x;
  const float delta_y = second.y - first.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  return {
      .first = first,
      .second = second,
      .delta_x = delta_x,
      .delta_y = delta_y,
      .inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F,
  };
}

Sample scaled_sample(CompactOperationSample sample, ZoomLevel zoom) {
  const float scale = zoom_scale(zoom);
  return {
      .x = static_cast<float>(sample.x_quarter) * 0.25F * scale,
      .y = static_cast<float>(sample.y_quarter) * 0.25F * scale,
      .radius =
          std::max(static_cast<float>(sample.radius_256) / 256.0F * scale, kMinimumScreenRadius),
  };
}

bool valid_surface(const RasterSurface& surface) {
  const int width = surface.level_bounds.x1 - surface.level_bounds.x0;
  const int height = surface.level_bounds.y1 - surface.level_bounds.y0;
  const int level_width = kWorldWidth * zoom_percent(surface.zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(surface.zoom) / 100;
  if (width <= 0 || height <= 0 || surface.level_bounds.x0 < 0 || surface.level_bounds.y0 < 0 ||
      surface.level_bounds.x1 > level_width || surface.level_bounds.y1 > level_height ||
      surface.stride < width) {
    return false;
  }
  const auto required =
      checked_surface_extent(static_cast<std::size_t>(width), static_cast<std::size_t>(height),
                             static_cast<std::size_t>(surface.stride));
  return required.has_value() && surface.pixels.size() >= *required;
}

PixelRect segment_bounds(const Segment& segment, PixelRect clip) {
  const float minimum_x =
      std::min(segment.first.x - segment.first.radius, segment.second.x - segment.second.radius);
  const float minimum_y =
      std::min(segment.first.y - segment.first.radius, segment.second.y - segment.second.radius);
  const float maximum_x =
      std::max(segment.first.x + segment.first.radius, segment.second.x + segment.second.radius);
  const float maximum_y =
      std::max(segment.first.y + segment.first.radius, segment.second.y + segment.second.radius);
  return {
      .x0 = std::max(clip.x0, fast_floor(minimum_x)),
      .y0 = std::max(clip.y0, fast_floor(minimum_y)),
      .x1 = std::min(clip.x1, fast_ceil(maximum_x)),
      .y1 = std::min(clip.y1, fast_ceil(maximum_y)),
  };
}

bool covers_pixel(const Segment& segment, float pixel_x, float pixel_y) {
  if (segment.inverse_length_squared == 0.0F) {
    const float distance_x = pixel_x - segment.first.x;
    const float distance_y = pixel_y - segment.first.y;
    const float radius = std::max(segment.first.radius, segment.second.radius);
    return distance_x * distance_x + distance_y * distance_y <= radius * radius;
  }
  const float projection = ((pixel_x - segment.first.x) * segment.delta_x +
                            (pixel_y - segment.first.y) * segment.delta_y) *
                           segment.inverse_length_squared;
  const float amount = std::clamp(projection, 0.0F, 1.0F);
  const float center_x = segment.first.x + amount * segment.delta_x;
  const float center_y = segment.first.y + amount * segment.delta_y;
  const float radius =
      segment.first.radius + amount * (segment.second.radius - segment.first.radius);
  const float distance_x = pixel_x - center_x;
  const float distance_y = pixel_y - center_y;
  return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

struct ScanSpan {
  int first = 0;
  int last = -1;

  [[nodiscard]] bool empty() const { return last < first; }
};

int find_first_covered(const Segment& segment, PixelRect bounds, float pixel_y, ScanSpan prior) {
  int x = prior.empty() ? bounds.x0 : std::clamp(prior.first, bounds.x0, bounds.x1 - 1);
  while (x > bounds.x0 && covers_pixel(segment, static_cast<float>(x - 1) + 0.5F, pixel_y)) {
    --x;
  }
  while (x < bounds.x1 && !covers_pixel(segment, static_cast<float>(x) + 0.5F, pixel_y)) {
    ++x;
  }
  return x;
}

int find_last_covered(const Segment& segment, PixelRect bounds, float pixel_y, ScanSpan prior,
                      int first_covered) {
  int x = prior.empty() ? bounds.x1 - 1 : std::clamp(prior.last, first_covered, bounds.x1 - 1);
  while (x + 1 < bounds.x1 && covers_pixel(segment, static_cast<float>(x + 1) + 0.5F, pixel_y)) {
    ++x;
  }
  while (x > first_covered && !covers_pixel(segment, static_cast<float>(x) + 0.5F, pixel_y)) {
    --x;
  }
  return x;
}

void paint_constant_radius_segment(const Segment& segment, PixelRect bounds, std::uint16_t color,
                                   const RasterSurface& surface) {
  ScanSpan prior{.first = bounds.x0, .last = bounds.x0 - 1};
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const float pixel_y = static_cast<float>(y) + 0.5F;
    const int first_covered = find_first_covered(segment, bounds, pixel_y, prior);
    if (first_covered == bounds.x1) {
      prior.last = prior.first - 1;
      continue;
    }
    const int last_covered = find_last_covered(segment, bounds, pixel_y, prior, first_covered);
    prior = {.first = first_covered, .last = last_covered};

    const std::size_t row = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                            static_cast<std::size_t>(surface.stride);
    const std::size_t column = static_cast<std::size_t>(first_covered - surface.level_bounds.x0);
    const auto begin = surface.pixels.begin() + static_cast<std::ptrdiff_t>(row + column);
    const int span_width = last_covered - first_covered + 1;
    std::fill_n(begin, static_cast<std::size_t>(span_width), color);
  }
}

struct ParameterInterval {
  float first = 0.0F;
  float last = 1.0F;

  [[nodiscard]] bool empty() const { return last < first; }
};

// Per-segment precomputation for the conservative row span. The two edge
// crossings are linear in the row coordinate, so the divisions historically
// paid per row (each a __divsf3 library call on the Xtensa toolchain) hoist
// into two reciprocals paid once per painted segment; each row then costs
// native multiplies only. The reciprocal changes crossings by at most a few
// ulp, which the rounding margin and whole-pixel guard absorb; covers_pixel
// remains the sole geometry authority inside the conservative interval.
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

TaperedSpanTable make_tapered_span_table(const Segment& segment) {
  const float radius_delta = segment.second.radius - segment.first.radius;
  TaperedSpanTable table{
      .origin_low = segment.first.y - segment.first.radius,
      .delta_low = segment.delta_y - radius_delta,
      .inverse_low = 0.0F,
      .origin_high = segment.first.y + segment.first.radius,
      .delta_high = segment.delta_y + radius_delta,
      .inverse_high = 0.0F,
      .left_origin = segment.first.x - segment.first.radius,
      .left_delta = segment.delta_x - radius_delta,
      .right_origin = segment.first.x + segment.first.radius,
      .right_delta = segment.delta_x + radius_delta,
  };
  table.inverse_low = table.delta_low != 0.0F ? 1.0F / table.delta_low : 0.0F;
  table.inverse_high = table.delta_high != 0.0F ? 1.0F / table.delta_high : 0.0F;
  return table;
}

ScanSpan conservative_tapered_row_span(const TaperedSpanTable& table, PixelRect bounds,
                                       float pixel_y) {
  // A covered pixel's projected center must be no farther than its
  // interpolated radius from this row. Both y-radius and y+radius are linear
  // in the segment parameter, so intersecting those two half-planes cheaply
  // rejects most of the old bounding-box scan without approximating the
  // final predicate.
  constexpr float kRoundingMargin = 0.01F;
  const ScanSpan empty{.first = bounds.x0, .last = bounds.x0 - 1};
  ParameterInterval interval{};
  if (table.delta_low == 0.0F) {
    if (table.origin_low > pixel_y + kRoundingMargin) {
      return empty;
    }
  } else {
    const float crossing = (pixel_y + kRoundingMargin - table.origin_low) * table.inverse_low;
    if (table.delta_low > 0.0F) {
      interval.last = std::min(interval.last, crossing);
    } else {
      interval.first = std::max(interval.first, crossing);
    }
  }
  if (table.delta_high == 0.0F) {
    if (table.origin_high < pixel_y - kRoundingMargin) {
      return empty;
    }
  } else {
    const float crossing = (pixel_y - kRoundingMargin - table.origin_high) * table.inverse_high;
    if (table.delta_high > 0.0F) {
      interval.first = std::max(interval.first, crossing);
    } else {
      interval.last = std::min(interval.last, crossing);
    }
  }
  if (interval.empty()) {
    return empty;
  }
  interval.first = std::clamp(interval.first, 0.0F, 1.0F);
  interval.last = std::clamp(interval.last, 0.0F, 1.0F);
  if (interval.empty()) {
    return empty;
  }
  const float minimum_x = std::min(table.left_origin + interval.first * table.left_delta,
                                   table.left_origin + interval.last * table.left_delta);
  const float maximum_x = std::max(table.right_origin + interval.first * table.right_delta,
                                   table.right_origin + interval.last * table.right_delta);
  // Keep a whole-pixel guard around float edge arithmetic. covers_pixel remains
  // the sole authority inside this conservative interval.
  const int first = std::max(bounds.x0, fast_floor(minimum_x - kRoundingMargin) - 1);
  const int last = std::min(bounds.x1 - 1, fast_ceil(maximum_x + kRoundingMargin));
  return {.first = first, .last = last};
}

void paint_tapered_segment(const Segment& segment, PixelRect bounds, std::uint16_t color,
                           const RasterSurface& surface) {
  const TaperedSpanTable table = make_tapered_span_table(segment);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const float pixel_y = static_cast<float>(y) + 0.5F;
    const ScanSpan row_span = conservative_tapered_row_span(table, bounds, pixel_y);
    if (row_span.empty()) {
      continue;
    }
    for (int x = row_span.first; x <= row_span.last; ++x) {
      if (!covers_pixel(segment, static_cast<float>(x) + 0.5F, pixel_y)) {
        continue;
      }
      const std::size_t row = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                              static_cast<std::size_t>(surface.stride);
      const std::size_t column = static_cast<std::size_t>(x - surface.level_bounds.x0);
      surface.pixels[row + column] = color;
    }
  }
}

void finalize_pixel(std::span<std::uint8_t> finalized, std::size_t pixel) {
  const std::uint8_t bit = static_cast<std::uint8_t>(1U << (pixel & 7U));
  finalized[pixel >> 3U] = static_cast<std::uint8_t>(finalized[pixel >> 3U] | bit);
}

struct UnsetWindow {
  std::size_t first = 0;
  std::size_t last = 0;
};

// Inclusive pixel range of not-yet-finalized pixels within [first, last], or
// nullopt when every mask bit in the range is set. Pixels outside the window
// are finalized, so exact masked painting may clamp both the span search and
// the span walk to it without changing any written pixel.
std::optional<UnsetWindow> mask_unset_window(std::span<const std::uint8_t> finalized,
                                             std::size_t first, std::size_t last) {
  const std::size_t first_byte = first >> 3U;
  const std::size_t last_byte = last >> 3U;
  const std::uint8_t first_mask = static_cast<std::uint8_t>(0xFFU << (first & 7U));
  const std::uint8_t last_mask = static_cast<std::uint8_t>(0xFFU >> (7U - (last & 7U)));
  std::size_t byte = first_byte;
  std::uint8_t unset = 0;
  while (byte <= last_byte) {
    std::uint8_t candidate = static_cast<std::uint8_t>(~finalized[byte]);
    if (byte == first_byte) {
      candidate &= first_mask;
    }
    if (byte == last_byte) {
      candidate &= last_mask;
    }
    if (candidate != 0U) {
      unset = candidate;
      break;
    }
    ++byte;
  }
  if (byte > last_byte) {
    return std::nullopt;
  }
  UnsetWindow window{};
  window.first = (byte << 3U) + static_cast<std::size_t>(std::countr_zero(unset));
  std::size_t tail_byte = last_byte;
  while (true) {
    std::uint8_t candidate = static_cast<std::uint8_t>(~finalized[tail_byte]);
    if (tail_byte == first_byte) {
      candidate &= first_mask;
    }
    if (tail_byte == last_byte) {
      candidate &= last_mask;
    }
    if (candidate != 0U) {
      window.last =
          (tail_byte << 3U) + (7U - static_cast<std::size_t>(std::countl_zero(candidate)));
      break;
    }
    --tail_byte;
  }
  return window;
}

// True when every mask bit in the inclusive pixel range [first, last] is set.
bool mask_range_all_set(std::span<const std::uint8_t> finalized, std::size_t first,
                        std::size_t last) {
  const std::size_t first_byte = first >> 3U;
  const std::size_t last_byte = last >> 3U;
  const std::uint8_t first_mask = static_cast<std::uint8_t>(0xFFU << (first & 7U));
  const std::uint8_t last_mask = static_cast<std::uint8_t>(0xFFU >> (7U - (last & 7U)));
  if (first_byte == last_byte) {
    const std::uint8_t need = first_mask & last_mask;
    return (finalized[first_byte] & need) == need;
  }
  if ((finalized[first_byte] & first_mask) != first_mask ||
      (finalized[last_byte] & last_mask) != last_mask) {
    return false;
  }
  return std::all_of(finalized.begin() + static_cast<std::ptrdiff_t>(first_byte + 1U),
                     finalized.begin() + static_cast<std::ptrdiff_t>(last_byte),
                     [](std::uint8_t byte) { return byte == 0xFFU; });
}

// Per-bit fallback for a chunk that mixes finalized and free pixels. Every
// pixel here is covered by construction, so no predicate runs. Returns the
// count of newly finalized pixels.
int paint_covered_chunk_bits(std::size_t pixel, int count, std::uint16_t color,
                             const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  int newly_finalized = 0;
  for (int offset = 0; offset < count; ++offset) {
    const std::size_t candidate = pixel + static_cast<std::size_t>(offset);
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << (candidate & 7U));
    if ((finalized[candidate >> 3U] & bit) != 0U) {
      TINYDRAW_V2_CENSUS_ADD(const_mask_skips, 1);
      continue;
    }
    surface.pixels[candidate] = color;
    finalize_pixel(finalized, candidate);
    ++newly_finalized;
  }
  return newly_finalized;
}

// Writes one exactly-covered span in mask-byte chunks: fully-finalized
// chunks are skipped, fully-free chunks are filled whole, and mixed chunks
// fall back to per-bit writes. Every span pixel is covered by construction,
// so no predicate runs here. Returns the count of newly finalized pixels.
int paint_masked_exact_span(int first_covered, int last_covered, std::size_t row,
                            std::uint16_t color, const RasterSurface& surface,
                            std::span<std::uint8_t> finalized) {
  int newly_finalized = 0;
  int x = first_covered;
  std::size_t pixel = row + static_cast<std::size_t>(x - surface.level_bounds.x0);
  while (x <= last_covered) {
    const std::size_t byte = pixel >> 3U;
    const unsigned bit = static_cast<unsigned>(pixel & 7U);
    const int in_byte = std::min(static_cast<int>(8U - bit), last_covered - x + 1);
    const std::uint8_t chunk_mask = static_cast<std::uint8_t>(
        in_byte == 8 ? 0xFFU : ((1U << static_cast<unsigned>(in_byte)) - 1U) << bit);
    const std::uint8_t have = finalized[byte];
    if ((have & chunk_mask) == chunk_mask) {
      TINYDRAW_V2_CENSUS_ADD(const_mask_skips, static_cast<std::uint64_t>(in_byte));
    } else if ((have & chunk_mask) == 0U) {
      std::fill_n(surface.pixels.begin() + static_cast<std::ptrdiff_t>(pixel),
                  static_cast<std::size_t>(in_byte), color);
      finalized[byte] = static_cast<std::uint8_t>(have | chunk_mask);
      newly_finalized += in_byte;
    } else {
      newly_finalized += paint_covered_chunk_bits(pixel, in_byte, color, surface, finalized);
    }
    x += in_byte;
    pixel += static_cast<std::size_t>(in_byte);
  }
  return newly_finalized;
}

// First covered pixel in [first, last], or last + 1 when none. The caller
// guarantees no covered-and-relevant pixel exists left of first, so the walk
// is monotone right and never misses coverage.
int first_covered_at_or_after(const Segment& segment, int first, int last, float pixel_y) {
  int x = first;
  while (x <= last && (TINYDRAW_V2_CENSUS_ADD(const_search_calls, 1),
                       !covers_pixel(segment, static_cast<float>(x) + 0.5F, pixel_y))) {
    ++x;
  }
  return x;
}

// Last covered pixel in [first, last]; the caller guarantees first is covered
// and no relevant covered pixel exists right of last.
int last_covered_at_or_before(const Segment& segment, int first, int last, float pixel_y) {
  int x = last;
  while (x > first && (TINYDRAW_V2_CENSUS_ADD(const_search_calls, 1),
                       TINYDRAW_V2_CENSUS_ADD(const_search_last_calls, 1),
                       !covers_pixel(segment, static_cast<float>(x) + 0.5F, pixel_y))) {
    --x;
  }
  return x;
}

// One conservative chord estimate per row, chosen per segment shape. The
// parallelogram interval from conservative_tapered_row_span degenerates to
// the full bounding box for zero-length segments (world-margin-clamped fat
// strokes replay as pure circles), so circle-like segments use the exact
// one-sqrt circle chord instead: a point capsule is a circle, and a segment
// shorter than its radius is contained in the circle at its midpoint with
// radius r + len/2. Every tier is conservative; covers_pixel remains the
// sole geometry authority via the probe searches that follow.
struct RowSeed {
  bool circular = false;
  float center_x = 0.0F;
  float center_y = 0.0F;
  float radius = 0.0F;
  TaperedSpanTable table{};
};

RowSeed make_row_seed(const Segment& segment) {
  const float length_squared =
      segment.delta_x * segment.delta_x + segment.delta_y * segment.delta_y;
  const float radius = std::max(segment.first.radius, segment.second.radius);
  if (length_squared > radius * radius) {
    return {.table = make_tapered_span_table(segment)};
  }
  // The padded radius keeps the containment argument conservative against
  // float rounding in the length and midpoint arithmetic.
  const float half_length = 0.5F * conservative_sqrt_upper(length_squared);
  return {
      .circular = true,
      .center_x = segment.first.x + 0.5F * segment.delta_x,
      .center_y = segment.first.y + 0.5F * segment.delta_y,
      .radius = radius + half_length + 0.01F,
  };
}

ScanSpan conservative_row_span(const RowSeed& seed, PixelRect bounds, float pixel_y) {
  if (!seed.circular) {
    return conservative_tapered_row_span(seed.table, bounds, pixel_y);
  }
  constexpr float kRoundingMargin = 0.01F;
  const float row_distance = pixel_y - seed.center_y;
  const float reach_squared = seed.radius * seed.radius - row_distance * row_distance;
  if (reach_squared < 0.0F) {
    return {.first = bounds.x0, .last = bounds.x0 - 1};
  }
  const float reach = conservative_sqrt_upper(reach_squared);
  const int first = std::max(bounds.x0, fast_floor(seed.center_x - reach - kRoundingMargin) - 1);
  const int last = std::min(bounds.x1 - 1, fast_ceil(seed.center_x + reach + kRoundingMargin));
  return {.first = first, .last = last};
}

// Paints one row of a constant-radius chord inside an externally computed
// unfinalized window, returning newly finalized pixels. The conservative
// seed and the window each bound the true chord one-sidedly, so the
// monotone probes cannot miss coverage; covers_pixel stays authoritative.
int paint_masked_const_row(const Segment& segment, const RowSeed& seed, PixelRect bounds, int y,
                           std::size_t row, int window_first, int window_last, std::uint16_t color,
                           const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  const float pixel_y = static_cast<float>(y) + 0.5F;
  const ScanSpan conservative = conservative_row_span(seed, bounds, pixel_y);
  const int search_first = std::max(conservative.first, window_first);
  const int search_last = std::min(conservative.last, window_last);
  if (search_last < search_first) {
    TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
    return 0;
  }
  TINYDRAW_V2_CENSUS_ADD(const_rows_scanned, 1);
  const int first_covered = first_covered_at_or_after(segment, search_first, search_last, pixel_y);
  if (first_covered > search_last) {
    TINYDRAW_V2_CENSUS_ADD(const_rows_probed_empty, 1);
    return 0;
  }
  const int last_covered = last_covered_at_or_before(segment, first_covered, search_last, pixel_y);
  TINYDRAW_V2_CENSUS_ADD(const_span_pixels,
                         static_cast<std::uint64_t>(last_covered - first_covered + 1));
  return paint_masked_exact_span(first_covered, last_covered, row, color, surface, finalized);
}

// Interactive-append masked const painter: the historical warm-start search.
// Appends paint one new operation into fresh per-tile masks, so nearly every
// row is searched and the adjacent-row warm hint (valid because the 0.75 px
// minimum screen radius keeps adjacent row chords overlapping in x) is the
// cheapest possible seed. The stateless windowed machinery below serves the
// cold producer, whose dense newest-first masks break warm chains; routing
// appends through it measurably regressed the mixed-draw append gate.
void paint_masked_constant_radius_segment(const Segment& segment, PixelRect bounds,
                                          std::uint16_t color, const RasterSurface& surface,
                                          std::span<std::uint8_t> finalized,
                                          MaskedRowSummary* summary) {
  ScanSpan prior{.first = bounds.x0, .last = bounds.x0 - 1};
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const int surface_row = y - surface.level_bounds.y0;
    const std::size_t row =
        static_cast<std::size_t>(surface_row) * static_cast<std::size_t>(surface.stride);
    const std::size_t row_first =
        row + static_cast<std::size_t>(bounds.x0 - surface.level_bounds.x0);
    const std::size_t row_last =
        row + static_cast<std::size_t>(bounds.x1 - 1 - surface.level_bounds.x0);
    if (mask_range_all_set(finalized, row_first, row_last)) {
      TINYDRAW_V2_CENSUS_ADD(rows_prefinalized, 1);
      // The warm-start hint assumes row-adjacent spans; after a skipped row
      // the next scanned row must search from the full bounds again.
      prior = {.first = bounds.x0, .last = bounds.x0 - 1};
      continue;
    }
    const float pixel_y = static_cast<float>(y) + 0.5F;
    const int first_covered = find_first_covered(segment, bounds, pixel_y, prior);
    if (first_covered == bounds.x1) {
      prior.last = prior.first - 1;
      continue;
    }
    const int last_covered = find_last_covered(segment, bounds, pixel_y, prior, first_covered);
    prior = {.first = first_covered, .last = last_covered};
    TINYDRAW_V2_CENSUS_ADD(const_span_pixels,
                           static_cast<std::uint64_t>(last_covered - first_covered + 1));
    const int newly_finalized =
        paint_masked_exact_span(first_covered, last_covered, row, color, surface, finalized);
    if (newly_finalized != 0 && summary != nullptr) {
      summary->note_finalized(surface_row, newly_finalized);
    }
  }
}

// Walks one conservative row span in mask-byte chunks. Fully-finalized
// chunks are skipped without touching the predicate; each unfinalized pixel
// still goes through covers_pixel, so per-pixel decisions are identical to
// probing every span pixel individually. Returns newly finalized pixels.
int paint_masked_tapered_row(const Segment& segment, int y, ScanSpan row_span, std::uint16_t color,
                             const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  const float pixel_y = static_cast<float>(y) + 0.5F;
  const std::size_t row = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                          static_cast<std::size_t>(surface.stride);
  int newly_finalized = 0;
  int x = row_span.first;
  std::size_t pixel = row + static_cast<std::size_t>(x - surface.level_bounds.x0);
  while (x <= row_span.last) {
    const std::size_t byte = pixel >> 3U;
    const unsigned bit = static_cast<unsigned>(pixel & 7U);
    const int in_byte = std::min(static_cast<int>(8U - bit), row_span.last - x + 1);
    const std::uint8_t chunk_mask = static_cast<std::uint8_t>(
        in_byte == 8 ? 0xFFU : ((1U << static_cast<unsigned>(in_byte)) - 1U) << bit);
    const std::uint8_t have = finalized[byte];
    if ((have & chunk_mask) == chunk_mask) {
      TINYDRAW_V2_CENSUS_ADD(mask_skips, static_cast<std::uint64_t>(in_byte));
      x += in_byte;
      pixel += static_cast<std::size_t>(in_byte);
      continue;
    }
    for (int offset = 0; offset < in_byte; ++offset) {
      if ((have & static_cast<std::uint8_t>(1U << (bit + static_cast<unsigned>(offset)))) != 0U) {
        TINYDRAW_V2_CENSUS_ADD(mask_skips, 1);
        continue;
      }
      TINYDRAW_V2_CENSUS_ADD(covers_calls, 1);
      if (!covers_pixel(segment, static_cast<float>(x + offset) + 0.5F, pixel_y)) {
        continue;
      }
      TINYDRAW_V2_CENSUS_ADD(covers_hits, 1);
      surface.pixels[pixel + static_cast<std::size_t>(offset)] = color;
      finalize_pixel(finalized, pixel + static_cast<std::size_t>(offset));
      ++newly_finalized;
    }
    x += in_byte;
    pixel += static_cast<std::size_t>(in_byte);
  }
  return newly_finalized;
}

// Newest-first masked tapered painter. Skips fully-finalized rows before the
// conservative span math and fully-finalized mask bytes inside the span; a
// skipped pixel is finalized by definition, so per-pixel decisions are
// identical to probing every span pixel. covers_pixel remains the sole
// geometry authority for every unfinalized pixel.
void paint_masked_tapered_segment(const Segment& segment, PixelRect bounds, std::uint16_t color,
                                  const RasterSurface& surface, std::span<std::uint8_t> finalized,
                                  MaskedRowSummary* summary) {
  // See paint_masked_constant_radius_segment: row-level saturation stays on
  // the mask byte scan; the summary is consulted at coarser levels only.
  const TaperedSpanTable table = make_tapered_span_table(segment);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const int surface_row = y - surface.level_bounds.y0;
    const std::size_t row =
        static_cast<std::size_t>(surface_row) * static_cast<std::size_t>(surface.stride);
    const std::size_t row_first =
        row + static_cast<std::size_t>(bounds.x0 - surface.level_bounds.x0);
    const std::size_t row_last =
        row + static_cast<std::size_t>(bounds.x1 - 1 - surface.level_bounds.x0);
    if (mask_range_all_set(finalized, row_first, row_last)) {
      TINYDRAW_V2_CENSUS_ADD(rows_prefinalized, 1);
      continue;
    }
    const float pixel_y = static_cast<float>(y) + 0.5F;
    TINYDRAW_V2_CENSUS_ADD(rows_scanned, 1);
    const ScanSpan row_span = conservative_tapered_row_span(table, bounds, pixel_y);
    if (row_span.empty()) {
      TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
      continue;
    }
    TINYDRAW_V2_CENSUS_ADD(span_pixels,
                           static_cast<std::uint64_t>(row_span.last - row_span.first + 1));
    const int newly_finalized =
        paint_masked_tapered_row(segment, y, row_span, color, surface, finalized);
    if (newly_finalized != 0 && summary != nullptr) {
      summary->note_finalized(surface_row, newly_finalized);
    }
  }
}

void paint_masked_segment(const Sample& start, const Sample& end, std::uint16_t color,
                          const RasterSurface& surface, std::span<std::uint8_t> finalized,
                          MaskedRowSummary* summary) {
  const Segment segment = make_segment(start, end);
  const PixelRect bounds = segment_bounds(segment, surface.level_bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return;
  }
  if (start.radius == end.radius) {
    paint_masked_constant_radius_segment(segment, bounds, color, surface, finalized, summary);
  } else {
    paint_masked_tapered_segment(segment, bounds, color, surface, finalized, summary);
  }
}

void paint_segment(const Sample& start, const Sample& end, std::uint16_t color,
                   const RasterSurface& surface) {
  const Segment segment = make_segment(start, end);
  const PixelRect bounds = segment_bounds(segment, surface.level_bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return;
  }
  if (start.radius == end.radius) {
    paint_constant_radius_segment(segment, bounds, color, surface);
  } else {
    paint_tapered_segment(segment, bounds, color, surface);
  }
}

void paint_bounded_segment(const Sample& first, const Sample& second, std::uint16_t color,
                           const RasterSurface& surface) {
  paint_segment(first, second, color, surface);
}

Sample midpoint(const Sample& first, const Sample& second) {
  return {
      .x = (first.x + second.x) * 0.5F,
      .y = (first.y + second.y) * 0.5F,
      .radius = (first.radius + second.radius) * 0.5F,
  };
}

struct CurveUnit {
  std::array<Segment, 3> segments{};
  std::size_t count = 0U;
};

std::optional<CurveUnit> curved_unit(std::span<const CompactOperationSample> samples,
                                     std::size_t endpoint, ZoomLevel zoom) {
  if (samples.empty()) {
    return std::nullopt;
  }
  if (samples.size() <= 2U) {
    if (endpoint + 1U != samples.size()) {
      return std::nullopt;
    }
    return CurveUnit{.segments = {make_segment(scaled_sample(samples.front(), zoom),
                                               scaled_sample(samples.back(), zoom))},
                     .count = 1U};
  }
  if (endpoint < 2U || endpoint >= samples.size()) {
    return std::nullopt;
  }
  const Sample prior = scaled_sample(samples[endpoint - 2U], zoom);
  const Sample control = scaled_sample(samples[endpoint - 1U], zoom);
  const Sample current = scaled_sample(samples[endpoint], zoom);
  const Sample start = endpoint == 2U ? prior : midpoint(prior, control);
  const Sample end = midpoint(control, current);
  const Sample curve_midpoint = midpoint(midpoint(start, control), midpoint(control, end));
  CurveUnit unit;
  unit.segments[unit.count++] = make_segment(start, curve_midpoint);
  unit.segments[unit.count++] = make_segment(curve_midpoint, end);
  if (endpoint + 1U == samples.size()) {
    unit.segments[unit.count++] = make_segment(end, current);
  }
  return unit;
}

// Warm-start row sweep over one curve unit's chords for the interactive
// append path. A unit's chords share one color and overlap at their joints,
// so under the finalized mask the union may paint in any order; sweeping
// them together pays one mask-range scan per row instead of one per chord,
// while each chord keeps the historical warm-start search that fresh append
// masks reward (no skipped rows, so warm chains never break).
void paint_masked_curve_unit_warm(const CurveUnit& unit, std::uint16_t color,
                                  const RasterSurface& surface, std::span<std::uint8_t> finalized,
                                  MaskedRowSummary* summary) {
  struct ChordState {
    Segment segment{};
    TaperedSpanTable table{};
    PixelRect bounds{};
    ScanSpan prior{};
    bool constant = false;
  };
  std::array<ChordState, 3> chords{};
  std::size_t chord_count = 0;
  PixelRect union_bounds{surface.level_bounds.x1, surface.level_bounds.y1, surface.level_bounds.x0,
                         surface.level_bounds.y0};
  for (std::size_t step = 0; step < unit.count; ++step) {
    const Segment& segment = unit.segments[step];
    const PixelRect bounds = segment_bounds(segment, surface.level_bounds);
    if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
      continue;
    }
    const bool constant = segment.first.radius == segment.second.radius;
    chords[chord_count] = {
        .segment = segment,
        .table = constant ? TaperedSpanTable{} : make_tapered_span_table(segment),
        .bounds = bounds,
        .prior = {.first = bounds.x0, .last = bounds.x0 - 1},
        .constant = constant,
    };
    ++chord_count;
    union_bounds.x0 = std::min(union_bounds.x0, bounds.x0);
    union_bounds.y0 = std::min(union_bounds.y0, bounds.y0);
    union_bounds.x1 = std::max(union_bounds.x1, bounds.x1);
    union_bounds.y1 = std::max(union_bounds.y1, bounds.y1);
  }
  if (chord_count == 0U) {
    return;
  }
  for (int y = union_bounds.y0; y < union_bounds.y1; ++y) {
    const int surface_row = y - surface.level_bounds.y0;
    const std::size_t row =
        static_cast<std::size_t>(surface_row) * static_cast<std::size_t>(surface.stride);
    const std::size_t row_first =
        row + static_cast<std::size_t>(union_bounds.x0 - surface.level_bounds.x0);
    const std::size_t row_last =
        row + static_cast<std::size_t>(union_bounds.x1 - 1 - surface.level_bounds.x0);
    if (mask_range_all_set(finalized, row_first, row_last)) {
      TINYDRAW_V2_CENSUS_ADD(rows_prefinalized, 1);
      for (std::size_t index = 0; index < chord_count; ++index) {
        chords[index].prior = {.first = chords[index].bounds.x0,
                               .last = chords[index].bounds.x0 - 1};
      }
      continue;
    }
    const float pixel_y = static_cast<float>(y) + 0.5F;
    for (std::size_t index = 0; index < chord_count; ++index) {
      ChordState& chord = chords[index];
      if (y < chord.bounds.y0 || y >= chord.bounds.y1) {
        continue;
      }
      int newly_finalized = 0;
      if (chord.constant) {
        const int first_covered =
            find_first_covered(chord.segment, chord.bounds, pixel_y, chord.prior);
        if (first_covered == chord.bounds.x1) {
          chord.prior.last = chord.prior.first - 1;
          continue;
        }
        const int last_covered =
            find_last_covered(chord.segment, chord.bounds, pixel_y, chord.prior, first_covered);
        chord.prior = {.first = first_covered, .last = last_covered};
        TINYDRAW_V2_CENSUS_ADD(const_span_pixels,
                               static_cast<std::uint64_t>(last_covered - first_covered + 1));
        newly_finalized =
            paint_masked_exact_span(first_covered, last_covered, row, color, surface, finalized);
      } else {
        TINYDRAW_V2_CENSUS_ADD(rows_scanned, 1);
        const ScanSpan row_span = conservative_tapered_row_span(chord.table, chord.bounds, pixel_y);
        if (row_span.empty()) {
          TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
          continue;
        }
        TINYDRAW_V2_CENSUS_ADD(span_pixels,
                               static_cast<std::uint64_t>(row_span.last - row_span.first + 1));
        newly_finalized =
            paint_masked_tapered_row(chord.segment, y, row_span, color, surface, finalized);
      }
      if (newly_finalized != 0 && summary != nullptr) {
        summary->note_finalized(surface_row, newly_finalized);
      }
    }
  }
}

}  // namespace

MaskedRowSummary::MaskedRowSummary(std::span<std::uint16_t> unset_counts,
                                   std::span<std::uint32_t> saturated_words)
    : unset_counts_(unset_counts), saturated_words_(saturated_words) {}

bool MaskedRowSummary::ready(std::size_t rows) const {
  return rows != 0U && static_cast<std::size_t>(rows_) >= rows && unset_counts_.size() >= rows &&
         saturated_words_.size() >= (rows + 31U) / 32U;
}

void MaskedRowSummary::reset(int rows, int width) {
  if (rows <= 0 || width <= 0 || static_cast<std::size_t>(rows) > unset_counts_.size() ||
      (static_cast<std::size_t>(rows) + 31U) / 32U > saturated_words_.size()) {
    rows_ = 0;
    unsaturated_rows_ = 0;
    return;
  }
  rows_ = rows;
  unsaturated_rows_ = rows;
  std::fill_n(unset_counts_.begin(), static_cast<std::size_t>(rows),
              static_cast<std::uint16_t>(width));
  std::fill_n(saturated_words_.begin(), (static_cast<std::size_t>(rows) + 31U) / 32U, 0U);
}

bool MaskedRowSummary::row_saturated(int row) const {
  return row >= 0 && row < rows_ &&
         (saturated_words_[static_cast<std::size_t>(row) >> 5U] &
          (1U << (static_cast<unsigned>(row) & 31U))) != 0U;
}

bool MaskedRowSummary::rows_saturated(int first_row, int last_row) const {
  if (first_row < 0 || last_row >= rows_ || first_row > last_row) {
    return false;
  }
  const std::size_t first_word = static_cast<std::size_t>(first_row) >> 5U;
  const std::size_t last_word = static_cast<std::size_t>(last_row) >> 5U;
  const std::uint32_t first_mask = ~0U << (static_cast<unsigned>(first_row) & 31U);
  const std::uint32_t last_mask = ~0U >> (31U - (static_cast<unsigned>(last_row) & 31U));
  if (first_word == last_word) {
    const std::uint32_t need = first_mask & last_mask;
    return (saturated_words_[first_word] & need) == need;
  }
  if ((saturated_words_[first_word] & first_mask) != first_mask ||
      (saturated_words_[last_word] & last_mask) != last_mask) {
    return false;
  }
  return std::all_of(saturated_words_.begin() + static_cast<std::ptrdiff_t>(first_word + 1U),
                     saturated_words_.begin() + static_cast<std::ptrdiff_t>(last_word),
                     [](std::uint32_t word) { return word == ~0U; });
}

bool MaskedRowSummary::all_saturated() const { return rows_ != 0 && unsaturated_rows_ == 0; }

void MaskedRowSummary::note_finalized(int row, int newly_finalized) {
  if (newly_finalized <= 0 || row < 0 || row >= rows_) {
    return;
  }
  std::uint16_t& unset = unset_counts_.data()[row];
  unset =
      static_cast<std::uint16_t>(unset - std::min<int>(newly_finalized, static_cast<int>(unset)));
  if (unset == 0U) {
    std::uint32_t& word = saturated_words_.data()[row >> 5];
    const std::uint32_t bit = 1U << (static_cast<unsigned>(row) & 31U);
    if ((word & bit) == 0U) {
      word |= bit;
      --unsaturated_rows_;
    }
  }
}

PixelRect operation_level_bounds(PixelRect world_bounds, ZoomLevel zoom) {
  const int percent = zoom_percent(zoom);
  return {
      .x0 = world_bounds.x0 * percent / 100,
      .y0 = world_bounds.y0 * percent / 100,
      .x1 = std::min(kWorldWidth * percent / 100, (world_bounds.x1 * percent + 99) / 100),
      .y1 = std::min(kWorldHeight * percent / 100, (world_bounds.y1 * percent + 99) / 100),
  };
}

PixelRect incremental_segment_level_bounds(CompactOperationSample first,
                                           CompactOperationSample second, ZoomLevel zoom) {
  const int level_width = kWorldWidth * zoom_percent(zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(zoom) / 100;
  return segment_bounds(make_segment(scaled_sample(first, zoom), scaled_sample(second, zoom)),
                        {0, 0, level_width, level_height});
}

std::size_t incremental_segment_step_count(CompactOperationSample, CompactOperationSample,
                                           ZoomLevel) {
  return 1U;
}

std::size_t incremental_segment_step_work(CompactOperationSample first,
                                          CompactOperationSample second, ZoomLevel zoom,
                                          PixelRect clip, std::size_t step) {
  if (step != 0U) {
    return 0U;
  }
  const PixelRect bounds =
      segment_bounds(make_segment(scaled_sample(first, zoom), scaled_sample(second, zoom)), clip);
  return static_cast<std::size_t>(std::max(0, bounds.x1 - bounds.x0)) *
         static_cast<std::size_t>(std::max(0, bounds.y1 - bounds.y0));
}

bool apply_incremental_segment_steps(const IncrementalSegment& segment,
                                     const RasterSurface& surface, std::size_t first_step,
                                     std::size_t step_count) {
  if (!valid_surface(surface)) {
    return false;
  }
  if (first_step != 0U || step_count != 1U) {
    return false;
  }
  paint_bounded_segment(
      scaled_sample(segment.first, surface.zoom), scaled_sample(segment.second, surface.zoom),
      segment.tool == OperationTool::kEraser ? kBackground : segment.color, surface);
  return true;
}

bool apply_masked_incremental_segment(const IncrementalSegment& segment,
                                      const RasterSurface& surface,
                                      std::span<std::uint8_t> finalized_pixels,
                                      MaskedRowSummary* summary) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(surface.pixels)),
      std::as_bytes(std::span(finalized_pixels)
                        .first(std::min(finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = surface.level_bounds.y1 - surface.level_bounds.y0;
  if (!valid_surface(surface) || finalized_pixels.size() < required_mask_bytes ||
      mask_aliases_pixels ||
      (summary != nullptr && !summary->ready(static_cast<std::size_t>(surface_rows)))) {
    return false;
  }
  paint_masked_segment(scaled_sample(segment.first, surface.zoom),
                       scaled_sample(segment.second, surface.zoom),
                       segment.tool == OperationTool::kEraser ? kBackground : segment.color,
                       surface, finalized_pixels, summary);
  return true;
}

bool apply_incremental_operation(const OperationAppend& operation, const RasterSurface& surface) {
  if (!valid_surface(surface) || operation.samples.empty()) {
    return false;
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  if (operation.samples.size() <= 2U) {
    paint_bounded_segment(scaled_sample(operation.samples.front(), surface.zoom),
                          scaled_sample(operation.samples.back(), surface.zoom), color, surface);
    return true;
  }
  for (std::size_t endpoint = 2U; endpoint < operation.samples.size(); ++endpoint) {
    const auto unit = curved_unit(operation.samples, endpoint, surface.zoom);
    if (!unit.has_value()) {
      return false;
    }
    for (std::size_t step = 0U; step < unit->count; ++step) {
      paint_segment(unit->segments[step].first, unit->segments[step].second, color, surface);
    }
  }
  return true;
}

bool apply_masked_incremental_operation(const OperationAppend& operation,
                                        const RasterSurface& surface,
                                        std::span<std::uint8_t> finalized_pixels,
                                        MaskedRowSummary* summary) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(surface.pixels)),
      std::as_bytes(std::span(finalized_pixels)
                        .first(std::min(finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = surface.level_bounds.y1 - surface.level_bounds.y0;
  if (!valid_surface(surface) || operation.samples.empty() ||
      finalized_pixels.size() < required_mask_bytes || mask_aliases_pixels ||
      (summary != nullptr && !summary->ready(static_cast<std::size_t>(surface_rows)))) {
    return false;
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  if (operation.samples.size() <= 2U) {
    paint_masked_segment(scaled_sample(operation.samples.front(), surface.zoom),
                         scaled_sample(operation.samples.back(), surface.zoom), color, surface,
                         finalized_pixels, summary);
    return true;
  }
  for (std::size_t endpoint = 2U; endpoint < operation.samples.size(); ++endpoint) {
    const auto unit = curved_unit(operation.samples, endpoint, surface.zoom);
    if (!unit.has_value()) {
      return false;
    }
    paint_masked_curve_unit_warm(*unit, color, surface, finalized_pixels, summary);
  }
  return true;
}

std::size_t incremental_curve_unit_step_count(std::span<const CompactOperationSample> samples,
                                              std::size_t endpoint, ZoomLevel zoom) {
  const auto unit = curved_unit(samples, endpoint, zoom);
  return unit.has_value() ? unit->count : 0U;
}

namespace {

PreparedCurveStep pack_prepared_step(const Segment& segment) {
  return {
      .first_x = segment.first.x,
      .first_y = segment.first.y,
      .first_radius = segment.first.radius,
      .second_x = segment.second.x,
      .second_y = segment.second.y,
      .second_radius = segment.second.radius,
      .delta_x = segment.delta_x,
      .delta_y = segment.delta_y,
      .inverse_length_squared = segment.inverse_length_squared,
  };
}

Segment unpack_prepared_step(const PreparedCurveStep& step) {
  return {
      .first = {.x = step.first_x, .y = step.first_y, .radius = step.first_radius},
      .second = {.x = step.second_x, .y = step.second_y, .radius = step.second_radius},
      .delta_x = step.delta_x,
      .delta_y = step.delta_y,
      .inverse_length_squared = step.inverse_length_squared,
  };
}

}  // namespace

std::optional<PreparedCurveUnit> prepare_incremental_curve_unit(
    std::span<const CompactOperationSample> samples, std::size_t endpoint, ZoomLevel zoom) {
  const auto unit = curved_unit(samples, endpoint, zoom);
  if (!unit.has_value()) {
    return std::nullopt;
  }
  PreparedCurveUnit prepared{.step_count = unit->count};
  for (std::size_t step = 0; step < unit->count; ++step) {
    prepared.steps[step] = pack_prepared_step(unit->segments[step]);
  }
  return prepared;
}

std::optional<PixelRect> prepared_curve_step_level_bounds(const PreparedCurveUnit& unit,
                                                          std::size_t step_index, ZoomLevel zoom) {
  if (unit.step_count > unit.steps.size() || step_index >= unit.step_count) {
    return std::nullopt;
  }
  const PixelRect clip{0, 0, kWorldWidth * zoom_percent(zoom) / 100,
                       kWorldHeight * zoom_percent(zoom) / 100};
  const PixelRect bounds = segment_bounds(unpack_prepared_step(unit.steps[step_index]), clip);
  return bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 ? std::optional{bounds} : std::nullopt;
}

bool apply_masked_prepared_curve_step(OperationTool tool, std::uint16_t color,
                                      const PreparedCurveUnit& unit, std::size_t step_index,
                                      const RasterSurface& surface,
                                      std::span<std::uint8_t> finalized_pixels,
                                      MaskedRowSummary* summary) {
  if (unit.step_count > unit.steps.size() || step_index >= unit.step_count) {
    return false;
  }
  // One chord is a one-step unit; the unit painter carries the windowed
  // producer machinery and its validation.
  const PreparedCurveUnit single{.step_count = 1U, .steps = {unit.steps[step_index]}};
  return apply_masked_prepared_curve_unit(tool, color, single, surface, finalized_pixels, summary);
}

std::optional<PixelRect> incremental_curve_step_level_bounds(
    std::span<const CompactOperationSample> samples, std::size_t endpoint, std::size_t step_index,
    ZoomLevel zoom) {
  const auto unit = curved_unit(samples, endpoint, zoom);
  if (!unit.has_value() || step_index >= unit->count) {
    return std::nullopt;
  }
  const PixelRect clip{0, 0, kWorldWidth * zoom_percent(zoom) / 100,
                       kWorldHeight * zoom_percent(zoom) / 100};
  const PixelRect bounds = segment_bounds(unit->segments[step_index], clip);
  return bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 ? std::optional{bounds} : std::nullopt;
}

bool apply_masked_incremental_curve_step(const OperationAppend& operation, std::size_t endpoint,
                                         std::size_t step_index, const RasterSurface& surface,
                                         std::span<std::uint8_t> finalized_pixels,
                                         MaskedRowSummary* summary) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(surface.pixels)),
      std::as_bytes(std::span(finalized_pixels)
                        .first(std::min(finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = surface.level_bounds.y1 - surface.level_bounds.y0;
  const auto unit = curved_unit(operation.samples, endpoint, surface.zoom);
  if (!valid_surface(surface) || !unit.has_value() || step_index >= unit->count ||
      finalized_pixels.size() < required_mask_bytes || mask_aliases_pixels ||
      (summary != nullptr && !summary->ready(static_cast<std::size_t>(surface_rows)))) {
    return false;
  }
  if (operation.samples.size() <= 2U) {
    return apply_masked_incremental_segment({.tool = operation.tool,
                                             .color = operation.color,
                                             .first = operation.samples.front(),
                                             .second = operation.samples.back()},
                                            surface, finalized_pixels, summary);
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  paint_masked_segment(unit->segments[step_index].first, unit->segments[step_index].second, color,
                       surface, finalized_pixels, summary);
  return true;
}

bool apply_masked_prepared_curve_unit(OperationTool tool, std::uint16_t color,
                                      const PreparedCurveUnit& unit, const RasterSurface& surface,
                                      std::span<std::uint8_t> finalized_pixels,
                                      MaskedRowSummary* summary) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(surface.pixels)),
      std::as_bytes(std::span(finalized_pixels)
                        .first(std::min(finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = surface.level_bounds.y1 - surface.level_bounds.y0;
  if (!valid_surface(surface) || unit.step_count == 0U || unit.step_count > unit.steps.size() ||
      finalized_pixels.size() < required_mask_bytes || mask_aliases_pixels ||
      (summary != nullptr && !summary->ready(static_cast<std::size_t>(surface_rows)))) {
    return false;
  }
  const std::uint16_t applied = tool == OperationTool::kEraser ? kBackground : color;
  // A unit's chords share one color and overlap around their joints, so one
  // row sweep over the union bounds may process them in any order: the
  // finalized mask keeps the written pixel set identical to sequential
  // per-chord painting, and a window left stale by an earlier chord on the
  // same row only widens a clamp that the masked walks re-check anyway.
  struct ChordPlan {
    Segment segment{};
    RowSeed seed{};
    PixelRect bounds{};
    bool constant = false;
  };
  std::array<ChordPlan, 3> plans{};
  std::size_t plan_count = 0;
  PixelRect union_bounds{surface.level_bounds.x1, surface.level_bounds.y1, surface.level_bounds.x0,
                         surface.level_bounds.y0};
  for (std::size_t step = 0; step < unit.step_count; ++step) {
    const Segment segment = unpack_prepared_step(unit.steps[step]);
    const PixelRect bounds = segment_bounds(segment, surface.level_bounds);
    if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
      continue;
    }
    plans[plan_count++] = {
        .segment = segment,
        .seed = make_row_seed(segment),
        .bounds = bounds,
        .constant = segment.first.radius == segment.second.radius,
    };
    union_bounds.x0 = std::min(union_bounds.x0, bounds.x0);
    union_bounds.y0 = std::min(union_bounds.y0, bounds.y0);
    union_bounds.x1 = std::max(union_bounds.x1, bounds.x1);
    union_bounds.y1 = std::max(union_bounds.y1, bounds.y1);
  }
  if (plan_count == 0U) {
    return true;
  }
  for (int y = union_bounds.y0; y < union_bounds.y1; ++y) {
    const int surface_row = y - surface.level_bounds.y0;
    const std::size_t row =
        static_cast<std::size_t>(surface_row) * static_cast<std::size_t>(surface.stride);
    const std::size_t row_first =
        row + static_cast<std::size_t>(union_bounds.x0 - surface.level_bounds.x0);
    const std::size_t row_last =
        row + static_cast<std::size_t>(union_bounds.x1 - 1 - surface.level_bounds.x0);
    const auto window = mask_unset_window(finalized_pixels, row_first, row_last);
    if (!window.has_value()) {
      TINYDRAW_V2_CENSUS_ADD(rows_prefinalized, 1);
      continue;
    }
    const int window_first = union_bounds.x0 + static_cast<int>(window->first - row_first);
    const int window_last = union_bounds.x0 + static_cast<int>(window->last - row_first);
    const float pixel_y = static_cast<float>(y) + 0.5F;
    for (std::size_t index = 0; index < plan_count; ++index) {
      const ChordPlan& plan = plans[index];
      if (y < plan.bounds.y0 || y >= plan.bounds.y1) {
        continue;
      }
      const int chord_first = std::max(window_first, plan.bounds.x0);
      const int chord_last = std::min(window_last, plan.bounds.x1 - 1);
      if (chord_last < chord_first) {
        continue;
      }
      int newly_finalized = 0;
      if (plan.constant) {
        newly_finalized =
            paint_masked_const_row(plan.segment, plan.seed, plan.bounds, y, row, chord_first,
                                   chord_last, applied, surface, finalized_pixels);
      } else {
        const ScanSpan conservative = conservative_row_span(plan.seed, plan.bounds, pixel_y);
        const ScanSpan row_span{
            .first = std::max(conservative.first, chord_first),
            .last = std::min(conservative.last, chord_last),
        };
        if (row_span.empty()) {
          TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
          continue;
        }
        TINYDRAW_V2_CENSUS_ADD(rows_scanned, 1);
        TINYDRAW_V2_CENSUS_ADD(span_pixels,
                               static_cast<std::uint64_t>(row_span.last - row_span.first + 1));
        newly_finalized =
            paint_masked_tapered_row(plan.segment, y, row_span, applied, surface, finalized_pixels);
      }
      if (newly_finalized != 0 && summary != nullptr) {
        summary->note_finalized(surface_row, newly_finalized);
      }
    }
  }
  return true;
}

namespace {

// One prepared chord with its per-row sweep plan. Lives in caller-funded
// opaque storage so the internal geometry types stay private.
struct OperationChordPlan {
  Segment segment{};
  RowSeed seed{};
  PixelRect bounds{};
  bool constant = false;
};
static_assert(sizeof(OperationChordPlan) <= kPreparedOperationChordBytes);
static_assert(alignof(OperationChordPlan) <= kPreparedOperationChordAlign);

[[nodiscard]] bool chord_storage_usable(std::span<const std::byte> storage,
                                        std::size_t chord_count) {
  return chord_count <= kOperationChordCapacity &&
         storage.size() >= kOperationChordStorageBytes &&
         reinterpret_cast<std::uintptr_t>(storage.data()) % alignof(OperationChordPlan) == 0U;
}

[[nodiscard]] OperationChordPlan* chord_plans(std::span<std::byte> storage) {
  return reinterpret_cast<OperationChordPlan*>(storage.data());
}

[[nodiscard]] const OperationChordPlan* chord_plans(std::span<const std::byte> storage) {
  return reinterpret_cast<const OperationChordPlan*>(storage.data());
}

// y0-ascending chord order, one byte per chord, stored after the plans.
[[nodiscard]] std::uint8_t* chord_order(std::span<std::byte> storage) {
  return reinterpret_cast<std::uint8_t*>(storage.data() +
                                         kOperationChordCapacity * kPreparedOperationChordBytes);
}

[[nodiscard]] const std::uint8_t* chord_order(std::span<const std::byte> storage) {
  return reinterpret_cast<const std::uint8_t*>(
      storage.data() + kOperationChordCapacity * kPreparedOperationChordBytes);
}

}  // namespace

std::optional<OperationChordBatch> prepare_operation_chord_batch(
    std::span<const CompactOperationSample> samples, std::size_t first_endpoint, ZoomLevel zoom,
    PixelRect surface_bounds, std::span<std::byte> chord_storage) {
  const std::size_t capacity = kOperationChordCapacity;
  if (samples.empty() || !chord_storage_usable(chord_storage, capacity)) {
    return std::nullopt;
  }
  OperationChordPlan* plans = chord_plans(chord_storage);
  OperationChordBatch batch{
      .clipped_bounds = {surface_bounds.x1, surface_bounds.y1, surface_bounds.x0,
                         surface_bounds.y0},
  };
  const auto append_unit = [&](std::size_t endpoint) -> bool {
    const auto unit = curved_unit(samples, endpoint, zoom);
    if (!unit.has_value()) {
      return false;
    }
    for (std::size_t step = 0; step < unit->count; ++step) {
      const Segment& segment = unit->segments[step];
      const PixelRect bounds = segment_bounds(segment, surface_bounds);
      if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
        continue;
      }
      plans[batch.chord_count++] = {
          .segment = segment,
          .seed = make_row_seed(segment),
          .bounds = bounds,
          .constant = segment.first.radius == segment.second.radius,
      };
      batch.raster_work += static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                           static_cast<std::size_t>(bounds.y1 - bounds.y0);
      batch.clipped_bounds.x0 = std::min(batch.clipped_bounds.x0, bounds.x0);
      batch.clipped_bounds.y0 = std::min(batch.clipped_bounds.y0, bounds.y0);
      batch.clipped_bounds.x1 = std::max(batch.clipped_bounds.x1, bounds.x1);
      batch.clipped_bounds.y1 = std::max(batch.clipped_bounds.y1, bounds.y1);
    }
    return true;
  };
  if (samples.size() <= 2U) {
    if (!append_unit(first_endpoint)) {
      return std::nullopt;
    }
    batch.next_endpoint = 0U;
  } else {
    if (first_endpoint < 2U || first_endpoint >= samples.size()) {
      return std::nullopt;
    }
    std::size_t endpoint = first_endpoint;
    while (true) {
      // Units are atomic: stop before an endpoint whose worst-case three
      // chords would not fit, so a resumed batch never re-prepares chords.
      if (batch.chord_count + 3U > capacity) {
        batch.next_endpoint = endpoint;
        break;
      }
      if (!append_unit(endpoint)) {
        return std::nullopt;
      }
      if (endpoint == 2U) {
        batch.next_endpoint = 0U;
        break;
      }
      --endpoint;
    }
  }
  std::uint8_t* order = chord_order(chord_storage);
  for (std::size_t index = 0; index < batch.chord_count; ++index) {
    order[index] = static_cast<std::uint8_t>(index);
  }
  std::sort(order, order + batch.chord_count, [plans](std::uint8_t left, std::uint8_t right) {
    return plans[left].bounds.y0 < plans[right].bounds.y0;
  });
  return batch;
}

bool apply_masked_operation_chord_rows(OperationTool tool, std::uint16_t color,
                                       std::span<const std::byte> chord_storage,
                                       const OperationChordBatch& batch, int first_row,
                                       std::size_t max_work_px, const RasterSurface& surface,
                                       std::span<std::uint8_t> finalized_pixels,
                                       MaskedRowSummary* summary, OperationSweepSlice& slice) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(surface.pixels)),
      std::as_bytes(std::span(finalized_pixels)
                        .first(std::min(finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = surface.level_bounds.y1 - surface.level_bounds.y0;
  const PixelRect bounds = batch.clipped_bounds;
  if (!valid_surface(surface) || batch.chord_count == 0U ||
      !chord_storage_usable(chord_storage, batch.chord_count) ||
      finalized_pixels.size() < required_mask_bytes || mask_aliases_pixels || max_work_px == 0U ||
      first_row < bounds.y0 || first_row >= bounds.y1 || bounds.x0 < surface.level_bounds.x0 ||
      bounds.y0 < surface.level_bounds.y0 || bounds.x1 > surface.level_bounds.x1 ||
      bounds.y1 > surface.level_bounds.y1 ||
      (summary != nullptr && !summary->ready(static_cast<std::size_t>(surface_rows)))) {
    return false;
  }
  const std::uint16_t applied = tool == OperationTool::kEraser ? kBackground : color;
  const OperationChordPlan* plans = chord_plans(chord_storage);
  const std::uint8_t* order = chord_order(chord_storage);
  // Scanline state over the y0-sorted order: enter admits chords as the
  // sweep reaches their top row; the active list drops chords lazily once
  // the sweep passes their bottom row. Resume rebuilds both in one pass.
  std::array<std::uint8_t, kOperationChordCapacity> active{};
  std::size_t active_count = 0;
  std::size_t enter = 0;
  while (enter < batch.chord_count && plans[order[enter]].bounds.y0 <= first_row) {
    if (plans[order[enter]].bounds.y1 > first_row) {
      active[active_count++] = order[enter];
    }
    ++enter;
  }
  int y = first_row;
  slice.rows_swept = 0;
  slice.work_px = 0;
  while (y < bounds.y1 && (slice.rows_swept == 0 || slice.work_px < max_work_px)) {
    while (enter < batch.chord_count && plans[order[enter]].bounds.y0 <= y) {
      active[active_count++] = order[enter];
      ++enter;
    }
    for (std::size_t index = 0; index < active_count;) {
      if (plans[active[index]].bounds.y1 <= y) {
        active[index] = active[--active_count];
      } else {
        ++index;
      }
    }
    if (active_count == 0U) {
      // Row gap between chords: jump to the next chord's top row for free.
      if (enter >= batch.chord_count) {
        y = bounds.y1;
        break;
      }
      y = plans[order[enter]].bounds.y0;
      continue;
    }
    int row_x0 = bounds.x1;
    int row_x1 = bounds.x0;
    for (std::size_t index = 0; index < active_count; ++index) {
      const OperationChordPlan& plan = plans[active[index]];
      row_x0 = std::min(row_x0, plan.bounds.x0);
      row_x1 = std::max(row_x1, plan.bounds.x1);
    }
    const int surface_row = y - surface.level_bounds.y0;
    const std::size_t row =
        static_cast<std::size_t>(surface_row) * static_cast<std::size_t>(surface.stride);
    const std::size_t row_first =
        row + static_cast<std::size_t>(row_x0 - surface.level_bounds.x0);
    const std::size_t row_last =
        row + static_cast<std::size_t>(row_x1 - 1 - surface.level_bounds.x0);
    ++slice.rows_swept;
    const auto window = mask_unset_window(finalized_pixels, row_first, row_last);
    if (!window.has_value()) {
      TINYDRAW_V2_CENSUS_ADD(rows_prefinalized, 1);
      ++y;
      continue;
    }
    const int window_first = row_x0 + static_cast<int>(window->first - row_first);
    const int window_last = row_x0 + static_cast<int>(window->last - row_first);
    // Flat per-row charge for the window scan itself.
    slice.work_px += 4U;
    const float pixel_y = static_cast<float>(y) + 0.5F;
    for (std::size_t index = 0; index < active_count; ++index) {
      const OperationChordPlan& plan = plans[active[index]];
      const int chord_first = std::max(window_first, plan.bounds.x0);
      const int chord_last = std::min(window_last, plan.bounds.x1 - 1);
      if (chord_last < chord_first) {
        continue;
      }
      slice.work_px += static_cast<std::size_t>(chord_last - chord_first + 1);
      int newly_finalized = 0;
      if (plan.constant) {
        newly_finalized =
            paint_masked_const_row(plan.segment, plan.seed, plan.bounds, y, row, chord_first,
                                   chord_last, applied, surface, finalized_pixels);
      } else {
        const ScanSpan conservative = conservative_row_span(plan.seed, plan.bounds, pixel_y);
        const ScanSpan row_span{
            .first = std::max(conservative.first, chord_first),
            .last = std::min(conservative.last, chord_last),
        };
        if (row_span.empty()) {
          TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
          continue;
        }
        TINYDRAW_V2_CENSUS_ADD(rows_scanned, 1);
        TINYDRAW_V2_CENSUS_ADD(span_pixels,
                               static_cast<std::uint64_t>(row_span.last - row_span.first + 1));
        newly_finalized = paint_masked_tapered_row(plan.segment, y, row_span, applied, surface,
                                                   finalized_pixels);
      }
      if (newly_finalized != 0 && summary != nullptr) {
        summary->note_finalized(surface_row, newly_finalized);
      }
    }
    ++y;
  }
  slice.next_row = y;
  return true;
}

std::optional<AffectedTileResult> affected_tiles(const OperationAppend& operation, ZoomLevel zoom,
                                                 std::span<TileKey> output) {
  if (zoom == ZoomLevel::k25Percent || operation.samples.empty()) {
    return std::nullopt;
  }
  const auto world_bounds = operation_world_bounds(operation.samples);
  if (!world_bounds.has_value()) {
    return std::nullopt;
  }
  const PixelRect bounds = operation_level_bounds(*world_bounds, zoom);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return AffectedTileResult{};
  }
  const int first_column = bounds.x0 / kTileWidth;
  const int last_column = (bounds.x1 - 1) / kTileWidth;
  const int first_row = bounds.y0 / kTileHeight;
  const int last_row = (bounds.y1 - 1) / kTileHeight;
  const std::size_t required = static_cast<std::size_t>(last_column - first_column + 1) *
                               static_cast<std::size_t>(last_row - first_row + 1);
  std::size_t written = 0;
  for (int row = first_row; row <= last_row && written < output.size(); ++row) {
    for (int column = first_column; column <= last_column && written < output.size(); ++column) {
      output[written++] = {zoom, static_cast<std::uint16_t>(column),
                           static_cast<std::uint16_t>(row)};
    }
  }
  return AffectedTileResult{.required = required, .written = written};
}

}  // namespace tinydraw::vector_v2
