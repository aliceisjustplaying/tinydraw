#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/document/vector_document.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/camera.h"

namespace tinydraw {

struct SettledRenderOptions {
  std::uint16_t background = 0xFFFFU;
  float minimum_screen_radius = 0.0F;
  // Optional append-time or caller-owned settled geometry. When supplied, the
  // per-stroke offsets/counts correspond to VectorDocument::strokes().
  std::span<const StrokeSample> lod_samples{};
  std::span<const std::uint32_t> lod_first_sample{};
  std::span<const std::uint16_t> lod_sample_count{};
  // Screen-space centerline spacing used when no precomputed LOD is supplied.
  // Zero preserves every source sample.
  float minimum_screen_sample_spacing = 0.0F;
  // Optional conservative candidate bitset in document order, one bit per
  // stroke index, identical to ViewportRenderOptions::candidate_strokes.
  std::span<const std::uint64_t> candidate_strokes{};
  bool (*cancelled)(void* context) = nullptr;
  void* cancellation_context = nullptr;
  // Optional microsecond clock used only for profiling counters in the result.
  std::uint64_t (*clock_us)(void* context) = nullptr;
  void* clock_context = nullptr;
};

struct SettledRenderStats {
  std::uint32_t strokes_tested = 0;
  std::uint32_t strokes_rendered = 0;
  std::uint32_t segments_rendered = 0;
  std::uint64_t clear_us = 0;
  std::uint64_t raster_us = 0;
  std::uint64_t composite_us = 0;
  bool complete = true;
};

// Deliberately noncanonical "settled" renderer: strokes become chains of
// variable-radius capsules with analytic one-pixel edge coverage instead of
// reconstructed curved ribbons with 4x4 supersampling. Painter order and
// eraser-as-background behavior match the canonical renderer; joins, curve
// interpolation, and edge antialiasing intentionally differ slightly.
//
// `destination` is a full kCanvasWidth x kCanvasHeight RGB565 canvas;
// `region` selects the rendered part and is cleared to the background first.
// `scratch` provides one byte of per-pixel union coverage and must hold at
// least the region's area. Cancellation is checked between strokes; a
// cancelled render returns complete=false and leaves the region partially
// rendered.
SettledRenderStats settled_render_region(const VectorDocument& document, Camera camera,
                                         std::span<std::uint16_t> destination, Rect region,
                                         std::span<std::uint8_t> scratch,
                                         const SettledRenderOptions& options = {});

}  // namespace tinydraw
