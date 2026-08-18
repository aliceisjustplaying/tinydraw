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

TEST_CASE("a hard-overflow down edge resynchronizes to the current contact") {
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
        vector_v2::TouchOfferResult::kResynchronized);
  REQUIRE(events.pending() == 1U);
  const auto down = events.pop();
  REQUIRE(down.has_value());
  CHECK(down->kind == vector_v2::TouchEventKind::kDown);
  CHECK(down->point.x == 9.0F);
}

TEST_CASE("an edge-only overflow resynchronizes a complete brief tap") {
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
        vector_v2::TouchOfferResult::kResynchronized);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 50U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 60U) !=
        vector_v2::TouchOfferResult::kOverflow);

  REQUIRE(events.pending() == 2U);
  const auto down = events.pop();
  REQUIRE(down.has_value());
  CHECK(down->kind == vector_v2::TouchEventKind::kDown);
  CHECK(down->point.x == 9.0F);
  const auto up = events.pop();
  REQUIRE(up.has_value());
  CHECK(up->kind == vector_v2::TouchEventKind::kUp);
  CHECK(up->point.x == 9.0F);
}

TEST_CASE("hard-overflow lift preserves an undelivered active gesture") {
  std::array<vector_v2::TouchEvent, 5> storage{};
  vector_v2::TouchEventBuffer events(storage);
  for (int gesture = 0; gesture < 2; ++gesture) {
    CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {static_cast<float>(gesture), 0.0F},
                       10U) == vector_v2::TouchOfferResult::kQueued);
    CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 20U) ==
          vector_v2::TouchOfferResult::kIgnored);
    CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 30U) ==
          vector_v2::TouchOfferResult::kQueued);
  }
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {7.0F, 8.0F}, 40U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 50U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 60U) ==
        vector_v2::TouchOfferResult::kResynchronized);

  REQUIRE(events.pending() == 2U);
  const auto down = events.pop();
  REQUIRE(down.has_value());
  CHECK(down->kind == vector_v2::TouchEventKind::kDown);
  CHECK(down->point.x == 7.0F);
  CHECK(down->point.y == 8.0F);
  const auto up = events.pop();
  REQUIRE(up.has_value());
  CHECK(up->kind == vector_v2::TouchEventKind::kUp);
  CHECK(up->point.x == 7.0F);
  CHECK(up->point.y == 8.0F);
  CHECK(up->timestamp_us > down->timestamp_us);
  CHECK(up->sequence > down->sequence);
}

TEST_CASE("hard-overflow down closes the delivered gesture before resynchronizing") {
  std::array<vector_v2::TouchEvent, 5> storage{};
  vector_v2::TouchEventBuffer events(storage);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {1.0F, 2.0F}, 10U) ==
        vector_v2::TouchOfferResult::kQueued);
  REQUIRE(events.pop()->kind == vector_v2::TouchEventKind::kDown);
  for (int gesture = 0; gesture < 2; ++gesture) {
    CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 20U) ==
          vector_v2::TouchOfferResult::kIgnored);
    CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 30U) ==
          vector_v2::TouchOfferResult::kQueued);
    CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {static_cast<float>(gesture + 3), 4.0F},
                       40U) == vector_v2::TouchOfferResult::kQueued);
  }
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 50U) ==
        vector_v2::TouchOfferResult::kIgnored);
  CHECK(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, 60U) ==
        vector_v2::TouchOfferResult::kQueued);
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {9.0F, 10.0F}, 70U) ==
        vector_v2::TouchOfferResult::kResynchronized);

  REQUIRE(events.pending() == 2U);
  const auto up = events.pop();
  REQUIRE(up.has_value());
  CHECK(up->kind == vector_v2::TouchEventKind::kUp);
  const auto down = events.pop();
  REQUIRE(down.has_value());
  CHECK(down->kind == vector_v2::TouchEventKind::kDown);
  CHECK(down->point.x == 9.0F);
  CHECK(down->point.y == 10.0F);
  CHECK(down->timestamp_us == up->timestamp_us);
  CHECK(down->sequence > up->sequence);
}

TEST_CASE("touch buffer rejects storage too small for bounded edge resynchronization") {
  std::array<vector_v2::TouchEvent, 1> storage{};
  vector_v2::TouchEventBuffer events(storage);
  CHECK_FALSE(events.ready());
  CHECK(events.offer(vector_v2::TouchContactRead::kPoint, {1.0F, 2.0F}, 10U) ==
        vector_v2::TouchOfferResult::kOverflow);
  CHECK(events.pending() == 0U);
}

TEST_CASE("overflow stress always delivers a well-formed contact stream") {
  std::array<vector_v2::TouchEvent, 4> storage{};
  vector_v2::TouchEventBuffer events(storage);
  std::uint32_t random = 0xC0FFEEU;
  std::uint32_t timestamp = 0U;
  std::uint32_t last_sequence = 0U;
  bool delivered_touching = false;
  const auto consume = [&] {
    const auto event = events.pop();
    if (!event.has_value()) {
      return;
    }
    CHECK(event->sequence > last_sequence);
    last_sequence = event->sequence;
    if (event->kind == vector_v2::TouchEventKind::kDown) {
      CHECK_FALSE(delivered_touching);
      delivered_touching = true;
    } else if (event->kind == vector_v2::TouchEventKind::kMove) {
      CHECK(delivered_touching);
    } else {
      CHECK(delivered_touching);
      delivered_touching = false;
    }
  };

  for (std::uint32_t iteration = 0; iteration < 12'000U; ++iteration) {
    random = random * 1'664'525U + 1'013'904'223U;
    timestamp += 1'000U;
    if ((random & 7U) < 5U) {
      static_cast<void>(events.offer(
          vector_v2::TouchContactRead::kPoint,
          {static_cast<float>(random & 255U), static_cast<float>((random >> 8U) & 255U)},
          timestamp));
    } else {
      static_cast<void>(events.offer(vector_v2::TouchContactRead::kNoTouch, {}, timestamp));
    }
    if ((random & 31U) == 0U) {
      consume();
    }
  }
  while (events.pending() != 0U) {
    consume();
  }
}
