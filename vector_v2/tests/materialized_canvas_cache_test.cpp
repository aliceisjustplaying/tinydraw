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

TEST_CASE("view protection uses half-open tile boundaries") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  std::array<vector_v2::MaterializedSlotStorage, 3> slots{};
  std::array<std::uint16_t, slots.size() * vector_v2::kTilePixels> tile_storage{};
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  TestCanvas canvas(overview, slots, tile_storage);
  REQUIRE(canvas.publish_overview({0}, overview));
  const vector_v2::TileKey boundary_left{vector_v2::ZoomLevel::k100Percent, 0, 0};
  const vector_v2::TileKey protected_tile{vector_v2::ZoomLevel::k100Percent, 1, 0};
  const vector_v2::TileKey unprotected_newer{vector_v2::ZoomLevel::k100Percent, 2, 0};
  REQUIRE(
      canvas.publish_tile(boundary_left, {0}, vector_v2::MaterializationQuality::kImmediate, tile));
  REQUIRE(canvas.publish_tile(protected_tile, {0}, vector_v2::MaterializationQuality::kImmediate,
                              tile));
  REQUIRE(canvas.publish_tile(unprotected_newer, {0}, vector_v2::MaterializationQuality::kImmediate,
                              tile));
  REQUIRE(canvas.remember_view(
      {.zoom = vector_v2::ZoomLevel::k100Percent, .level_pixels = {64, 0, 128, 64}}));

  REQUIRE(canvas.publish_tile({vector_v2::ZoomLevel::k100Percent, 3, 0}, {0},
                              vector_v2::MaterializationQuality::kImmediate, tile));
  CHECK(canvas.lookup(boundary_left)->kind == vector_v2::SourceKind::kOverview);
  CHECK(canvas.lookup(protected_tile)->kind == vector_v2::SourceKind::kTileSlot);
  CHECK(canvas.lookup(unprotected_newer)->kind == vector_v2::SourceKind::kTileSlot);
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

