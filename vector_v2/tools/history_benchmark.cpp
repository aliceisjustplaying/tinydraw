#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/memory_layout.h"

namespace v2 = tinydraw::vector_v2;

namespace {

constexpr std::size_t kOperationCount = 4'000U;
constexpr int kTimingCycles = 2'000;

bool intersects(v2::PixelRect left, v2::PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

struct Rig {
  std::vector<v2::OperationRecord> records = std::vector<v2::OperationRecord>(kOperationCount);
  std::vector<v2::CompactOperationSample> samples =
      std::vector<v2::CompactOperationSample>(kOperationCount);
  std::vector<std::uint64_t> spatial_cells =
      std::vector<std::uint64_t>(v2::operation_spatial_cell_word_count(kOperationCount));
  std::vector<std::uint64_t> spatial_large =
      std::vector<std::uint64_t>(v2::operation_spatial_word_count(kOperationCount));
  v2::OperationSpatialIndex spatial_index{kOperationCount, spatial_cells, spatial_large};
  v2::OperationLog log{records, samples, &spatial_index};
  std::vector<std::uint16_t> overview = std::vector<std::uint16_t>(v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> scratch = std::vector<std::uint16_t>(v2::kOverviewPixels);
  std::vector<v2::MaterializedUniformStorage> uniforms =
      std::vector<v2::MaterializedUniformStorage>(v2::kMaterializedTileIdentityCount);
  std::vector<std::uint8_t> occupancy = std::vector<std::uint8_t>(v2::kOccupancyBytes);
  std::vector<v2::MaterializedSlotStorage> slots{};
  std::vector<std::uint16_t> tile_pixels{};
  std::vector<std::uint16_t> raw_directory =
      std::vector<std::uint16_t>(v2::kMaterializedTileIdentityCount);
  v2::MaterializedCanvas canvas{overview,    uniforms, occupancy,    slots,
                                tile_pixels, {0},      raw_directory};

  bool initialize(bool dense) {
    if (!log.ready() || !canvas.ready()) {
      return false;
    }
    for (std::size_t index = 0; index < kOperationCount; ++index) {
      const bool local = dense || index + 1U == kOperationCount;
      const std::array point{v2::CompactOperationSample{
          .x_quarter =
              static_cast<std::uint16_t>((local ? 32 : 1'280) * v2::kSampleUnitsPerWorldUnit),
          .y_quarter =
              static_cast<std::uint16_t>((local ? 32 : 1'600) * v2::kSampleUnitsPerWorldUnit),
          .radius_256 = 256U,
      }};
      if (!log.append({.color = static_cast<std::uint16_t>(0x001FU + (index & 1U)),
                       .gesture_id = static_cast<std::uint16_t>(index + 1U),
                       .samples = point})
               .has_value()) {
        return false;
      }
    }
    if (!v2::replay_active_overview(log, scratch)) {
      return false;
    }
    return canvas.publish_overview(log.current_revision(), scratch);
  }
};

bool baseline_move(Rig& rig, v2::HistoryDirection direction) {
  auto prepared =
      direction == v2::HistoryDirection::kUndo ? rig.log.prepare_undo() : rig.log.prepare_redo();
  if (!prepared.has_value()) {
    return false;
  }
  const v2::HistoryChange change = prepared->change();
  const v2::PixelRect bounds = v2::overview_bounds_for_world(change.affected_world_bounds);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  auto pixels = std::span(rig.scratch).first(pixel_count);
  std::fill(pixels.begin(), pixels.end(), 0xFFFFU);
  const v2::RasterSurface surface{
      .zoom = v2::ZoomLevel::k25Percent,
      .level_bounds = bounds,
      .pixels = pixels,
      .stride = width,
  };
  for (std::size_t index = 0; index < change.active_operation_count; ++index) {
    const auto operation = prepared->target_operation(index);
    if (!operation.has_value()) {
      return false;
    }
    if (!intersects(v2::operation_level_bounds(operation->world_bounds, v2::ZoomLevel::k25Percent),
                    bounds)) {
      continue;
    }
    if (!v2::apply_incremental_operation(
            {.tool = operation->tool, .color = operation->color, .samples = operation->samples},
            surface)) {
      return false;
    }
  }
  if (!rig.canvas.commit_incremental_revision(change.generation,
                                              {.bounds = bounds, .pixels = pixels},
                                              change.affected_world_bounds, {})) {
    return false;
  }
  prepared->publish();
  return true;
}

bool treatment_move(Rig& rig, v2::HistoryDirection direction) {
  return v2::move_history_incrementally(rig.log, rig.canvas, direction, rig.scratch).has_value();
}

struct QueryCounts {
  std::size_t undo_authority = 0U;
  std::size_t undo_candidates = 0U;
  bool undo_fallback = false;
  std::size_t redo_authority = 0U;
  std::size_t redo_candidates = 0U;
  bool redo_fallback = false;
};

QueryCounts measure_queries(Rig& rig) {
  QueryCounts counts;
  auto measure = [&](v2::PreparedHistoryChange& prepared, std::size_t& authority,
                     std::size_t& candidates, bool& fallback) {
    const v2::PixelRect overview =
        v2::overview_bounds_for_world(prepared.change().affected_world_bounds);
    const v2::PixelRect world{overview.x0 * 4, overview.y0 * 4, overview.x1 * 4, overview.y1 * 4};
    const std::size_t pixel_count = static_cast<std::size_t>(overview.x1 - overview.x0) *
                                    static_cast<std::size_t>(overview.y1 - overview.y0);
    v2::OperationSpatialQueryStats stats;
    const auto count =
        prepared.query_target_spatial(world, std::span(rig.scratch).subspan(pixel_count), &stats);
    authority = prepared.change().active_operation_count;
    candidates = stats.deduplicated_candidates;
    fallback = !count.has_value();
  };

  auto undo = rig.log.prepare_undo();
  if (!undo.has_value()) {
    return {};
  }
  measure(*undo, counts.undo_authority, counts.undo_candidates, counts.undo_fallback);
  undo->publish();
  auto redo = rig.log.prepare_redo();
  if (!redo.has_value()) {
    return {};
  }
  measure(*redo, counts.redo_authority, counts.redo_candidates, counts.redo_fallback);
  redo->publish();
  return counts;
}

template <typename Move>
double time_cycles(Rig& rig, Move move) {
  const auto started = std::chrono::steady_clock::now();
  for (int cycle = 0; cycle < kTimingCycles; ++cycle) {
    if (!move(rig, v2::HistoryDirection::kUndo) || !move(rig, v2::HistoryDirection::kRedo)) {
      return -1.0;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;
  return std::chrono::duration<double, std::milli>(elapsed).count() /
         static_cast<double>(kTimingCycles * 2);
}

bool run_corpus(const char* name, bool dense) {
  Rig query;
  Rig baseline;
  Rig treatment;
  Rig exact_baseline;
  Rig exact_treatment;
  if (!query.initialize(dense) || !baseline.initialize(dense) || !treatment.initialize(dense) ||
      !exact_baseline.initialize(dense) || !exact_treatment.initialize(dense)) {
    return false;
  }
  const QueryCounts counts = measure_queries(query);
  const double baseline_ms = time_cycles(baseline, baseline_move);
  const double treatment_ms = time_cycles(treatment, treatment_move);
  if (baseline_ms < 0.0 || treatment_ms < 0.0 ||
      !baseline_move(exact_baseline, v2::HistoryDirection::kUndo) ||
      !treatment_move(exact_treatment, v2::HistoryDirection::kUndo) ||
      exact_baseline.overview != exact_treatment.overview ||
      !baseline_move(exact_baseline, v2::HistoryDirection::kRedo) ||
      !treatment_move(exact_treatment, v2::HistoryDirection::kRedo) ||
      exact_baseline.overview != exact_treatment.overview) {
    return false;
  }
  std::printf(
      "corpus=%s undo_authority=%zu undo_candidates=%zu undo_fallback=%d "
      "redo_authority=%zu redo_candidates=%zu redo_fallback=%d baseline_ms=%.6f "
      "treatment_ms=%.6f speedup=%.2fx exact=1 extra_bytes=0\n",
      name, counts.undo_authority, counts.undo_candidates, counts.undo_fallback ? 1 : 0,
      counts.redo_authority, counts.redo_candidates, counts.redo_fallback ? 1 : 0, baseline_ms,
      treatment_ms, baseline_ms / treatment_ms);
  return true;
}

}  // namespace

int main() {
  if (!run_corpus("sparse", false) || !run_corpus("dense", true)) {
    std::fputs("history benchmark failed\n", stderr);
    return 1;
  }
  return 0;
}
