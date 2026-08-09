#include "tinydraw/ink/ink_stream.h"

#include <doctest.h>

#include <cmath>

TEST_CASE("a stroke starts at the touch-down point") {
  tinydraw::InkStream stream;

  const auto point = stream.begin({.x = 12.0F, .y = 34.0F, .timestamp_us = 1'000U});

  CHECK(stream.active());
  CHECK(point.position.x == doctest::Approx(12.0F));
  CHECK(point.position.y == doctest::Approx(34.0F));
  CHECK(point.pressure == doctest::Approx(0.25F));
  CHECK(point.radius > 0.0F);
}

TEST_CASE("brush size is the diameter at neutral pressure") {
  tinydraw::InkConfig config;
  config.simulate_pressure = false;
  tinydraw::InkStream stream(config);
  static_cast<void>(stream.begin({.x = 0.0F, .y = 0.0F, .timestamp_us = 0U}));

  const auto point = stream.update({.x = 1.0F, .y = 0.0F, .timestamp_us = 8'000U});

  CHECK(point.pressure == doctest::Approx(0.5F));
  CHECK(point.radius == doctest::Approx(3.0F));
}

TEST_CASE("movement is streamlined and drives simulated pressure") {
  tinydraw::InkStream stream;
  static_cast<void>(stream.begin({.x = 0.0F, .y = 0.0F, .timestamp_us = 1'000U}));

  const auto point = stream.update({.x = 100.0F, .y = 0.0F, .timestamp_us = 9'000U});

  CHECK(point.position.x == doctest::Approx(70.25F));
  CHECK(point.distance == doctest::Approx(70.25F));
  CHECK(point.running_length == doctest::Approx(70.25F));
  CHECK(point.pressure == doctest::Approx(0.18125F));
  CHECK(point.radius < 2.7F);
}

namespace {

tinydraw::InkPoint replay_linear_gesture(std::uint32_t interval_us) {
  tinydraw::InkStream stream;
  auto result = stream.begin({.x = 0.0F, .y = 20.0F, .timestamp_us = 0U});
  for (std::uint32_t time_us = interval_us; time_us <= 100'000U; time_us += interval_us) {
    const float x = static_cast<float>(time_us) / 1'000.0F;
    result = stream.update({.x = x, .y = 20.0F, .timestamp_us = time_us});
  }
  return result;
}

}  // namespace

TEST_CASE("filtering is stable across modest sample-rate changes") {
  const auto at_100_hz = replay_linear_gesture(10'000U);
  const auto at_200_hz = replay_linear_gesture(5'000U);

  CHECK(at_100_hz.position.x == doctest::Approx(at_200_hz.position.x).epsilon(0.03));
  CHECK(at_100_hz.pressure == doctest::Approx(at_200_hz.pressure).epsilon(0.08));
  CHECK(at_100_hz.radius == doctest::Approx(at_200_hz.radius).epsilon(0.08));
}

TEST_CASE("equal timestamps remain finite and advance the stroke") {
  tinydraw::InkStream stream;
  static_cast<void>(stream.begin({.x = 10.0F, .y = 10.0F, .timestamp_us = 42U}));

  const auto point = stream.update({.x = 20.0F, .y = 10.0F, .timestamp_us = 42U});

  CHECK(std::isfinite(point.position.x));
  CHECK(std::isfinite(point.pressure));
  CHECK(std::isfinite(point.radius));
  CHECK(point.position.x > 10.0F);
}

TEST_CASE("a backward timestamp uses a nominal interval without poisoning the next sample") {
  tinydraw::InkStream stream;
  static_cast<void>(stream.begin({.x = 0.0F, .y = 0.0F, .timestamp_us = 10'000U}));

  const auto regressed = stream.update({.x = 10.0F, .y = 0.0F, .timestamp_us = 9'000U});
  const auto recovered = stream.update({.x = 20.0F, .y = 0.0F, .timestamp_us = 18'000U});

  CHECK(regressed.timestamp_us == 10'000U);
  CHECK(regressed.position.x < 10.0F);
  CHECK(recovered.timestamp_us == 18'000U);
  CHECK(recovered.position.x < 20.0F);
}

TEST_CASE("timestamp wrap-around preserves the short elapsed interval") {
  tinydraw::InkStream stream;
  static_cast<void>(stream.begin({.x = 0.0F, .y = 0.0F, .timestamp_us = 0xFFFF'F000U}));

  const auto point = stream.update({.x = 10.0F, .y = 0.0F, .timestamp_us = 0x0000'0F40U});

  CHECK(point.timestamp_us == 0x0000'0F40U);
  CHECK(point.position.x == doctest::Approx(7.025F));
}

TEST_CASE("ending a stroke permits a new independent stroke") {
  tinydraw::InkStream stream;
  static_cast<void>(stream.begin({.x = 1.0F, .y = 2.0F, .timestamp_us = 0U}));
  static_cast<void>(stream.update({.x = 50.0F, .y = 60.0F, .timestamp_us = 8'000U}));
  stream.end();

  const auto point = stream.begin({.x = 300.0F, .y = 400.0F, .timestamp_us = 20'000U});

  CHECK(point.position.x == doctest::Approx(300.0F));
  CHECK(point.position.y == doctest::Approx(400.0F));
  CHECK(point.running_length == doctest::Approx(0.0F));
}
