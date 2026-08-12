#include "tinydraw/graphics/settled_renderer.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "tinydraw/document/vector_document.h"
#include "tinydraw/graphics/viewport_renderer.h"

namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kBlue = 0x001FU;
constexpr std::uint16_t kRed = 0xF800U;
constexpr std::size_t kPixels =
    static_cast<std::size_t>(tinydraw::kCanvasWidth) * tinydraw::kCanvasHeight;

std::size_t pixel(int x, int y) { return static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x); }

bool inked(std::uint16_t value) { return value != kWhite; }

void add_wavy_stroke(tinydraw::VectorDocument& document, std::uint16_t color, float base_x,
                     float base_y, std::size_t samples) {
  REQUIRE(document.begin_stroke(color, tinydraw::VectorTool::kPen,
                                {.x = base_x, .y = base_y, .radius = 2.5F}));
  for (std::size_t index = 1U; index < samples; ++index) {
    const float t = static_cast<float>(index);
    REQUIRE(document.append({.x = base_x + t * 3.0F,
                             .y = base_y + 6.0F * std::sin(t * 0.6F),
                             .radius = 2.0F + 1.2F * std::sin(t * 0.4F)}));
  }
  REQUIRE(document.finish_stroke());
}

}  // namespace

TEST_CASE("settled renderer approximates canonical ink placement") {
  std::array<tinydraw::VectorStroke, 4> stroke_storage;
  std::array<tinydraw::StrokeSample, 128> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  add_wavy_stroke(document, kBlue, 40.0F, 60.0F, 24U);
  add_wavy_stroke(document, kRed, 60.0F, 120.0F, 30U);

  std::vector<std::uint16_t> canonical(kPixels, 0U);
  std::vector<std::uint8_t> renderer_scratch(tinydraw::ViewportRenderer::kScratchBytes);
  tinydraw::ViewportRenderer renderer(renderer_scratch);
  REQUIRE(renderer.render(document, {}, canonical).complete);

  std::vector<std::uint16_t> settled(kPixels, 0U);
  std::vector<std::uint8_t> settled_scratch(kPixels);
  const tinydraw::Rect full{0, 0, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight};
  const auto stats = settled_render_region(document, {}, settled, full, settled_scratch);
  REQUIRE(stats.complete);
  CHECK(stats.strokes_rendered == 2U);

  // The settled output is intentionally noncanonical, but solid canonical ink
  // should almost always be ink in the settled output and settled ink should
  // stay within a small halo of canonical ink.
  std::size_t canonical_solid = 0U;
  std::size_t covered = 0U;
  std::size_t settled_outside_halo = 0U;
  for (int y = 0; y < tinydraw::kCanvasHeight; ++y) {
    for (int x = 0; x < tinydraw::kCanvasWidth; ++x) {
      const std::uint16_t exact = canonical[pixel(x, y)];
      if (exact != kWhite && (exact == kBlue || exact == kRed)) {
        ++canonical_solid;
        if (inked(settled[pixel(x, y)])) {
          ++covered;
        }
      }
      if (inked(settled[pixel(x, y)])) {
        bool near_canonical = false;
        for (int dy = -2; dy <= 2 && !near_canonical; ++dy) {
          for (int dx = -2; dx <= 2 && !near_canonical; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx >= 0 && nx < tinydraw::kCanvasWidth && ny >= 0 && ny < tinydraw::kCanvasHeight &&
                inked(canonical[pixel(nx, ny)])) {
              near_canonical = true;
            }
          }
        }
        if (!near_canonical) {
          ++settled_outside_halo;
        }
      }
    }
  }
  REQUIRE(canonical_solid > 500U);
  CHECK(covered * 100U >= canonical_solid * 85U);
  CHECK(settled_outside_halo == 0U);
}

