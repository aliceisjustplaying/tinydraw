#include <doctest.h>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/application.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

class DemoApplicationFixture {
 public:
  explicit DemoApplicationFixture(std::size_t demo_capacity = 64U)
      : records(32U),
        samples(512U),
        stroke_samples(128U),
        demo_samples(demo_capacity),
        canvas(vector_v2::kOverviewPixels),
        working(vector_v2::kOverviewPixels),
        frame(vector_v2::kOverviewPixels),
        overview(vector_v2::kOverviewPixels),
        working_overview(vector_v2::kOverviewPixels),
        chrome_cache(vector_v2::kChromeStagingCachePixels),
        app({.records = records,
             .samples = samples,
             .stroke_samples = stroke_samples,
             .demo_samples = demo_samples,
             .canvas_pixels = canvas,
             .working_pixels = working,
             .frame_pixels = frame,
             .overview_pixels = overview,
             .working_overview_pixels = working_overview,
             .chrome_cache_pixels = chrome_cache}) {
    REQUIRE(app.ready());
  }

  vector_v2::ApplicationAdvanceResult advance(
      std::initializer_list<vector_v2::ApplicationEvent> events, std::uint32_t now_us,
      std::size_t work_quanta = 64U) {
    return app.advance(now_us,
                       std::span<const vector_v2::ApplicationEvent>(events.begin(), events.size()),
                       work_quanta);
  }

  vector_v2::ApplicationAdvanceResult idle(std::uint32_t now_us, std::size_t work_quanta = 64U) {
    return app.advance(now_us, {}, work_quanta);
  }

  void event(vector_v2::ApplicationEventKind kind, std::uint32_t at_us, float x = 0.0F,
             float y = 0.0F) {
    const auto result = advance({{.kind = kind, .x = x, .y = y, .timestamp_us = at_us}}, at_us);
    REQUIRE(result.error == vector_v2::ApplicationError::kNone);
  }

