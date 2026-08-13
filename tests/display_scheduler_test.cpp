#include "tinydraw/production/display_scheduler.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace production = tinydraw::production;

TEST_CASE("display scheduler preserves strip order and completion identity") {
  std::array<production::DisplayStrip, 2> storage{};
  production::DisplayScheduler scheduler(storage);
  scheduler.require_revision({3});
  std::array<std::uint16_t, 368U * 2U> first_pixels{};
  std::array<std::uint16_t, 368U * 2U> second_pixels{};
  const auto first = scheduler.schedule(
      {.revision = {3}, .panel_bounds = {0, 0, 368, 2}, .pixels = first_pixels, .stride = 368});
  const auto second = scheduler.schedule(
      {.revision = {3}, .panel_bounds = {0, 2, 368, 4}, .pixels = second_pixels, .stride = 368});
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  const auto scheduled_first = scheduler.front();
  REQUIRE(scheduled_first.has_value());
  CHECK(scheduled_first->sequence == *first);
  CHECK(scheduled_first->strip.pixels.data() == first_pixels.data());
  CHECK_FALSE(scheduler.front());
  CHECK_FALSE(scheduler.complete(*second));
  REQUIRE(scheduler.complete(*first));
  CHECK(scheduler.front()->sequence == *second);
  REQUIRE(scheduler.complete(*second));
  CHECK_FALSE(scheduler.front());
  CHECK(scheduler.stats().completed == 2U);
}

TEST_CASE("display scheduler rejects stale malformed and over-capacity strips") {
  std::array<production::DisplayStrip, 1> storage{};
  production::DisplayScheduler scheduler(storage);
  scheduler.require_revision({2});
  std::array<std::uint16_t, 368U * 2U> pixels{};
  CHECK_FALSE(scheduler.schedule(
      {.revision = {1}, .panel_bounds = {0, 0, 368, 2}, .pixels = pixels, .stride = 368}));
  CHECK_FALSE(scheduler.schedule(
      {.revision = {2}, .panel_bounds = {1, 0, 368, 2}, .pixels = pixels, .stride = 368}));
  REQUIRE(scheduler.schedule(
      {.revision = {2}, .panel_bounds = {0, 0, 368, 2}, .pixels = pixels, .stride = 368}));
  CHECK_FALSE(scheduler.schedule(
      {.revision = {2}, .panel_bounds = {0, 2, 368, 4}, .pixels = pixels, .stride = 368}));
  CHECK(scheduler.stats().rejected == 3U);
  CHECK(scheduler.stats().stale_rejected == 1U);
}

TEST_CASE("display scheduler drops queued stale revisions before transport") {
  std::array<production::DisplayStrip, 3> storage{};
  production::DisplayScheduler scheduler(storage);
  std::array<std::uint16_t, 368U * 2U> pixels{};
  scheduler.require_revision({1});
  REQUIRE(scheduler.schedule(
      {.revision = {1}, .panel_bounds = {0, 0, 368, 2}, .pixels = pixels, .stride = 368}));
  REQUIRE(scheduler.schedule(
      {.revision = {1}, .panel_bounds = {0, 2, 368, 4}, .pixels = pixels, .stride = 368}));

  scheduler.require_revision({2});
  CHECK_FALSE(scheduler.front());
  CHECK(scheduler.stats().queued == 0U);
  CHECK(scheduler.stats().stale_rejected == 2U);
  REQUIRE(scheduler.schedule(
      {.revision = {2}, .panel_bounds = {0, 4, 368, 6}, .pixels = pixels, .stride = 368}));
  CHECK(scheduler.front()->strip.revision == production::DocumentRevision{2});
}

TEST_CASE("display scheduler lets an in-flight old revision complete before dropping stale queue") {
  std::array<production::DisplayStrip, 2> storage{};
  production::DisplayScheduler scheduler(storage);
  std::array<std::uint16_t, 368U * 2U> pixels{};
  scheduler.require_revision({1});
  const auto first = scheduler.schedule(
      {.revision = {1}, .panel_bounds = {0, 0, 368, 2}, .pixels = pixels, .stride = 368});
  REQUIRE(first.has_value());
  REQUIRE(scheduler.schedule(
      {.revision = {1}, .panel_bounds = {0, 2, 368, 4}, .pixels = pixels, .stride = 368}));
  const auto in_flight = scheduler.front();
  REQUIRE(in_flight.has_value());
  CHECK(in_flight->sequence == *first);

  scheduler.require_revision({2});
  CHECK_FALSE(scheduler.front());
  REQUIRE(scheduler.complete(*first));
  CHECK_FALSE(scheduler.front());
  CHECK(scheduler.stats().stale_rejected == 1U);
}

TEST_CASE("display scheduler aborts a strip that transport cannot stage") {
  std::array<production::DisplayStrip, 1> storage{};
  production::DisplayScheduler scheduler(storage);
  scheduler.require_revision({1});
  std::array<std::uint16_t, 368U * 2U> pixels{};
  const auto sequence = scheduler.schedule(
      {.revision = {1}, .panel_bounds = {0, 0, 368, 2}, .pixels = pixels, .stride = 368});
  REQUIRE(sequence.has_value());
  REQUIRE(scheduler.front().has_value());
  REQUIRE(scheduler.abort(*sequence));
  CHECK_FALSE(scheduler.front());
  CHECK(scheduler.stats().completed == 0U);
  CHECK(scheduler.stats().rejected == 1U);
  CHECK(scheduler.stats().queued == 0U);
}

TEST_CASE("display scheduler validates stride-backed input") {
  std::array<production::DisplayStrip, 1> storage{};
  production::DisplayScheduler scheduler(storage);
  scheduler.require_revision({0});
  std::array<std::uint16_t, 8> pixels{};
  CHECK_FALSE(scheduler.schedule(
      {.revision = {0}, .panel_bounds = {0, 0, 4, 2}, .pixels = pixels, .stride = 3}));
  REQUIRE(scheduler.schedule(
      {.revision = {0}, .panel_bounds = {0, 0, 4, 2}, .pixels = pixels, .stride = 4}));
}
