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
  // Optional conservative candidate bitset in document order, one bit per
  // stroke index, identical to ViewportRenderOptions::candidate_strokes.
  std::span<const std::uint64_t> candidate_strokes{};
  bool (*cancelled)(void* context) = nullptr;
  void* cancellation_context = nullptr;
};

struct SettledRenderStats {
  std::uint32_t strokes_tested = 0;
  std::uint32_t strokes_rendered = 0;
  std::uint32_t segments_rendered = 0;
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
