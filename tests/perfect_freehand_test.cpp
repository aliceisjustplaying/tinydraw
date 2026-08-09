#include "tinydraw/ink/perfect_freehand.h"

#include <doctest.h>

#include <array>
#include <fstream>
#include <string>
#include <vector>

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

TEST_CASE("PF outline matches every point in the pinned reference fixture") {
  constexpr std::array input{
      tinydraw::Point{30.0F, 40.0F},   tinydraw::Point{80.0F, 90.0F},
      tinydraw::Point{150.0F, 50.0F},  tinydraw::Point{220.0F, 180.0F},
      tinydraw::Point{180.0F, 300.0F}, tinydraw::Point{280.0F, 350.0F},
      tinydraw::Point{340.0F, 410.0F},
  };
  std::ifstream reference(std::string(TINYDRAW_SOURCE_DIR) +
                          "/testdata/reference/pf-zigzag.outline");
  REQUIRE(reference.good());
  std::vector<tinydraw::Point> expected;
  tinydraw::Point point{};
  while (reference >> point.x >> point.y) {
    expected.push_back(point);
  }

  const auto actual = tinydraw::perfect_freehand::get_stroke(input, {}, true);

  REQUIRE(actual.size() == expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    CAPTURE(index);
    CHECK(actual[index].x == doctest::Approx(expected[index].x).epsilon(0.0001));
    CHECK(actual[index].y == doctest::Approx(expected[index].y).epsilon(0.0001));
  }
}

TEST_CASE("PF outline matches the longer dependency probe") {
  constexpr std::array input{
      tinydraw::Point{24.0F, 40.0F},   tinydraw::Point{35.0F, 45.0F},
      tinydraw::Point{48.0F, 52.0F},   tinydraw::Point{64.0F, 63.0F},
      tinydraw::Point{82.0F, 80.0F},   tinydraw::Point{101.0F, 104.0F},
      tinydraw::Point{121.0F, 132.0F}, tinydraw::Point{144.0F, 160.0F},
      tinydraw::Point{170.0F, 182.0F}, tinydraw::Point{198.0F, 194.0F},
      tinydraw::Point{224.0F, 187.0F}, tinydraw::Point{244.0F, 164.0F},
      tinydraw::Point{252.0F, 132.0F}, tinydraw::Point{242.0F, 100.0F},
      tinydraw::Point{218.0F, 76.0F},  tinydraw::Point{186.0F, 66.0F},
      tinydraw::Point{154.0F, 76.0F},  tinydraw::Point{132.0F, 102.0F},
      tinydraw::Point{128.0F, 134.0F}, tinydraw::Point{144.0F, 162.0F},
  };
  std::ifstream reference(std::string(TINYDRAW_SOURCE_DIR) +
                          "/testdata/reference/pf-dependency-probe.outline");
  REQUIRE(reference.good());
  std::vector<tinydraw::Point> expected;
  tinydraw::Point point{};
  while (reference >> point.x >> point.y) {
    expected.push_back(point);
  }

  const auto actual = tinydraw::perfect_freehand::get_stroke(input, {}, true);

  REQUIRE(actual.size() == expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    CAPTURE(index);
    CHECK(actual[index].x == doctest::Approx(expected[index].x).epsilon(0.0001));
    CHECK(actual[index].y == doctest::Approx(expected[index].y).epsilon(0.0001));
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
