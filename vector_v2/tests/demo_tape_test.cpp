#include "tinydraw/vector_v2/demo_tape.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("V2 demo tape preserves touch and zoom timing") {
  std::array<vector_v2::DemoSample, 8> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(1'000U);

  CHECK(tape.record_touch({{12.0F, 34.0F}, 1'100U, 7U, vector_v2::TouchEventKind::kDown}));
  CHECK(tape.record_zoom(1'250U));
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

  REQUIRE(tape.pop_replay(10'250U)->kind == vector_v2::DemoEventKind::kZoom);
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
  CHECK(tape.record_zoom(110U));
  CHECK(tape.record_zoom(120U));
  CHECK_FALSE(tape.record_zoom(130U));
  CHECK(tape.overflowed());
  CHECK_FALSE(tape.recording());
  CHECK(tape.size() == 2U);
}

TEST_CASE("V2 demo replay handles wrapping microsecond clocks") {
  std::array<vector_v2::DemoSample, 2> storage{};
  vector_v2::DemoTape tape(storage);
  tape.begin_recording(UINT32_MAX - 20U);
  CHECK(tape.record_zoom(UINT32_MAX - 10U));
  CHECK(tape.record_zoom(9U));
  tape.stop_recording();

  REQUIRE(tape.begin_replay(UINT32_MAX - 5U));
  CHECK_FALSE(tape.replay_due(3U));
  CHECK(tape.replay_due(4U));
  REQUIRE(tape.pop_replay(4U).has_value());
  CHECK_FALSE(tape.replay_due(23U));
  CHECK(tape.replay_due(24U));
}
