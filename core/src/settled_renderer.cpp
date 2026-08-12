#include "tinydraw/graphics/settled_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tinydraw {
namespace {

constexpr float kEdgePixels = 1.0F;

struct LocalRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;  // exclusive
  int y1 = 0;  // exclusive

  [[nodiscard]] bool empty() const { return x0 >= x1 || y0 >= y1; }

  void include(const LocalRect& other) {
    if (other.empty()) {
      return;
    }
    if (empty()) {
      *this = other;
      return;
    }
    x0 = std::min(x0, other.x0);
    y0 = std::min(y0, other.y0);
    x1 = std::max(x1, other.x1);
    y1 = std::max(y1, other.y1);
  }
};

struct ScreenSample {
  float x = 0.0F;
  float y = 0.0F;
  float radius = 0.0F;
};

bool cancelled(const SettledRenderOptions& options) {
  return options.cancelled != nullptr && options.cancelled(options.cancellation_context);
}

std::uint64_t clock_us(const SettledRenderOptions& options) {
  return options.clock_us != nullptr ? options.clock_us(options.clock_context) : 0U;
}

// Unions the analytic coverage of one variable-radius capsule into the
// region-local scratch buffer and returns the touched local bounds.
LocalRect rasterize_capsule(ScreenSample from, ScreenSample to, Rect region, int region_width,
                            std::span<std::uint8_t> scratch, const SettledRenderOptions& options,
                            bool& aborted) {
  const float maximum_radius = std::max(from.radius, to.radius);
  const float pad = maximum_radius + kEdgePixels;
  const float minimum_x = std::clamp(std::min(from.x, to.x) - pad, static_cast<float>(region.x0),
                                     static_cast<float>(region.x1));
  const float minimum_y = std::clamp(std::min(from.y, to.y) - pad, static_cast<float>(region.y0),
                                     static_cast<float>(region.y1));
  const float maximum_x = std::clamp(std::max(from.x, to.x) + pad, static_cast<float>(region.x0),
                                     static_cast<float>(region.x1));
  const float maximum_y = std::clamp(std::max(from.y, to.y) + pad, static_cast<float>(region.y0),
                                     static_cast<float>(region.y1));
  const LocalRect bounds{
      .x0 = static_cast<int>(std::floor(minimum_x)) - region.x0,
      .y0 = static_cast<int>(std::floor(minimum_y)) - region.y0,
      .x1 = std::min(static_cast<int>(std::ceil(maximum_x)) + 1, region.x1) - region.x0,
      .y1 = std::min(static_cast<int>(std::ceil(maximum_y)) + 1, region.y1) - region.y0,
  };
  if (bounds.empty()) {
    return {};
  }

  const float delta_x = to.x - from.x;
  const float delta_y = to.y - from.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  const float inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F;
  const float delta_radius = to.radius - from.radius;
  const float t_step = delta_x * inverse_length_squared;
  const float half_edge = 0.5F * kEdgePixels;

  for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
    if ((local_y & 7) == 0 && options.cancelled_frequently != nullptr &&
        options.cancelled_frequently(options.cancellation_context)) {
      aborted = true;
      return {};
    }
    const float pixel_y = static_cast<float>(region.y0 + local_y) + 0.5F;
    const float to_pixel_y = pixel_y - from.y;
    const float first_to_pixel_x = static_cast<float>(region.x0 + bounds.x0) + 0.5F - from.x;
    float t_unclamped =
        (first_to_pixel_x * delta_x + to_pixel_y * delta_y) * inverse_length_squared;
    std::uint8_t* row = scratch.data() + static_cast<std::ptrdiff_t>(local_y * region_width);
    for (int local_x = bounds.x0; local_x < bounds.x1; ++local_x) {
      const float t = std::clamp(t_unclamped, 0.0F, 1.0F);
      const float to_pixel_x = static_cast<float>(region.x0 + local_x) + 0.5F - from.x;
      const float nearest_x = to_pixel_x - t * delta_x;
      const float nearest_y = to_pixel_y - t * delta_y;
      const float distance_squared = nearest_x * nearest_x + nearest_y * nearest_y;
      const float radius = from.radius + t * delta_radius;
      const float outer = radius + half_edge;
      const float outer_squared = outer * outer;
      if (distance_squared < outer_squared) {
        const float inner = std::max(radius - half_edge, 0.0F);
        std::uint8_t alpha = 255U;
        const float inner_squared = inner * inner;
        if (inner == 0.0F || distance_squared > inner_squared) {
          // The exact linear-distance ramp requires a software-emulated sqrt
          // on ESP32-S3. Squared-distance interpolation preserves the same
          // fully covered and empty boundaries with a slightly different edge.
          const float denominator = outer_squared - inner_squared;
          const float coverage =
              std::clamp((outer_squared - distance_squared) / denominator, 0.0F, 1.0F);
          alpha = static_cast<std::uint8_t>(coverage * 255.0F + 0.5F);
        }
        row[local_x] = std::max(row[local_x], alpha);
      }
      t_unclamped += t_step;
    }
  }
  return bounds;
}

