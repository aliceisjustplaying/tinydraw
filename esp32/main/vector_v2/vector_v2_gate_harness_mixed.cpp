#include "vector_v2_gate_harness_internal.h"

namespace tinydraw::esp32::gate_harness {

bool run_cache_tour_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                         MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome) {
  struct TourTotals {
    std::size_t steps = 0;
    std::size_t tiles_published = 0;
    std::int64_t wall_us = 0;
  };
  const auto fill_view = [&](const vector_v2::ViewRequest& view, TourTotals& totals) -> bool {
    const std::int64_t started = esp_timer_get_time();
    for (std::size_t step_index = 0; step_index < 100'000U; ++step_index) {
      const auto step = producer.produce_next(view);
      if (!step.has_value()) {
        return false;
      }
      ++totals.steps;
      totals.tiles_published += step->tiles_published;
      if (step->complete) {
        totals.wall_us += esp_timer_get_time() - started;
        return true;
      }
    }
    return false;
  };
  const auto view_at = [](ZoomLevel zoom, int x, int y) {
    return vector_v2::ViewRequest{
        .zoom = zoom,
        .level_pixels = {x, y, x + vector_v2::kOverviewWidth, y + vector_v2::kOverviewHeight},
    };
  };

  // Home view at 100% over inked content; presenter.set_view also registers
  // the protected footprint exactly like real navigation.
  if (!canvas.discard_tiles() ||
      !presenter.set_view(ZoomLevel::k100Percent, 63, 63, chrome, now_us()).passed) {
    return false;
  }
  TourTotals home_fill{};
  if (!fill_view(view_at(ZoomLevel::k100Percent, 63, 63), home_fill)) {
    return false;
  }

  constexpr std::array<int, 4> kTourX{200, 1'900, 3'600, 5'300};
  constexpr std::array<int, 4> kTourY{300, 2'400, 4'500, 6'600};
  TourTotals forward{};
  for (const int y : kTourY) {
    for (const int x : kTourX) {
      if (!presenter.set_view(ZoomLevel::k400Percent, x, y, chrome, now_us()).passed ||
          !fill_view(view_at(ZoomLevel::k400Percent, x, y), forward)) {
        return false;
      }
    }
  }

  // Return trip in reverse order: count what was evicted before refilling.
  TourTotals return_trip{};
  std::size_t return_missing_tiles = 0;
  for (std::size_t stop = kTourX.size() * kTourY.size(); stop-- > 0U;) {
    const int x = kTourX[stop % kTourX.size()];
    const int y = kTourY[stop / kTourX.size()];
    const auto view = view_at(ZoomLevel::k400Percent, x, y);
    const auto missing = producer.visible_tiles_remaining(view);
    if (!missing.has_value() ||
        !presenter.set_view(ZoomLevel::k400Percent, x, y, chrome, now_us()).passed) {
      return false;
    }
    return_missing_tiles += *missing;
    if (!fill_view(view, return_trip)) {
      return false;
    }
  }

  // Protected home footprint must return sharp with no producer work.
  const auto home_view = view_at(ZoomLevel::k100Percent, 63, 63);
  const auto home_missing = producer.visible_tiles_remaining(home_view);
  const auto home_return = presenter.set_view(ZoomLevel::k100Percent, 63, 63, chrome, now_us());
  const bool home_sharp = home_missing.has_value() && *home_missing == 0U && home_return.passed &&
                          home_return.fallback_pixels == 0U;
  const bool passed = home_sharp;
  std::printf(
      "TINYDRAW_GATE1_CACHE_TOUR slots=%lu stops=%lu forward_steps=%lu forward_tiles=%lu "
      "forward_wall_us=%lld return_missing_tiles=%lu return_steps=%lu return_tiles=%lu "
      "return_wall_us=%lld home_missing=%lu home_fallback_pixels=%lu pass=%u\n",
      static_cast<unsigned long>(canvas.slot_capacity()),
      static_cast<unsigned long>(kTourX.size() * kTourY.size()),
      static_cast<unsigned long>(forward.steps),
      static_cast<unsigned long>(forward.tiles_published), static_cast<long long>(forward.wall_us),
      static_cast<unsigned long>(return_missing_tiles),
      static_cast<unsigned long>(return_trip.steps),
      static_cast<unsigned long>(return_trip.tiles_published),
      static_cast<long long>(return_trip.wall_us),
      static_cast<unsigned long>(home_missing.value_or(999U)),
      static_cast<unsigned long>(home_return.fallback_pixels), passed);
  std::fflush(stdout);
  return passed;
}

// ---- Mixed-zoom drawing gate ----
//
// Deterministic reproduction of the manual-session drawing regression: a
// helpfully warm multi-zoom cache makes every committed chunk eagerly mutate
// resident raw tiles at every zoom, so interactive drawing gets slower the
// better the cache is doing its job. The gate warms four tiled-zoom viewports
// over the same dense seed-7 world corner, then draws and erases a
// boustrophedon XL gesture at every zoom through the product chunk policy and
// the product in-place commit call. Each 48-sample chunk spans a full
// viewport-width band, maximizing per-chunk cross-zoom tile fanout. The
// product alarm is 15 ms per chunk; the target is 10-12 ms.

struct MixedDrawStrokeStats {
  std::size_t chunks = 0;
  std::int64_t append_total_us = 0;
  std::int64_t append_max_us = 0;
  vector_v2::InPlaceAppendPhases phase_max{};
  vector_v2::InPlaceRetainDrops drops{};
  std::size_t affected_tiles = 0;
  std::size_t published_tiles = 0;
  std::size_t fallback_tiles = 0;
  std::size_t visible_fallback_tiles = 0;
  // Committed-overlay drain receipts: absorption work that ran off the
  // input path in cooperative product-sized slices.
  std::size_t drain_ops = 0;
  std::size_t drain_slices = 0;
  std::size_t max_pending_operations = 0;
  std::int64_t drain_total_us = 0;
  std::int64_t drain_max_slice_us = 0;
  vector_v2::PendingAbsorptionWorkUnit drain_max_unit = vector_v2::PendingAbsorptionWorkUnit::kNone;
  bool committed = false;
  bool authority = false;
  bool refresh_passed = false;
};

const char* tool_name(OperationTool tool) {
  return tool == OperationTool::kEraser ? "eraser" : "pen";
}

const char* absorption_unit_name(vector_v2::PendingAbsorptionWorkUnit unit) {
  using Unit = vector_v2::PendingAbsorptionWorkUnit;
  switch (unit) {
    case Unit::kNone:
      return "none";
    case Unit::kCopyOverview:
      return "copy_overview";
    case Unit::kRasterOverview:
      return "raster_overview";
    case Unit::kEnumerate:
      return "enumerate";
    case Unit::kUniform:
      return "uniform";
    case Unit::kVisibleRaw:
      return "visible_raw";
    case Unit::kOffscreenRaw:
      return "offscreen_raw";
    case Unit::kStageOverview:
      return "stage_overview";
    case Unit::kStageUniforms:
      return "stage_uniforms";
    case Unit::kStageRawSlots:
      return "stage_raw_slots";
    case Unit::kStageRerenderDamage:
      return "stage_rerender";
    case Unit::kStageOccupancy:
      return "stage_occupancy";
    case Unit::kCommit:
      return "commit";
  }
  return "unknown";
}

// Every mixed-draw viewport is anchored at world (48, 48) so each zoom's
// resident footprint overlaps every stroke's world bounds. 25% is the
// complete overview and keeps its native origin.
int mixed_draw_level_origin(ZoomLevel zoom) {
  return zoom == ZoomLevel::k25Percent ? 0 : 48 * vector_v2::zoom_percent(zoom) / 100;
}

vector_v2::ViewRequest mixed_draw_view(ZoomLevel zoom) {
  const int origin = mixed_draw_level_origin(zoom);
  return {
      .zoom = zoom,
      .level_pixels = {origin, origin, origin + vector_v2::kOverviewWidth,
                       origin + vector_v2::kOverviewHeight},
  };
}

bool fill_view_to_completion(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                             std::size_t& tiles, std::int64_t& wall_us) {
  const std::int64_t started = esp_timer_get_time();
  for (std::size_t step_index = 0; step_index < 100'000U; ++step_index) {
    // Yield periodically so the CPU0 idle task feeds the task watchdog.
    if (step_index % 50U == 49U) {
      vTaskDelay(1);
    }
    const auto step = producer.produce_next(view);
    if (!step.has_value()) {
      return false;
    }
    tiles += step->tiles_published;
    if (step->complete) {
      wall_us += esp_timer_get_time() - started;
      return true;
    }
  }
  return false;
}

// lookup is const and does not touch recency, so the census cannot perturb
// eviction order.
MixedDrawCensus census_zoom_tiles(const MaterializedCanvas& canvas, ZoomLevel zoom) {
  MixedDrawCensus census;
  const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      if (!source.has_value()) {
        continue;
      }
      if (source->kind == vector_v2::SourceKind::kTileSlot) {
        ++census.raw;
      } else if (source->kind == vector_v2::SourceKind::kUniform) {
        ++census.uniform;
      }
    }
  }
  return census;
}

