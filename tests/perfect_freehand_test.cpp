#include "tinydraw/ink/perfect_freehand.h"

#include <doctest.h>

#include <array>

TEST_CASE("PF stroke points match the pinned reference fixture") {
  constexpr std::array input{
      tinydraw::Point{30.0F, 40.0F},   tinydraw::Point{80.0F, 90.0F},
      tinydraw::Point{150.0F, 50.0F},  tinydraw::Point{220.0F, 180.0F},
      tinydraw::Point{180.0F, 300.0F}, tinydraw::Point{280.0F, 350.0F},
      tinydraw::Point{340.0F, 410.0F},
  };
  constexpr std::array expected_positions{
      tinydraw::Point{30.0F, 40.0F},
      tinydraw::Point{65.125F, 75.125F},
      tinydraw::Point{124.7496875F, 57.4746875F},
      tinydraw::Point{191.66303203125F, 143.54871953125F},
      tinydraw::Point{183.46975202929687F, 253.45574406054686F},
      tinydraw::Point{251.28225122871584F, 321.2780838580127F},
      tinydraw::Point{340.0F, 410.0F},
  };
  constexpr std::array expected_distances{0.0F,
                                          49.67425137835497F,
                                          62.18228759719532F,
                                          109.0235509716796F,
                                          110.21199516420101F,
                                          95.90831467226454F,
                                          125.46879034624716F};
  constexpr std::array expected_running_lengths{
      0.0F,
      49.67425137835497F,
      111.85653897555028F,
      220.88008994722986F,
      331.0920851114309F,
      427.00039978369546F,
      552.4691901299426F,
  };

  const auto actual = tinydraw::perfect_freehand::get_stroke_points(input, {}, true);

  REQUIRE(actual.size() == expected_positions.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    CAPTURE(index);
    CHECK(actual[index].position.x == doctest::Approx(expected_positions[index].x).epsilon(0.0001));
    CHECK(actual[index].position.y == doctest::Approx(expected_positions[index].y).epsilon(0.0001));
    CHECK(actual[index].distance == doctest::Approx(expected_distances[index]).epsilon(0.0001));
    CHECK(actual[index].running_length ==
          doctest::Approx(expected_running_lengths[index]).epsilon(0.0001));
  }
}

TEST_CASE("PF stroke point degenerates match reference behavior") {
  CHECK(tinydraw::perfect_freehand::get_stroke_points({}, {}, false).empty());

  constexpr std::array one_point{tinydraw::Point{10.0F, 20.0F}};
  const auto dot = tinydraw::perfect_freehand::get_stroke_points(one_point, {}, false);
  REQUIRE(dot.size() == 2U);
  CHECK(dot.front().position.x == doctest::Approx(10.0F));
  CHECK(dot.back().position.x == doctest::Approx(10.7025F));

  constexpr std::array duplicate_points{tinydraw::Point{10.0F, 20.0F},
                                        tinydraw::Point{10.0F, 20.0F}};
  const auto duplicate = tinydraw::perfect_freehand::get_stroke_points(duplicate_points, {}, false);
  CHECK(duplicate.size() == 1U);
}