bool composite_and_clear(std::span<std::uint16_t> destination, Rect region, int region_width,
                         std::span<std::uint8_t> scratch, LocalRect dirty, std::uint16_t color,
                         const SettledRenderOptions& options) {
  const std::uint32_t source_red = (color >> 11U) & 0x1FU;
  const std::uint32_t source_green = (color >> 5U) & 0x3FU;
  const std::uint32_t source_blue = color & 0x1FU;
  for (int local_y = dirty.y0; local_y < dirty.y1; ++local_y) {
    if ((local_y & 7) == 0 && options.cancelled_frequently != nullptr &&
        options.cancelled_frequently(options.cancellation_context)) {
      return false;
    }
    std::uint8_t* coverage_row =
        scratch.data() + static_cast<std::ptrdiff_t>(local_y * region_width);
    std::uint16_t* destination_row =
        destination.data() +
        static_cast<std::ptrdiff_t>((region.y0 + local_y) * kCanvasWidth + region.x0);
    for (int local_x = dirty.x0; local_x < dirty.x1; ++local_x) {
      const std::uint8_t alpha = coverage_row[local_x];
      if (alpha == 0U) {
        continue;
      }
      coverage_row[local_x] = 0U;
      if (alpha == 255U) {
        destination_row[local_x] = color;
        continue;
      }
      const std::uint16_t current = destination_row[local_x];
      const std::uint32_t inverse = 255U - alpha;
      const auto blend = [&](std::uint32_t existing, std::uint32_t source) {
        return (existing * inverse + source * alpha + 127U) / 255U;
      };
      const std::uint32_t red = blend((current >> 11U) & 0x1FU, source_red);
      const std::uint32_t green = blend((current >> 5U) & 0x3FU, source_green);
      const std::uint32_t blue = blend(current & 0x1FU, source_blue);
      destination_row[local_x] = static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
    }
  }
  return true;
}

}  // namespace

