#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "tinydraw/vector_v2/application.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

std::vector<std::byte> owner_document() {
  const std::string path =
      std::string(TINYDRAW_SOURCE_DIR) + "/testdata/documents/captured-drawing-2026-08-19.tdoc";
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  std::transform(chars.begin(), chars.end(), bytes.begin(),
                 [](char value) { return static_cast<std::byte>(value); });
  return bytes;
}

class ImportApplicationFixture {
 public:
  explicit ImportApplicationFixture(std::size_t record_capacity = 128U,
                                    std::size_t sample_capacity = 4'096U,
                                    std::size_t import_record_capacity = 128U,
                                    std::size_t import_sample_capacity = 4'096U)
      : records(record_capacity),
        samples(sample_capacity),
        stroke_samples(128U),
        import_records(import_record_capacity),
        import_samples(import_sample_capacity),
        demo_samples(64U),
        canvas(vector_v2::kOverviewPixels),
        working(vector_v2::kOverviewPixels),
        frame(vector_v2::kOverviewPixels),
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
        producer_candidates(record_capacity),
        app({.records = records,
             .samples = samples,
             .stroke_samples = stroke_samples,
             .import_records = import_records,
             .import_samples = import_samples,
             .demo_samples = demo_samples,
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
             .producer_chord_plans = producer_plans,
             .producer_candidate_indices = producer_candidates}) {
    REQUIRE(app.ready());
  }

  void draw() {
    const vector_v2::ApplicationEvent events[]{
        {.kind = vector_v2::ApplicationEventKind::kTouchDown,
         .x = 60.0F,
         .y = 90.0F,
         .timestamp_us = 100'000U},
        {.kind = vector_v2::ApplicationEventKind::kTouchMove,
         .x = 130.0F,
         .y = 120.0F,
         .timestamp_us = 120'000U},
        {.kind = vector_v2::ApplicationEventKind::kTouchUp,
         .x = 180.0F,
         .y = 100.0F,
         .timestamp_us = 140'000U},
    };
    const auto result = app.advance(140'000U, events, 64U);
    REQUIRE(result.error == vector_v2::ApplicationError::kNone);
    REQUIRE(app.status().operation_count == 1U);
  }

  [[nodiscard]] std::vector<std::uint16_t> frame_copy() const {
    return {app.frame().begin(), app.frame().end()};
  }

  std::vector<vector_v2::OperationRecord> records;
  std::vector<vector_v2::CompactOperationSample> samples;
  std::vector<vector_v2::CompactOperationSample> stroke_samples;
  std::vector<vector_v2::OperationRecord> import_records;
  std::vector<vector_v2::CompactOperationSample> import_samples;
  std::vector<vector_v2::DemoSample> demo_samples;
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
  std::vector<std::byte> producer_plans;
  std::vector<std::uint16_t> producer_candidates;
  vector_v2::Application app;
};

void check_unchanged(const ImportApplicationFixture& fixture,
                     const vector_v2::ApplicationStatus& before,
                     const std::vector<std::uint16_t>& frame_before) {
  const auto after = fixture.app.status();
  CHECK(after.revision == before.revision);
  CHECK(after.operation_count == before.operation_count);
  CHECK(after.retained_operation_count == before.retained_operation_count);
  CHECK(after.sample_count == before.sample_count);
  CHECK(after.authority_fingerprint == before.authority_fingerprint);
  CHECK(after.zoom == before.zoom);
  CHECK(after.origin == before.origin);
  CHECK(after.demo_mode == before.demo_mode);
  CHECK(after.frame_epoch == before.frame_epoch);
  CHECK(fixture.frame_copy() == frame_before);
}

}  // namespace

TEST_CASE("Application imports the owner TDOC exactly and rebuilds in bounded work") {
  const auto document = owner_document();
  REQUIRE(document.size() == 22'170U);
  ImportApplicationFixture fixture;
  fixture.draw();
  const vector_v2::ApplicationEvent zoom{.kind = vector_v2::ApplicationEventKind::kZoomNext,
                                         .timestamp_us = 200'000U};
  REQUIRE(fixture.app.advance(200'000U, std::span{&zoom, 1U}, 64U).error ==
          vector_v2::ApplicationError::kNone);

  REQUIRE(fixture.app.import_tdoc(document) == vector_v2::ApplicationError::kNone);
  auto status = fixture.app.status();
  CHECK(status.operation_count == 102U);
  CHECK(status.retained_operation_count == 102U);
  CHECK(status.sample_count == 2'706U);
  CHECK(status.authority_fingerprint == 11'617'349'970'098'677'125ULL);
  CHECK(status.zoom == vector_v2::ZoomLevel::k25Percent);
  CHECK(status.origin == vector_v2::NavigationPoint{});
  CHECK(status.chrome.tool == vector_v2::ChromeTool::kDraw);
  CHECK(status.demo_mode == vector_v2::ApplicationDemoMode::kEmpty);
  CHECK(status.background_pending);

  const auto old_frame = fixture.frame_copy();
  auto advance = fixture.app.advance(210'000U, {}, 0U);
  CHECK(advance.error == vector_v2::ApplicationError::kNone);
  CHECK_FALSE(advance.frame_changed);
  CHECK(fixture.frame_copy() == old_frame);

  advance = fixture.app.advance(220'000U, {}, 1U);
  CHECK(advance.error == vector_v2::ApplicationError::kNone);
  CHECK(advance.wants_immediate_advance);
  CHECK_FALSE(advance.frame_changed);
  for (std::uint32_t tick = 1U; tick < 256U && fixture.app.status().background_pending; ++tick) {
    advance = fixture.app.advance(220'000U + tick * 10'000U, {}, 1U);
    CHECK(advance.error == vector_v2::ApplicationError::kNone);
  }
  status = fixture.app.status();
  CHECK_FALSE(status.background_pending);
  CHECK(advance.frame_changed);
  CHECK(status.operation_count == 102U);
  CHECK(status.sample_count == 2'706U);
  CHECK(fixture.frame_copy() != old_frame);
}

TEST_CASE("late malformed TDOC rejection is atomic") {
  auto document = owner_document();
  REQUIRE(document.size() == 22'170U);
  document[document.size() - 4U] = std::byte{0};
  document[document.size() - 3U] = std::byte{0};
  ImportApplicationFixture fixture;
  fixture.draw();
  const auto before = fixture.app.status();
  const auto frame_before = fixture.frame_copy();

  CHECK(fixture.app.import_tdoc(document) == vector_v2::ApplicationError::kMalformedDocument);
  check_unchanged(fixture, before, frame_before);
}

TEST_CASE("TDOC authority or transaction workspace overflow is atomic") {
  const auto document = owner_document();
  ImportApplicationFixture fixture(32U, 1'024U, 32U, 1'024U);
  fixture.draw();
  const auto before = fixture.app.status();
  const auto frame_before = fixture.frame_copy();

  CHECK(fixture.app.import_tdoc(document) == vector_v2::ApplicationError::kImportCapacity);
  check_unchanged(fixture, before, frame_before);
}

TEST_CASE("TDOC requires its complete canonical byte shape") {
  auto document = owner_document();
  ImportApplicationFixture fixture;
  document.pop_back();
  CHECK(fixture.app.import_tdoc(document) == vector_v2::ApplicationError::kMalformedDocument);

  document = owner_document();
  document[0] = std::byte{'X'};
  CHECK(fixture.app.import_tdoc(document) == vector_v2::ApplicationError::kMalformedDocument);
}