  void stroke(std::uint32_t started_us) {
    event(vector_v2::ApplicationEventKind::kTouchDown, started_us, 70.0F, 100.0F);
    event(vector_v2::ApplicationEventKind::kTouchMove, started_us + 20'000U, 105.0F, 120.0F);
    event(vector_v2::ApplicationEventKind::kTouchMove, started_us + 40'000U, 145.0F, 90.0F);
    event(vector_v2::ApplicationEventKind::kTouchUp, started_us + 60'000U, 180.0F, 110.0F);
  }

  [[nodiscard]] std::vector<std::uint16_t> frame_copy() const {
    return {app.frame().begin(), app.frame().end()};
  }

  std::vector<vector_v2::OperationRecord> records;
  std::vector<vector_v2::CompactOperationSample> samples;
  std::vector<vector_v2::CompactOperationSample> stroke_samples;
  std::vector<vector_v2::DemoSample> demo_samples;
  std::vector<std::uint16_t> canvas;
  std::vector<std::uint16_t> working;
  std::vector<std::uint16_t> frame;
  std::vector<std::uint16_t> overview;
  std::vector<std::uint16_t> working_overview;
  std::vector<std::uint16_t> chrome_cache;
  vector_v2::Application app;
};

}  // namespace

TEST_CASE("application demo replays identical authority and frame through normal input") {
  DemoApplicationFixture fixture;
  fixture.stroke(100'000U);
  REQUIRE(fixture.app.status().operation_count > 0U);

  fixture.event(vector_v2::ApplicationEventKind::kDemoLongPress, 1'000'000U);
  auto status = fixture.app.status();
  CHECK(status.demo_mode == vector_v2::ApplicationDemoMode::kRecording);
  CHECK(status.chrome.recording);
  CHECK(status.operation_count == 0U);
  CHECK(status.retained_operation_count == 0U);
  CHECK(status.zoom == vector_v2::ZoomLevel::k25Percent);
  CHECK(status.origin == vector_v2::NavigationPoint{});

  fixture.stroke(1'020'000U);
  fixture.event(vector_v2::ApplicationEventKind::kZoomNext, 1'100'000U);
  status = fixture.app.status();
  CHECK(status.demo_mode == vector_v2::ApplicationDemoMode::kRecording);
  CHECK(status.demo_sample_count == 5U);
  CHECK(status.zoom == vector_v2::ZoomLevel::k50Percent);

  fixture.event(vector_v2::ApplicationEventKind::kDemoLongPress, 1'200'000U);
  const auto recorded = fixture.app.status();
  const auto recorded_frame = fixture.frame_copy();
  REQUIRE(recorded.demo_mode == vector_v2::ApplicationDemoMode::kReady);
  REQUIRE_FALSE(recorded.chrome.recording);
  REQUIRE(recorded.operation_count > 0U);

  fixture.event(vector_v2::ApplicationEventKind::kDemoLongPress, 2'000'000U);
  CHECK(fixture.app.status().demo_mode == vector_v2::ApplicationDemoMode::kReplaying);
  CHECK(fixture.app.status().operation_count == 0U);
  REQUIRE(fixture.idle(2'020'000U).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.idle(2'040'000U).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.idle(2'060'000U).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.idle(2'080'000U).error == vector_v2::ApplicationError::kNone);
  REQUIRE(fixture.idle(2'100'000U).error == vector_v2::ApplicationError::kNone);

  const auto replayed = fixture.app.status();
  CHECK(replayed.demo_mode == vector_v2::ApplicationDemoMode::kReady);
  CHECK_FALSE(replayed.background_pending);
  CHECK(replayed.operation_count == recorded.operation_count);
  CHECK(replayed.retained_operation_count == recorded.retained_operation_count);
  CHECK(replayed.authority_fingerprint == recorded.authority_fingerprint);
  CHECK(replayed.zoom == recorded.zoom);
  CHECK(replayed.origin == recorded.origin);
  CHECK(fixture.frame_copy() == recorded_frame);
}

TEST_CASE("application demo overflow is visible and leaves ordinary input usable") {
  DemoApplicationFixture fixture(1U);
  fixture.event(vector_v2::ApplicationEventKind::kDemoLongPress, 1'000'000U);
  fixture.event(vector_v2::ApplicationEventKind::kTouchDown, 1'010'000U, 80.0F, 100.0F);
  fixture.event(vector_v2::ApplicationEventKind::kTouchMove, 1'020'000U, 120.0F, 110.0F);

  auto status = fixture.app.status();
  CHECK(status.demo_mode == vector_v2::ApplicationDemoMode::kReady);
  CHECK(status.demo_overflowed);
  CHECK_FALSE(status.chrome.recording);
  CHECK(status.demo_sample_count == 1U);

  fixture.event(vector_v2::ApplicationEventKind::kTouchUp, 1'030'000U, 150.0F, 105.0F);
  fixture.event(vector_v2::ApplicationEventKind::kZoomNext, 1'040'000U);
  status = fixture.app.status();
  CHECK(status.operation_count > 0U);
  CHECK(status.zoom == vector_v2::ZoomLevel::k50Percent);
  CHECK(status.demo_sample_count == 1U);
}

TEST_CASE("empty demo storage disables demo controls and preserves short zoom") {
  DemoApplicationFixture fixture(0U);
  fixture.stroke(100'000U);
  const auto before = fixture.app.status();
  REQUIRE(before.demo_mode == vector_v2::ApplicationDemoMode::kUnavailable);

  fixture.event(vector_v2::ApplicationEventKind::kDemoLongPress, 500'000U);
  auto status = fixture.app.status();
  CHECK(status.demo_mode == vector_v2::ApplicationDemoMode::kUnavailable);
  CHECK(status.operation_count == before.operation_count);
  CHECK(status.authority_fingerprint == before.authority_fingerprint);

  fixture.event(vector_v2::ApplicationEventKind::kZoomNext, 510'000U);
  status = fixture.app.status();
  CHECK(status.zoom == vector_v2::ZoomLevel::k50Percent);
  CHECK(status.operation_count == before.operation_count);
}
