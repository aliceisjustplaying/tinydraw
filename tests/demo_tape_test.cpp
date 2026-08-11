#include "tinydraw/demo/demo_tape.h"

#include <doctest.h>

#include <array>
#include <cstdint>

TEST_CASE("demo tape stores relative timing and recreates input events") {
  std::array<tinydraw::DemoSample, 3> storage{};
  tinydraw::DemoTape tape(storage);

  tape.begin_recording(10'000U);
  CHECK(tape.record({.point = {12.0F, 34.0F}, .timestamp_us = 10'000U, .touching = true}));
  CHECK(tape.record({.point = {56.0F, 78.0F}, .timestamp_us = 23'500U, .touching = true}));
  CHECK(tape.record({.point = {56.0F, 78.0F}, .timestamp_us = 30'000U, .touching = false}));
  tape.stop_recording();

  REQUIRE(tape.size() == 3U);
  CHECK_FALSE(tape.recording());
  CHECK_FALSE(tape.overflowed());
  CHECK(tape.samples()[1].offset_us == 13'500U);

  const auto replayed = tape.event_at(1U, 1'000'000U);
  REQUIRE(replayed.has_value());
  CHECK(replayed->point.x == doctest::Approx(56.0F));
  CHECK(replayed->point.y == doctest::Approx(78.0F));
  CHECK(replayed->timestamp_us == 1'013'500U);
  CHECK(replayed->touching);
}

TEST_CASE("demo tape is bounded and reports overflow") {
  std::array<tinydraw::DemoSample, 1> storage{};
  tinydraw::DemoTape tape(storage);

  tape.begin_recording(100U);
  CHECK(tape.record({.point = {1.0F, 2.0F}, .timestamp_us = 100U, .touching = true}));
  CHECK_FALSE(tape.record({.point = {3.0F, 4.0F}, .timestamp_us = 200U, .touching = false}));

  CHECK(tape.size() == 1U);
  CHECK(tape.overflowed());
}

TEST_CASE("demo tape handles the 32-bit input clock wrapping") {
  std::array<tinydraw::DemoSample, 1> storage{};
  tinydraw::DemoTape tape(storage);

  tape.begin_recording(UINT32_MAX - 9U);
  CHECK(tape.record({.point = {5.0F, 6.0F}, .timestamp_us = 10U, .touching = true}));

  CHECK(tape.samples().front().offset_us == 20U);
}