bool run_mixed_zoom_stroke(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                           OperationLog& log, MaterializedCanvas& canvas,
                           const vector_v2::ChromeState& chrome,
                           const vector_v2::InPlaceAppendWorkspace& workspace,
                           std::span<CompactOperationSample> builder_storage, ZoomLevel zoom,
                           OperationTool tool, std::uint16_t color, std::uint16_t gesture_id,
                           MixedDrawStrokeStats& stats) {
  const int origin = mixed_draw_level_origin(zoom);
  if (!presenter.set_view(zoom, origin, origin, chrome, now_us()).passed) {
    return false;
  }
  const auto view = mixed_draw_view(zoom);
  // Mirror the product coordinator exactly: no priority view at 25%.
  const std::optional<vector_v2::ViewRequest> priority_view =
      zoom == ZoomLevel::k25Percent ? std::optional<vector_v2::ViewRequest>{} : std::optional{view};
  const float scale = 100.0F / static_cast<float>(vector_v2::zoom_percent(zoom));
  // XL brush: 20 screen pixels at every zoom, like the product tool.
  const float radius = 20.0F * scale;
  // The eraser pass shifts down half a brush so it crosses the pen bands
  // instead of retracing identical pixels.
  const float start_offset = tool == OperationTool::kEraser ? radius * 0.5F : 0.0F;
  const float margin = radius + 2.0F;
  const float wx0 = static_cast<float>(origin) * scale;
  const float wy0 = wx0;
  const float x_min = wx0 + margin;
  const float x_max = wx0 + static_cast<float>(vector_v2::kOverviewWidth) * scale - margin;
  const float y_min = wy0 + margin + start_offset;
  const float y_max = wy0 + static_cast<float>(vector_v2::kOverviewHeight) * scale - margin;
  constexpr std::size_t kStrokeSamples = 1'536;
  constexpr std::size_t kSweeps = 32;
  // 48 samples per horizontal sweep: one interactive chunk spans one full
  // viewport-width band.
  const float dx = (x_max - x_min) / 47.0F;
  const float dy = (y_max - y_min) / static_cast<float>(kSweeps);

  vector_v2::ChainedOperationBuilder builder(builder_storage, kGateChunkSampleLimit);
  std::optional<vector_v2::PixelRect> world_bounds;
  const auto accumulate = [&](const vector_v2::IncrementalAppendResult& result) {
    stats.phase_max.prepare_us = std::max(stats.phase_max.prepare_us, result.phases.prepare_us);
    stats.phase_max.overview_us = std::max(stats.phase_max.overview_us, result.phases.overview_us);
    stats.phase_max.enumerate_us =
        std::max(stats.phase_max.enumerate_us, result.phases.enumerate_us);
    stats.phase_max.uniform_retain_us =
        std::max(stats.phase_max.uniform_retain_us, result.phases.uniform_retain_us);
    stats.phase_max.raw_retain_us =
        std::max(stats.phase_max.raw_retain_us, result.phases.raw_retain_us);
    stats.phase_max.offscreen_retain_us =
        std::max(stats.phase_max.offscreen_retain_us, result.phases.offscreen_retain_us);
    stats.phase_max.commit_us = std::max(stats.phase_max.commit_us, result.phases.commit_us);
    stats.affected_tiles += result.affected_resident_tiles;
    stats.published_tiles += result.published_tiles;
    stats.fallback_tiles += result.fallback_tiles;
    stats.visible_fallback_tiles += result.visible_fallback_tiles;
    stats.drops.visible_uniform_no_slot += result.drops.visible_uniform_no_slot;
    stats.drops.visible_uniform_paint_fail += result.drops.visible_uniform_paint_fail;
    stats.drops.visible_raw_edit_fail += result.drops.visible_raw_edit_fail;
    stats.drops.visible_raw_paint_fail += result.drops.visible_raw_paint_fail;
    stats.drops.offscreen_skipped += result.drops.offscreen_skipped;
    if (!world_bounds.has_value()) {
      world_bounds = result.affected_world_bounds;
    } else {
      world_bounds->x0 = std::min(world_bounds->x0, result.affected_world_bounds.x0);
      world_bounds->y0 = std::min(world_bounds->y0, result.affected_world_bounds.y0);
      world_bounds->x1 = std::max(world_bounds->x1, result.affected_world_bounds.x1);
      world_bounds->y1 = std::max(world_bounds->y1, result.affected_world_bounds.y1);
    }
  };
  vector_v2::PendingOperationAbsorption absorption;
  const auto absorb_slice = [&]() -> bool {
    if (!absorption.active() && vector_v2::pending_operation_count(log, canvas) == 0U) {
      return true;
    }
    if (!absorption.active()) {
      // Producer batches retain prepared chords between calls. An absorption
      // commit supersedes their canvas revision and shares the plan storage,
      // so abandon unpublished producer work before the first slice.
      producer.cancel_pending_work();
    }
    const std::int64_t started_us = esp_timer_get_time();
    const MixedDrawAbsorbLimit limit{.deadline_us = started_us + kMixedDrawAbsorbSliceBudgetUs};
    const auto absorbed = vector_v2::absorb_pending_operation_slice(
        {log,
         canvas,
         workspace,
         absorption,
         priority_view,
         {.requested = &MixedDrawAbsorbLimit::requested,
          .context = &limit,
          .raster_work_px = kMixedDrawAbsorbRasterWorkPixels},
         {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs}});
    const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
    ++stats.drain_slices;
    stats.drain_total_us += elapsed_us;
    if (elapsed_us > stats.drain_max_slice_us) {
      stats.drain_max_slice_us = elapsed_us;
      stats.drain_max_unit = absorbed.work_unit;
    }
    if (absorbed.status == vector_v2::PendingAbsorptionStatus::kError) {
      absorption.cancel();
      return false;
    }
    if (absorbed.status == vector_v2::PendingAbsorptionStatus::kComplete) {
      ++stats.drain_ops;
      accumulate(absorbed.result);
    }
    return true;
  };
  const auto commit_ready = [&](vector_v2::ChainedOperationStatus status)
      -> std::optional<vector_v2::ChainedOperationStatus> {
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto pending = builder.pending_append();
      if (!pending.has_value()) {
        return std::nullopt;
      }
      // Product input publishes authority directly; absorption never blocks a
      // high-water append now that the pending overlay stays exact.
      const std::int64_t started_us = esp_timer_get_time();
      const auto committed =
          vector_v2::append_authority_only(log, *pending, {.now_us = &esp_timer_get_time});
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      if (!committed.has_value()) {
        return std::nullopt;
      }
      stats.append_total_us += elapsed_us;
      stats.append_max_us = std::max(stats.append_max_us, elapsed_us);
      ++stats.chunks;
      accumulate(*committed);
      status = builder.acknowledge_commit();
      stats.max_pending_operations =
          std::max(stats.max_pending_operations, vector_v2::pending_operation_count(log, canvas));
    }
    if (status != vector_v2::ChainedOperationStatus::kAccepted &&
        status != vector_v2::ChainedOperationStatus::kComplete) {
      return std::nullopt;
    }
    return status;
  };

  float x = x_min;
  float y = y_min;
  float direction = 1.0F;
  std::uint32_t timestamp_us = now_us();
  if (!builder.begin(
          tool, color, gesture_id,
          {.world_x = x, .world_y = y, .radius = radius, .timestamp_us = timestamp_us})) {
    return false;
  }
  for (std::size_t index = 1; index < kStrokeSamples; ++index) {
    x += dx * direction;
    if (x > x_max || x < x_min) {
      direction = -direction;
      x = std::clamp(x, x_min, x_max);
      y = std::min(y + dy, y_max);
    }
    timestamp_us += 8'000U;
    const vector_v2::OperationPoint point{
        .world_x = x, .world_y = y, .radius = radius, .timestamp_us = timestamp_us};
    const bool final_sample = index + 1U == kStrokeSamples;
    if (!commit_ready(final_sample ? builder.finish(point) : builder.add(point)).has_value()) {
      return false;
    }
    // Product polls background after every handled sample, not only after a
    // 32-sample authority chunk. Carry one bounded absorption quantum through
    // each of those opportunities so backlog telemetry has product cadence.
    if (!absorb_slice()) {
      return false;
    }
  }
  // Post-stroke drain: production absorbs in idle slices; the gate
  // compresses that into one receipted loop before the exact swap refresh.
  while (absorption.active() || vector_v2::pending_operation_count(log, canvas) != 0U) {
    if (!absorb_slice()) {
      return false;
    }
  }
  stats.committed = !builder.active();
  stats.authority = log.current_revision() == canvas.current_revision();
  if (world_bounds.has_value()) {
    stats.refresh_passed =
        presenter
            .refresh_region(vector_v2::operation_level_bounds(*world_bounds, zoom), chrome,
                            now_us())
            .passed;
  }
  return true;
}

