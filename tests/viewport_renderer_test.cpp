#include "tinydraw/graphics/viewport_renderer.h"

#include <doctest.h>

#include <algorithm>
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

bool cancel_after_checks(void* raw) {
  auto& checks = *static_cast<int*>(raw);
  return ++checks >= 8;
}

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

TEST_CASE("region render matches a full render and preserves pixels outside the region") {
  std::array<tinydraw::VectorStroke, 3> stroke_storage;
  std::array<tinydraw::StrokeSample, 9> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 30.0F, .y = 90.0F, .radius = 7.0F}));
  REQUIRE(document.append({.x = 180.0F, .y = 140.0F, .radius = 7.0F}));
  REQUIRE(document.append({.x = 340.0F, .y = 210.0F, .radius = 7.0F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> full(kPixels, 0U);
  std::vector<std::uint16_t> partial(kPixels, 0x1234U);
  tinydraw::ViewportRenderer renderer(scratch);
  static_cast<void>(renderer.render(document, {}, full));
  const tinydraw::Rect region{.x0 = 73, .y0 = 61, .x1 = 289, .y1 = 277};
  const auto stats = renderer.render_region(document, {}, partial, region);

  REQUIRE(stats.complete);
  for (int y = 0; y < tinydraw::kCanvasHeight; ++y) {
    for (int x = 0; x < tinydraw::kCanvasWidth; ++x) {
      const bool inside = x >= region.x0 && x < region.x1 && y >= region.y0 && y < region.y1;
      CHECK(partial[pixel(x, y)] == (inside ? full[pixel(x, y)] : 0x1234U));
    }
  }
}

TEST_CASE("cached integer pan plus exposed strip render equals a full rebuild") {
  std::array<tinydraw::VectorStroke, 3> stroke_storage;
  std::array<tinydraw::StrokeSample, 9> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 20.0F, .y = 40.0F, .radius = 6.0F}));
  REQUIRE(document.append({.x = 220.0F, .y = 180.0F, .radius = 8.0F}));
  REQUIRE(document.append({.x = 430.0F, .y = 300.0F, .radius = 5.0F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> cached(kPixels);
  std::vector<std::uint16_t> expected(kPixels);
  tinydraw::ViewportRenderer renderer(scratch);
  const tinydraw::Camera old_camera{.x = 0.0, .y = 0.0, .zoom = 1.0F};
  constexpr int kPanPixels = 37;
  const tinydraw::Camera new_camera{.x = kPanPixels, .y = 0.0, .zoom = 1.0F};
  static_cast<void>(renderer.render(document, old_camera, cached));
  static_cast<void>(renderer.render(document, new_camera, expected));

  for (int y = 0; y < tinydraw::kCanvasHeight; ++y) {
    auto* row = cached.data() + static_cast<std::ptrdiff_t>(y * tinydraw::kCanvasWidth);
    std::move(row + kPanPixels, row + tinydraw::kCanvasWidth, row);
  }
  const auto stats = renderer.render_region(document, new_camera, cached,
                                            {.x0 = tinydraw::kCanvasWidth - kPanPixels,
                                             .y0 = 0,
                                             .x1 = tinydraw::kCanvasWidth,
                                             .y1 = tinydraw::kCanvasHeight});

  CHECK(stats.complete);
  CHECK(cached == expected);
}

TEST_CASE("a stroke larger than the primitive arena renders completely") {
  constexpr std::size_t kSampleCount = 900U;
  std::array<tinydraw::VectorStroke, 1> stroke_storage;
  std::array<tinydraw::StrokeSample, kSampleCount> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 30.0F, .y = 200.0F, .radius = 1.25F}));
  for (std::size_t index = 1; index < kSampleCount; ++index) {
    const float x = 30.0F + static_cast<float>(index % 300U);
    const float y = index % 2U == 0U ? 200.0F : 204.0F;
    REQUIRE(document.append({.x = x, .y = y, .radius = 1.25F}));
  }
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels);
  tinydraw::ViewportRenderer renderer(scratch);
  const auto stats = renderer.render(document, {}, rebuilt);

  CHECK(stats.complete);
  CHECK(stats.samples_processed == kSampleCount);
  CHECK(stats.primitives_rasterized > renderer.primitive_capacity());
  CHECK(rebuilt[pixel(328, 204)] != kWhite);
}

