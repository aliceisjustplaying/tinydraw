#include "vector_v2_gate_harness_internal.h"

namespace tinydraw::esp32::gate_harness {

// FNV-1a over every identity's lookup kind at one zoom: a repair pass that
// swaps identities while preserving the resident count changes this.
std::uint64_t zoom_identity_signature(MaterializedCanvas& canvas, ZoomLevel zoom) {
  const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1'099'511'628'211ULL;
  };
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      mix(static_cast<std::uint64_t>(row) << 32U | static_cast<std::uint64_t>(column));
      mix(source.has_value() ? static_cast<std::uint64_t>(source->kind) + 1U : 0U);
    }
  }
  return hash;
}

std::size_t count_zoom_fallback(MaterializedCanvas& canvas, ZoomLevel zoom) {
  const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
  std::size_t fallback = 0;
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      if (!source.has_value() || source->kind == vector_v2::SourceKind::kOverview) {
        ++fallback;
      }
    }
  }
  return fallback;
}

// Deterministic pseudo-random stream for the hairline corpus: reproducible
// receipts without grid regularity.
struct HairlineRandom {
  std::uint32_t state = 0x5EED7u;
  std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  // Uniform in [low, high).
  float range(float low, float high) {
    return low + (high - low) * (static_cast<float>(next() & 0xFFFFFFu) / 16'777'216.0F);
  }
};

struct HairlineAppendStats {
  std::size_t operations = 0;
  std::size_t samples = 0;
  std::int64_t append_total_us = 0;
  std::int64_t append_max_us = 0;
};

// One wandering stroke committed as chained operations of at most 12 samples
// sharing endpoints, mirroring interactive chunked commits.
bool append_hairline_stroke(OperationLog& log, MaterializedCanvas& canvas,
                            const InPlaceAppendWorkspace& workspace, HairlineRandom& random,
                            float radius, std::uint16_t color, OperationTool tool,
                            std::uint16_t gesture_id, float length, HairlineAppendStats& stats) {
  constexpr std::size_t kChunkSamples = 12;
  const float margin = radius + 2.0F;
  float x = random.range(margin, static_cast<float>(vector_v2::kWorldWidth) - margin);
  float y = random.range(margin, static_cast<float>(vector_v2::kWorldHeight) - margin);
  float angle = random.range(0.0F, 6.2831853F);
  float remaining = length;
  std::uint16_t elapsed_ms = 0;
  bool continuing = false;
  std::array<CompactOperationSample, kChunkSamples> chunk{};
  while (remaining > 0.0F) {
    std::size_t count = 0;
    if (continuing) {
      // Chained chunks share endpoints: the first sample repeats the
      // previous chunk's final position so no segment is skipped.
      chunk[count++] = {
          .x_quarter = static_cast<std::uint16_t>(x * 16.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 16.0F),
          .radius_256 = static_cast<std::uint16_t>(radius * 256.0F),
          .elapsed_ms = elapsed_ms,
      };
    }
    while (count < kChunkSamples && remaining > 0.0F) {
      if (continuing || count != 0U) {
        const float step = random.range(24.0F, 40.0F);
        angle += random.range(-0.15F, 0.15F);
        x = std::clamp(x + step * std::cos(angle), margin,
                       static_cast<float>(vector_v2::kWorldWidth) - margin);
        y = std::clamp(y + step * std::sin(angle), margin,
                       static_cast<float>(vector_v2::kWorldHeight) - margin);
        elapsed_ms = static_cast<std::uint16_t>(elapsed_ms + 8U);
        remaining -= step;
      }
      chunk[count++] = {
          .x_quarter = static_cast<std::uint16_t>(x * 16.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 16.0F),
          .radius_256 = static_cast<std::uint16_t>(radius * 256.0F),
          .elapsed_ms = elapsed_ms,
      };
    }
    continuing = true;
    const std::int64_t append_started = esp_timer_get_time();
    const auto result =
        append_and_absorb(log, canvas,
                          vector_v2::OperationAppend{.tool = tool,
                                                     .color = color,
                                                     .gesture_id = gesture_id,
                                                     .samples = std::span(chunk.data(), count)},
                          workspace);
    const std::int64_t append_us = esp_timer_get_time() - append_started;
    if (!result.has_value()) {
      return false;
    }
    ++stats.operations;
    stats.samples += count;
    stats.append_total_us += append_us;
    stats.append_max_us = std::max(stats.append_max_us, append_us);
  }
  return true;
}