bool run_mixed_zoom_draw_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const vector_v2::InPlaceAppendWorkspace& workspace,
                              std::span<CompactOperationSample> builder_storage) {
  constexpr std::array kWarmZooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent,
                                  ZoomLevel::k200Percent, ZoomLevel::k400Percent};
  constexpr std::array kDrawZooms{ZoomLevel::k25Percent, ZoomLevel::k50Percent,
                                  ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                                  ZoomLevel::k400Percent};
  // Self-contained cache state: discard, then warm deterministically so the
  // measurement does not depend on whichever gate ran before this one.
  if (!canvas.discard_tiles()) {
    return false;
  }
  for (const ZoomLevel zoom : kWarmZooms) {
    const int origin = mixed_draw_level_origin(zoom);
    if (!presenter.set_view(zoom, origin, origin, chrome, now_us()).passed) {
      return false;
    }
    std::size_t tiles = 0;
    std::int64_t wall_us = 0;
    if (!fill_view_to_completion(producer, mixed_draw_view(zoom), tiles, wall_us)) {
      return false;
    }
    const MixedDrawCensus census = census_zoom_tiles(canvas, zoom);
    std::printf(
        "TINYDRAW_GATE1_MIXED_DRAW_WARM zoom=%s fill_tiles=%lu fill_us=%lld raw=%lu "
        "uniform=%lu\n",
        zoom_name(zoom), static_cast<unsigned long>(tiles), static_cast<long long>(wall_us),
        static_cast<unsigned long>(census.raw), static_cast<unsigned long>(census.uniform));
  }

  bool strokes_correct = true;
  bool timing_pass = true;
  std::int64_t worst_append_us = 0;
  std::size_t strokes = 0;
  std::uint16_t gesture_id = 1;
  for (const ZoomLevel zoom : kDrawZooms) {
    // Re-warm every tiled viewport before each zoom's stroke pair. Strokes
    // drop affected resident tiles (that is the accepted policy), so without
    // re-warming the later zooms would measure an empty cache instead of the
    // product nightmare: drawing at the active zoom with a fully warm
    // multi-zoom cache underneath.
    for (const ZoomLevel warm_zoom : kWarmZooms) {
      const int warm_origin = mixed_draw_level_origin(warm_zoom);
      if (!presenter.set_view(warm_zoom, warm_origin, warm_origin, chrome, now_us()).passed) {
        return false;
      }
      std::size_t warm_tiles = 0;
      std::int64_t warm_wall_us = 0;
      if (!fill_view_to_completion(producer, mixed_draw_view(warm_zoom), warm_tiles,
                                   warm_wall_us)) {
        return false;
      }
    }
    for (const OperationTool tool : {OperationTool::kPen, OperationTool::kEraser}) {
      MixedDrawStrokeStats stats{};
      const bool run_ok = run_mixed_zoom_stroke(
          presenter, producer, log, canvas, chrome, workspace, builder_storage, zoom, tool,
          tool == OperationTool::kPen ? 0x001FU : 0x0000U, gesture_id++, stats);
      const bool correct = run_ok && stats.committed && stats.authority && stats.refresh_passed &&
                           stats.chunks >= 24U;
      // Visible tiles are budget-exempt: any dropped tile intersecting the
      // priority view is an on-glass blur. Off-view drops at the active zoom
      // are the accepted budget behavior (brush bleed past the viewport) and
      // idle repair rebuilds them. 25% has no priority view and stays exempt.
      const bool visible_sharp =
          zoom == ZoomLevel::k25Percent || stats.visible_fallback_tiles == 0U;
      const bool cooperative_evidence = stats.drain_slices > stats.drain_ops &&
                                        stats.drain_max_slice_us <= kMixedDrawAbsorbSliceGuardUs &&
                                        stats.max_pending_operations <= kPendingOperationHighWater;
      const bool stroke_pass =
          correct && visible_sharp && stats.append_max_us < 15'000 && cooperative_evidence;
      std::printf(
          "TINYDRAW_GATE1_MIXED_DRAW zoom=%s tool=%s chunks=%lu append_max_us=%lld "
          "append_avg_us=%lld append_total_us=%lld affected_tiles=%lu published=%lu "
          "fallback=%lu visible_fallback=%lu drop_uni_slot=%lu drop_uni_paint=%lu "
          "drop_raw_edit=%lu drop_raw_paint=%lu off_skip=%lu "
          "drain_ops=%lu drain_slices=%lu max_pending=%lu drain_total_us=%lld "
          "drain_max_slice_us=%lld drain_max_unit=%s "
          "ph_prepare_max_us=%lld ph_overview_max_us=%lld "
          "ph_enumerate_max_us=%lld ph_uniform_max_us=%lld ph_raw_max_us=%lld "
          "ph_offscreen_max_us=%lld "
          "ph_commit_max_us=%lld committed=%u authority=%u refresh=%u run_ok=%u "
          "pass=%u\n",
          zoom_name(zoom), tool_name(tool), static_cast<unsigned long>(stats.chunks),
          static_cast<long long>(stats.append_max_us),
          static_cast<long long>(stats.chunks == 0U ? 0
                                                    : stats.append_total_us /
                                                          static_cast<std::int64_t>(stats.chunks)),
          static_cast<long long>(stats.append_total_us),
          static_cast<unsigned long>(stats.affected_tiles),
          static_cast<unsigned long>(stats.published_tiles),
          static_cast<unsigned long>(stats.fallback_tiles),
          static_cast<unsigned long>(stats.visible_fallback_tiles),
          static_cast<unsigned long>(stats.drops.visible_uniform_no_slot),
          static_cast<unsigned long>(stats.drops.visible_uniform_paint_fail),
          static_cast<unsigned long>(stats.drops.visible_raw_edit_fail),
          static_cast<unsigned long>(stats.drops.visible_raw_paint_fail),
          static_cast<unsigned long>(stats.drops.offscreen_skipped),
          static_cast<unsigned long>(stats.drain_ops),
          static_cast<unsigned long>(stats.drain_slices),
          static_cast<unsigned long>(stats.max_pending_operations),
          static_cast<long long>(stats.drain_total_us),
          static_cast<long long>(stats.drain_max_slice_us),
          absorption_unit_name(stats.drain_max_unit),
          static_cast<long long>(stats.phase_max.prepare_us),
          static_cast<long long>(stats.phase_max.overview_us),
          static_cast<long long>(stats.phase_max.enumerate_us),
          static_cast<long long>(stats.phase_max.uniform_retain_us),
          static_cast<long long>(stats.phase_max.raw_retain_us),
          static_cast<long long>(stats.phase_max.offscreen_retain_us),
          static_cast<long long>(stats.phase_max.commit_us), stats.committed, stats.authority,
          stats.refresh_passed, run_ok, stroke_pass);
      std::fflush(stdout);
      worst_append_us = std::max(worst_append_us, stats.append_max_us);
      ++strokes;
      strokes_correct = strokes_correct && correct;
      timing_pass = timing_pass && stroke_pass;
      if (!run_ok) {
        return false;
      }
    }
  }

  // What did drawing cost the warm cache? A future mutation policy that
  // invalidates instead of painting must pay here, visibly.
  for (const ZoomLevel zoom : kWarmZooms) {
    const auto view = mixed_draw_view(zoom);
    const auto missing = producer.visible_tiles_remaining(view);
    const int origin = mixed_draw_level_origin(zoom);
    if (!missing.has_value() ||
        !presenter.set_view(zoom, origin, origin, chrome, now_us()).passed) {
      return false;
    }
    std::size_t tiles = 0;
    std::int64_t wall_us = 0;
    if (!fill_view_to_completion(producer, view, tiles, wall_us)) {
      return false;
    }
    std::printf(
        "TINYDRAW_GATE1_MIXED_DRAW_REVISIT zoom=%s missing=%lu refill_tiles=%lu "
        "refill_us=%lld\n",
        zoom_name(zoom), static_cast<unsigned long>(*missing), static_cast<unsigned long>(tiles),
        static_cast<long long>(wall_us));
  }

  const bool passed = strokes_correct && timing_pass;
  std::printf(
      "TINYDRAW_GATE1_MIXED_DRAW_SUMMARY slots=%lu strokes=%lu worst_append_us=%lld "
      "pass=%u\n",
      static_cast<unsigned long>(canvas.slot_capacity()), static_cast<unsigned long>(strokes),
      static_cast<long long>(worst_append_us), passed);
  std::fflush(stdout);
  return passed;
}

