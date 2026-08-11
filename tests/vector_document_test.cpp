#include "tinydraw/document/vector_document.h"

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstdint>

TEST_CASE("vector document records processed samples and stroke metadata") {
  std::array<tinydraw::VectorStroke, 2> strokes;
  std::array<tinydraw::StrokeSample, 8> samples;
  tinydraw::VectorDocument document(strokes, samples);

  CHECK(document.begin_stroke(0x1234U, tinydraw::VectorTool::kPen,
                              {.x = -10.0F, .y = 20.0F, .radius = 2.0F}));
  CHECK(document.append({.x = 5.0F, .y = 30.0F, .radius = 4.0F}));
  CHECK(document.finish_stroke());

  REQUIRE(document.stroke_count() == 1U);
  CHECK(document.sample_count() == 2U);
  const auto stroke = document.strokes().front();
  CHECK(stroke.color == 0x1234U);
  CHECK(stroke.tool == tinydraw::VectorTool::kPen);
  CHECK(stroke.bounds.x0 == doctest::Approx(-12.0F));
  CHECK(stroke.bounds.y0 == doctest::Approx(18.0F));
  CHECK(stroke.bounds.x1 == doctest::Approx(9.0F));
  CHECK(stroke.bounds.y1 == doctest::Approx(34.0F));
  const auto recorded = document.samples(stroke);
  REQUIRE(recorded.size() == 2U);
  CHECK(recorded[0].x == doctest::Approx(-10.0F));
  CHECK(recorded[1].radius == doctest::Approx(4.0F));
}

TEST_CASE("canceling a vector stroke reclaims its samples") {
  std::array<tinydraw::VectorStroke, 1> strokes;
  std::array<tinydraw::StrokeSample, 2> samples;
  tinydraw::VectorDocument document(strokes, samples);

  CHECK(document.begin_stroke(0U, tinydraw::VectorTool::kEraser,
                              {.x = 1.0F, .y = 2.0F, .radius = 3.0F}));
  CHECK(document.append({.x = 4.0F, .y = 5.0F, .radius = 6.0F}));
  document.cancel_stroke();

  CHECK_FALSE(document.active());
  CHECK(document.stroke_count() == 0U);
  CHECK(document.sample_count() == 0U);
  CHECK(document.begin_stroke(0U, tinydraw::VectorTool::kPen,
                              {.x = 7.0F, .y = 8.0F, .radius = 1.0F}));
}

TEST_CASE("vector document reports invalid order and bounded capacity") {
  std::array<tinydraw::VectorStroke, 1> strokes;
  std::array<tinydraw::StrokeSample, 2> samples;
  tinydraw::VectorDocument document(strokes, samples);

  CHECK_FALSE(document.append({.x = 0.0F, .y = 0.0F, .radius = 1.0F}));
  CHECK_FALSE(document.finish_stroke());
  CHECK_FALSE(document.begin_stroke(0U, tinydraw::VectorTool::kPen,
                                    {.x = 0.0F, .y = 0.0F, .radius = 0.0F}));
  CHECK_FALSE(document.begin_stroke(0U, tinydraw::VectorTool::kPen,
                                    {.x = std::nanf(""), .y = 0.0F, .radius = 1.0F}));
  CHECK(document.begin_stroke(0U, tinydraw::VectorTool::kPen,
                              {.x = 0.0F, .y = 0.0F, .radius = 1.0F}));
  CHECK(document.append({.x = 1.0F, .y = 1.0F, .radius = 1.0F}));
  CHECK_FALSE(document.append({.x = 2.0F, .y = 2.0F, .radius = 1.0F}));
  CHECK(document.finish_stroke());
  CHECK_FALSE(document.begin_stroke(0U, tinydraw::VectorTool::kPen,
                                    {.x = 3.0F, .y = 3.0F, .radius = 1.0F}));

  document.clear();
  CHECK(document.stroke_count() == 0U);
  CHECK(document.sample_count() == 0U);
}
