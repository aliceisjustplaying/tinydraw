#include "tinydraw/vector_v2/application.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace vector_v2 = tinydraw::vector_v2;

namespace {

class ApplicationFixture {
 public:
  ApplicationFixture()
      : records(32),
        samples(512),
        stroke_samples(128),
        staged_stroke_samples(512),
        staged_stroke_appends(records.size()),
        canvas(vector_v2::kOverviewPixels),
        working(vector_v2::kOverviewPixels),
        frame(vector_v2::kOverviewPixels),
        live(vector_v2::kOverviewPixels),
        overview(vector_v2::kOverviewPixels),
        working_overview(vector_v2::kOverviewPixels),
        chrome_cache(vector_v2::kChromeStagingCachePixels),
        app({.records = records,
             .samples = samples,
             .stroke_samples = stroke_samples,
             .staged_stroke_samples = staged_stroke_samples,
             .staged_stroke_appends = staged_stroke_appends,
             .canvas_pixels = canvas,
             .working_pixels = working,
             .frame_pixels = frame,
             .live_pixels = live,
             .overview_pixels = overview,
             .working_overview_pixels = working_overview,
             .chrome_cache_pixels = chrome_cache}) {}

  vector_v2::ApplicationAdvanceResult advance(std::span<const vector_v2::ApplicationEvent> events,
                                              std::size_t work = 64U,
                                              std::uint32_t now_us = 100'000U) {
    return app.advance(now_us, events, work);
  }

  vector_v2::ApplicationAdvanceResult advance(
      std::initializer_list<vector_v2::ApplicationEvent> events, std::size_t work = 64U,
      std::uint32_t now_us = 100'000U) {
    return advance(std::span<const vector_v2::ApplicationEvent>(events.begin(), events.size()),
                   work, now_us);
  }

