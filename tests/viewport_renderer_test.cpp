#include "tinydraw/graphics/viewport_renderer.h"

#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "tinydraw/graphics/stroke_raster.h"
#include "tinydraw/ink/ink_stream.h"

namespace {

constexpr std::size_t kPixels = tinydraw::ViewportRenderer::kPixelCount;
constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kBlue = 0x035FU;

std::size_t pixel(int x, int y) { return static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x); }

}  // namespace

TEST_CASE("offline viewport replay matches the settled live raster") {
  std::array<tinydraw::VectorStroke, 2> stroke_storage;
  std::array<tinydraw::StrokeSample, 16> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  std::vector<std::uint16_t> committed(kPixels, kWhite);
  std::vector<std::uint16_t> visible(kPixels, kWhite);
  std::vector<std::uint8_t> live_coverage(kPixels, 0U);
  tinydraw::StrokeRaster live(committed, visible, live_coverage);
  tinydraw::InkConfig config;
  config.size = 12.0F;
  tinydraw::InkStream ink(config);
  tinydraw::CurvedRibbonStream ribbon;

  const auto first = ink.begin({.x = 40.0F, .y = 60.0F, .timestamp_us = 0U});
  REQUIRE(document.begin_stroke(
      kBlue, tinydraw::VectorTool::kPen,
      {.x = first.position.x, .y = first.position.y, .radius = first.radius}));
  static_cast<void>(live.update(ribbon.append(first), kBlue));
  for (std::uint32_t index = 1; index < 6; ++index) {
    const auto point = ink.update({.x = 40.0F + static_cast<float>(index) * 28.0F,
                                   .y = 60.0F + static_cast<float>(index % 2U) * 50.0F,
                                   .timestamp_us = index * 16'000U});
    REQUIRE(
        document.append({.x = point.position.x, .y = point.position.y, .radius = point.radius}));
    static_cast<void>(live.update(ribbon.append(point), kBlue));
  }
  const auto final = ink.finish({.x = 180.0F, .y = 110.0F, .timestamp_us = 112'000U});
  REQUIRE(document.append({.x = final.position.x, .y = final.position.y, .radius = final.radius}));
  REQUIRE(document.finish_stroke());
  static_cast<void>(live.finish(ribbon.finish(final), kBlue));

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels, 0U);
  tinydraw::ViewportRenderer renderer(scratch);
  const auto stats = renderer.render(document, {}, rebuilt);

  CHECK(rebuilt == committed);
  CHECK(stats.strokes_tested == 1U);
  CHECK(stats.strokes_intersecting == 1U);
  CHECK(stats.samples_processed == 7U);
  CHECK(stats.primitives_rasterized > 0U);
}

TEST_CASE("viewport renderer culls offscreen strokes before replay") {
  std::array<tinydraw::VectorStroke, 3> stroke_storage;
  std::array<tinydraw::StrokeSample, 6> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 20.0F, .y = 30.0F, .radius = 4.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 1'000.0F, .y = 1'000.0F, .radius = 4.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = -1'000.0F, .y = -1'000.0F, .radius = 4.0F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels);
  tinydraw::ViewportRenderer renderer(scratch);
  const auto stats = renderer.render(document, {}, rebuilt);

  CHECK(stats.strokes_tested == 3U);
  CHECK(stats.strokes_intersecting == 1U);
  CHECK(stats.samples_processed == 1U);
  CHECK(rebuilt[pixel(20, 30)] != kWhite);
}

TEST_CASE("camera transform renders negative world coordinates and zoom") {
  std::array<tinydraw::VectorStroke, 1> stroke_storage;
  std::array<tinydraw::StrokeSample, 1> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = -90.0F, .y = -30.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels);
  tinydraw::ViewportRenderer renderer(scratch);
  const auto stats = renderer.render(document, {.x = -100.0, .y = -50.0, .zoom = 2.0F}, rebuilt,
                                     {.background = kWhite, .minimum_screen_radius = 0.45F});

  CHECK(stats.strokes_intersecting == 1U);
  CHECK(rebuilt[pixel(20, 40)] == kBlue);
}

TEST_CASE("ordered eraser strokes remove raster ink") {
  std::array<tinydraw::VectorStroke, 2> stroke_storage;
  std::array<tinydraw::StrokeSample, 4> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 100.0F, .y = 100.0F, .radius = 10.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(0U, tinydraw::VectorTool::kEraser,
                                {.x = 100.0F, .y = 100.0F, .radius = 5.0F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels, 0U);
  tinydraw::ViewportRenderer renderer(scratch);
  static_cast<void>(renderer.render(document, {}, rebuilt));

  CHECK(rebuilt[pixel(100, 100)] == kWhite);
  CHECK(rebuilt[pixel(108, 100)] != kWhite);
}

TEST_CASE("empty document clears a reused viewport") {
  std::array<tinydraw::VectorStroke, 1> stroke_storage;
  std::array<tinydraw::StrokeSample, 1> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  std::vector<std::uint8_t> scratch(kPixels, 255U);
  std::vector<std::uint16_t> rebuilt(kPixels, 0U);
  tinydraw::ViewportRenderer renderer(scratch);

  const auto stats = renderer.render(document, {}, rebuilt);
  CHECK(stats.strokes_tested == 0U);
  CHECK(rebuilt[pixel(0, 0)] == kWhite);
  CHECK(rebuilt[pixel(tinydraw::kCanvasWidth - 1, tinydraw::kCanvasHeight - 1)] == kWhite);
}
