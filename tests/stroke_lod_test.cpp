#include "tinydraw/graphics/stroke_lod.h"

#include <doctest.h>

#include <array>

TEST_CASE("stroke LOD retains endpoints and drops close stable samples") {
  const std::array input{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 0.5F, .y = 0.0F, .radius = 2.1F},
      tinydraw::StrokeSample{.x = 1.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 3.0F, .y = 0.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, input.size()> output;
  const auto simplified = tinydraw::simplify_stroke_samples(input, output, 2.0F, 0.25F);
  REQUIRE(simplified.size() == 2U);
  CHECK(simplified.front().x == 0.0F);
  CHECK(simplified.back().x == 3.0F);
}

TEST_CASE("stroke LOD preserves meaningful pressure changes") {
  const std::array input{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 0.5F, .y = 0.0F, .radius = 3.0F},
      tinydraw::StrokeSample{.x = 1.0F, .y = 0.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, input.size()> output;
  const auto simplified = tinydraw::simplify_stroke_samples(input, output, 2.0F, 0.25F);
  REQUIRE(simplified.size() == input.size());
  CHECK(simplified[1].radius == 3.0F);
}

TEST_CASE("stroke LOD validates output and options") {
  const std::array input{tinydraw::StrokeSample{.x = 1.0F, .y = 2.0F, .radius = 3.0F}};
  std::array<tinydraw::StrokeSample, 1> output;
  REQUIRE(tinydraw::simplify_stroke_samples(input, output, 1.0F, 0.25F).size() == 1U);
  CHECK(tinydraw::simplify_stroke_samples(input, {}, 1.0F, 0.25F).empty());
  CHECK(tinydraw::simplify_stroke_samples(input, output, -1.0F, 0.25F).empty());
}
