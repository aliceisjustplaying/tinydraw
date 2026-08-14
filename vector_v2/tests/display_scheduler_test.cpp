#include "tinydraw/vector_v2/display_scheduler.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("display scheduler preserves strip order and completion identity") {
  std::array<vector_v2::DisplayStrip, 2> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
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
  std::array<vector_v2::DisplayStrip, 1> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
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
  std::array<vector_v2::DisplayStrip, 3> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
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
  CHECK(scheduler.front()->strip.revision == vector_v2::DocumentRevision{2});
}

TEST_CASE("display scheduler lets an in-flight old revision complete before dropping stale queue") {
  std::array<vector_v2::DisplayStrip, 2> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
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
  std::array<vector_v2::DisplayStrip, 1> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
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
  std::array<vector_v2::DisplayStrip, 1> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
  scheduler.require_revision({0});
  std::array<std::uint16_t, 8> pixels{};
  CHECK_FALSE(scheduler.schedule(
      {.revision = {0}, .panel_bounds = {0, 0, 4, 2}, .pixels = pixels, .stride = 3}));
  REQUIRE(scheduler.schedule(
      {.revision = {0}, .panel_bounds = {0, 0, 4, 2}, .pixels = pixels, .stride = 4}));
}

TEST_CASE("scheduler validates ring-addressed strips") {
  std::array<vector_v2::DisplayStrip, 4> storage{};
  vector_v2::DisplayScheduler scheduler(storage);
  scheduler.require_revision({1});
  std::vector<std::uint16_t> area(static_cast<std::size_t>(vector_v2::kOverviewWidth) * 100U);
  const vector_v2::DisplayStrip ring{
      .revision = {1},
      .panel_bounds = {0, 0, vector_v2::kOverviewWidth, 20},
      .pixels = area,
      .stride = vector_v2::kOverviewWidth,
      .source_shift_x = 30,
      .source_shift_y = 7,
      .source_area_width = vector_v2::kOverviewWidth,
      .source_area_height = 100,
  };
  CHECK(scheduler.schedule(ring).has_value());
  // Shift outside the ring extent is rejected.
  auto bad_shift = ring;
  bad_shift.source_shift_x = vector_v2::kOverviewWidth;
  CHECK_FALSE(scheduler.schedule(bad_shift).has_value());
  // A ring smaller than the panel bounds is rejected.
  auto small_area = ring;
  small_area.source_area_height = 10;
  CHECK_FALSE(scheduler.schedule(small_area).has_value());
  // Linear strips must not carry ring fields.
  auto stray = ring;
  stray.source_area_width = 0;
  CHECK_FALSE(scheduler.schedule(stray).has_value());
  // An undersized ring span is rejected.
  auto undersized = ring;
  undersized.pixels = std::span(area).first(area.size() - vector_v2::kOverviewWidth);
  CHECK_FALSE(scheduler.schedule(undersized).has_value());
}
