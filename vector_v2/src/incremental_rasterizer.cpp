#include "tinydraw/vector_v2/incremental_rasterizer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS) && !defined(__XTENSA__)
#include <chrono>
#endif
#include <cmath>
#include <cstring>

#include "incremental_rasterizer_internal.h"
#include "tinydraw/vector_v2/raster_census.h"

namespace tinydraw::vector_v2 {

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
RasterCensus g_raster_census{};
#if !defined(__XTENSA__)
std::uint32_t raster_census_now() {
  return static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif
#endif

namespace raster_internal {

// A center on a pixel-grid intersection is sqrt(0.5) pixels from the nearest
// pixel center. Keep thin projected operations above that distance so stroke
// presence survives every committed zoom.
constexpr float kMinimumScreenRadius = 0.75F;

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

static void make_segment_in_place(const Sample& first, const Sample& second, Segment& segment) {
  const float delta_x = second.x - first.x;
  const float delta_y = second.y - first.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  segment.first.x = first.x;
  segment.first.y = first.y;
  segment.first.radius = first.radius;
  segment.second.x = second.x;
  segment.second.y = second.y;
  segment.second.radius = second.radius;
  segment.delta_x = delta_x;
  segment.delta_y = delta_y;
  segment.inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F;
}

static Sample scaled_sample(CompactOperationSample sample, float scale) {
  // 1/16 is an exact binary fraction, so this stays bit-identical to a
  // division by kSampleUnitsPerWorldUnit without the libcall.
  return {
      .x = static_cast<float>(sample.x_quarter) * 0.0625F * scale,
      .y = static_cast<float>(sample.y_quarter) * 0.0625F * scale,
      .radius =
          std::max(static_cast<float>(sample.radius_256) / 256.0F * scale, kMinimumScreenRadius),
  };
}

Sample scaled_sample(CompactOperationSample sample, ZoomLevel zoom) {
  return scaled_sample(sample, zoom_scale(zoom));
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
  const std::size_t required =
      static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(surface.stride) +
      static_cast<std::size_t>(width);
  return surface.pixels.size() >= required;
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

int find_first_covered(const PixelCoverageRow& coverage, PixelRect bounds, ScanSpan prior) {
  int x = prior.empty() ? bounds.x0 : std::clamp(prior.first, bounds.x0, bounds.x1 - 1);
  while (x > bounds.x0 && covers_pixel(coverage, pixel_center(x - 1))) {
    --x;
  }
  while (x < bounds.x1 && !covers_pixel(coverage, pixel_center(x))) {
    ++x;
  }
  return x;
}

int find_last_covered(const PixelCoverageRow& coverage, PixelRect bounds, ScanSpan prior,
                      int first_covered) {
  int x = prior.empty() ? bounds.x1 - 1 : std::clamp(prior.last, first_covered, bounds.x1 - 1);
  while (x + 1 < bounds.x1 && covers_pixel(coverage, pixel_center(x + 1))) {
    ++x;
  }
  while (x > first_covered && !covers_pixel(coverage, pixel_center(x))) {
    --x;
  }
  return x;
}

void paint_constant_radius_segment(const Segment& segment, PixelRect bounds, std::uint16_t color,
                                   const RasterSurface& surface) {
  ScanSpan prior{.first = bounds.x0, .last = bounds.x0 - 1};
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const float pixel_y = static_cast<float>(y) + 0.5F;
    const PixelCoverageRow coverage = make_pixel_coverage_row(segment, pixel_y);
    const int first_covered = find_first_covered(coverage, bounds, prior);
    if (first_covered == bounds.x1) {
      prior.last = prior.first - 1;
      continue;
    }
    const int last_covered = find_last_covered(coverage, bounds, prior, first_covered);
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
static void make_tapered_span_table(const Segment& segment, TaperedSpanTable& table) {
  const float radius_delta = segment.second.radius - segment.first.radius;
  table.origin_low = segment.first.y - segment.first.radius;
  table.delta_low = segment.delta_y - radius_delta;
  table.origin_high = segment.first.y + segment.first.radius;
  table.delta_high = segment.delta_y + radius_delta;
  table.left_origin = segment.first.x - segment.first.radius;
  table.left_delta = segment.delta_x - radius_delta;
  table.right_origin = segment.first.x + segment.first.radius;
  table.right_delta = segment.delta_x + radius_delta;
  table.inverse_low = table.delta_low != 0.0F ? 1.0F / table.delta_low : 0.0F;
  table.inverse_high = table.delta_high != 0.0F ? 1.0F / table.delta_high : 0.0F;
}

TaperedSpanTable make_tapered_span_table(const Segment& segment) {
  TaperedSpanTable table;
  make_tapered_span_table(segment, table);
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
    const PixelCoverageRow coverage = make_pixel_coverage_row(segment, pixel_y);
    for (int x = row_span.first; x <= row_span.last; ++x) {
      if (!covers_pixel(coverage, pixel_center(x))) {
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

// The finalized mask may begin at any byte phase in internal RAM or PSRAM.
// Once a scan reaches a four-byte boundary, an aligned native word load is
// safe on ESP32-S3 and skips four saturated bytes at once.  The host fallback
// preserves the byte array's effective type for strict-aliasing sanitizers.
[[nodiscard, gnu::always_inline]] inline std::uint32_t load_aligned_mask_word(
    const std::uint8_t* bytes) {
#if defined(__XTENSA__)
  std::uint32_t word;
  asm("l32i %0, %1, 0" : "=r"(word) : "r"(bytes) : "memory");
  return word;
#else
  std::uint32_t word;
  std::memcpy(&word, bytes, sizeof(word));
  return word;
#endif
}

[[nodiscard]] const std::uint8_t* first_non_full_mask_byte(const std::uint8_t* first,
                                                           const std::uint8_t* last) {
  while (first != last && (reinterpret_cast<std::uintptr_t>(first) & 3U) != 0U) {
    if (*first != 0xFFU) {
      return first;
    }
    ++first;
  }
  while (static_cast<std::size_t>(last - first) >= sizeof(std::uint32_t)) {
    if (load_aligned_mask_word(first) != UINT32_MAX) {
      break;
    }
    first += sizeof(std::uint32_t);
  }
  while (first != last && *first == 0xFFU) {
    ++first;
  }
  return first;
}

[[nodiscard]] const std::uint8_t* last_non_full_mask_byte(const std::uint8_t* first,
                                                          const std::uint8_t* last) {
  while (first != last && (reinterpret_cast<std::uintptr_t>(last) & 3U) != 0U) {
    --last;
    if (*last != 0xFFU) {
      return last;
    }
  }
  while (static_cast<std::size_t>(last - first) >= sizeof(std::uint32_t)) {
    const std::uint8_t* word = last - sizeof(std::uint32_t);
    if (load_aligned_mask_word(word) != UINT32_MAX) {
      break;
    }
    last = word;
  }
  while (first != last) {
    --last;
    if (*last != 0xFFU) {
      return last;
    }
  }
  return nullptr;
}

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
  if (first_byte == last_byte) {
    const std::uint8_t unset =
        static_cast<std::uint8_t>(~finalized[first_byte]) & first_mask & last_mask;
    if (unset == 0U) {
      return std::nullopt;
    }
    return UnsetWindow{
        .first = (first_byte << 3U) + static_cast<std::size_t>(std::countr_zero(unset)),
        .last = (first_byte << 3U) + (7U - static_cast<std::size_t>(std::countl_zero(unset))),
    };
  }

  std::size_t byte = first_byte;
  std::uint8_t unset = static_cast<std::uint8_t>(~finalized[first_byte]) & first_mask;
  if (unset == 0U) {
    const std::uint8_t* const begin = finalized.data() + first_byte + 1U;
    const std::uint8_t* const end = finalized.data() + last_byte;
    const std::uint8_t* const found = first_non_full_mask_byte(begin, end);
    if (found != end) {
      byte = static_cast<std::size_t>(found - finalized.data());
      unset = static_cast<std::uint8_t>(~*found);
    } else {
      byte = last_byte;
      unset = static_cast<std::uint8_t>(~finalized[last_byte]) & last_mask;
    }
  }
  if (unset == 0U) {
    return std::nullopt;
  }

  UnsetWindow window{
      .first = (byte << 3U) + static_cast<std::size_t>(std::countr_zero(unset)),
  };
  std::size_t tail_byte = last_byte;
  unset = static_cast<std::uint8_t>(~finalized[last_byte]) & last_mask;
  if (unset == 0U) {
    const std::uint8_t* const begin = finalized.data() + first_byte + 1U;
    const std::uint8_t* const end = finalized.data() + last_byte;
    const std::uint8_t* const found = last_non_full_mask_byte(begin, end);
    if (found != nullptr) {
      tail_byte = static_cast<std::size_t>(found - finalized.data());
      unset = static_cast<std::uint8_t>(~*found);
    } else {
      tail_byte = first_byte;
      unset = static_cast<std::uint8_t>(~finalized[first_byte]) & first_mask;
    }
  }
  assert(unset != 0U);
  window.last = (tail_byte << 3U) + (7U - static_cast<std::size_t>(std::countl_zero(unset)));
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
  const std::uint8_t* const end = finalized.data() + last_byte;
  return first_non_full_mask_byte(finalized.data() + first_byte + 1U, end) == end;
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

// GCC 15 emits an eight-iteration halfword loop for fill_n here, even though
// a free mask byte proves that all eight RGB565 stores are unconditional.
// Every uint16_t pointer has one of two word phases. The common aligned path
// takes four word stores; the other phase peels both outside halfwords around
// three aligned words. Neither path reads or writes outside the proven-free
// chunk.
[[gnu::always_inline]] inline void paint_free_mask_byte(std::uint16_t* pixels,
                                                        std::uint16_t color) {
#if defined(__XTENSA__)
  std::uint32_t replicated;
  std::uint16_t* interior;
  asm volatile(
      "slli %0, %3, 16\n"
      "or %0, %0, %3\n"
      "bbsi %2, 1, 1f\n"
      "s32i %0, %2, 0\n"
      "s32i %0, %2, 4\n"
      "s32i %0, %2, 8\n"
      "s32i %0, %2, 12\n"
      "j 2f\n"
      "1:\n"
      "s16i %3, %2, 0\n"
      "addi %1, %2, 2\n"
      "s32i %0, %1, 0\n"
      "s32i %0, %1, 4\n"
      "s32i %0, %1, 8\n"
      "s16i %3, %2, 14\n"
      "2:"
      : "=&r"(replicated), "=&r"(interior)
      : "r"(pixels), "r"(color)
      : "memory");
#else
  std::fill_n(pixels, 8U, color);
#endif
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
      auto* const output = surface.pixels.data() + pixel;
      if (in_byte == 8) {
        paint_free_mask_byte(output, color);
      } else {
        std::fill_n(output, static_cast<std::size_t>(in_byte), color);
      }
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
  const PixelCoverageRow row = make_pixel_coverage_row(segment, pixel_y);
  int x = first;
  while (x <= last &&
         (TINYDRAW_V2_CENSUS_ADD(const_search_calls, 1), !covers_pixel(row, pixel_center(x)))) {
    ++x;
  }
  return x;
}

// Last covered pixel in [first, last]; the caller guarantees first is covered
// and no relevant covered pixel exists right of last.
int last_covered_at_or_before(const Segment& segment, int first, int last, float pixel_y) {
  const PixelCoverageRow row = make_pixel_coverage_row(segment, pixel_y);
  int x = last;
  while (x > first && (TINYDRAW_V2_CENSUS_ADD(const_search_calls, 1),
                       TINYDRAW_V2_CENSUS_ADD(const_search_last_calls, 1),
                       !covers_pixel(row, pixel_center(x)))) {
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
void make_row_seed(const Segment& segment, RowSeed& seed) {
  const float length_squared =
      segment.delta_x * segment.delta_x + segment.delta_y * segment.delta_y;
  const float radius = std::max(segment.first.radius, segment.second.radius);
  if (length_squared > radius * radius) {
    seed.circular = false;
    seed.center_x = 0.0F;
    seed.center_y = 0.0F;
    seed.radius = 0.0F;
    make_tapered_span_table(segment, seed.table);
    return;
  }
  // The padded radius keeps the containment argument conservative against
  // float rounding in the length and midpoint arithmetic.
  const float half_length = 0.5F * conservative_sqrt_upper(length_squared);
  seed.circular = true;
  seed.center_x = segment.first.x + 0.5F * segment.delta_x;
  seed.center_y = segment.first.y + 0.5F * segment.delta_y;
  seed.radius = radius + half_length + 0.01F;
  seed.table = {};
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
int paint_masked_const_row(const Segment& segment, const RowSeed& seed, PixelRect bounds,
                           const MaskedRowTarget& target) {
  const float pixel_y = static_cast<float>(target.y) + 0.5F;
  const ScanSpan conservative = conservative_row_span(seed, bounds, pixel_y);
  const int search_first = std::max(conservative.first, target.window_first);
  const int search_last = std::min(conservative.last, target.window_last);
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
  return paint_masked_exact_span(first_covered, last_covered, target.row, target.color,
                                 target.surface, target.finalized);
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
    const PixelCoverageRow coverage = make_pixel_coverage_row(segment, pixel_y);
    const int first_covered = find_first_covered(coverage, bounds, prior);
    if (first_covered == bounds.x1) {
      prior.last = prior.first - 1;
      continue;
    }
    const int last_covered = find_last_covered(coverage, bounds, prior, first_covered);
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
  const PixelCoverageRow coverage = make_pixel_coverage_row(segment, pixel_y);
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
      if (!covers_pixel(coverage, pixel_center(x + offset))) {
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

Sample midpoint(const Sample& first, const Sample& second) {
  return {
      .x = (first.x + second.x) * 0.5F,
      .y = (first.y + second.y) * 0.5F,
      .radius = (first.radius + second.radius) * 0.5F,
  };
}

static void make_curved_unit(const Sample& start, const Sample& control, const Sample& current,
                             bool last, CurveUnit& unit) {
  const Sample end = midpoint(control, current);
  const Sample curve_midpoint = midpoint(midpoint(start, control), midpoint(control, end));
  make_segment_in_place(start, curve_midpoint, unit.segments[0]);
  make_segment_in_place(curve_midpoint, end, unit.segments[1]);
  unit.count = 2U;
  if (last) {
    make_segment_in_place(end, current, unit.segments[2]);
    unit.count = 3U;
  }
}

void initialize_curve_sample_window(std::span<const CompactOperationSample> samples,
                                    std::size_t endpoint, ZoomLevel zoom, CurveSampleWindow& window,
                                    CurveUnit& unit) {
  window.scale = zoom_scale(zoom);
  window.prior = scaled_sample(samples[endpoint - 2U], window.scale);
  window.control = scaled_sample(samples[endpoint - 1U], window.scale);
  window.current = scaled_sample(samples[endpoint], window.scale);
  const Sample start = endpoint == 2U ? window.prior : midpoint(window.prior, window.control);
  make_curved_unit(start, window.control, window.current, endpoint + 1U == samples.size(), unit);
}

void advance_curve_sample_window(CurveSampleWindow& window, CompactOperationSample next, bool last,
                                 CurveUnit& unit) {
  window.prior = window.control;
  window.control = window.current;
  window.current = scaled_sample(next, window.scale);
  // The previous unit already carries this exact midpoint as its final
  // chord endpoint. Reuse its rounded bits instead of recomputing it.
  make_curved_unit(unit.segments[1].second, window.control, window.current, last, unit);
}

void retreat_curve_sample_window(CurveSampleWindow& window, CompactOperationSample prior,
                                 bool first, CurveUnit& unit) {
  window.current = window.control;
  window.control = window.prior;
  window.prior = scaled_sample(prior, window.scale);
  const Sample start = first ? window.prior : midpoint(window.prior, window.control);
  make_curved_unit(start, window.control, window.current, false, unit);
}

bool curved_unit(std::span<const CompactOperationSample> samples, std::size_t endpoint,
                 ZoomLevel zoom, CurveUnit& unit) {
  if (samples.empty()) {
    return false;
  }
  if (samples.size() <= 2U) {
    if (endpoint + 1U != samples.size()) {
      return false;
    }
    const Sample first = scaled_sample(samples.front(), zoom);
    const Sample second = scaled_sample(samples.back(), zoom);
    make_segment_in_place(first, second, unit.segments[0]);
    unit.count = 1U;
    return true;
  }
  if (endpoint < 2U || endpoint >= samples.size()) {
    return false;
  }
  CurveSampleWindow window;
  initialize_curve_sample_window(samples, endpoint, zoom, window, unit);
  return true;
}

// Warm-start row sweep over one curve unit's chords for the interactive
// append path. A unit's chords share one color and overlap at their joints,
// so under the finalized mask the union may paint in any order; sweeping
// them together pays one mask-range scan per row instead of one per chord,
// while each chord keeps the historical warm-start search that fresh append
// masks reward (no skipped rows, so warm chains never break).
struct WarmChord {
  Segment segment{};
  TaperedSpanTable table{};
  PixelRect bounds{};
  ScanSpan prior{};
  bool constant = false;
};

std::size_t prepare_warm_chords(const CurveUnit& unit, const RasterSurface& surface,
                                std::span<WarmChord> chords, PixelRect& union_bounds) {
  std::size_t chord_count = 0;
  for (std::size_t step = 0; step < unit.count; ++step) {
    const Segment& segment = unit.segments[step];
    const PixelRect bounds = segment_bounds(segment, surface.level_bounds);
    if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
      continue;
    }
    const bool constant = segment.first.radius == segment.second.radius;
    WarmChord& chord = chords[chord_count++];
    copy_segment(segment, chord.segment);
    if (constant) {
      chord.table = {};
    } else {
      make_tapered_span_table(segment, chord.table);
    }
    chord.bounds = bounds;
    chord.prior = {.first = bounds.x0, .last = bounds.x0 - 1};
    chord.constant = constant;
    union_bounds.x0 = std::min(union_bounds.x0, bounds.x0);
    union_bounds.y0 = std::min(union_bounds.y0, bounds.y0);
    union_bounds.x1 = std::max(union_bounds.x1, bounds.x1);
    union_bounds.y1 = std::max(union_bounds.y1, bounds.y1);
  }
  return chord_count;
}

void reset_warm_spans(std::span<WarmChord> chords) {
  for (WarmChord& chord : chords) {
    chord.prior = {.first = chord.bounds.x0, .last = chord.bounds.x0 - 1};
  }
}

int paint_warm_chord_row(WarmChord& chord, int y, std::size_t row, std::uint16_t color,
                         const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  if (y < chord.bounds.y0 || y >= chord.bounds.y1) {
    return 0;
  }
  const float pixel_y = static_cast<float>(y) + 0.5F;
  if (chord.constant) {
    const PixelCoverageRow coverage = make_pixel_coverage_row(chord.segment, pixel_y);
    const int first_covered = find_first_covered(coverage, chord.bounds, chord.prior);
    if (first_covered == chord.bounds.x1) {
      chord.prior.last = chord.prior.first - 1;
      return 0;
    }
    const int last_covered = find_last_covered(coverage, chord.bounds, chord.prior, first_covered);
    chord.prior = {.first = first_covered, .last = last_covered};
    TINYDRAW_V2_CENSUS_ADD(const_span_pixels,
                           static_cast<std::uint64_t>(last_covered - first_covered + 1));
    return paint_masked_exact_span(first_covered, last_covered, row, color, surface, finalized);
  }

  TINYDRAW_V2_CENSUS_ADD(rows_scanned, 1);
  const ScanSpan row_span = conservative_tapered_row_span(chord.table, chord.bounds, pixel_y);
  if (row_span.empty()) {
    TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
    return 0;
  }
  TINYDRAW_V2_CENSUS_ADD(span_pixels,
                         static_cast<std::uint64_t>(row_span.last - row_span.first + 1));
  return paint_masked_tapered_row(chord.segment, y, row_span, color, surface, finalized);
}

void paint_masked_curve_unit_warm(const CurveUnit& unit, std::uint16_t color,
                                  const RasterSurface& surface, std::span<std::uint8_t> finalized,
                                  MaskedRowSummary* summary) {
  std::array<WarmChord, 3> chords{};
  PixelRect union_bounds{surface.level_bounds.x1, surface.level_bounds.y1, surface.level_bounds.x0,
                         surface.level_bounds.y0};
  const std::size_t chord_count = prepare_warm_chords(unit, surface, chords, union_bounds);
  if (chord_count == 0U) {
    return;
  }
  const auto active_chords = std::span(chords).first(chord_count);
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
      reset_warm_spans(active_chords);
      continue;
    }
    for (WarmChord& chord : active_chords) {
      const int newly_finalized = paint_warm_chord_row(chord, y, row, color, surface, finalized);
      if (newly_finalized != 0 && summary != nullptr) {
        summary->note_finalized(surface_row, newly_finalized);
      }
    }
  }
}

}  // namespace raster_internal

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
  assert(world_bounds.x0 >= 0 && world_bounds.y0 >= 0 && world_bounds.x0 <= world_bounds.x1 &&
         world_bounds.y0 <= world_bounds.y1 && world_bounds.x1 <= kWorldWidth &&
         world_bounds.y1 <= kWorldHeight);
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return {
          .x0 = world_bounds.x0 >> 2,
          .y0 = world_bounds.y0 >> 2,
          .x1 = std::min(kWorldWidth >> 2, (world_bounds.x1 + 3) >> 2),
          .y1 = std::min(kWorldHeight >> 2, (world_bounds.y1 + 3) >> 2),
      };
    case ZoomLevel::k50Percent:
      return {
          .x0 = world_bounds.x0 >> 1,
          .y0 = world_bounds.y0 >> 1,
          .x1 = std::min(kWorldWidth >> 1, (world_bounds.x1 + 1) >> 1),
          .y1 = std::min(kWorldHeight >> 1, (world_bounds.y1 + 1) >> 1),
      };
    case ZoomLevel::k100Percent:
      return {
          .x0 = world_bounds.x0,
          .y0 = world_bounds.y0,
          .x1 = std::min(kWorldWidth, world_bounds.x1),
          .y1 = std::min(kWorldHeight, world_bounds.y1),
      };
    case ZoomLevel::k200Percent:
      return {
          .x0 = world_bounds.x0 * 2,
          .y0 = world_bounds.y0 * 2,
          .x1 = std::min(kWorldWidth * 2, world_bounds.x1 * 2),
          .y1 = std::min(kWorldHeight * 2, world_bounds.y1 * 2),
      };
    case ZoomLevel::k400Percent:
      return {
          .x0 = world_bounds.x0 * 4,
          .y0 = world_bounds.y0 * 4,
          .x1 = std::min(kWorldWidth * 4, world_bounds.x1 * 4),
          .y1 = std::min(kWorldHeight * 4, world_bounds.y1 * 4),
      };
  }
  return {};
}

}  // namespace tinydraw::vector_v2