TEST_CASE("compact eviction index exactly matches the scan policy under randomized mutations") {
  constexpr std::size_t kSlots = 17U;
  constexpr std::size_t kKeyCount = 96U;
  auto overview_scan = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto overview_index = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  auto uniforms_scan = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                                   vector_v2::kMaterializedTileIdentityCount>>();
  auto uniforms_index = std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                                    vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy_scan{};
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy_index{};
  std::array<vector_v2::MaterializedSlotStorage, kSlots> slots_scan{};
  std::array<vector_v2::MaterializedSlotStorage, kSlots> slots_index{};
  auto pixels_scan = std::make_unique<std::array<std::uint16_t, kSlots * vector_v2::kTilePixels>>();
  auto pixels_index =
      std::make_unique<std::array<std::uint16_t, kSlots * vector_v2::kTilePixels>>();
  auto directory_scan =
      std::make_unique<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>();
  auto directory_index =
      std::make_unique<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>();
  std::array<std::uint16_t, kSlots * 2U> links{};
  TestCanvas scan(*overview_scan, *uniforms_scan, occupancy_scan, slots_scan, *pixels_scan, {0},
                  *directory_scan);
  TestCanvas indexed(*overview_index, *uniforms_index, occupancy_index, slots_index, *pixels_index,
                     {0}, *directory_index, links);
  REQUIRE(scan.publish_overview({0}, *overview_scan));
  REQUIRE(indexed.publish_overview({0}, *overview_index));

  std::array<vector_v2::TileKey, kKeyCount> keys{};
  for (std::size_t index = 0; index < keys.size(); ++index) {
    keys[index] = {vector_v2::ZoomLevel::k400Percent, static_cast<std::uint16_t>(index % 24U),
                   static_cast<std::uint16_t>(index / 24U)};
  }
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  std::array<std::uint16_t, vector_v2::kTilePixels> composed_scan{};
  std::array<std::uint16_t, vector_v2::kTilePixels> composed_index{};
  std::uint32_t random = 0x51ED270BU;
  const auto next_random = [&random]() {
    random = random * 1'664'525U + 1'013'904'223U;
    return random;
  };

  for (std::size_t iteration = 0; iteration < 5'000U; ++iteration) {
    const vector_v2::TileKey key = keys[next_random() % keys.size()];
    const std::uint32_t action = next_random() % 11U;
    CAPTURE(iteration);
    CAPTURE(action);
    CAPTURE(key.column);
    CAPTURE(key.row);
    if (action < 5U) {
      tile.fill(static_cast<std::uint16_t>(iteration * 37U + 11U));
      const auto scan_slot =
          scan.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, tile);
      const auto indexed_slot =
          indexed.publish_tile(key, {0}, vector_v2::MaterializationQuality::kImmediate, tile);
      CHECK(scan_slot == indexed_slot);
    } else if (action == 5U) {
      const int x0 = static_cast<int>((next_random() % 20U) * 64U);
      const int y0 = static_cast<int>((next_random() % 12U) * 64U);
      const vector_v2::ViewRequest view{
          .zoom = vector_v2::ZoomLevel::k400Percent,
          .level_pixels = {x0, y0, x0 + 256, y0 + 256},
      };
      CHECK(scan.remember_view(view) == indexed.remember_view(view));
    } else if (action == 6U) {
      const vector_v2::PixelRect bounds = vector_v2::tile_pixel_bounds(key);
      const vector_v2::ViewRequest view{.zoom = key.zoom, .level_pixels = bounds};
      const auto scan_stats = scan.compose_view(view, composed_scan);
      const auto index_stats = indexed.compose_view(view, composed_index);
      CHECK(scan_stats.has_value() == index_stats.has_value());
      CHECK(composed_scan == composed_index);
    } else if (action == 7U) {
      scan.invalidate_identity(key);
      indexed.invalidate_identity(key);
    } else if (action == 8U) {
      const auto scan_uniform =
          scan.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kImmediate,
                               static_cast<std::uint16_t>(iteration));
      const auto index_uniform =
          indexed.publish_uniform(key, {0}, vector_v2::MaterializationQuality::kImmediate,
                                  static_cast<std::uint16_t>(iteration));
      CHECK(scan_uniform == index_uniform);
    } else if (action == 9U) {
      const auto scan_edit = scan.materialize_uniform_as_raw(key);
      const auto index_edit = indexed.materialize_uniform_as_raw(key);
      CHECK(scan_edit.has_value() == index_edit.has_value());
    } else {
      CHECK(scan.discard_tiles() == indexed.discard_tiles());
    }

    CHECK(scan.resident_raw_tiles() == indexed.resident_raw_tiles());
    for (const vector_v2::TileKey probe : keys) {
      const auto scan_source = scan.lookup(probe);
      const auto indexed_source = indexed.lookup(probe);
      REQUIRE(scan_source.has_value() == indexed_source.has_value());
      if (scan_source.has_value()) {
        CHECK(scan_source->kind == indexed_source->kind);
        CHECK(scan_source->revision == indexed_source->revision);
        CHECK(scan_source->quality == indexed_source->quality);
      }
    }
  }

  REQUIRE(scan.discard_tiles());
  REQUIRE(indexed.discard_tiles());
  for (std::size_t index = 0; index < kSlots; ++index) {
    const auto scan_slot =
        scan.publish_tile(keys[index], {0}, vector_v2::MaterializationQuality::kImmediate, tile);
    const auto indexed_slot =
        indexed.publish_tile(keys[index], {0}, vector_v2::MaterializationQuality::kImmediate, tile);
    REQUIRE(scan_slot == indexed_slot);
  }
  const vector_v2::PixelRect whole_world{0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight};
  auto history_patch = std::make_unique<std::array<std::uint16_t, vector_v2::kOverviewPixels>>();
  const vector_v2::OverviewRevisionPublication history_overview{
      .bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
      .pixels = *history_patch,
  };
  REQUIRE(scan.commit_history_revision({1}, history_overview, whole_world, 99U, 10U, 0U));
  const vector_v2::OverviewRevisionPublication indexed_history_overview{
      .bounds = history_overview.bounds,
      .pixels = *history_patch,
  };
  REQUIRE(
      indexed.commit_history_revision({1}, indexed_history_overview, whole_world, 99U, 10U, 0U));
  CHECK(scan.last_history_commit_stats() == indexed.last_history_commit_stats());
  const auto scan_reuse =
      scan.publish_tile(keys[30], {1}, vector_v2::MaterializationQuality::kImmediate, tile);
  const auto indexed_reuse =
      indexed.publish_tile(keys[30], {1}, vector_v2::MaterializationQuality::kImmediate, tile);
  CHECK(scan_reuse == indexed_reuse);
  REQUIRE(scan.commit_history_revision({2}, history_overview, whole_world, 99U, 20U, 10U));
  REQUIRE(
      indexed.commit_history_revision({2}, indexed_history_overview, whole_world, 99U, 20U, 10U));
  CHECK(scan.last_history_commit_stats() == indexed.last_history_commit_stats());
  const auto scan_after_swap =
      scan.publish_tile(keys[31], {2}, vector_v2::MaterializationQuality::kImmediate, tile);
  const auto indexed_after_swap =
      indexed.publish_tile(keys[31], {2}, vector_v2::MaterializationQuality::kImmediate, tile);
  CHECK(scan_after_swap == indexed_after_swap);
}