TEST_CASE("settled renderer preserves painter order and eraser behavior") {
  std::array<tinydraw::VectorStroke, 3> stroke_storage;
  std::array<tinydraw::StrokeSample, 16> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);

  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 80.0F, .y = 100.0F, .radius = 6.0F}));
  REQUIRE(document.append({.x = 120.0F, .y = 100.0F, .radius = 6.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(kRed, tinydraw::VectorTool::kPen,
                                {.x = 100.0F, .y = 90.0F, .radius = 5.0F}));
  REQUIRE(document.append({.x = 100.0F, .y = 110.0F, .radius = 5.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(0x07E0U, tinydraw::VectorTool::kEraser,
                                {.x = 112.0F, .y = 100.0F, .radius = 4.0F}));
  REQUIRE(document.append({.x = 120.0F, .y = 100.0F, .radius = 4.0F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint16_t> settled(kPixels, 0U);
  std::vector<std::uint8_t> scratch(kPixels);
  const tinydraw::Rect full{0, 0, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight};
  REQUIRE(settled_render_region(document, {}, settled, full, scratch).complete);

  // The red stroke crosses the blue one and was drawn later.
  CHECK(settled[pixel(100, 100)] == kRed);
  // The blue stroke interior away from red remains blue.
  CHECK(settled[pixel(85, 100)] == kBlue);
  // The eraser painted background over the blue tail.
  CHECK(settled[pixel(116, 100)] == kWhite);
}

TEST_CASE("settled region rendering only writes inside the region") {
  std::array<tinydraw::VectorStroke, 1> stroke_storage;
  std::array<tinydraw::StrokeSample, 32> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  add_wavy_stroke(document, kBlue, 30.0F, 20.0F, 24U);

  std::vector<std::uint16_t> full_render(kPixels, 0U);
  std::vector<std::uint8_t> scratch(kPixels);
  const tinydraw::Rect full{0, 0, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight};
  REQUIRE(settled_render_region(document, {}, full_render, full, scratch).complete);

  std::vector<std::uint16_t> partial(kPixels, 0x1234U);
  const tinydraw::Rect region{0, 0, tinydraw::kCanvasWidth, 32};
  REQUIRE(settled_render_region(document, {}, partial, region, scratch).complete);
  for (int y = 0; y < tinydraw::kCanvasHeight; ++y) {
    for (int x = 0; x < tinydraw::kCanvasWidth; ++x) {
      if (y < 32) {
        CHECK(partial[pixel(x, y)] == full_render[pixel(x, y)]);
      } else {
        CHECK(partial[pixel(x, y)] == 0x1234U);
      }
    }
  }
}

TEST_CASE("settled renderer preserves LOD pressure, loops, painter order, and erasing") {
  std::array<tinydraw::VectorStroke, 3> stroke_storage;
  std::array<tinydraw::StrokeSample, 16> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);

  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 40.0F, .y = 80.0F, .radius = 2.0F}));
  REQUIRE(document.append({.x = 80.0F, .y = 40.0F, .radius = 8.0F}));
  REQUIRE(document.append({.x = 120.0F, .y = 80.0F, .radius = 2.0F}));
  REQUIRE(document.append({.x = 80.0F, .y = 120.0F, .radius = 2.0F}));
  REQUIRE(document.append({.x = 40.0F, .y = 80.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(kRed, tinydraw::VectorTool::kPen,
                                {.x = 74.0F, .y = 40.0F, .radius = 3.0F}));
  REQUIRE(document.append({.x = 86.0F, .y = 40.0F, .radius = 3.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(0U, tinydraw::VectorTool::kEraser,
                                {.x = 38.0F, .y = 80.0F, .radius = 3.0F}));
  REQUIRE(document.append({.x = 46.0F, .y = 80.0F, .radius = 3.0F}));
  REQUIRE(document.finish_stroke());

  const std::array lod_samples{
      tinydraw::StrokeSample{.x = 40.0F, .y = 80.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 80.0F, .y = 40.0F, .radius = 8.0F},
      tinydraw::StrokeSample{.x = 120.0F, .y = 80.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 80.0F, .y = 120.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 40.0F, .y = 80.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 74.0F, .y = 40.0F, .radius = 3.0F},
      tinydraw::StrokeSample{.x = 86.0F, .y = 40.0F, .radius = 3.0F},
      tinydraw::StrokeSample{.x = 38.0F, .y = 80.0F, .radius = 3.0F},
      tinydraw::StrokeSample{.x = 46.0F, .y = 80.0F, .radius = 3.0F},
  };
  const std::array<std::uint32_t, 3> first{0U, 5U, 7U};
  const std::array<std::uint16_t, 3> count{5U, 2U, 2U};
  tinydraw::SettledRenderOptions options;
  options.lod_samples = lod_samples;
  options.lod_first_sample = first;
  options.lod_sample_count = count;

  std::vector<std::uint16_t> settled(kPixels, 0U);
  std::vector<std::uint8_t> scratch(kPixels);
  const tinydraw::Rect full{0, 0, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight};
  REQUIRE(settled_render_region(document, {}, settled, full, scratch, options).complete);
  CHECK(settled[pixel(80, 40)] == kRed);
  CHECK(inked(settled[pixel(80, 47)]));
  CHECK(inked(settled[pixel(120, 80)]));
  CHECK(inked(settled[pixel(80, 120)]));
  CHECK(settled[pixel(42, 80)] == kWhite);
}

TEST_CASE("malformed settled LOD falls back to the complete raw document") {
  std::array<tinydraw::VectorStroke, 2> stroke_storage;
  std::array<tinydraw::StrokeSample, 32> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  add_wavy_stroke(document, kBlue, 40.0F, 60.0F, 10U);
  add_wavy_stroke(document, kRed, 40.0F, 160.0F, 10U);

  const tinydraw::Rect full{0, 0, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight};
  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> expected(kPixels, 0U);
  REQUIRE(settled_render_region(document, {}, expected, full, scratch).complete);

  const std::array<tinydraw::StrokeSample, 1> bad_samples{{}};
  const std::array<std::uint32_t, 2> bad_first{0U, 99U};
  const std::array<std::uint16_t, 2> bad_count{1U, 2U};
  tinydraw::SettledRenderOptions options;
  options.lod_samples = bad_samples;
  options.lod_first_sample = bad_first;
  options.lod_sample_count = bad_count;
  std::vector<std::uint16_t> actual(kPixels, 0U);
  REQUIRE(settled_render_region(document, {}, actual, full, scratch, options).complete);
  CHECK(actual == expected);
}

TEST_CASE("settled renderer honors candidates, cancellation, and validation") {
  std::array<tinydraw::VectorStroke, 2> stroke_storage;
  std::array<tinydraw::StrokeSample, 64> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  add_wavy_stroke(document, kBlue, 40.0F, 60.0F, 16U);
  add_wavy_stroke(document, kRed, 40.0F, 160.0F, 16U);

  std::vector<std::uint16_t> settled(kPixels, 0U);
  std::vector<std::uint8_t> scratch(kPixels);
  const tinydraw::Rect full{0, 0, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight};

  // Candidate bitset selecting only the second stroke.
  const std::array<std::uint64_t, 1> candidates{0b10U};
  tinydraw::SettledRenderOptions options;
  options.candidate_strokes = candidates;
  auto stats = settled_render_region(document, {}, settled, full, scratch, options);
  REQUIRE(stats.complete);
  CHECK(stats.strokes_tested == 1U);
  CHECK(stats.strokes_rendered == 1U);
  CHECK(settled[pixel(41, 160)] == kRed);
  CHECK(settled[pixel(41, 60)] == kWhite);

  // Immediate cancellation reports an incomplete render.
  tinydraw::SettledRenderOptions cancel_options;
  cancel_options.cancelled = [](void*) { return true; };
  stats = settled_render_region(document, {}, settled, full, scratch, cancel_options);
  CHECK_FALSE(stats.complete);

  // Insufficient scratch is rejected.
  std::vector<std::uint8_t> tiny_scratch(16U);
  stats = settled_render_region(document, {}, settled, full, tiny_scratch);
  CHECK_FALSE(stats.complete);
}
