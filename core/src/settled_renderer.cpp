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

// Unions the analytic coverage of one variable-radius capsule into the
// region-local scratch buffer and returns the touched local bounds.
LocalRect rasterize_capsule(ScreenSample from, ScreenSample to, Rect region, int region_width,
                            std::span<std::uint8_t> scratch) {
  const float maximum_radius = std::max(from.radius, to.radius);
  const float pad = maximum_radius + kEdgePixels;
  const LocalRect bounds{
      .x0 = std::max(static_cast<int>(std::floor(std::min(from.x, to.x) - pad)), region.x0) -
            region.x0,
      .y0 = std::max(static_cast<int>(std::floor(std::min(from.y, to.y) - pad)), region.y0) -
            region.y0,
      .x1 = std::min(static_cast<int>(std::ceil(std::max(from.x, to.x) + pad)) + 1, region.x1) -
            region.x0,
      .y1 = std::min(static_cast<int>(std::ceil(std::max(from.y, to.y) + pad)) + 1, region.y1) -
            region.y0,
  };
  if (bounds.empty()) {
    return {};
  }

  const float delta_x = to.x - from.x;
  const float delta_y = to.y - from.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  const float inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F;
  const float delta_radius = to.radius - from.radius;

  for (int local_y = bounds.y0; local_y < bounds.y1; ++local_y) {
    const float pixel_y = static_cast<float>(region.y0 + local_y) + 0.5F;
    std::uint8_t* row = scratch.data() + static_cast<std::ptrdiff_t>(local_y * region_width);
    for (int local_x = bounds.x0; local_x < bounds.x1; ++local_x) {
      const float pixel_x = static_cast<float>(region.x0 + local_x) + 0.5F;
      const float to_pixel_x = pixel_x - from.x;
      const float to_pixel_y = pixel_y - from.y;
      const float t = std::clamp(
          (to_pixel_x * delta_x + to_pixel_y * delta_y) * inverse_length_squared, 0.0F, 1.0F);
      const float nearest_x = to_pixel_x - t * delta_x;
      const float nearest_y = to_pixel_y - t * delta_y;
      const float distance_squared = nearest_x * nearest_x + nearest_y * nearest_y;
      const float radius = from.radius + t * delta_radius;
      const float outer = radius + 0.5F * kEdgePixels;
      if (distance_squared >= outer * outer) {
        continue;
      }
      const float inner = radius - 0.5F * kEdgePixels;
      std::uint8_t alpha = 255U;
      if (inner <= 0.0F || distance_squared > inner * inner) {
        const float distance = std::sqrt(distance_squared);
        const float coverage = std::clamp((outer - distance) / kEdgePixels, 0.0F, 1.0F);
        alpha = static_cast<std::uint8_t>(coverage * 255.0F + 0.5F);
        if (alpha == 0U) {
          continue;
        }
      }
      row[local_x] = std::max(row[local_x], alpha);
    }
  }
  return bounds;
}

void composite_and_clear(std::span<std::uint16_t> destination, Rect region, int region_width,
                         std::span<std::uint8_t> scratch, LocalRect dirty, std::uint16_t color) {
  const std::uint32_t source_red = (color >> 11U) & 0x1FU;
  const std::uint32_t source_green = (color >> 5U) & 0x3FU;
  const std::uint32_t source_blue = color & 0x1FU;
  for (int local_y = dirty.y0; local_y < dirty.y1; ++local_y) {
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
  for (int y = region.y0; y < region.y1; ++y) {
    std::fill_n(destination.begin() + static_cast<std::ptrdiff_t>(y * kCanvasWidth + region.x0),
                static_cast<std::size_t>(region_width), options.background);
  }
  std::fill_n(scratch.begin(), region_area, std::uint8_t{0});

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
    const auto samples = document.samples(stroke);
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
    ScreenSample previous = project(samples[0]);
    if (samples.size() == 1U) {
      stroke_dirty.include(rasterize_capsule(previous, previous, region, region_width, scratch));
      ++stats.segments_rendered;
    }
    for (std::size_t index = 1U; index < samples.size(); ++index) {
      const ScreenSample current = project(samples[index]);
      stroke_dirty.include(rasterize_capsule(previous, current, region, region_width, scratch));
      ++stats.segments_rendered;
      previous = current;
    }
    if (!stroke_dirty.empty()) {
      composite_and_clear(destination, region, region_width, scratch, stroke_dirty, color);
      ++stats.strokes_rendered;
    }
  }
  if (cancelled(options)) {
    stats.complete = false;
  }
  return stats;
}

}  // namespace tinydraw
