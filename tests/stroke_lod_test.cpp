#include "tinydraw/graphics/stroke_lod.h"

#include <doctest.h>

#include <array>

TEST_CASE("stroke LOD retains endpoints while simplifying a straight run") {
  const std::array input{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 1.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 2.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 3.0F, .y = 0.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, input.size()> output;
  const auto simplified = tinydraw::simplify_stroke_samples(input, output, 0.5F, 0.25F);
  REQUIRE(simplified.size() == 2U);
  CHECK(simplified.front().x == 0.0F);
  CHECK(simplified.back().x == 3.0F);
}

TEST_CASE("stroke LOD preserves tight loop and hairpin geometry") {
  const std::array loop{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 4.0F, .y = 4.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 8.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 4.0F, .y = -4.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, loop.size()> output;
  const auto simplified = tinydraw::simplify_stroke_samples(loop, output, 1.0F, 0.25F);
  CHECK(simplified.size() >= 4U);

  const std::array hairpin{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 8.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 8.0F, .y = 2.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 0.0F, .y = 2.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, hairpin.size()> hairpin_output;
  CHECK(tinydraw::simplify_stroke_samples(hairpin, hairpin_output, 0.75F, 0.25F).size() ==
        hairpin.size());
}

TEST_CASE("stroke LOD preserves pressure and eraser radius pulses") {
  const std::array input{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 7.0F},
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, input.size()> output;
  const auto simplified = tinydraw::simplify_stroke_samples(input, output, 1.0F, 0.5F);
  REQUIRE(simplified.size() == input.size());
  CHECK(simplified[1].radius == 7.0F);

  // Preserve even a sub-tolerance local extremum: its screen-space impact
  // grows with zoom and the simplifier is intentionally zoom-independent.
  const std::array subtle_pulse{
      tinydraw::StrokeSample{.x = 0.0F, .y = 0.0F, .radius = 2.0F},
      tinydraw::StrokeSample{.x = 1.0F, .y = 0.0F, .radius = 2.5F},
      tinydraw::StrokeSample{.x = 2.0F, .y = 0.0F, .radius = 2.0F},
  };
  std::array<tinydraw::StrokeSample, subtle_pulse.size()> subtle_output;
  const auto subtle = tinydraw::simplify_stroke_samples(subtle_pulse, subtle_output, 2.0F, 0.75F);
  REQUIRE(subtle.size() == subtle_pulse.size());
  CHECK(subtle[1].radius == 2.5F);
}

TEST_CASE("stroke LOD validates output and options") {
  const std::array input{tinydraw::StrokeSample{.x = 1.0F, .y = 2.0F, .radius = 3.0F}};
  std::array<tinydraw::StrokeSample, 1> output;
  REQUIRE(tinydraw::simplify_stroke_samples(input, output, 1.0F, 0.25F).size() == 1U);
  CHECK(tinydraw::simplify_stroke_samples(input, {}, 1.0F, 0.25F).empty());
  CHECK(tinydraw::simplify_stroke_samples(input, output, -1.0F, 0.25F).empty());
}
