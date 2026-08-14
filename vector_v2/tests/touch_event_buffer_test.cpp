#include "tinydraw/vector_v2/touch_event_buffer.h"

#include <doctest.h>

#include <array>

namespace vector_v2 = tinydraw::vector_v2;

TEST_CASE("touch errors hold contact and lift publishes the final point") {
  std::array<vector_v2::TouchEvent, 8> storage{};
  vector_v2::TouchEventBuffer events(storage);

  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {10.0F, 20.0F}, 100U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {15.0F, 25.0F}, 200U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 300U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kError, {}, 400U) ==
        vector_v2::TouchOfferResult::kErrorHeld);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 500U) ==
        vector_v2::TouchOfferResult::kQueued);

  const auto down = events.pop();
  REQUIRE(down.has_value());
  CHECK(down->kind == vector_v2::TouchEventKind::kDown);
  const auto move = events.pop();
  REQUIRE(move.has_value());
  CHECK(move->kind == vector_v2::TouchEventKind::kMove);
  CHECK(move->point.x == 15.0F);
  const auto up = events.pop();
  REQUIRE(up.has_value());
  CHECK(up->kind == vector_v2::TouchEventKind::kUp);
  CHECK(up->point.x == 15.0F);
  CHECK(up->point.y == 25.0F);
  CHECK(up->timestamp_us == 500U);
}

TEST_CASE("touch buffer coalesces moves without replacing gesture edges") {
  std::array<vector_v2::TouchEvent, 8> storage{};
  vector_v2::TouchEventBuffer events(storage);

  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {1.0F, 1.0F}, 10U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {2.0F, 2.0F}, 20U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {3.0F, 3.0F}, 30U) ==
        vector_v2::TouchOfferResult::kMoveCoalesced);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 40U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 50U) ==
        vector_v2::TouchOfferResult::kQueued);

  REQUIRE(events.pending() == 3U);
  CHECK(events.pop()->kind == vector_v2::TouchEventKind::kDown);
  const auto move = events.pop();
  REQUIRE(move.has_value());
  CHECK(move->kind == vector_v2::TouchEventKind::kMove);
  CHECK(move->point.x == 3.0F);
  CHECK(events.pop()->kind == vector_v2::TouchEventKind::kUp);
}

TEST_CASE("gesture edges displace queued moves before overflowing") {
  std::array<vector_v2::TouchEvent, 4> storage{};
  vector_v2::TouchEventBuffer events(storage);

  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {1.0F, 1.0F}, 10U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {2.0F, 2.0F}, 20U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 30U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 40U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {3.0F, 3.0F}, 50U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 60U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 70U) ==
        vector_v2::TouchOfferResult::kQueued);

  REQUIRE(events.pending() == 4U);
  CHECK(events.pop()->kind == vector_v2::TouchEventKind::kDown);
  CHECK(events.pop()->kind == vector_v2::TouchEventKind::kUp);
  CHECK(events.pop()->kind == vector_v2::TouchEventKind::kDown);
  CHECK(events.pop()->kind == vector_v2::TouchEventKind::kUp);
}

TEST_CASE("a refused down edge is retried instead of changing contact state") {
  std::array<vector_v2::TouchEvent, 4> storage{};
  vector_v2::TouchEventBuffer events(storage);
  for (int gesture = 0; gesture < 2; ++gesture) {
    CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {static_cast<float>(gesture), 0.0F},
                       10U) == vector_v2::TouchOfferResult::kQueued);
    CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 20U) ==
          vector_v2::TouchOfferResult::kIgnored);
    CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 30U) ==
          vector_v2::TouchOfferResult::kQueued);
  }
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {9.0F, 9.0F}, 40U) ==
        vector_v2::TouchOfferResult::kOverflow);
  REQUIRE(events.pop().has_value());
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {9.0F, 9.0F}, 50U) ==
        vector_v2::TouchOfferResult::kQueued);
}
