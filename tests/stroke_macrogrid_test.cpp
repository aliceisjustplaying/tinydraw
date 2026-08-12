#include "tinydraw/document/stroke_macrogrid.h"

#include <doctest.h>

#include <array>
#include <cstdint>

TEST_CASE("macrogrid returns candidates in document-order bits") {
  std::array<tinydraw::VectorStroke, 4> strokes{};
  std::array<tinydraw::StrokeSample, 8> samples{};
  tinydraw::VectorDocument document(strokes, samples);
  REQUIRE(document.begin_stroke(1U, tinydraw::VectorTool::kPen,
                                {.x = 10.0F, .y = 10.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(2U, tinydraw::VectorTool::kPen,
                                {.x = 700.0F, .y = 700.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(3U, tinydraw::VectorTool::kPen,
                                {.x = 20.0F, .y = 20.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());

  std::array<std::uint64_t, tinydraw::StrokeMacrogrid::kCellCount> cells{};
  std::array<std::uint64_t, 1> query{};
  tinydraw::StrokeMacrogrid index(cells, query, strokes.size());
  REQUIRE(index.rebuild(document));

  const auto candidates = index.query({.x0 = 0.0F, .y0 = 0.0F, .x1 = 100.0F, .y1 = 100.0F});
  REQUIRE(candidates.size() == 1U);
  CHECK(candidates[0] == 0b101U);
}

TEST_CASE("macrogrid falls back conservatively outside its indexed world") {
  std::array<tinydraw::VectorStroke, 4> strokes{};
  std::array<tinydraw::StrokeSample, 4> samples{};
  tinydraw::VectorDocument document(strokes, samples);
  REQUIRE(document.begin_stroke(1U, tinydraw::VectorTool::kPen,
                                {.x = 10.0F, .y = 10.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());
  REQUIRE(document.begin_stroke(2U, tinydraw::VectorTool::kPen,
                                {.x = 700.0F, .y = 700.0F, .radius = 2.0F}));
  REQUIRE(document.finish_stroke());

  std::array<std::uint64_t, tinydraw::StrokeMacrogrid::kCellCount> cells{};
  std::array<std::uint64_t, 1> query{};
  tinydraw::StrokeMacrogrid index(cells, query, strokes.size());
  REQUIRE(index.rebuild(document));

  const auto candidates = index.query({.x0 = -1.0F, .y0 = 0.0F, .x1 = 100.0F, .y1 = 100.0F});
  CHECK(candidates[0] == 0b11U);
}

TEST_CASE("macrogrid appends one committed stroke without rebuilding") {
  std::array<std::uint64_t, tinydraw::StrokeMacrogrid::kCellCount> cells{};
  std::array<std::uint64_t, 1> query{};
  tinydraw::StrokeMacrogrid index(cells, query, 4U);

  REQUIRE(index.append(0U, {.x0 = 300.0F, .y0 = 300.0F, .x1 = 320.0F, .y1 = 320.0F}));
  CHECK(index.indexed_strokes() == 1U);
  CHECK(index.query({.x0 = 256.0F, .y0 = 256.0F, .x1 = 400.0F, .y1 = 400.0F})[0] == 1U);
}
