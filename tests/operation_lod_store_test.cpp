#include "tinydraw/production/operation_lod_store.h"

#include <doctest.h>

#include <array>
#include <cstdint>

namespace production = tinydraw::production;

namespace {

std::array<std::span<const production::CompactLodSample>, production::kLodZoomCount> all_zooms(
    std::span<const production::CompactLodSample> samples) {
  return {samples, samples, samples, samples};
}

}  // namespace

TEST_CASE("LOD store publishes four zoom spans as one operation") {
  std::array<production::LodSpan, 8> spans{};
  std::array<production::CompactLodSample, 12> storage{};
  production::OperationLodStore store(spans, storage);
  const production::OperationLogEpoch epoch{4};
  REQUIRE(store.reset(epoch, {8}));
  const std::array samples{
      production::CompactLodSample{4, 8, 256},
      production::CompactLodSample{12, 16, 512},
  };

  auto prepared =
      store.prepare({.epoch = epoch, .identity = {{9}, 0}, .zoom_samples = all_zooms(samples)});
  REQUIRE(prepared.has_value());
  CHECK(store.operation_count() == 0U);
  CHECK_FALSE(store.lod(epoch, {{9}, 0}, production::ZoomLevel::k100Percent));
  prepared->publish();

  CHECK(store.current_revision() == production::DocumentRevision{9});
  CHECK(store.operation_count() == 1U);
  CHECK(store.sample_count() == 8U);
  for (const production::ZoomLevel zoom : production::kLodZooms) {
    const auto lod = store.lod(epoch, {{9}, 0}, zoom);
    REQUIRE(lod.has_value());
    CHECK(lod->samples.size() == 2U);
    CHECK(lod->samples[1] == samples[1]);
  }
  CHECK_FALSE(store.lod(epoch, {{9}, 0}, production::ZoomLevel::k25Percent));
}

TEST_CASE("LOD store capacity failure and cancellation leave authority unchanged") {
  std::array<production::LodSpan, 4> spans{};
  std::array<production::CompactLodSample, 4> storage{};
  production::OperationLodStore store(spans, storage);
  const production::OperationLogEpoch epoch{1};
  REQUIRE(store.reset(epoch, {0}));
  const std::array sample{production::CompactLodSample{4, 4, 256}};
  auto prepared =
      store.prepare({.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(sample)});
  REQUIRE(prepared.has_value());
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(sample)}));
  CHECK_FALSE(store.reset({2}, {0}));
  prepared->cancel();
  CHECK(store.operation_count() == 0U);
  CHECK(store.sample_count() == 0U);
  CHECK(store.current_revision() == production::DocumentRevision{0});

  const std::array too_many{
      production::CompactLodSample{4, 4, 256},
      production::CompactLodSample{8, 8, 256},
  };
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(too_many)}));
  CHECK(store.sample_count() == 0U);
}

TEST_CASE("LOD store rejects mismatched identity and malformed zoom input") {
  std::array<production::LodSpan, 8> spans{};
  std::array<production::CompactLodSample, 12> storage{};
  production::OperationLodStore store(spans, storage);
  const production::OperationLogEpoch epoch{7};
  REQUIRE(store.reset(epoch, {3}));
  const std::array sample{production::CompactLodSample{4, 4, 256}};
  CHECK_FALSE(
      store.prepare({.epoch = {6}, .identity = {{4}, 0}, .zoom_samples = all_zooms(sample)}));
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{5}, 0}, .zoom_samples = all_zooms(sample)}));
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{4}, 1}, .zoom_samples = all_zooms(sample)}));
  auto missing_zoom = all_zooms(sample);
  missing_zoom[2] = {};
  CHECK_FALSE(store.prepare({.epoch = epoch, .identity = {{4}, 0}, .zoom_samples = missing_zoom}));
  const std::array malformed{production::CompactLodSample{4, 4, 0}};
  CHECK_FALSE(
      store.prepare({.epoch = epoch, .identity = {{4}, 0}, .zoom_samples = all_zooms(malformed)}));
}

TEST_CASE("LOD reset invalidates prior epoch and adopts a snapshot base") {
  std::array<production::LodSpan, 8> spans{};
  std::array<production::CompactLodSample, 8> storage{};
  production::OperationLodStore store(spans, storage);
  const production::OperationLogEpoch old_epoch{1};
  const std::array sample{production::CompactLodSample{4, 4, 256}};
  REQUIRE(store.reset(old_epoch, {0}));
  auto prepared =
      store.prepare({.epoch = old_epoch, .identity = {{1}, 0}, .zoom_samples = all_zooms(sample)});
  REQUIRE(prepared.has_value());
  prepared->publish();
  REQUIRE(store.lod(old_epoch, {{1}, 0}, production::ZoomLevel::k50Percent));

  const production::OperationLogEpoch new_epoch{2};
  REQUIRE(store.reset(new_epoch, {8}));
  CHECK_FALSE(store.lod(old_epoch, {{1}, 0}, production::ZoomLevel::k50Percent));
  auto after_restore =
      store.prepare({.epoch = new_epoch, .identity = {{9}, 0}, .zoom_samples = all_zooms(sample)});
  REQUIRE(after_restore.has_value());
  after_restore->publish();
  CHECK(store.lod(new_epoch, {{9}, 0}, production::ZoomLevel::k400Percent)->identity ==
        production::OperationIdentity{{9}, 0});
}

TEST_CASE("LOD store rejects append input that aliases owned sample storage") {
  std::array<production::LodSpan, 4> spans{};
  std::array<production::CompactLodSample, 8> storage{};
  production::OperationLodStore store(spans, storage);
  REQUIRE(store.reset({1}, {0}));
  storage[0] = {4, 4, 256};
  const auto aliased = std::span<const production::CompactLodSample>(storage).first(1);

  CHECK_FALSE(
      store.prepare({.epoch = {1}, .identity = {{1}, 0}, .zoom_samples = all_zooms(aliased)}));
  CHECK(store.sample_count() == 0U);
}

TEST_CASE("LOD store rejects malformed span layout") {
  std::array<production::LodSpan, 5> spans{};
  std::array<production::CompactLodSample, 4> storage{};
  production::OperationLodStore store(spans, storage);
  CHECK_FALSE(store.ready());
}