TEST_CASE("an over-capacity self-overlapping stroke is blended once") {
  constexpr std::size_t kLongSamples = 900U;
  std::array<tinydraw::VectorStroke, 1> long_stroke_storage;
  std::array<tinydraw::StrokeSample, kLongSamples> long_sample_storage;
  tinydraw::VectorDocument long_document(long_stroke_storage, long_sample_storage);
  REQUIRE(long_document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                     {.x = 100.25F, .y = 100.25F, .radius = 0.4F}));
  for (std::size_t index = 1; index < kLongSamples; ++index) {
    REQUIRE(long_document.append(
        {.x = index % 2U == 0U ? 100.25F : 110.25F, .y = 100.25F, .radius = 0.4F}));
  }
  REQUIRE(long_document.finish_stroke());

  std::array<tinydraw::VectorStroke, 1> short_stroke_storage;
  std::array<tinydraw::StrokeSample, 4> short_sample_storage;
  tinydraw::VectorDocument short_document(short_stroke_storage, short_sample_storage);
  REQUIRE(short_document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                      {.x = 100.25F, .y = 100.25F, .radius = 0.4F}));
  REQUIRE(short_document.append({.x = 110.25F, .y = 100.25F, .radius = 0.4F}));
  REQUIRE(short_document.append({.x = 100.25F, .y = 100.25F, .radius = 0.4F}));
  REQUIRE(short_document.append({.x = 110.25F, .y = 100.25F, .radius = 0.4F}));
  REQUIRE(short_document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> long_result(kPixels);
  std::vector<std::uint16_t> short_result(kPixels);
  tinydraw::ViewportRenderer renderer(scratch);
  const auto stats = renderer.render(long_document, {}, long_result);
  static_cast<void>(renderer.render(short_document, {}, short_result));

  REQUIRE(stats.complete);
  CHECK(stats.primitives_rasterized > renderer.primitive_capacity());
  CHECK(long_result == short_result);
}

TEST_CASE("geometry cancellation is observed before a long stroke completes") {
  constexpr std::size_t kSampleCount = 900U;
  std::array<tinydraw::VectorStroke, 1> stroke_storage;
  std::array<tinydraw::StrokeSample, kSampleCount> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = 20.0F, .y = 80.0F, .radius = 2.0F}));
  for (std::size_t index = 1; index < kSampleCount; ++index) {
    REQUIRE(document.append({.x = 20.0F + static_cast<float>(index % 320U),
                             .y = 80.0F + static_cast<float>(index % 5U),
                             .radius = 2.0F}));
  }
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels, 0U);
  tinydraw::ViewportRenderer renderer(scratch);
  int cancellation_checks = 0;
  const auto stats = renderer.render(
      document, {}, rebuilt,
      {.cancelled = cancel_after_checks, .cancellation_context = &cancellation_checks});

  CHECK_FALSE(stats.complete);
  CHECK(cancellation_checks == 8);
  CHECK(stats.samples_processed == 0U);
}

TEST_CASE("low zoom culling keeps minimum-radius edge and corner coverage") {
  std::array<tinydraw::VectorStroke, 2> stroke_storage;
  std::array<tinydraw::StrokeSample, 2> sample_storage;
  tinydraw::VectorDocument document(stroke_storage, sample_storage);
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = -10.0F, .y = 200.0F, .radius = 0.1F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(kBlue, tinydraw::VectorTool::kPen,
                                {.x = -5.0F, .y = -5.0F, .radius = 0.1F}));
  REQUIRE(document.finish_stroke());

  std::vector<std::uint8_t> scratch(kPixels);
  std::vector<std::uint16_t> rebuilt(kPixels);
  tinydraw::ViewportRenderer renderer(scratch);
  const auto stats =
      renderer.render(document, {.zoom = 0.1F}, rebuilt, {.minimum_screen_radius = 2.0F});

  CHECK(stats.complete);
  CHECK(stats.strokes_intersecting == 2U);
  CHECK(rebuilt[pixel(0, 20)] != kWhite);
  CHECK(rebuilt[pixel(0, 0)] != kWhite);
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
