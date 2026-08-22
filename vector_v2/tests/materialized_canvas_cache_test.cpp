#include <doctest.h>

#include <algorithm>
#include <vector>

#include "test_support/materialized_canvas_fixture.h"
#include "tinydraw/vector_v2/memory_layout.h"

TEST_CASE("strided full-tile publication is exact at every 16-byte pointer phase") {
  constexpr std::size_t kSourceStride = 128U;
  constexpr std::size_t kSourcePixels =
      (vector_v2::kTileHeight - 1U) * kSourceStride + vector_v2::kTileWidth;
  constexpr std::uint16_t kGuard = 0xDEADU;
  const vector_v2::TileKey key{vector_v2::ZoomLevel::k100Percent, 0, 0};

  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  for (std::size_t source_phase = 0; source_phase < 8U; ++source_phase) {
    for (std::size_t destination_phase = 0; destination_phase < 8U; ++destination_phase) {
      std::vector<std::uint16_t> source_storage(kSourcePixels + 9U, kGuard);
      std::vector<std::uint16_t> tile_storage(vector_v2::kTilePixels + 9U, kGuard);
      std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
      auto source = std::span(source_storage).subspan(source_phase, kSourcePixels);
      auto destination = std::span(tile_storage).subspan(destination_phase, vector_v2::kTilePixels);

      for (int row = 0; row < vector_v2::kTileHeight; ++row) {
        for (int column = 0; column < vector_v2::kTileWidth; ++column) {
          source[static_cast<std::size_t>(row) * kSourceStride + static_cast<std::size_t>(column)] =
              static_cast<std::uint16_t>(row * 257 + column * 17 + 0x1234);
        }
      }

      TestCanvas canvas(*overview, slots, destination);
      CAPTURE(source_phase);
      CAPTURE(destination_phase);
      REQUIRE(canvas.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, source,
                                  kSourceStride));
      for (int row = 0; row < vector_v2::kTileHeight; ++row) {
        for (int column = 0; column < vector_v2::kTileWidth; ++column) {
          CHECK(destination[static_cast<std::size_t>(row) * vector_v2::kTileWidth +
                            static_cast<std::size_t>(column)] ==
                source[static_cast<std::size_t>(row) * kSourceStride +
                       static_cast<std::size_t>(column)]);
        }
      }
      CHECK(tile_storage[destination_phase + vector_v2::kTilePixels] == kGuard);
      if (destination_phase != 0U) {
        CHECK(tile_storage[destination_phase - 1U] == kGuard);
      }
    }
  }
}

TEST_CASE("tile publication keeps clipped stride guards and rejects pool overlap") {
  constexpr std::size_t kSourceStride = 128U;
  constexpr int kWidth = 32;
  constexpr int kHeight = 64;
  constexpr std::size_t kSourcePixels =
      (kHeight - 1U) * kSourceStride + static_cast<std::size_t>(kWidth);
  constexpr std::uint16_t kGuard = 0xA55AU;
  const vector_v2::TileKey edge{vector_v2::ZoomLevel::k50Percent, 11, 13};
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  std::array<vector_v2::MaterializedSlotStorage, 1> slots{};
  std::vector<std::uint16_t> source_storage(kSourcePixels + 4U, kGuard);
  std::vector<std::uint16_t> tile_storage(vector_v2::kTilePixels + 4U, kGuard);
  auto source = std::span(source_storage).subspan(2U, kSourcePixels);
  auto destination = std::span(tile_storage).subspan(3U, vector_v2::kTilePixels);
  for (int row = 0; row < kHeight; ++row) {
    for (int column = 0; column < kWidth; ++column) {
      source[static_cast<std::size_t>(row) * kSourceStride + static_cast<std::size_t>(column)] =
          static_cast<std::uint16_t>(row * 101 + column + 7);
    }
  }

  TestCanvas canvas(*overview, slots, destination);
  REQUIRE(canvas.publish_tile(edge, {0}, vector_v2::MaterializationQuality::kImmediate, source,
                              kSourceStride));
  for (int row = 0; row < kHeight; ++row) {
    for (int column = 0; column < kWidth; ++column) {
      CHECK(
          destination[static_cast<std::size_t>(row) * vector_v2::kTileWidth +
                      static_cast<std::size_t>(column)] ==
          source[static_cast<std::size_t>(row) * kSourceStride + static_cast<std::size_t>(column)]);
    }
  }
  CHECK(tile_storage[2] == kGuard);
  CHECK(tile_storage[3U + vector_v2::kTilePixels] == kGuard);

  CHECK_FALSE(canvas.publish_tile(edge, {0}, vector_v2::MaterializationQuality::kSettled,
                                  destination, vector_v2::kTileWidth));
}