SettledRenderStats settled_render_region(const VectorDocument& document, Camera camera,
                                         std::span<std::uint16_t> destination, Rect region,
                                         std::span<std::uint8_t> scratch,
                                         const SettledRenderOptions& options) {
  SettledRenderStats stats;
  const bool region_valid = region.x0 >= 0 && region.y0 >= 0 && region.x1 <= kCanvasWidth &&
                            region.y1 <= kCanvasHeight && region.x0 <= region.x1 &&
                            region.y0 <= region.y1;
  const std::size_t region_area = region_valid ? static_cast<std::size_t>(region.x1 - region.x0) *
                                                     static_cast<std::size_t>(region.y1 - region.y0)
                                               : 0U;
  if (!region_valid ||
      destination.size() <
          static_cast<std::size_t>(kCanvasWidth) * static_cast<std::size_t>(kCanvasHeight) ||
      !camera_valid(camera) || scratch.size() < region_area ||
      !std::isfinite(options.minimum_screen_radius) || options.minimum_screen_radius < 0.0F) {
    stats.complete = false;
    return stats;
  }
  if (region.x0 == region.x1 || region.y0 == region.y1) {
    return stats;
  }

  const int region_width = region.x1 - region.x0;
  std::uint64_t phase_started = clock_us(options);
  for (int y = region.y0; y < region.y1; ++y) {
    std::fill_n(destination.begin() + static_cast<std::ptrdiff_t>(y * kCanvasWidth + region.x0),
                static_cast<std::size_t>(region_width), options.background);
  }
  std::fill_n(scratch.begin(), region_area, std::uint8_t{0});
  if (options.clock_us != nullptr) {
    const std::uint64_t now = clock_us(options);
    stats.clear_us = now - phase_started;
    phase_started = now;
  }

  const double inverse_zoom = 1.0 / static_cast<double>(camera.zoom);
  const double world_halo =
      (static_cast<double>(options.minimum_screen_radius) + kEdgePixels) * inverse_zoom;
  const RectF viewport{
      .x0 = static_cast<float>(camera.x + region.x0 * inverse_zoom - world_halo),
      .y0 = static_cast<float>(camera.y + region.y0 * inverse_zoom - world_halo),
      .x1 = static_cast<float>(camera.x + region.x1 * inverse_zoom + world_halo),
      .y1 = static_cast<float>(camera.y + region.y1 * inverse_zoom + world_halo),
  };

  const auto strokes = document.strokes();
  const std::size_t required_candidate_words = (strokes.size() + 63U) / 64U;
  if (!options.candidate_strokes.empty() &&
      options.candidate_strokes.size() < required_candidate_words) {
    stats.complete = false;
    return stats;
  }
  const bool lod_maps_absent = options.lod_first_sample.empty() && options.lod_sample_count.empty();
  bool lod_maps_valid = lod_maps_absent || (options.lod_first_sample.size() >= strokes.size() &&
                                            options.lod_sample_count.size() >= strokes.size());
  if (lod_maps_valid && !lod_maps_absent) {
    for (std::size_t stroke_index = 0; stroke_index < strokes.size(); ++stroke_index) {
      const std::size_t first = options.lod_first_sample[stroke_index];
      const std::size_t count = options.lod_sample_count[stroke_index];
      if (count == 0U || first > options.lod_samples.size() ||
          count > options.lod_samples.size() - first) {
        lod_maps_valid = false;
        break;
      }
      const auto samples = options.lod_samples.subspan(first, count);
      if (std::any_of(samples.begin(), samples.end(), [](const StrokeSample& sample) {
            return !std::isfinite(sample.x) || !std::isfinite(sample.y) ||
                   !std::isfinite(sample.radius) || sample.radius < 0.0F;
          })) {
        lod_maps_valid = false;
        break;
      }
    }
  }
  for (std::size_t stroke_index = 0; stroke_index < strokes.size(); ++stroke_index) {
    if (!options.candidate_strokes.empty()) {
      const std::size_t word = stroke_index / 64U;
      const std::uint64_t bit = std::uint64_t{1} << (stroke_index % 64U);
      if (word >= options.candidate_strokes.size() ||
          (options.candidate_strokes[word] & bit) == 0U) {
        continue;
      }
    }
    if (cancelled(options)) {
      stats.complete = false;
      return stats;
    }
    const VectorStroke& stroke = strokes[stroke_index];
    ++stats.strokes_tested;
    if (!rects_intersect(stroke.bounds, viewport)) {
      continue;
    }
    std::span<const StrokeSample> samples = document.samples(stroke);
    if (lod_maps_valid && !lod_maps_absent) {
      const std::size_t first = options.lod_first_sample[stroke_index];
      const std::size_t count = options.lod_sample_count[stroke_index];
      if (first <= options.lod_samples.size() && count <= options.lod_samples.size() - first) {
        samples = options.lod_samples.subspan(first, count);
      }
    }
    if (samples.empty()) {
      continue;
    }

    const std::uint16_t color =
        stroke.tool == VectorTool::kEraser ? options.background : stroke.color;
    const auto project = [&](StrokeSample sample) {
      const Point position = camera_project(camera, sample.x, sample.y);
      return ScreenSample{
          .x = position.x,
          .y = position.y,
          .radius = camera_project_radius(camera, sample.radius, options.minimum_screen_radius),
      };
    };

    LocalRect stroke_dirty;
    const std::uint64_t raster_started = clock_us(options);
    bool aborted = false;
    ScreenSample previous = project(samples[0]);
    if (samples.size() == 1U) {
      stroke_dirty.include(
          rasterize_capsule(previous, previous, region, region_width, scratch, options, aborted));
      if (aborted) {
        stats.complete = false;
        return stats;
      }
      ++stats.segments_rendered;
    }
    for (std::size_t index = 1U; index < samples.size(); ++index) {
      const ScreenSample current = project(samples[index]);
      stroke_dirty.include(
          rasterize_capsule(previous, current, region, region_width, scratch, options, aborted));
      if (aborted) {
        stats.complete = false;
        return stats;
      }
      ++stats.segments_rendered;
      previous = current;
    }
    if (options.clock_us != nullptr) {
      const std::uint64_t now = clock_us(options);
      stats.raster_us += now - raster_started;
      phase_started = now;
    }
    if (!stroke_dirty.empty()) {
      if (!composite_and_clear(destination, region, region_width, scratch, stroke_dirty, color,
                               options)) {
        stats.complete = false;
        return stats;
      }
      if (options.clock_us != nullptr) {
        const std::uint64_t now = clock_us(options);
        stats.composite_us += now - phase_started;
        phase_started = now;
      }
      ++stats.strokes_rendered;
    }
  }
  if (cancelled(options)) {
    stats.complete = false;
  }
  return stats;
}

}  // namespace tinydraw