// Encodes the idle-repair product promise: drawing at 25% drops cross-zoom
// tiles by design (the in-place commit budget), so after one quiet moment
// the 100% level must be fully repaired and edge panning meets zero cold
// fallback. Every producer slice stays under the 15 ms input-poll alarm.
bool run_idle_repair_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                          OperationLog& log, MaterializedCanvas& canvas,
                          const vector_v2::ChromeState& chrome,
                          const vector_v2::InPlaceAppendWorkspace& workspace,
                          std::span<CompactOperationSample> builder_storage) {
  if (!canvas.discard_tiles()) {
    return false;
  }
  const int origin = mixed_draw_level_origin(ZoomLevel::k100Percent);
  if (!presenter.set_view(ZoomLevel::k100Percent, origin, origin, chrome, now_us()).passed) {
    return false;
  }
  std::size_t warm_tiles = 0;
  std::int64_t warm_us = 0;
  if (!fill_view_to_completion(producer, mixed_draw_view(ZoomLevel::k100Percent), warm_tiles,
                               warm_us)) {
    return false;
  }
  // The Alice scenario: an XL 25% stroke sweeps the world and drops warm
  // tiles at every other zoom.
  MixedDrawStrokeStats stroke_stats{};
  if (!run_mixed_zoom_stroke(presenter, producer, log, canvas, chrome, workspace, builder_storage,
                             ZoomLevel::k25Percent, OperationTool::kPen, 0x001FU, 5'000,
                             stroke_stats) ||
      !stroke_stats.committed) {
    return false;
  }
  // Back at 100% with the visible fill complete: exactly the state the
  // product loop reaches before its idle-repair branch runs.
  if (!presenter.set_view(ZoomLevel::k100Percent, origin, origin, chrome, now_us()).passed) {
    return false;
  }
  const auto active_view = mixed_draw_view(ZoomLevel::k100Percent);
  std::size_t refill_tiles = 0;
  std::int64_t refill_us = 0;
  if (!fill_view_to_completion(producer, active_view, refill_tiles, refill_us)) {
    return false;
  }
  const std::size_t damaged = count_zoom_fallback(canvas, ZoomLevel::k100Percent);
  const auto plan = vector_v2::plan_idle_repair(active_view, canvas.recent_views());
  const std::int64_t repair_started = esp_timer_get_time();
  std::size_t repair_steps = 0;
  std::int64_t worst_step_us = 0;
  for (std::size_t index = 0; index < plan.count; ++index) {
    for (std::size_t guard = 0; guard < 100'000U; ++guard) {
      // Yield periodically so the CPU0 idle task feeds the task watchdog.
      if (guard % 50U == 49U) {
        vTaskDelay(1);
      }
      const std::int64_t step_started = esp_timer_get_time();
      const auto step = producer.produce_next(plan.views[index]);
      const std::int64_t step_us = esp_timer_get_time() - step_started;
      worst_step_us = std::max(worst_step_us, step_us);
      if (!step.has_value()) {
        return false;
      }
      ++repair_steps;
      if (step->complete) {
        break;
      }
    }
  }
  const std::int64_t repair_us = esp_timer_get_time() - repair_started;
  const std::size_t remaining = count_zoom_fallback(canvas, ZoomLevel::k100Percent);
  // Zero fallback identities across the whole 100% level means any pan
  // destination composes without cold work: the border tour is implied.
  const bool passed = damaged != 0U && remaining == 0U && worst_step_us < 15'000;
  std::printf(
      "TINYDRAW_GATE1_IDLE_REPAIR damaged=%lu remaining=%lu plan_views=%lu steps=%lu "
      "repair_us=%lld worst_step_us=%lld warm_tiles=%lu pass=%u\n",
      static_cast<unsigned long>(damaged), static_cast<unsigned long>(remaining),
      static_cast<unsigned long>(plan.count), static_cast<unsigned long>(repair_steps),
      static_cast<long long>(repair_us), static_cast<long long>(worst_step_us),
      static_cast<unsigned long>(warm_tiles), passed);
  return passed;
}

}  // namespace tinydraw::esp32::gate_harness