// Alice's evil corpus: lots of somewhat-random thin strokes drawn at 25%
// (1.3-2 world px), a thin layer at 50% pen width (3.5-4.7 px), and a few
// thick sweeps with erasers mixed in. Dense hairlines defeat uniform-tile
// coverage, so this is the capacity worst case for the raw slot pool.
bool append_hairline_document(OperationLog& log, MaterializedCanvas& canvas,
                              const InPlaceAppendWorkspace& workspace, HairlineAppendStats& stats) {
  constexpr std::array<std::uint16_t, 6> kColors{0x0000U, 0x001FU, 0xF800U,
                                                 0x07E0U, 0x4208U, 0x8010U};
  HairlineRandom random;
  std::uint16_t gesture_id = 7'000;
  for (int stroke = 0; stroke < 220; ++stroke) {
    const bool eraser = (random.next() % 12U) == 0U;
    if (!append_hairline_stroke(log, canvas, workspace, random, random.range(1.3F, 2.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? OperationTool::kEraser : OperationTool::kPen, gesture_id++,
                                random.range(300.0F, 1'400.0F), stats)) {
      return false;
    }
  }
  for (int stroke = 0; stroke < 60; ++stroke) {
    if (!append_hairline_stroke(log, canvas, workspace, random, random.range(3.5F, 4.7F),
                                kColors[random.next() % kColors.size()], OperationTool::kPen,
                                gesture_id++, random.range(200.0F, 800.0F), stats)) {
      return false;
    }
  }
  for (int stroke = 0; stroke < 10; ++stroke) {
    const bool eraser = stroke == 4 || stroke == 9;
    if (!append_hairline_stroke(log, canvas, workspace, random, random.range(40.0F, 80.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? OperationTool::kEraser : OperationTool::kPen, gesture_id++,
                                random.range(800.0F, 2'000.0F), stats)) {
      return false;
    }
  }
  return true;
}

bool append_general_cold_document(OperationLog& log, MaterializedCanvas& canvas,
                                  const InPlaceAppendWorkspace& workspace) {
  if (!append_adversarial_tapered_document(log, canvas, workspace)) {
    return false;
  }
  HairlineAppendStats hairline_stats{};
  const std::int64_t started = esp_timer_get_time();
  const bool appended = append_hairline_document(log, canvas, workspace, hairline_stats);
  std::printf(
      "TINYDRAW_GENERAL_COLD_WORKLOAD operations=%lu samples=%lu hairline_operations=%lu "
      "hairline_samples=%lu hairline_load_us=%lld appended=%u\n",
      static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()),
      static_cast<unsigned long>(hairline_stats.operations),
      static_cast<unsigned long>(hairline_stats.samples),
      static_cast<long long>(esp_timer_get_time() - started), appended);
  return appended;
}

struct MeasuredFill {
  std::size_t tiles = 0;
  std::size_t steps = 0;
  std::int64_t wall_us = 0;
  std::int64_t worst_step_us = 0;
  bool complete = false;
};

bool fill_view_measured(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                        MeasuredFill& fill) {
  const std::int64_t started = esp_timer_get_time();
  for (std::size_t step_index = 0; step_index < 200'000U; ++step_index) {
    // Yield periodically so the CPU0 idle task feeds the task watchdog; the
    // yield sits outside step timing so worst_step_us stays honest.
    if (step_index % 50U == 49U) {
      vTaskDelay(1);
    }
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    const std::int64_t step_us = esp_timer_get_time() - step_started;
    if (!step.has_value()) {
      return false;
    }
    ++fill.steps;
    fill.tiles += step->tiles_published;
    fill.worst_step_us = std::max(fill.worst_step_us, step_us);
    if (step->complete) {
      fill.wall_us = esp_timer_get_time() - started;
      fill.complete = true;
      return true;
    }
  }
  return false;
}

// The capacity worst case on the battery: dense hairline documents exceed
// the raw slot pool at 100%, so this gate records the fresh-cold fill
// costs, proves the idle-repair saturation guard stops without churn, and
// measures the felt cost of edge panning at 100% after a guarded repair.
bool run_hairline_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                       OperationLog& log, MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                       const vector_v2::ChromeState& chrome,
                       const InPlaceAppendWorkspace& workspace,
                       std::span<const std::uint16_t> blank_snapshot) {
  const DocumentRevision baseline{canvas.current_revision().value + 1U};
  if (!vector_v2::restore_document_snapshot(log, canvas, baseline, blank_snapshot) ||
      !producer.reset_uniform_baseline(baseline)) {
    return false;
  }
  HairlineAppendStats append_stats{};
  if (!append_hairline_document(log, canvas, workspace, append_stats)) {
    return false;
  }
  std::printf("TINYDRAW_HAIRLINE_DOC ops=%lu samples=%lu append_total_us=%lld append_max_us=%lld\n",
              static_cast<unsigned long>(append_stats.operations),
              static_cast<unsigned long>(append_stats.samples),
              static_cast<long long>(append_stats.append_total_us),
              static_cast<long long>(append_stats.append_max_us));
  // Fresh-cold single-view fills, the adversarial-comparable numbers.
  const vector_v2::ViewRequest center_100{
      .zoom = ZoomLevel::k100Percent,
      .level_pixels = {552, 672, 552 + vector_v2::kOverviewWidth, 672 + vector_v2::kOverviewHeight},
  };
  bool fills_ok =
      run_paced_cold_gate(presenter, producer, canvas, touch, chrome, ZoomLevel::k100Percent, 552,
                          672, "evil_hairlines_capacity", contract::kColdViewportRequiredUs);
  fills_ok =
      run_paced_cold_gate(presenter, producer, canvas, touch, chrome, ZoomLevel::k400Percent, 2'760,
                          3'360, "evil_hairlines_capacity", contract::kColdViewportRequiredUs) &&
      fills_ok;
  // Guarded idle repair from a quiet moment at 100%, then a second plan
  // pass: saturation must stop the sweep without churning the pool.
  bool repair_ok = canvas.discard_tiles() &&
                   presenter.set_view(ZoomLevel::k100Percent, 552, 672, chrome, now_us()).passed;
  MeasuredFill active_fill{};
  repair_ok = repair_ok && fill_view_measured(producer, center_100, active_fill);
  std::int64_t repair_wall = 0;
  std::size_t repair_steps = 0;
  std::int64_t repair_worst = 0;
  bool grid_stopped = false;
  const auto run_guarded_plan = [&](std::size_t& steps, std::int64_t& wall,
                                    std::int64_t& worst) -> bool {
    const auto plan = vector_v2::plan_idle_repair(center_100, canvas.recent_views());
    const std::int64_t plan_started = esp_timer_get_time();
    for (std::size_t index = 0; index < plan.count; ++index) {
      if (index >= plan.grid_start &&
          canvas.resident_raw_tiles() + kRepairSaturationHeadroomTiles >= canvas.slot_capacity()) {
        grid_stopped = true;
        break;
      }
      MeasuredFill view_fill{};
      if (!fill_view_measured(producer, plan.views[index], view_fill)) {
        return false;
      }
      steps += view_fill.steps;
      worst = std::max(worst, view_fill.worst_step_us);
    }
    wall += esp_timer_get_time() - plan_started;
    return true;
  };
  repair_ok = repair_ok && run_guarded_plan(repair_steps, repair_wall, repair_worst);
  const std::size_t resident_after_pass1 = canvas.resident_raw_tiles();
  const std::uint64_t signature_pass1 = zoom_identity_signature(canvas, ZoomLevel::k100Percent);
  std::size_t pass2_steps = 0;
  std::int64_t pass2_wall = 0;
  std::int64_t pass2_worst = 0;
  repair_ok = repair_ok && run_guarded_plan(pass2_steps, pass2_wall, pass2_worst);
  const std::size_t resident_after_pass2 = canvas.resident_raw_tiles();
  // Identity signature, not resident count: replacing every identity while
  // preserving the count is still churn.
  const bool no_churn = resident_after_pass1 == resident_after_pass2 &&
                        signature_pass1 == zoom_identity_signature(canvas, ZoomLevel::k100Percent);
  std::printf(
      "TINYDRAW_HAIRLINE_REPAIR steps=%lu wall_us=%lld worst_step_us=%lld resident=%lu/%lu "
      "grid_stopped=%u pass2_steps=%lu pass2_wall_us=%lld no_churn=%u\n",
      static_cast<unsigned long>(repair_steps), static_cast<long long>(repair_wall),
      static_cast<long long>(repair_worst), static_cast<unsigned long>(resident_after_pass1),
      static_cast<unsigned long>(canvas.slot_capacity()), grid_stopped,
      static_cast<unsigned long>(pass2_steps), static_cast<long long>(pass2_wall), no_churn);
  // Edge tour at 100% after the guarded repair: the felt cost of edge
  // panning on a dense document. Producer-only by design: it prices the
  // cold compute, not presentation.
  constexpr std::array<std::array<int, 2>, 8> kTourStops{{{0, 0},
                                                          {1'104, 0},
                                                          {0, 1'344},
                                                          {1'104, 1'344},
                                                          {552, 0},
                                                          {0, 672},
                                                          {1'104, 672},
                                                          {552, 1'344}}};
  std::int64_t tour_total = 0;
  std::int64_t tour_worst_stop = 0;
  std::int64_t tour_worst_step = 0;
  bool tour_ok = repair_ok;
  for (const auto& stop : kTourStops) {
    if (!tour_ok) {
      break;
    }
    const vector_v2::ViewRequest view{
        .zoom = ZoomLevel::k100Percent,
        .level_pixels = {stop[0], stop[1], stop[0] + vector_v2::kOverviewWidth,
                         stop[1] + vector_v2::kOverviewHeight},
    };
    MeasuredFill stop_fill{};
    tour_ok =
        presenter.set_view(ZoomLevel::k100Percent, stop[0], stop[1], chrome, now_us()).passed &&
        fill_view_measured(producer, view, stop_fill);
    tour_total += stop_fill.wall_us;
    tour_worst_stop = std::max(tour_worst_stop, stop_fill.wall_us);
    tour_worst_step = std::max(tour_worst_step, stop_fill.worst_step_us);
  }
  std::printf(
      "TINYDRAW_HAIRLINE_TOUR stops=%lu total_us=%lld worst_stop_us=%lld worst_step_us=%lld\n",
      static_cast<unsigned long>(kTourStops.size()), static_cast<long long>(tour_total),
      static_cast<long long>(tour_worst_stop), static_cast<long long>(tour_worst_step));
  const MixedDrawCensus census = census_zoom_tiles(canvas, ZoomLevel::k100Percent);
  const std::size_t fallback = count_zoom_fallback(canvas, ZoomLevel::k100Percent);
  const std::int64_t worst_step = std::max({repair_worst, pass2_worst, tour_worst_step});
  // This corpus always saturates the pool, so the guard must have engaged.
  const bool passed =
      fills_ok && repair_ok && tour_ok && no_churn && grid_stopped && worst_step < 15'000;
  std::printf(
      "TINYDRAW_GATE1_HAIRLINE raw=%lu uniform=%lu fallback=%lu capacity=%lu worst_step_us=%lld "
      "grid_stopped=%u pass=%u\n",
      static_cast<unsigned long>(census.raw), static_cast<unsigned long>(census.uniform),
      static_cast<unsigned long>(fallback), static_cast<unsigned long>(canvas.slot_capacity()),
      static_cast<long long>(worst_step), grid_stopped, passed);
  return passed;
}

bool verify_export_reserve() {
  // Honest envelope check (2026-08-18): the product sequences its two
  // transient PSRAM peaks by construction - export is gated on
  // autosave.flush() (worker staging freed) before its own allocation, and
  // export mode is modal so no new checkpoint can arrive. The gate
  // therefore holds each measured envelope in sequence and reports the
  // slack above the tighter one. Receipts:
  // benchmark-results/export-memory-math-2026-08-18/RECEIPT.md.
  const std::size_t free_before = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_before = heap_caps_get_largest_free_block(kExternalCaps);
  void* autosave_reserve = heap_caps_malloc(vector_v2::kAutosaveStagingReserveBytes, kExternalCaps);
  const std::size_t autosave_slack = heap_caps_get_free_size(kExternalCaps);
  const bool autosave_held = autosave_reserve != nullptr;
  heap_caps_free(autosave_reserve);
  void* export_reserve = heap_caps_malloc(vector_v2::kExportWorkspaceReserveBytes, kExternalCaps);
  const std::size_t export_slack = heap_caps_get_free_size(kExternalCaps);
  const bool export_held = export_reserve != nullptr;
  heap_caps_free(export_reserve);
  const bool passed = autosave_held && export_held;
  std::printf(
      "TINYDRAW_EXPORT_RESERVE sequence=autosave_then_export autosave_requested=%lu "
      "export_requested=%lu free_before=%lu largest_before=%lu autosave_slack=%lu "
      "export_slack=%lu pass=%u\n",
      static_cast<unsigned long>(vector_v2::kAutosaveStagingReserveBytes),
      static_cast<unsigned long>(vector_v2::kExportWorkspaceReserveBytes),
      static_cast<unsigned long>(free_before), static_cast<unsigned long>(largest_before),
      static_cast<unsigned long>(autosave_slack), static_cast<unsigned long>(export_slack), passed);
  return passed;
}

bool append_stress_document(OperationLog& log, MaterializedCanvas& canvas,
                            const InPlaceAppendWorkspace& workspace) {
  std::array<CompactOperationSample, kStressSamplesPerOperation> samples{};
  const std::int64_t started = esp_timer_get_time();
  std::int64_t maximum_us = 0;
  for (std::uint32_t operation = 0; operation < kStressOperations; ++operation) {
    const float base_x = 24.0F + static_cast<float>((operation * 47U) % 1'360U);
    const float base_y = 24.0F + static_cast<float>((operation * 73U) % 1'680U);
    for (std::uint32_t index = 0; index < kStressSamplesPerOperation; ++index) {
      const float x = std::clamp(base_x + static_cast<float>(index) * 2.25F, 0.0F,
                                 static_cast<float>(vector_v2::kWorldWidth));
      const int wave = static_cast<int>((operation + index) % 9U) - 4;
      const float y = std::clamp(base_y + static_cast<float>(wave * 3), 0.0F,
                                 static_cast<float>(vector_v2::kWorldHeight));
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>(x * 16.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 16.0F),
          .radius_256 = static_cast<std::uint16_t>((3U + operation % 6U) * 256U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    const std::int64_t append_started = esp_timer_get_time();
    const auto result = append_and_absorb(
        log, canvas,
        vector_v2::OperationAppend{
            .tool = operation % 11U == 10U ? OperationTool::kEraser : OperationTool::kPen,
            .color = static_cast<std::uint16_t>(0x1800U + (operation * 97U) % 0xCFFFU),
            .samples = samples},
        workspace);
    maximum_us = std::max(maximum_us, esp_timer_get_time() - append_started);
    if (!result.has_value()) {
      std::printf("TINYDRAW_LIVE_STRESS_FAIL operation=%lu revision=%lu\n",
                  static_cast<unsigned long>(operation),
                  static_cast<unsigned long>(canvas.current_revision().value));
      return false;
    }
  }
  const std::int64_t elapsed = esp_timer_get_time() - started;
  std::printf(
      "TINYDRAW_LIVE_STRESS operations=%lu samples=%lu total_us=%lld average_us=%lld "
      "maximum_us=%lld free_psram=%lu largest_psram=%lu\n",
      static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<long long>(elapsed),
      static_cast<long long>(elapsed / kStressOperations), static_cast<long long>(maximum_us),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
  return true;
}

}  // namespace tinydraw::esp32::gate_harness
