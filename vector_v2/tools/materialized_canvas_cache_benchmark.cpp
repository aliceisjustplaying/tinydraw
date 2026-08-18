#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/memory_layout.h"

namespace v2 = tinydraw::vector_v2;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kWarmupRuns = 5U;
constexpr std::size_t kMeasuredRuns = 31U;

struct Rig {
  std::vector<std::uint16_t> overview = std::vector<std::uint16_t>(v2::kOverviewPixels, 0xFFFFU);
  std::vector<v2::MaterializedUniformStorage> uniforms =
      std::vector<v2::MaterializedUniformStorage>(v2::kMaterializedTileIdentityCount);
  std::vector<std::uint8_t> occupancy = std::vector<std::uint8_t>(v2::kOccupancyBytes);
  std::vector<v2::MaterializedSlotStorage> slots =
      std::vector<v2::MaterializedSlotStorage>(v2::kTileSlotCount);
  std::vector<std::uint16_t> tile_pixels =
      std::vector<std::uint16_t>(v2::kTileSlotCount * v2::kTilePixels);
  std::vector<std::uint16_t> directory =
      std::vector<std::uint16_t>(v2::kMaterializedTileIdentityCount);
  std::array<std::uint16_t, v2::kTilePixels> tile{};
  v2::MaterializedCanvas canvas{overview, uniforms, occupancy, slots, tile_pixels, {0}, directory};

  Rig() { tile.fill(0x39E7U); }

  bool ready() { return canvas.ready() && canvas.publish_overview({0}, overview); }
};

struct Measurement {
  const char* name = "";
  double median_us = 0.0;
  std::size_t iterations = 0U;
  std::size_t slot_scans = 0U;
  std::size_t uniform_window_scans = 0U;
  std::size_t retained_comparisons = 0U;
  std::size_t retained_marks = 0U;
};