TEST_CASE("cache retains every zoom viewport across a disjoint pan fill") {
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent,
      vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  auto overview = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto slots =
      std::make_unique<std::array<vector_v2::MaterializedSlotStorage, vector_v2::kTileSlotCount>>();
  auto tile_storage = std::make_unique<
      std::array<std::uint16_t, vector_v2::kTileSlotCount * vector_v2::kTilePixels>>();
  auto composed = std::make_unique<
      std::array<std::uint16_t, vector_v2::kMaximumVisibleTiles * vector_v2::kTilePixels>>();
  TestCanvas canvas(*overview, *slots, *tile_storage);
  REQUIRE(canvas.publish_overview({0}, *overview));
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};

  for (const auto zoom : zooms) {
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        REQUIRE(canvas.publish_tile({zoom, column, row}, {0},
                                    vector_v2::MaterializationQuality::kImmediate, tile));
      }
    }
  }

  // Fill a second disjoint 400% viewport after the four zoom views. The first
  // viewport at every zoom must still survive.
  for (std::uint16_t row = 8; row < 16; ++row) {
    for (std::uint16_t column = 8; column < 15; ++column) {
      REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, column, row}, {0},
                                  vector_v2::MaterializationQuality::kImmediate, tile));
    }
  }
  for (const auto zoom : zooms) {
    REQUIRE(canvas.compose_view({.zoom = zoom, .level_pixels = {0, 0, 448, 512}}, *composed));
    for (std::uint16_t row = 0; row < 8; ++row) {
      for (std::uint16_t column = 0; column < 7; ++column) {
        const vector_v2::TileKey key{zoom, column, row};
        CHECK(canvas.lookup(key)->kind == vector_v2::SourceKind::kTileSlot);
      }
    }
  }

  // Consume the remaining slots, then prove the next publication evicts
  // exactly the least-recently-used entry rather than an arbitrary viewport.
  // The disjoint destination predates the mark_used calls above, so its first
  // tile is now the oldest resident entry.
  constexpr std::size_t kRetainedFootprints = 5U;
  constexpr std::size_t kAdditionalSlots =
      vector_v2::kTileSlotCount - kRetainedFootprints * vector_v2::kMaximumVisibleTiles;
  // Spread the filler keys across rows: the 400% grid has 92 columns, fewer
  // than the spare-slot count at the current pool size.
  constexpr std::uint16_t kFillerColumns = 64U;
  const auto filler_key = [](std::size_t index) {
    return vector_v2::TileKey{vector_v2::ZoomLevel::k400Percent,
                              static_cast<std::uint16_t>(index % kFillerColumns),
                              static_cast<std::uint16_t>(16U + index / kFillerColumns)};
  };
  for (std::size_t index = 0; index < kAdditionalSlots; ++index) {
    REQUIRE(canvas.publish_tile(filler_key(index), {0},
                                vector_v2::MaterializationQuality::kImmediate, tile));
  }
  const vector_v2::TileKey oldest{vector_v2::ZoomLevel::k400Percent, 8, 8};
  CHECK(canvas.lookup(oldest)->kind == vector_v2::SourceKind::kTileSlot);
  const vector_v2::TileKey overflow_key{vector_v2::ZoomLevel::k400Percent, 0, 40};
  REQUIRE(
      canvas.publish_tile(overflow_key, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(oldest)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup({vector_v2::ZoomLevel::k50Percent, 0, 0})->kind ==
        vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(overflow_key)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("recent view footprints softly outrank global LRU eviction") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey protected_inactive{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey protected_active{vector_v2::ZoomLevel::k400Percent, 0, 0};
  const vector_v2::TileKey unprotected{vector_v2::ZoomLevel::k400Percent, 8, 8};
  REQUIRE(canvas.publish_tile(protected_inactive, {0},
                              vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.publish_tile(protected_active, {0}, vector_v2::MaterializationQuality::kImmediate,
                              tile));
  REQUIRE(
      canvas.publish_tile(unprotected, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.remember_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}}));
  REQUIRE(canvas.remember_view(
      {.zoom = vector_v2::ZoomLevel::k400Percent, .level_pixels = {0, 0, 64, 64}}));

  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, 9, 8}, {0},
                              vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(unprotected)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(protected_inactive)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(protected_active)->kind == vector_v2::SourceKind::kTileSlot);

  // Protection is soft: once all entries belong to remembered footprints,
  // the inactive zoom yields before the active viewport rather than refusing.
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, 1, 0}, {0},
                              vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.remember_view(
      {.zoom = vector_v2::ZoomLevel::k400Percent, .level_pixels = {0, 0, 128, 64}}));
  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k400Percent, 2, 0}, {0},
                              vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(protected_inactive)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(protected_active)->kind == vector_v2::SourceKind::kTileSlot);
}

TEST_CASE("composing a tile refreshes its LRU recency") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 2> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey first{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey second{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey third{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(canvas.publish_tile(first, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.publish_tile(second, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  std::array<std::uint16_t, vector_v2::kTilePixels> composed{};
  REQUIRE(canvas.compose_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {0, 0, 64, 64}}, composed));

  REQUIRE(canvas.publish_tile(third, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(first)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(second)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(third)->kind == vector_v2::SourceKind::kTileSlot);
}
