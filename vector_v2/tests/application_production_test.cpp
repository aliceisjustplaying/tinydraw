#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

#include "tinydraw/vector_v2/application.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct Publication {
  std::vector<std::uint16_t> frame{};
  bool background_pending = false;
};

class ApplicationBackend {
 public:
  explicit ApplicationBackend(bool production)
      : records(256U),
        samples(8'192U),
        stroke_samples(256U),
        canvas(vector_v2::kOverviewPixels),
        working(vector_v2::kOverviewPixels),
        frame(vector_v2::kOverviewPixels),
        overview(vector_v2::kOverviewPixels),
        working_overview(vector_v2::kOverviewPixels),
        chrome_cache(vector_v2::kChromeStagingCachePixels),
        uniforms(production ? vector_v2::kMaterializedTileIdentityCount : 0U),
        occupancy(production ? vector_v2::kOccupancyBytes : 0U),
        slots(production ? 64U : 0U),
        tile_pixels(slots.size() * vector_v2::kTilePixels),
        raw_directory(production ? vector_v2::kMaterializedTileIdentityCount : 0U),
        producer_pixels(production ? vector_v2::kTileProducerPixels : 0U),
        producer_mask(production ? vector_v2::kTileProducerMaskBytes : 0U),
        producer_rows(production ? vector_v2::kTileProducerSummaryRows : 0U),
        producer_words(production ? vector_v2::kTileProducerSummaryWords : 0U),
        producer_plan_words(
            production ? (vector_v2::kOperationChordStorageBytes + sizeof(std::uint32_t) - 1U) /
                             sizeof(std::uint32_t)
                       : 0U),
        producer_candidates(production ? records.size() : 0U),
        app({.records = records,
             .samples = samples,
             .stroke_samples = stroke_samples,
             .canvas_pixels = canvas,
             .working_pixels = working,
             .frame_pixels = frame,
             .overview_pixels = overview,
             .working_overview_pixels = working_overview,
             .chrome_cache_pixels = chrome_cache,
             .materialized_uniforms = uniforms,
             .materialized_occupancy = occupancy,
             .materialized_slots = slots,
             .materialized_tile_pixels = tile_pixels,
             .materialized_raw_slot_directory = raw_directory,
             .producer_supertask_pixels = producer_pixels,
             .producer_finalized_pixels = producer_mask,
             .producer_summary_rows = producer_rows,
             .producer_summary_words = producer_words,
             .producer_chord_plans = plan_bytes(),
             .producer_candidate_indices = producer_candidates}) {
    REQUIRE(app.ready());
  }

  vector_v2::ApplicationAdvanceResult advance(
      std::initializer_list<vector_v2::ApplicationEvent> events, std::size_t work_quanta = 0U) {
    now_us += 1'000U;
    return app.advance(now_us,
                       std::span<const vector_v2::ApplicationEvent>(events.begin(), events.size()),
                       work_quanta);
  }

  vector_v2::ApplicationAdvanceResult boot() { return advance({}, 0U); }

  vector_v2::ApplicationAdvanceResult zoom_next(std::size_t work_quanta = 0U) {
    return advance(
        {{.kind = vector_v2::ApplicationEventKind::kZoomNext, .timestamp_us = now_us + 1'000U}},
        work_quanta);
  }

  vector_v2::ApplicationAdvanceResult tap(float x, float y, std::size_t work_quanta = 0U) {
    return advance({{.kind = vector_v2::ApplicationEventKind::kTouchDown,
                     .x = x,
                     .y = y,
                     .timestamp_us = now_us + 1'000U},
                    {.kind = vector_v2::ApplicationEventKind::kTouchUp,
                     .x = x,
                     .y = y,
                     .timestamp_us = now_us + 2'000U}},
                   work_quanta);
  }

  vector_v2::ApplicationAdvanceResult stroke(float y, std::size_t work_quanta = 0U) {
    return advance({{.kind = vector_v2::ApplicationEventKind::kTouchDown,
                     .x = 60.0F,
                     .y = y,
                     .timestamp_us = now_us + 1'000U},
                    {.kind = vector_v2::ApplicationEventKind::kTouchMove,
                     .x = 110.0F,
                     .y = y + 15.0F,
                     .timestamp_us = now_us + 11'000U},
                    {.kind = vector_v2::ApplicationEventKind::kTouchMove,
                     .x = 155.0F,
                     .y = y - 10.0F,
                     .timestamp_us = now_us + 21'000U},
                    {.kind = vector_v2::ApplicationEventKind::kTouchUp,
                     .x = 190.0F,
                     .y = y,
                     .timestamp_us = now_us + 31'000U}},
                   work_quanta);
  }

  vector_v2::ApplicationAdvanceResult pan() {
    return advance({{.kind = vector_v2::ApplicationEventKind::kTouchDown,
                     .x = 200.0F,
                     .y = 150.0F,
                     .timestamp_us = now_us + 1'000U},
                    {.kind = vector_v2::ApplicationEventKind::kTouchMove,
                     .x = 120.0F,
                     .y = 100.0F,
                     .timestamp_us = now_us + 11'000U},
                    {.kind = vector_v2::ApplicationEventKind::kTouchUp,
                     .x = 120.0F,
                     .y = 100.0F,
                     .timestamp_us = now_us + 21'000U}},
                   0U);
  }

  std::vector<Publication> drain() {
    std::vector<Publication> publications;
    for (std::size_t tick = 0U; tick < 1'024U && app.status().background_pending; ++tick) {
      now_us += 1'000U;
      const auto result = app.advance(now_us, {}, 1U);
      REQUIRE(result.error == vector_v2::ApplicationError::kNone);
      if (result.frame_changed) {
        publications.push_back(
            {.frame = frame_copy(), .background_pending = app.status().background_pending});
      }
    }
    REQUIRE_FALSE(app.status().background_pending);
    return publications;
  }

  [[nodiscard]] std::vector<std::uint16_t> frame_copy() const {
    return {app.frame().begin(), app.frame().end()};
  }

  std::vector<vector_v2::OperationRecord> records;
  std::vector<vector_v2::CompactOperationSample> samples;
  std::vector<vector_v2::CompactOperationSample> stroke_samples;
  std::vector<std::uint16_t> canvas;
  std::vector<std::uint16_t> working;
  std::vector<std::uint16_t> frame;
  std::vector<std::uint16_t> overview;
  std::vector<std::uint16_t> working_overview;
  std::vector<std::uint16_t> chrome_cache;
  std::vector<vector_v2::MaterializedUniformStorage> uniforms;
  std::vector<std::uint8_t> occupancy;
  std::vector<vector_v2::MaterializedSlotStorage> slots;
  std::vector<std::uint16_t> tile_pixels;
  std::vector<std::uint16_t> raw_directory;
  std::vector<std::uint16_t> producer_pixels;
  std::vector<std::uint8_t> producer_mask;
  std::vector<std::uint16_t> producer_rows;
  std::vector<std::uint32_t> producer_words;
  std::vector<std::uint32_t> producer_plan_words;
  std::vector<std::uint16_t> producer_candidates;
  vector_v2::Application app;
  std::uint32_t now_us = 100'000U;

 private:
  [[nodiscard]] std::span<std::byte> plan_bytes() {
    const auto bytes = std::as_writable_bytes(std::span(producer_plan_words));
    return bytes.first(std::min(bytes.size(), vector_v2::kOperationChordStorageBytes));
  }
};

class BackendPair {
 public:
  BackendPair() : compact(false), production(true) {
    const auto compact_boot = compact.boot();
    const auto production_boot = production.boot();
    REQUIRE(compact_boot.frame_changed);
    REQUIRE(production_boot.frame_changed);
    REQUIRE(compact_boot.damage == production_boot.damage);
    check_equal();
  }

  template <typename Action>
  void act(Action action) {
    const auto compact_result = action(compact);
    const auto production_result = action(production);
    REQUIRE(compact_result.error == vector_v2::ApplicationError::kNone);
    REQUIRE(production_result.error == vector_v2::ApplicationError::kNone);
  }

  void drain_and_check_equal() {
    static_cast<void>(compact.drain());
    static_cast<void>(production.drain());
    check_equal();
  }

  void check_equal() const {
    const auto compact_status = compact.app.status();
    const auto production_status = production.app.status();
    CHECK(production_status.operation_count == compact_status.operation_count);
    CHECK(production_status.retained_operation_count == compact_status.retained_operation_count);
    CHECK(production_status.sample_count == compact_status.sample_count);
    CHECK(production_status.authority_fingerprint == compact_status.authority_fingerprint);
    CHECK(production_status.zoom == compact_status.zoom);
    CHECK(production_status.origin == compact_status.origin);
    CHECK(production_status.chrome == compact_status.chrome);
    CHECK(production.frame_copy() == compact.frame_copy());
  }

  ApplicationBackend compact;
  ApplicationBackend production;
};

}  // namespace

TEST_CASE("production Application boots, draws, and publishes zoom fallback then final pixels") {
  BackendPair pair;
  pair.act([](ApplicationBackend& backend) { return backend.stroke(100.0F); });
  pair.drain_and_check_equal();

  pair.act([](ApplicationBackend& backend) { return backend.zoom_next(); });
  REQUIRE(pair.production.app.status().background_pending);
  const auto publications = pair.production.drain();
  static_cast<void>(pair.compact.drain());
  REQUIRE(publications.size() == 2U);
  CHECK(publications.front().background_pending);
  CHECK(publications.back().background_pending);
  pair.check_equal();
}

TEST_CASE("production Application preserves pan and history parity through convergence") {
  BackendPair pair;
  pair.act([](ApplicationBackend& backend) { return backend.stroke(90.0F); });
  pair.act([](ApplicationBackend& backend) { return backend.stroke(150.0F); });
  pair.drain_and_check_equal();
  pair.act([](ApplicationBackend& backend) { return backend.zoom_next(); });
  pair.drain_and_check_equal();

  pair.act([](ApplicationBackend& backend) { return backend.tap(150.0F, 410.0F); });
  pair.act([](ApplicationBackend& backend) { return backend.tap(306.0F, 331.0F); });
  const auto origin_before = pair.production.app.status().origin;
  pair.act([](ApplicationBackend& backend) { return backend.pan(); });
  CHECK(pair.production.app.status().origin != origin_before);
  pair.drain_and_check_equal();

  pair.act([](ApplicationBackend& backend) { return backend.tap(30.0F, 410.0F); });
  pair.drain_and_check_equal();
  CHECK(pair.production.app.status().operation_count == 1U);
  pair.act([](ApplicationBackend& backend) { return backend.tap(90.0F, 410.0F); });
  pair.drain_and_check_equal();
  CHECK(pair.production.app.status().operation_count == 2U);
}

TEST_CASE("production Application accepts drawing during fill and confirms New") {
  BackendPair pair;
  pair.act([](ApplicationBackend& backend) { return backend.stroke(90.0F); });
  pair.drain_and_check_equal();
  pair.act([](ApplicationBackend& backend) { return backend.zoom_next(1U); });
  REQUIRE(pair.production.app.status().background_pending);

  pair.act([](ApplicationBackend& backend) { return backend.stroke(170.0F, 1U); });
  CHECK(pair.production.app.status().operation_count == 2U);
  pair.drain_and_check_equal();

  pair.act([](ApplicationBackend& backend) { return backend.tap(330.0F, 410.0F); });
  pair.act([](ApplicationBackend& backend) { return backend.tap(60.0F, 331.0F); });
  REQUIRE(pair.production.app.status().chrome.confirm_new);
  pair.act([](ApplicationBackend& backend) { return backend.tap(260.0F, 230.0F); });
  pair.drain_and_check_equal();
  CHECK(pair.production.app.status().operation_count == 0U);
  CHECK(pair.production.app.status().retained_operation_count == 0U);
}

TEST_CASE("production Application wraps maximum zoom to an exact overview frame") {
  BackendPair pair;
  pair.act([](ApplicationBackend& backend) { return backend.stroke(115.0F); });
  pair.drain_and_check_equal();
  const auto overview = pair.production.frame_copy();

  for (std::size_t step = 0U; step < 5U; ++step) {
    pair.act([](ApplicationBackend& backend) { return backend.zoom_next(); });
    pair.drain_and_check_equal();
  }
  CHECK(pair.production.app.status().zoom == vector_v2::ZoomLevel::k25Percent);
  CHECK(pair.production.frame_copy() == overview);
}
