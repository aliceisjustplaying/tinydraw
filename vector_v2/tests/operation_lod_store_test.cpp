#include "tinydraw/vector_v2/operation_lod_store.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace vector_v2 = tinydraw::vector_v2;

namespace {

std::array<std::span<const vector_v2::CompactLodSample>, vector_v2::kLodZoomCount> all_zooms(
    std::span<const vector_v2::CompactLodSample> samples) {
  return {samples, samples, samples, samples};
}

}  // namespace

TEST_CASE("LOD store publishes four zoom spans as one operation") {
  std::array<vector_v2::LodSpan, 8> spans{};
  std::array<vector_v2::CompactLodSample, 12> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  const vector_v2::OperationLogEpoch epoch{4};
  REQUIRE(store.reset(epoch, {8}));
  const std::array samples{
      vector_v2::CompactLodSample{4, 8, 256},
      vector_v2::CompactLodSample{12, 16, 512},
  };

  auto prepared =
      store.prepare({.epoch = epoch, .identity = {{9}, 0}, .zoom_samples = all_zooms(samples)});
  REQUIRE(prepared.has_value());
  CHECK(store.operation_count() == 0U);
  CHECK_FALSE(store.lod(epoch, {{9}, 0}, vector_v2::ZoomLevel::k100Percent));
  prepared->publish();

  CHECK(store.current_revision() == vector_v2::DocumentRevision{9});
  CHECK(store.operation_count() == 1U);
  CHECK(store.sample_count() == 8U);
  for (const vector_v2::ZoomLevel zoom : vector_v2::kLodZooms) {
    const auto lod = store.lod(epoch, {{9}, 0}, zoom);
    REQUIRE(lod.has_value());
    CHECK(lod->samples.size() == 2U);
    CHECK(lod->samples[1] == samples[1]);
  }
  CHECK_FALSE(store.lod(epoch, {{9}, 0}, vector_v2::ZoomLevel::k25Percent));
}

TEST_CASE("LOD store capacity failure and cancellation leave authority unchanged") {
  std::array<vector_v2::LodSpan, 4> spans{};
  std::array<vector_v2::CompactLodSample, 4> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  const vector_v2::OperationLogEpoch epoch{1};
  REQUIRE(store.reset(epoch, {0}));
  const std::array sample{vector_v2::CompactLodSample{4, 4, 256}};
  auto prepared =
      store.prepare({.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(sample)});
  REQUIRE(prepared.has_value());
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(sample)}));
  CHECK_FALSE(store.reset({2}, {0}));
  prepared->cancel();
  CHECK(store.operation_count() == 0U);
  CHECK(store.sample_count() == 0U);
  CHECK(store.current_revision() == vector_v2::DocumentRevision{0});

  const std::array too_many{
      vector_v2::CompactLodSample{4, 4, 256},
      vector_v2::CompactLodSample{8, 8, 256},
  };
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(too_many)}));
  CHECK(store.sample_count() == 0U);
}

TEST_CASE("LOD store keeps published operations visible while the next is prepared") {
  std::array<vector_v2::LodSpan, 8> spans{};
  std::array<vector_v2::CompactLodSample, 8> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  const vector_v2::OperationLogEpoch epoch{1};
  const std::array first_sample{vector_v2::CompactLodSample{4, 4, 256}};
  const std::array second_sample{vector_v2::CompactLodSample{8, 8, 256}};
  REQUIRE(store.reset(epoch, {0}));
  auto first = store.prepare(
      {.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(first_sample)});
  REQUIRE(first.has_value());
  first->publish();
  auto second = store.prepare(
      {.epoch = epoch, .identity = {{2}, 1}, .zoom_samples = all_zooms(second_sample)});
  REQUIRE(second.has_value());

  const auto published = store.lod(epoch, {{1}, 0}, vector_v2::ZoomLevel::k100Percent);
  REQUIRE(published.has_value());
  CHECK(published->samples.front() == first_sample.front());
  CHECK_FALSE(store.lod(epoch, {{2}, 1}, vector_v2::ZoomLevel::k100Percent));
  second->cancel();
}

TEST_CASE("LOD store rejects mismatched identity and malformed zoom input") {
  std::array<vector_v2::LodSpan, 8> spans{};
  std::array<vector_v2::CompactLodSample, 12> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  const vector_v2::OperationLogEpoch epoch{7};
  REQUIRE(store.reset(epoch, {3}));
  const std::array sample{vector_v2::CompactLodSample{4, 4, 256}};
  CHECK_FALSE(
      store.prepare({.epoch = {6}, .identity = {{4}, 0}, .zoom_samples = all_zooms(sample)}));
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{5}, 0}, .zoom_samples = all_zooms(sample)}));
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{4}, 1}, .zoom_samples = all_zooms(sample)}));
  auto missing_zoom = all_zooms(sample);
  missing_zoom[2] = {};
  CHECK_FALSE(store.prepare({.epoch = epoch, .identity = {{4}, 0}, .zoom_samples = missing_zoom}));
  const std::array malformed{vector_v2::CompactLodSample{4, 4, 0}};
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{4}, 0}, .zoom_samples = all_zooms(malformed)}));
}

TEST_CASE("LOD reset invalidates prior epoch and adopts a snapshot base") {
  std::array<vector_v2::LodSpan, 8> spans{};
  std::array<vector_v2::CompactLodSample, 8> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  const vector_v2::OperationLogEpoch old_epoch{1};
  const std::array sample{vector_v2::CompactLodSample{4, 4, 256}};
  REQUIRE(store.reset(old_epoch, {0}));
  CHECK_FALSE(store.reset(old_epoch, {8}));
  auto prepared =
      store.prepare({.epoch = old_epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(sample)});
  REQUIRE(prepared.has_value());
  prepared->publish();
  REQUIRE(store.lod(old_epoch, {{1}, 0}, vector_v2::ZoomLevel::k50Percent));

  const vector_v2::OperationLogEpoch new_epoch{2};
  REQUIRE(store.reset(new_epoch, {8}));
  CHECK_FALSE(store.lod(old_epoch, {{1}, 0}, vector_v2::ZoomLevel::k50Percent));
  auto after_restore =
      store.prepare({.epoch = new_epoch, .identity = {{9}, 0}, .zoom_samples = all_zooms(sample)});
  REQUIRE(after_restore.has_value());
  after_restore->publish();
  CHECK(store.lod(new_epoch, {{9}, 0}, vector_v2::ZoomLevel::k400Percent)->identity ==
        vector_v2::OperationIdentity{{9}, 0});
}

TEST_CASE("LOD store rejects append input that aliases owned sample storage") {
  std::array<vector_v2::LodSpan, 4> spans{};
  std::array<vector_v2::CompactLodSample, 8> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  REQUIRE(store.reset({1}, {0}));
  storage[0] = {4, 4, 256};
  const auto aliased = std::span<const vector_v2::CompactLodSample>(storage).first(1);

  CHECK_FALSE(
      store.prepare({.epoch = {1}, .identity = {{1}, 0}, .zoom_samples = all_zooms(aliased)}));
  CHECK(store.sample_count() == 0U);
}

TEST_CASE("LOD store rejects malformed span layout") {
  std::array<vector_v2::LodSpan, 5> spans{};
  std::array<vector_v2::CompactLodSample, 4> storage{};
  vector_v2::OperationLodStore store(spans, storage);
  CHECK_FALSE(store.ready());
}