std::vector<v2::TileKey> all_keys() {
  std::vector<v2::TileKey> keys;
  keys.reserve(v2::kMaterializedTileIdentityCount);
  constexpr std::array zooms{v2::ZoomLevel::k50Percent, v2::ZoomLevel::k100Percent,
                             v2::ZoomLevel::k200Percent, v2::ZoomLevel::k400Percent};
  for (const auto zoom : zooms) {
    const auto grid = v2::tile_grid(zoom);
    for (int row = 0; row < grid.rows; ++row) {
      for (int column = 0; column < grid.columns; ++column) {
        keys.push_back({zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      }
    }
  }
  return keys;
}

std::vector<v2::TileKey> full_tile_keys(std::span<const v2::TileKey> keys) {
  std::vector<v2::TileKey> output;
  output.reserve(keys.size());
  for (const auto key : keys) {
    const auto bounds = v2::tile_pixel_bounds(key);
    if (bounds.x1 - bounds.x0 == v2::kTileWidth && bounds.y1 - bounds.y0 == v2::kTileHeight) {
      output.push_back(key);
    }
  }
  return output;
}

double median(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2U];
}

template <typename Function>
double measured_median(Function&& function) {
  std::vector<double> samples;
  samples.reserve(kMeasuredRuns);
  for (std::size_t run = 0; run < kWarmupRuns + kMeasuredRuns; ++run) {
    const auto start = Clock::now();
    const bool ok = function(run);
    const auto end = Clock::now();
    if (!ok) {
      return -1.0;
    }
    if (run >= kWarmupRuns) {
      samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
  }
  return median(std::move(samples));
}

bool publish_raw(Rig& rig, v2::TileKey key, v2::MaterializationQuality quality) {
  return rig.canvas.publish_tile(key, rig.canvas.current_revision(), quality, rig.tile).has_value();
}

bool fill_raw(Rig& rig, std::span<const v2::TileKey> keys) {
  for (const auto key : keys) {
    if (!publish_raw(rig, key, v2::MaterializationQuality::kImmediate)) {
      return false;
    }
  }
  return true;
}

v2::PixelRect tile_world_bounds(v2::TileKey key) {
  const auto bounds = v2::tile_pixel_bounds(key);
  const int percent = v2::zoom_percent(key.zoom);
  const auto ceil_div = [](int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
  };
  return {.x0 = bounds.x0 * 100 / percent,
          .y0 = bounds.y0 * 100 / percent,
          .x1 = ceil_div(bounds.x1 * 100, percent),
          .y1 = ceil_div(bounds.y1 * 100, percent)};
}

bool stage_overview(Rig& rig, v2::DocumentRevision revision, v2::PixelRect world_bounds,
                    std::vector<std::uint16_t>& patch, v2::InPlaceOverviewStage& stage,
                    v2::OverviewRevisionPublication& publication) {
  const auto bounds = v2::overview_bounds_for_world(world_bounds);
  const std::size_t pixels = static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                             static_cast<std::size_t>(bounds.y1 - bounds.y0);
  patch.assign(pixels, 0x7BEFU);
  publication = {.bounds = bounds, .pixels = patch};
  while (!stage.complete()) {
    if (rig.canvas.stage_in_place_overview_rows(revision, publication, world_bounds, 32U, stage) ==
        v2::OverviewStageStatus::kError) {
      return false;
    }
  }
  return true;
}

double measure_raw_metadata_fresh(std::span<const v2::TileKey> resident,
                                  std::span<const v2::TileKey> retained,
                                  v2::PixelRect world_bounds) {
  std::vector<double> samples;
  samples.reserve(kMeasuredRuns);
  for (std::size_t run = 0; run < kWarmupRuns + kMeasuredRuns; ++run) {
    Rig rig;
    if (!rig.ready() || !fill_raw(rig, resident)) {
      return -1.0;
    }
    std::vector<std::uint16_t> patch;
    v2::InPlaceOverviewStage stage;
    v2::OverviewRevisionPublication publication;
    if (!stage_overview(rig, {1}, world_bounds, patch, stage, publication)) {
      return -1.0;
    }
    const auto start = Clock::now();
    while (true) {
      const auto slice =
          rig.canvas.stage_in_place_metadata({1}, publication, world_bounds, retained, 64U, stage);
      if (slice.status == v2::OverviewStageStatus::kError) {
        return -1.0;
      }
      if (slice.status == v2::OverviewStageStatus::kComplete) {
        break;
      }
    }
    const auto end = Clock::now();
    if (!rig.canvas.commit_staged_in_place_revision({1}, publication, world_bounds, retained,
                                                    stage)) {
      return -1.0;
    }
    if (run >= kWarmupRuns) {
      samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
  }
  return median(std::move(samples));
}

double measure_uniform_metadata_fresh(std::span<const v2::TileKey> keys,
                                      std::span<const v2::TileKey> retained,
                                      v2::PixelRect world_bounds) {
  std::vector<double> samples;
  samples.reserve(kMeasuredRuns);
  for (std::size_t run = 0; run < kWarmupRuns + kMeasuredRuns; ++run) {
    Rig rig;
    if (!rig.ready()) {
      return -1.0;
    }
    for (const auto key : keys) {
      if (!rig.canvas.publish_uniform(key, {0}, v2::MaterializationQuality::kSettled).has_value()) {
        return -1.0;
      }
    }
    std::vector<std::uint16_t> patch;
    v2::InPlaceOverviewStage stage;
    v2::OverviewRevisionPublication publication;
    if (!stage_overview(rig, {1}, world_bounds, patch, stage, publication)) {
      return -1.0;
    }
    const auto start = Clock::now();
    while (true) {
      const auto slice =
          rig.canvas.stage_in_place_metadata({1}, publication, world_bounds, retained, 64U, stage);
      if (slice.status == v2::OverviewStageStatus::kError) {
        return -1.0;
      }
      if (slice.status == v2::OverviewStageStatus::kComplete) {
        break;
      }
    }
    const auto end = Clock::now();
    if (!rig.canvas.commit_staged_in_place_revision({1}, publication, world_bounds, retained,
                                                    stage)) {
      return -1.0;
    }
    if (run >= kWarmupRuns) {
      samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
  }
  return median(std::move(samples));
}

std::size_t uniform_window_count(v2::PixelRect world_bounds) {
  std::size_t count = 0U;
  constexpr std::array zooms{v2::ZoomLevel::k50Percent, v2::ZoomLevel::k100Percent,
                             v2::ZoomLevel::k200Percent, v2::ZoomLevel::k400Percent};
  const auto ceil_div = [](int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
  };
  for (const auto zoom : zooms) {
    const int percent = v2::zoom_percent(zoom);
    const auto grid = v2::tile_grid(zoom);
    const int first_column =
        std::clamp(world_bounds.x0 * percent / 100 / v2::kTileWidth - 1, 0, grid.columns - 1);
    const int last_column = std::clamp(
        (ceil_div(world_bounds.x1 * percent, 100) - 1) / v2::kTileWidth + 1, 0, grid.columns - 1);
    const int first_row =
        std::clamp(world_bounds.y0 * percent / 100 / v2::kTileHeight - 1, 0, grid.rows - 1);
    const int last_row = std::clamp(
        (ceil_div(world_bounds.y1 * percent, 100) - 1) / v2::kTileHeight + 1, 0, grid.rows - 1);
    count += static_cast<std::size_t>(last_column - first_column + 1) *
             static_cast<std::size_t>(last_row - first_row + 1);
  }
  return count;
}

std::vector<v2::TileKey> absorption_resident_keys(std::span<const v2::TileKey> raw_keys) {
  std::vector<v2::TileKey> output;
  output.reserve(v2::kTileSlotCount);
  for (std::uint16_t row = 0; row < 8U; ++row) {
    for (std::uint16_t column = 0; column < 7U; ++column) {
      output.push_back({v2::ZoomLevel::k400Percent, column, row});
    }
  }
  for (const auto key : raw_keys) {
    if (output.size() == v2::kTileSlotCount) {
      break;
    }
    const auto bounds = tile_world_bounds(key);
    if (bounds.x0 < 112 && bounds.y0 < 128) {
      continue;
    }
    output.push_back(key);
  }
  return output;
}

Measurement aa_warm_publication(std::span<const v2::TileKey> keys) {
  constexpr std::size_t kIterations = 512U;
  Rig rig;
  if (!rig.ready() || !publish_raw(rig, keys.front(), v2::MaterializationQuality::kImmediate)) {
    return {};
  }
  const double wall = measured_median([&](std::size_t) {
    for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
      if (!publish_raw(rig, keys.front(), v2::MaterializationQuality::kSettled)) {
        return false;
      }
    }
    return true;
  });
  return {.name = "aa_warm_publication",
          .median_us = wall / static_cast<double>(kIterations),
          .iterations = kIterations};
}

Measurement aa_free_publication(std::span<const v2::TileKey> keys) {
  constexpr std::size_t kResident = 280U;
  constexpr std::size_t kIterations = 112U;
  const double wall = measured_median([&](std::size_t) {
    Rig rig;
    if (!rig.ready() || !fill_raw(rig, keys.first(kResident))) {
      return false;
    }
    return fill_raw(rig, keys.subspan(kResident, kIterations));
  });
  const std::size_t scans = (2U * kResident + kIterations + 1U) * kIterations / 2U;
  return {.name = "aa_free_publication_280",
          .median_us = wall / static_cast<double>(kIterations),
          .iterations = kIterations,
          .slot_scans = scans};
}

Measurement aa_eviction_publication(std::span<const v2::TileKey> keys) {
  constexpr std::size_t kIterations = 512U;
  const double wall = measured_median([&](std::size_t) {
    Rig rig;
    if (!rig.ready() || !fill_raw(rig, keys.first(v2::kTileSlotCount))) {
      return false;
    }
    return fill_raw(rig, keys.subspan(v2::kTileSlotCount, kIterations));
  });
  return {.name = "aa_eviction_full",
          .median_us = wall / static_cast<double>(kIterations),
          .iterations = kIterations,
          .slot_scans = v2::kTileSlotCount * kIterations};
}

Measurement absorption_commit(std::span<const v2::TileKey> keys) {
  constexpr std::size_t kRetained = v2::kMaximumVisibleTiles;
  const auto resident = absorption_resident_keys(keys);
  if (resident.size() != v2::kTileSlotCount) {
    return {};
  }
  const auto retained = std::span(resident).first(kRetained);
  const auto world_bounds = v2::PixelRect{0, 0, 112, 128};
  return {.name = "absorption_commit_visible_56",
          .median_us = measure_raw_metadata_fresh(resident, retained, world_bounds),
          .iterations = 1U,
          .slot_scans = v2::kTileSlotCount,
          .uniform_window_scans = uniform_window_count(world_bounds),
          .retained_comparisons = 0U,
          .retained_marks = retained.size()};
}

Measurement history_raw_commit(std::span<const v2::TileKey> keys) {
  const auto retained = keys.first(v2::kTileSlotCount);
  const auto world_bounds = v2::PixelRect{0, 0, v2::kWorldWidth, v2::kWorldHeight};
  return {.name = "history_commit_raw_448",
          .median_us = measure_raw_metadata_fresh(retained, retained, world_bounds),
          .iterations = 1U,
          .slot_scans = v2::kTileSlotCount,
          .uniform_window_scans = v2::kMaterializedTileIdentityCount,
          .retained_comparisons = 0U,
          .retained_marks = retained.size()};
}

Measurement history_uniform_commit(std::span<const v2::TileKey> keys) {
  const auto retained = keys.first(v2::kTileSlotCount);
  const auto world_bounds = v2::PixelRect{0, 0, v2::kWorldWidth, v2::kWorldHeight};
  return {.name = "history_commit_uniform_catalog",
          .median_us = measure_uniform_metadata_fresh(keys, retained, world_bounds),
          .iterations = 1U,
          .slot_scans = v2::kTileSlotCount,
          .uniform_window_scans = v2::kMaterializedTileIdentityCount,
          .retained_comparisons = 0U,
          .retained_marks = retained.size()};
}

}  // namespace

int main() {
  const auto keys = all_keys();
  const auto raw_keys = full_tile_keys(keys);
  if (keys.size() != v2::kMaterializedTileIdentityCount || raw_keys.size() < 8'000U) {
    return EXIT_FAILURE;
  }
  const std::array measurements{
      aa_warm_publication(raw_keys),     aa_free_publication(raw_keys),
      aa_eviction_publication(raw_keys), absorption_commit(raw_keys),
      history_raw_commit(raw_keys),      history_uniform_commit(keys),
  };
  bool ok = true;
  for (const auto& measurement : measurements) {
    ok = ok && measurement.name[0] != '\0' && measurement.median_us >= 0.0;
    std::printf(
        "cache_benchmark name=%s median_us=%.3f iterations=%zu slot_scans=%zu "
        "uniform_window_scans=%zu retained_comparisons=%zu retained_marks=%zu\n",
        measurement.name, measurement.median_us, measurement.iterations, measurement.slot_scans,
        measurement.uniform_window_scans, measurement.retained_comparisons,
        measurement.retained_marks);
  }
  std::printf("cache_benchmark exact=%u slots=%zu identities=%zu verdict=%s\n", ok ? 1U : 0U,
              v2::kTileSlotCount, v2::kMaterializedTileIdentityCount, ok ? "PASS" : "FAIL");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