  vector_v2::ApplicationAdvanceResult tap(float x, float y, std::size_t work = 64U) {
    return advance({{vector_v2::ApplicationEventKind::kTouchDown, x, y, 100'000U},
                    {vector_v2::ApplicationEventKind::kTouchUp, x, y, 101'000U}},
                   work);
  }

  void draw_multimove() {
    const auto result =
        advance({{vector_v2::ApplicationEventKind::kTouchDown, 70.0F, 100.0F, 100'000U},
                 {vector_v2::ApplicationEventKind::kTouchMove, 100.0F, 115.0F, 110'000U},
                 {vector_v2::ApplicationEventKind::kTouchMove, 130.0F, 85.0F, 120'000U},
                 {vector_v2::ApplicationEventKind::kTouchMove, 160.0F, 105.0F, 130'000U},
                 {vector_v2::ApplicationEventKind::kTouchUp, 175.0F, 100.0F, 140'000U}},
                64U, 140'000U);
    REQUIRE(result.error == vector_v2::ApplicationError::kNone);
    REQUIRE(result.quiescent);
  }

  std::vector<vector_v2::OperationRecord> records;
  std::vector<vector_v2::CompactOperationSample> samples;
  std::vector<vector_v2::CompactOperationSample> stroke_samples;
  std::vector<vector_v2::CompactOperationSample> staged_stroke_samples;
  std::vector<vector_v2::OperationAppend> staged_stroke_appends;
  std::vector<std::uint16_t> canvas;
  std::vector<std::uint16_t> working;
  std::vector<std::uint16_t> frame;
  std::vector<std::uint16_t> live;
  std::vector<std::uint16_t> overview;
  std::vector<std::uint16_t> working_overview;
  std::vector<std::uint16_t> chrome_cache;
  vector_v2::Application app;
};

class ProductionApplicationFixture {
 public:
  ProductionApplicationFixture()
      : records(128U),
        samples(4'096U),
        stroke_samples(128U),
        staged_stroke_samples(4'096U),
        staged_stroke_appends(records.size()),
        canvas(vector_v2::kOverviewPixels),
        working(vector_v2::kOverviewPixels),
        frame(vector_v2::kOverviewPixels),
        live(vector_v2::kOverviewPixels),
        overview(vector_v2::kOverviewPixels),
        working_overview(vector_v2::kOverviewPixels),
        chrome_cache(vector_v2::kChromeStagingCachePixels),
        uniforms(vector_v2::kMaterializedTileIdentityCount),
        occupancy(vector_v2::kOccupancyBytes),
        slots(64U),
        tile_pixels(slots.size() * vector_v2::kTilePixels),
        raw_directory(vector_v2::kMaterializedTileIdentityCount),
        producer_pixels(vector_v2::kTileProducerPixels),
        producer_mask(vector_v2::kTileProducerMaskBytes),
        producer_rows(vector_v2::kTileProducerSummaryRows),
        producer_words(vector_v2::kTileProducerSummaryWords),
        producer_plans(vector_v2::kOperationChordStorageBytes),
        producer_candidates(records.size()),
        settle_operation_alpha(vector_v2::kTilePixels),
        settle_accumulated_alpha(vector_v2::kTilePixels),
        settle_red(vector_v2::kTilePixels),
        settle_green(vector_v2::kTilePixels),
        settle_blue(vector_v2::kTilePixels),
        settle_pixels(vector_v2::kTilePixels),
        app({.records = records,
             .samples = samples,
             .stroke_samples = stroke_samples,
             .staged_stroke_samples = staged_stroke_samples,
             .staged_stroke_appends = staged_stroke_appends,
             .canvas_pixels = canvas,
             .working_pixels = working,
             .frame_pixels = frame,
             .live_pixels = live,
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
             .producer_chord_plans = producer_plans,
             .producer_candidate_indices = producer_candidates,
             .settle_operation_alpha = settle_operation_alpha,
             .settle_accumulated_alpha = settle_accumulated_alpha,
             .settle_red = settle_red,
             .settle_green = settle_green,
             .settle_blue = settle_blue,
             .settle_pixels = settle_pixels}) {}

  vector_v2::ApplicationAdvanceResult advance(
      std::initializer_list<vector_v2::ApplicationEvent> events, std::size_t work = 0U) {
    return app.advance(500'000U,
                       std::span<const vector_v2::ApplicationEvent>(events.begin(), events.size()),
                       work);
  }

  void drain() {
    for (std::size_t tick = 0U; tick < 4'096U && app.status().background_pending; ++tick) {
      REQUIRE(app.advance(500'000U + static_cast<std::uint32_t>(tick), {}, 1U).error ==
              vector_v2::ApplicationError::kNone);
    }
    REQUIRE_FALSE(app.status().background_pending);
  }

  void stroke(float y) {
    REQUIRE(advance({{vector_v2::ApplicationEventKind::kTouchDown, 60.0F, y, 100'000U},
                     {vector_v2::ApplicationEventKind::kTouchMove, 130.0F, y + 10.0F, 120'000U},
                     {vector_v2::ApplicationEventKind::kTouchUp, 180.0F, y, 140'000U}})
                .error == vector_v2::ApplicationError::kNone);
    drain();
  }

  std::vector<vector_v2::OperationRecord> records;
  std::vector<vector_v2::CompactOperationSample> samples;
  std::vector<vector_v2::CompactOperationSample> stroke_samples;
  std::vector<vector_v2::CompactOperationSample> staged_stroke_samples;
  std::vector<vector_v2::OperationAppend> staged_stroke_appends;
  std::vector<std::uint16_t> canvas;
  std::vector<std::uint16_t> working;
  std::vector<std::uint16_t> frame;
  std::vector<std::uint16_t> live;
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
  std::vector<std::byte> producer_plans;
  std::vector<std::uint16_t> producer_candidates;
  std::vector<std::uint8_t> settle_operation_alpha;
  std::vector<std::uint8_t> settle_accumulated_alpha;
  std::vector<std::uint16_t> settle_red;
  std::vector<std::uint16_t> settle_green;
  std::vector<std::uint16_t> settle_blue;
  std::vector<std::uint16_t> settle_pixels;
  vector_v2::Application app;
};

std::vector<std::uint16_t> copy_frame(const vector_v2::Application& app) {
  return {app.frame().begin(), app.frame().end()};
}

}  // namespace

TEST_CASE("application rejects incomplete caller storage") {
  vector_v2::Application app({});
  CHECK_FALSE(app.ready());
  const auto result = app.advance(0U, {}, 0U);
  CHECK(result.error == vector_v2::ApplicationError::kInvalidStorage);
  CHECK(app.frame().empty());
}

TEST_CASE("application rejects overlapping caller storage") {
  std::array<vector_v2::OperationRecord, 2> records{};
  std::array<vector_v2::CompactOperationSample, 8> samples{};
  std::array<vector_v2::CompactOperationSample, 32> stroke_samples{};
  std::array<vector_v2::CompactOperationSample, 32> staged_stroke_samples{};
  std::array<vector_v2::OperationAppend, 2> staged_stroke_appends{};
  std::vector<std::uint16_t> shared(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> frame(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> live(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> overview(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> working_overview(vector_v2::kOverviewPixels);
  std::vector<std::uint16_t> chrome_cache(vector_v2::kChromeStagingCachePixels);
  vector_v2::Application app({.records = records,
                              .samples = samples,
                              .stroke_samples = stroke_samples,
                              .staged_stroke_samples = staged_stroke_samples,
                              .staged_stroke_appends = staged_stroke_appends,
                              .canvas_pixels = shared,
                              .working_pixels = shared,
                              .frame_pixels = frame,
                              .live_pixels = live,
                              .overview_pixels = overview,
                              .working_overview_pixels = working_overview,
                              .chrome_cache_pixels = chrome_cache});
  CHECK_FALSE(app.ready());
}

TEST_CASE("application publishes an initial frame on its first advance") {
  ApplicationFixture fixture;
  REQUIRE(fixture.app.ready());
  const auto first = fixture.advance({}, 0U);
  CHECK(first.frame_changed);
  CHECK(first.damage ==
        vector_v2::PixelRect{0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight});
  CHECK(first.frame_epoch == 1U);
  CHECK(first.quiescent);
  CHECK(std::any_of(fixture.app.frame().begin(), fixture.app.frame().end(),
                    [](std::uint16_t pixel) { return pixel != 0xFFFFU; }));

  const auto second = fixture.advance({}, 0U);
  CHECK_FALSE(second.frame_changed);
  CHECK(second.frame_epoch == first.frame_epoch);
}

TEST_CASE("production canvas keeps history revisions coherent") {
  ProductionApplicationFixture fixture;
  REQUIRE(fixture.app.ready());
  fixture.stroke(90.0F);
  fixture.stroke(140.0F);
  const auto drawn = copy_frame(fixture.app);

  REQUIRE(fixture
              .advance({{vector_v2::ApplicationEventKind::kTouchDown, 30.0F, 410.0F, 200'000U},
                        {vector_v2::ApplicationEventKind::kTouchUp, 30.0F, 410.0F, 201'000U}})
              .error == vector_v2::ApplicationError::kNone);
  fixture.drain();
  CHECK(fixture.app.status().operation_count == 1U);

  REQUIRE(fixture
              .advance({{vector_v2::ApplicationEventKind::kTouchDown, 90.0F, 410.0F, 210'000U},
                        {vector_v2::ApplicationEventKind::kTouchUp, 90.0F, 410.0F, 211'000U}})
              .error == vector_v2::ApplicationError::kNone);
  fixture.drain();
  CHECK(fixture.app.status().operation_count == 2U);
  CHECK(copy_frame(fixture.app) == drawn);
}

TEST_CASE("stroke commits whole-gesture authority and converges to cold replay") {
  ApplicationFixture fixture;
  fixture.draw_multimove();
  const auto drawn = copy_frame(fixture.app);
  const auto status = fixture.app.status();
  CHECK(status.operation_count == 1U);
  CHECK(status.retained_operation_count == 1U);
  CHECK(status.chrome.can_undo);
  CHECK_FALSE(status.chrome.can_redo);
  CHECK(std::any_of(drawn.begin(),
                    drawn.begin() + vector_v2::kChromeCanvasBottom * vector_v2::kOverviewWidth,
                    [](std::uint16_t pixel) { return pixel != 0xFFFFU; }));

  REQUIRE(fixture.tap(30.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().operation_count == 0U);
  CHECK(fixture.app.status().retained_operation_count == 1U);
  CHECK(fixture.app.status().chrome.can_redo);

  REQUIRE(fixture.tap(90.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().operation_count == 1U);
  CHECK(copy_frame(fixture.app) == drawn);
}

TEST_CASE("live stroke preview is replaceable and matches committed cold replay") {
  ApplicationFixture fixture;
  REQUIRE(fixture
              .advance({{vector_v2::ApplicationEventKind::kTouchDown, 70.0F, 100.0F, 100'000U},
                        {vector_v2::ApplicationEventKind::kTouchMove, 100.0F, 115.0F, 110'000U},
                        {vector_v2::ApplicationEventKind::kTouchMove, 130.0F, 85.0F, 120'000U},
                        {vector_v2::ApplicationEventKind::kTouchMove, 160.0F, 105.0F, 130'000U}},
                       0U, 130'000U)
              .error == vector_v2::ApplicationError::kNone);
  const auto live = copy_frame(fixture.app);
  REQUIRE(fixture.app.status().stroke_active);

  REQUIRE(fixture
              .advance({{vector_v2::ApplicationEventKind::kTouchUp, 160.0F, 105.0F, 140'000U}}, 64U,
                       140'000U)
              .error == vector_v2::ApplicationError::kNone);
  const auto committed = copy_frame(fixture.app);
  std::size_t obsolete_live_pixels = 0U;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(vector_v2::kChromeCanvasBottom) * vector_v2::kOverviewWidth;
       ++pixel) {
    const bool obsolete_live_pixel = live[pixel] != 0xFFFFU && committed[pixel] == 0xFFFFU;
    obsolete_live_pixels += static_cast<std::size_t>(obsolete_live_pixel);
  }
  CHECK(obsolete_live_pixels == 0U);

  REQUIRE(fixture.tap(30.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.tap(90.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(copy_frame(fixture.app) == committed);
}

TEST_CASE("more than 128 moves remain one whole-stroke undo and redo") {
  ApplicationFixture fixture;
  std::vector<vector_v2::ApplicationEvent> events;
  events.push_back({vector_v2::ApplicationEventKind::kTouchDown, 40.0F, 100.0F, 100'000U});
  for (std::uint32_t index = 1U; index <= 160U; ++index) {
    events.push_back({vector_v2::ApplicationEventKind::kTouchMove,
                      40.0F + static_cast<float>(index), 100.0F + static_cast<float>(index % 7U),
                      100'000U + index * 1'000U});
  }
  events.push_back({vector_v2::ApplicationEventKind::kTouchUp, 200.0F, 106.0F, 261'000U});
  const auto drawn_result = fixture.advance(events, 64U, 261'000U);
  REQUIRE(drawn_result.error == vector_v2::ApplicationError::kNone);
  REQUIRE(drawn_result.quiescent);
  REQUIRE(fixture.app.status().operation_count > 1U);
  const std::size_t chunk_count = fixture.app.status().operation_count;
  const auto drawn = copy_frame(fixture.app);

  REQUIRE(fixture.tap(30.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().operation_count == 0U);
  CHECK(fixture.app.status().retained_operation_count == chunk_count);

  REQUIRE(fixture.tap(90.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().operation_count == chunk_count);
  CHECK(copy_frame(fixture.app) == drawn);
}

TEST_CASE("foreground stroke is accepted while a prior view rebuild is pending") {
  ApplicationFixture fixture;
  fixture.draw_multimove();
  const vector_v2::ApplicationEvent zoom{vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F,
                                         200'000U};
  REQUIRE(fixture.advance({zoom}, 0U, 200'000U).wants_immediate_advance);
  const std::size_t before = fixture.app.status().operation_count;

  const auto stroke =
      fixture.advance({{vector_v2::ApplicationEventKind::kTouchDown, 200.0F, 150.0F, 210'000U},
                       {vector_v2::ApplicationEventKind::kTouchMove, 220.0F, 160.0F, 220'000U},
                       {vector_v2::ApplicationEventKind::kTouchUp, 230.0F, 165.0F, 230'000U}},
                      1U, 230'000U);
  CHECK(stroke.error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().operation_count == before + 1U);
}

TEST_CASE("fixed work quanta make zoom convergence explicit") {
  ApplicationFixture fixture;
  fixture.draw_multimove();
  const std::uint32_t before_epoch = fixture.app.status().frame_epoch;
  const vector_v2::ApplicationEvent zoom{vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F,
                                         200'000U};
  const auto requested = fixture.advance({zoom}, 0U, 200'000U);
  CHECK(requested.error == vector_v2::ApplicationError::kNone);
  CHECK(requested.wants_immediate_advance);
  CHECK_FALSE(requested.quiescent);
  CHECK(fixture.app.status().zoom == vector_v2::ZoomLevel::k50Percent);
  CHECK(fixture.app.status().frame_epoch == before_epoch);

  const auto converged = fixture.advance({}, 1U, 201'000U);
  CHECK(converged.error == vector_v2::ApplicationError::kNone);
  CHECK(converged.frame_changed);
  CHECK(converged.quiescent);
  CHECK_FALSE(converged.wants_immediate_advance);
  CHECK(fixture.app.status().origin != vector_v2::NavigationPoint{});
}

TEST_CASE("one work quantum advances a bounded operation batch") {
  ApplicationFixture fixture;
  for (int stroke = 0; stroke < 8; ++stroke) {
    const float y = 70.0F + static_cast<float>(stroke * 15);
    REQUIRE(fixture
                .advance({{vector_v2::ApplicationEventKind::kTouchDown, 50.0F, y, 100'000U},
                          {vector_v2::ApplicationEventKind::kTouchMove, 150.0F, y, 110'000U},
                          {vector_v2::ApplicationEventKind::kTouchUp, 180.0F, y, 120'000U}},
                         1U, 120'000U)
                .error == vector_v2::ApplicationError::kNone);
  }
  const vector_v2::ApplicationEvent zoom{vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F,
                                         200'000U};
  REQUIRE(fixture.advance({zoom}, 0U, 200'000U).wants_immediate_advance);
  const auto converged = fixture.advance({}, 1U, 201'000U);
  CHECK(converged.quiescent);
  CHECK(converged.frame_changed);
}

TEST_CASE("host zoom-next cycles from maximum back to overview") {
  ApplicationFixture fixture;
  for (int step = 0; step < 5; ++step) {
    const vector_v2::ApplicationEvent zoom{vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F,
                                           static_cast<std::uint32_t>(200'000 + step)};
    REQUIRE(fixture.advance({zoom}, 1U).error == vector_v2::ApplicationError::kNone);
  }
  CHECK(fixture.app.status().zoom == vector_v2::ZoomLevel::k25Percent);
}

TEST_CASE("zoom rail drag promotes to camera pan while Draw remains selected") {
  ApplicationFixture fixture;
  const vector_v2::ApplicationEvent zoom{vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F,
                                         200'000U};
  REQUIRE(fixture.advance({zoom}, 64U, 200'000U).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.app.status().chrome.tool == vector_v2::ChromeTool::kDraw);
  const auto origin_before = fixture.app.status().origin;

  const auto result =
      fixture.advance({{vector_v2::ApplicationEventKind::kTouchDown, 332.0F, 150.0F, 210'000U},
                       {vector_v2::ApplicationEventKind::kTouchMove, 300.0F, 150.0F, 220'000U},
                       {vector_v2::ApplicationEventKind::kTouchUp, 300.0F, 150.0F, 230'000U}},
                      64U, 230'000U);
  CHECK(result.error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().origin != origin_before);
  CHECK(fixture.app.status().chrome.tool == vector_v2::ChromeTool::kDraw);
  CHECK(fixture.app.status().operation_count == 0U);
}

TEST_CASE("frame damage remains publishable when another event reports an error") {
  ApplicationFixture fixture;
  const auto result =
      fixture.advance({{vector_v2::ApplicationEventKind::kTouchMove, -1.0F, 20.0F, 200'000U},
                       {vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F, 200'001U}},
                      1U, 200'001U);
  CHECK(result.error == vector_v2::ApplicationError::kInvalidEvent);
  CHECK(result.frame_changed);
  CHECK(result.damage ==
        vector_v2::PixelRect{0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight});
}

TEST_CASE("chrome owns pan, undo redo, and new document behavior") {
  ApplicationFixture fixture;
  fixture.draw_multimove();

  REQUIRE(fixture.tap(150.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.app.status().chrome.popup == vector_v2::ChromePopup::kTools);
  REQUIRE(fixture.tap(306.0F, 331.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().chrome.tool == vector_v2::ChromeTool::kPan);

  const vector_v2::ApplicationEvent zoom{vector_v2::ApplicationEventKind::kZoomNext, 0.0F, 0.0F,
                                         210'000U};
  REQUIRE(fixture.advance({zoom}, 64U, 210'000U).error == vector_v2::ApplicationError::kNone);
  const auto origin_before = fixture.app.status().origin;
  REQUIRE(fixture
              .advance({{vector_v2::ApplicationEventKind::kTouchDown, 100.0F, 150.0F, 220'000U},
                        {vector_v2::ApplicationEventKind::kTouchMove, 50.0F, 120.0F, 230'000U},
                        {vector_v2::ApplicationEventKind::kTouchUp, 50.0F, 120.0F, 240'000U}},
                       64U, 240'000U)
              .error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().origin != origin_before);

  const auto before_minimap = fixture.app.status().origin;
  REQUIRE(fixture.tap(352.0F, 356.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().origin != before_minimap);

  REQUIRE(fixture.tap(330.0F, 410.0F).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.app.status().chrome.popup == vector_v2::ChromePopup::kDocument);
  REQUIRE(fixture.tap(60.0F, 331.0F).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.app.status().chrome.confirm_new);
  REQUIRE(fixture.tap(260.0F, 230.0F).error == vector_v2::ApplicationError::kNone);
  CHECK(fixture.app.status().operation_count == 0U);
  CHECK(fixture.app.status().retained_operation_count == 0U);
  CHECK_FALSE(fixture.app.status().chrome.can_undo);
  CHECK_FALSE(fixture.app.status().chrome.confirm_new);
}
