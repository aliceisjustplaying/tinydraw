#include "tinydraw/vector_v2/demo_tape.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <limits>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("V2 demo tape preserves touch and chrome-toggle timing") {
  std::array<vector_v2::DemoSample, 8> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(1'000U);

  CHECK(tape.record_touch({{12.0F, 34.0F}, 1'100U, 7U, vector_v2::TouchEventKind::kDown}));
  CHECK(tape.record_chrome_toggle(1'250U));
  CHECK(tape.record_touch({{56.0F, 78.0F}, 1'400U, 8U, vector_v2::TouchEventKind::kUp}));
  tape.stop_recording();

  REQUIRE(tape.size() == 3U);
  REQUIRE(tape.begin_replay(10'000U));
  CHECK_FALSE(tape.replay_due(10'099U));
  REQUIRE(tape.replay_due(10'100U));
  const auto down = tape.pop_replay(10'100U);
  REQUIRE(down.has_value());
  CHECK(down->kind == vector_v2::DemoEventKind::kTouchDown);
  CHECK(down->point.x == 12.0F);
  CHECK(down->point.y == 34.0F);
  CHECK(down->timestamp_us == 10'100U);

  const auto chrome_toggle = tape.pop_replay(10'250U);
  REQUIRE(chrome_toggle.has_value());
  CHECK(chrome_toggle->kind == vector_v2::DemoEventKind::kChromeToggle);
  CHECK_FALSE(tape.replay_due(10'399U));
  const auto up = tape.pop_replay(10'400U);
  REQUIRE(up.has_value());
  CHECK(up->kind == vector_v2::DemoEventKind::kTouchUp);
  CHECK_FALSE(tape.replaying());
}

TEST_CASE("V2 demo tape stops at capacity without overwriting the take") {
  std::array<vector_v2::DemoSample, 2> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(100U);
  CHECK(tape.record_chrome_toggle(110U));
  CHECK(tape.record_chrome_toggle(120U));
  CHECK_FALSE(tape.record_chrome_toggle(130U));
  CHECK(tape.overflowed());
  CHECK_FALSE(tape.recording());
  CHECK(tape.size() == 2U);
}

TEST_CASE("V2 demo tape does not spend capacity on stationary touch polls") {
  std::array<vector_v2::DemoSample, 3> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(100U);

  REQUIRE(tape.record_touch({{12.0F, 34.0F}, 110U, 1U, vector_v2::TouchEventKind::kDown}));
  bool all_recorded = true;
  for (std::uint32_t index = 0U; index < 10'000U; ++index) {
    all_recorded &= tape.record_touch(
        {{12.0F, 34.0F}, 120U + index, index + 2U, vector_v2::TouchEventKind::kMove});
  }
  REQUIRE(tape.record_touch({{56.0F, 78.0F}, 10'120U, 10'002U, vector_v2::TouchEventKind::kMove}));
  for (std::uint32_t index = 0U; index < 10'000U; ++index) {
    all_recorded &= tape.record_touch(
        {{56.0F, 78.0F}, 10'121U + index, index + 10'003U, vector_v2::TouchEventKind::kMove});
  }
  REQUIRE(all_recorded);
  REQUIRE(tape.record_touch({{56.0F, 78.0F}, 20'121U, 20'003U, vector_v2::TouchEventKind::kUp}));

  CHECK(tape.recording());
  CHECK_FALSE(tape.overflowed());
  REQUIRE(tape.size() == 3U);
  REQUIRE(tape.begin_replay(30'000U));
  REQUIRE(tape.pop_replay(30'010U).has_value());
  const auto move = tape.pop_replay(40'020U);
  REQUIRE(move.has_value());
  CHECK(move->kind == vector_v2::DemoEventKind::kTouchMove);
  CHECK(move->point.x == 56.0F);
  CHECK(move->point.y == 78.0F);
  CHECK(move->timestamp_us == 40'020U);
}

TEST_CASE("V2 demo replay handles wrapping microsecond clocks") {
  std::array<vector_v2::DemoSample, 2> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(UINT32_MAX - 20U);
  CHECK(tape.record_chrome_toggle(UINT32_MAX - 10U));
  CHECK(tape.record_chrome_toggle(9U));
  tape.stop_recording();

  REQUIRE(tape.begin_replay(UINT32_MAX - 5U));
  CHECK_FALSE(tape.replay_due(3U));
  CHECK(tape.replay_due(4U));
  REQUIRE(tape.pop_replay(4U).has_value());
  CHECK_FALSE(tape.replay_due(23U));
  CHECK(tape.replay_due(24U));
}

TEST_CASE("V2 demo tape rejects non-finite touch coordinates") {
  std::array<vector_v2::DemoSample, 2> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(100U);

  CHECK_FALSE(tape.record_touch({{std::numeric_limits<float>::quiet_NaN(), 10.0F},
                                 110U,
                                 1U,
                                 vector_v2::TouchEventKind::kDown}));
  CHECK_FALSE(tape.record_touch({{10.0F, std::numeric_limits<float>::infinity()},
                                 120U,
                                 2U,
                                 vector_v2::TouchEventKind::kMove}));
  CHECK(tape.recording());
  CHECK(tape.size() == 0U);
}

TEST_CASE("V2 demo tape stops before replay timing becomes ambiguous") {
  std::array<vector_v2::DemoSample, 2> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(100U);

  CHECK(tape.record_chrome_toggle(100U + vector_v2::kMaximumDemoDurationUs));
  CHECK_FALSE(tape.record_chrome_toggle(100U + vector_v2::kMaximumDemoDurationUs + 1U));
  CHECK(tape.overflowed());
  CHECK_FALSE(tape.recording());
  CHECK(tape.size() == 1U);
}
