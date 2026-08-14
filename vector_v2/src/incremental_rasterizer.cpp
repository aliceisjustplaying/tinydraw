#include "tinydraw/vector_v2/incremental_rasterizer.h"

#include <algorithm>
#include <cmath>

#include "tinydraw/vector_v2/raster_census.h"

namespace tinydraw::vector_v2 {

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
RasterCensus g_raster_census{};
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
  const float scale = static_cast<float>(zoom_percent(zoom)) / 100.0F;
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
      .x0 = std::max(clip.x0, static_cast<int>(std::floor(minimum_x))),
      .y0 = std::max(clip.y0, static_cast<int>(std::floor(minimum_y))),
      .x1 = std::min(clip.x1, static_cast<int>(std::ceil(maximum_x))),
      .y1 = std::min(clip.y1, static_cast<int>(std::ceil(maximum_y))),
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

bool constrain_at_most(float origin, float delta, float limit, ParameterInterval& interval) {
  if (delta == 0.0F) {
    return origin <= limit;
  }
  const float crossing = (limit - origin) / delta;
  if (delta > 0.0F) {
    interval.last = std::min(interval.last, crossing);
  } else {
    interval.first = std::max(interval.first, crossing);
  }
  return !interval.empty();
}

bool constrain_at_least(float origin, float delta, float limit, ParameterInterval& interval) {
  return constrain_at_most(-origin, -delta, -limit, interval);
}

ScanSpan conservative_tapered_row_span(const Segment& segment, PixelRect bounds, float pixel_y) {
  // A covered pixel's projected center must be no farther than its interpolated
  // radius from this row. Both y-radius and y+radius are linear in the segment
  // parameter, so intersecting those two half-planes cheaply rejects most of
  // the old bounding-box scan without approximating the final predicate.
  constexpr float kRoundingMargin = 0.01F;
  const float radius_delta = segment.second.radius - segment.first.radius;
  ParameterInterval interval{};
  if (!constrain_at_most(segment.first.y - segment.first.radius, segment.delta_y - radius_delta,
                         pixel_y + kRoundingMargin, interval) ||
      !constrain_at_least(segment.first.y + segment.first.radius, segment.delta_y + radius_delta,
                          pixel_y - kRoundingMargin, interval)) {
    return {.first = bounds.x0, .last = bounds.x0 - 1};
  }
  interval.first = std::clamp(interval.first, 0.0F, 1.0F);
  interval.last = std::clamp(interval.last, 0.0F, 1.0F);
  if (interval.empty()) {
    return {.first = bounds.x0, .last = bounds.x0 - 1};
  }

  const float left_origin = segment.first.x - segment.first.radius;
  const float left_delta = segment.delta_x - radius_delta;
  const float right_origin = segment.first.x + segment.first.radius;
  const float right_delta = segment.delta_x + radius_delta;
  const float minimum_x =
      std::min(left_origin + interval.first * left_delta, left_origin + interval.last * left_delta);
  const float maximum_x = std::max(right_origin + interval.first * right_delta,
                                   right_origin + interval.last * right_delta);
  // Keep a whole-pixel guard around float edge arithmetic. covers_pixel remains
  // the sole authority inside this conservative interval.
  const int first =
      std::max(bounds.x0, static_cast<int>(std::floor(minimum_x - kRoundingMargin)) - 1);
  const int last =
      std::min(bounds.x1 - 1, static_cast<int>(std::ceil(maximum_x + kRoundingMargin)));
  return {.first = first, .last = last};
}

void paint_tapered_segment(const Segment& segment, PixelRect bounds, std::uint16_t color,
                           const RasterSurface& surface) {
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const float pixel_y = static_cast<float>(y) + 0.5F;
    const ScanSpan row_span = conservative_tapered_row_span(segment, bounds, pixel_y);
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
// pixel here is covered by construction, so no predicate runs.
void paint_covered_chunk_bits(std::size_t pixel, int count, std::uint16_t color,
                              const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  for (int offset = 0; offset < count; ++offset) {
    const std::size_t candidate = pixel + static_cast<std::size_t>(offset);
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << (candidate & 7U));
    if ((finalized[candidate >> 3U] & bit) != 0U) {
      TINYDRAW_V2_CENSUS_ADD(const_mask_skips, 1);
      continue;
    }
    surface.pixels[candidate] = color;
    finalize_pixel(finalized, candidate);
  }
}

// Writes one exactly-covered span in mask-byte chunks: fully-finalized
// chunks are skipped, fully-free chunks are filled whole, and mixed chunks
// fall back to per-bit writes. Every span pixel is covered by construction,
// so no predicate runs here.
void paint_masked_exact_span(int first_covered, int last_covered, std::size_t row,
                             std::uint16_t color, const RasterSurface& surface,
                             std::span<std::uint8_t> finalized) {
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
    } else {
      paint_covered_chunk_bits(pixel, in_byte, color, surface, finalized);
    }
    x += in_byte;
    pixel += static_cast<std::size_t>(in_byte);
  }
}

void paint_masked_constant_radius_segment(const Segment& segment, PixelRect bounds,
                                          std::uint16_t color, const RasterSurface& surface,
                                          std::span<std::uint8_t> finalized) {
  ScanSpan prior{.first = bounds.x0, .last = bounds.x0 - 1};
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const std::size_t row = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                            static_cast<std::size_t>(surface.stride);
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
    paint_masked_exact_span(first_covered, last_covered, row, color, surface, finalized);
  }
}

// Walks one conservative row span in mask-byte chunks. Fully-finalized
// chunks are skipped without touching the predicate; each unfinalized pixel
// still goes through covers_pixel, so per-pixel decisions are identical to
// probing every span pixel individually.
void paint_masked_tapered_row(const Segment& segment, int y, ScanSpan row_span, std::uint16_t color,
                              const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  const float pixel_y = static_cast<float>(y) + 0.5F;
  const std::size_t row = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                          static_cast<std::size_t>(surface.stride);
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
    }
    x += in_byte;
    pixel += static_cast<std::size_t>(in_byte);
  }
}

// Newest-first masked tapered painter. Skips fully-finalized rows before the
// conservative span math and fully-finalized mask bytes inside the span; a
// skipped pixel is finalized by definition, so per-pixel decisions are
// identical to probing every span pixel. covers_pixel remains the sole
// geometry authority for every unfinalized pixel.
void paint_masked_tapered_segment(const Segment& segment, PixelRect bounds, std::uint16_t color,
                                  const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const std::size_t row = static_cast<std::size_t>(y - surface.level_bounds.y0) *
                            static_cast<std::size_t>(surface.stride);
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
    const ScanSpan row_span = conservative_tapered_row_span(segment, bounds, pixel_y);
    if (row_span.empty()) {
      TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
      continue;
    }
    TINYDRAW_V2_CENSUS_ADD(span_pixels,
                           static_cast<std::uint64_t>(row_span.last - row_span.first + 1));
    paint_masked_tapered_row(segment, y, row_span, color, surface, finalized);
  }
}

void paint_masked_segment(const Sample& start, const Sample& end, std::uint16_t color,
                          const RasterSurface& surface, std::span<std::uint8_t> finalized) {
  const Segment segment = make_segment(start, end);
  const PixelRect bounds = segment_bounds(segment, surface.level_bounds);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
    return;
  }
  if (start.radius == end.radius) {
    paint_masked_constant_radius_segment(segment, bounds, color, surface, finalized);
  } else {
    paint_masked_tapered_segment(segment, bounds, color, surface, finalized);
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

}  // namespace

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
                                      std::span<std::uint8_t> finalized_pixels) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  if (!valid_surface(surface) || finalized_pixels.size() < required_mask_bytes) {
    return false;
  }
  paint_masked_segment(scaled_sample(segment.first, surface.zoom),
                       scaled_sample(segment.second, surface.zoom),
                       segment.tool == OperationTool::kEraser ? kBackground : segment.color,
                       surface, finalized_pixels);
  return true;
}

bool apply_incremental_operation(const OperationAppend& operation, const RasterSurface& surface) {
  if (!valid_surface(surface) || operation.samples.empty()) {
    return false;
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  if (operation.samples.size() == 1U) {
    const Sample sample = scaled_sample(operation.samples.front(), surface.zoom);
    paint_bounded_segment(sample, sample, color, surface);
    return true;
  }
  for (std::size_t index = 1; index < operation.samples.size(); ++index) {
    paint_bounded_segment(scaled_sample(operation.samples[index - 1U], surface.zoom),
                          scaled_sample(operation.samples[index], surface.zoom), color, surface);
  }
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
