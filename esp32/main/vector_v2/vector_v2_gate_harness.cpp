#include "vector_v2_gate_harness_internal.h"

#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
#include "vector_v2_tile_census.h"
#endif

namespace tinydraw::esp32::gate_harness {

[[gnu::noinline]] bool classify_minimap_navigation(VectorV2Presenter& presenter,
                                                   const vector_v2::ChromeState& chrome) {
  const auto initial = presenter.set_view(ZoomLevel::k100Percent, 400, 600, chrome, now_us());
  const auto tap = presenter.pan_minimap_from(400, 600, {312.0F, 307.0F}, chrome, now_us());
  const bool tap_position = presenter.level_x() == 552 && presenter.level_y() == 710;
  const int drag_start_x = presenter.level_x();
  const int drag_start_y = presenter.level_y();
  const auto drag =
      presenter.pan_minimap_from(drag_start_x, drag_start_y, {316.0F, 311.0F}, chrome, now_us());
  const bool drag_position = presenter.level_x() == 626 && presenter.level_y() == 782;
  const auto acquire_initial =
      presenter.set_view(ZoomLevel::k400Percent, 5'520, 6'796, chrome, now_us());
  const auto acquire = presenter.pan_minimap_from(5'520, 6'796, {312.0F, 307.0F}, chrome, now_us());
  const bool acquire_position = presenter.level_x() == 2'760 && presenter.level_y() == 3'398;
  const int edge_start_x = presenter.level_x();
  const int edge_start_y = presenter.level_y();
  const auto edge =
      presenter.pan_minimap_from(edge_start_x, edge_start_y, {272.0F, 258.0F}, chrome, now_us());
  const bool edge_position = presenter.level_x() == 0 && presenter.level_y() == 0;
  const bool passed = initial.passed && tap.passed && tap_position && drag.passed &&
                      drag.frame_reused && drag_position && acquire_initial.passed &&
                      acquire.passed && acquire_position && edge.passed && edge_position;
  std::printf(
      "TINYDRAW_GATE1_MINIMAP_NAV mode=absolute tap_x=552 tap_y=710 "
      "tap_complete_us=%lld tap_pass=%u drag_x=626 drag_y=782 drag_complete_us=%lld "
      "drag_reused=%u drag_pass=%u bottom_right_to_center_x=2760 "
      "bottom_right_to_center_y=3398 acquire_complete_us=%lld acquire_pass=%u "
      "edge_x=0 edge_y=0 edge_complete_us=%lld edge_pass=%u pass=%u\n",
      static_cast<long long>(tap.complete_us), tap.passed && tap_position,
      static_cast<long long>(drag.complete_us), drag.frame_reused, drag.passed && drag_position,
      static_cast<long long>(acquire.complete_us), acquire.passed && acquire_position,
      static_cast<long long>(edge.complete_us), edge.passed && edge_position, passed);
  std::fflush(stdout);
  return passed;
}

void print_gate_presentation(const char* kind, const VectorV2Presenter& presenter,
                             const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld scroll_us=%lld "
      "exposed_compose_us=%lld chrome_us=%lld chrome_prepare_us=%lld chrome_stage_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld "
      "transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu submitted_pixels=%lu pushes=%lu tear_wait_us=%lld "
      "tear_edge_isr_to_resume_us=%lu "
      "tear_edge_observed=%u tear_edge_wait_resumed=%u tear_edge_timeout=%u "
      "tear_heal_attempted=%u "
      "tear_heal_command_sent=%u presentation_experiment=%s te_edge=%s "
      "clock_mhz=%d "
      "frame_reused=%u pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.scroll_us),
      static_cast<long long>(timing.exposed_compose_us), static_cast<long long>(timing.chrome_us),
      static_cast<long long>(timing.chrome_prepare_us),
      static_cast<long long>(timing.chrome_stage_us),
      static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.tile_pixels),
      static_cast<unsigned long>(timing.uniform_pixels),
      static_cast<unsigned long>(timing.overview_pixels),
      static_cast<unsigned long>(timing.fallback_pixels),
      static_cast<unsigned long>(timing.resident_tiles),
      static_cast<unsigned long>(timing.fallback_tiles),
      static_cast<unsigned long>(timing.submitted_pixels),
      static_cast<unsigned long>(timing.pushes), static_cast<long long>(timing.tear_wait_us),
      static_cast<unsigned long>(timing.tear_edge_isr_to_resume_us), timing.tear_edge_observed,
      timing.tear_edge_wait_resumed, timing.tear_edge_timed_out, timing.tear_heal_attempted,
      timing.tear_heal_command_sent, presentation_experiment_name(), selected_tear_edge_name(),
      kCo5300ClockMHz, timing.frame_reused, timing.passed);
}

// Gate setup follows the production authority-first path. This helper is
bool load_realistic_document(OperationLog& log, MaterializedCanvas& canvas,
                             const InPlaceAppendWorkspace& workspace,
                             std::span<VectorStroke> stroke_storage,
                             std::span<StrokeSample> sample_storage,
                             std::span<CompactOperationSample> conversion_storage) {
  VectorDocument source(stroke_storage, sample_storage);
  RealisticWorkloadStats stats{};
  const RectF area{.x0 = 0.0F,
                   .y0 = 0.0F,
                   .x1 = static_cast<float>(vector_v2::kWorldWidth),
                   .y1 = static_cast<float>(vector_v2::kWorldHeight)};
  if (!populate_realistic_handwriting(source, 7U, kRealisticStrokeCapacity, area, &stats)) {
    return false;
  }
  const std::int64_t started = esp_timer_get_time();
  for (const VectorStroke& stroke : source.strokes()) {
    const auto input = source.samples(stroke);
    if (input.empty() || input.size() > conversion_storage.size()) {
      return false;
    }
    for (std::size_t index = 0; index < input.size(); ++index) {
      conversion_storage[index] = {
          .x_quarter = static_cast<std::uint16_t>(std::lround(input[index].x * 16.0F)),
          .y_quarter = static_cast<std::uint16_t>(std::lround(input[index].y * 16.0F)),
          .radius_256 = static_cast<std::uint16_t>(std::lround(input[index].radius * 256.0F)),
          .elapsed_ms = static_cast<std::uint16_t>(index * 15U),
      };
    }
    const auto result =
        append_and_absorb(log, canvas,
                          vector_v2::OperationAppend{
                              .tool = stroke.tool == VectorTool::kEraser ? OperationTool::kEraser
                                                                         : OperationTool::kPen,
                              .color = stroke.color,
                              .samples = conversion_storage.first(input.size())},
                          workspace);
    if (!result.has_value()) {
      return false;
    }
  }
  std::printf(
      "TINYDRAW_GATE1_WORKLOAD kind=realistic seed=7 operations=%lu samples=%lu "
      "maximum_stroke=%lu load_us=%lld raw_source=1 lod_copies=0\n",
      static_cast<unsigned long>(stats.strokes), static_cast<unsigned long>(stats.samples),
      static_cast<unsigned long>(stats.maximum_stroke_samples),
      static_cast<long long>(esp_timer_get_time() - started));
  return true;
}

}  // namespace tinydraw::esp32::gate_harness

namespace tinydraw::esp32 {
using namespace gate_harness;

bool run_vector_v2_gate_harness(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                OperationLog& log, MaterializedCanvas& canvas,
                                VectorV2TouchSampler& touch, const vector_v2::ChromeState& chrome,
                                const InPlaceAppendWorkspace& workspace, VectorV2Export& exporter,
                                std::span<const std::uint16_t> blank_snapshot,
                                std::span<CompactOperationSample> conversion_storage,
                                std::span<std::uint16_t> tile_scratch,
                                std::span<std::uint16_t> overview_scratch,
                                const vector_v2::SettledTileWorkspace& settle_workspace,
                                std::span<std::uint16_t> settle_pixels) {
  const bool native_kernels = run_native_kernel_gate();
  vector_v2::ChromeState palette = chrome;
  palette.popup = vector_v2::ChromePopup::kColors;
  const std::int64_t color_started = esp_timer_get_time();
  auto color_open = presenter.present_frame_region(
      {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, palette, now_us());
  if (!color_open.passed) {
    color_open = presenter.refresh(palette, now_us());
  }
  const std::int64_t color_wall_us = esp_timer_get_time() - color_started;
  constexpr std::int64_t kColorDialogMaximumUs = 40'000;
  const bool color_dialog = color_open.passed && color_wall_us <= kColorDialogMaximumUs;
  std::printf(
      "TINYDRAW_GATE1_COLOR_DIALOG wall_us=%lld maximum_us=%lld compose_us=%lld chrome_us=%lld "
      "chrome_prepare_us=%lld chrome_stage_us=%lld complete_us=%lld pushes=%lu pass=%u\n",
      static_cast<long long>(color_wall_us), static_cast<long long>(kColorDialogMaximumUs),
      static_cast<long long>(color_open.compose_us), static_cast<long long>(color_open.chrome_us),
      static_cast<long long>(color_open.chrome_prepare_us),
      static_cast<long long>(color_open.chrome_stage_us),
      static_cast<long long>(color_open.complete_us), static_cast<unsigned long>(color_open.pushes),
      color_dialog);
  std::fflush(stdout);
  auto color_close = presenter.present_frame_region(
      {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome, now_us());
  if (!color_close.passed) {
    color_close = presenter.refresh(chrome, now_us());
  }
  const bool cooperative_compose =
      color_close.passed &&
      run_cooperative_compose_gate(presenter, producer, log, canvas, workspace, chrome);

  const bool minimap_navigation = classify_minimap_navigation(presenter, chrome);

  const bool stress_ready = append_stress_document(log, canvas, workspace);
  const bool stress_100 = stress_ready && run_tile_gate(presenter, producer, log, canvas, chrome,
                                                        ZoomLevel::k100Percent);
  const bool stress_400 =
      stress_100 && run_tile_gate(presenter, producer, log, canvas, chrome, ZoomLevel::k400Percent);
  const DocumentRevision overlap_baseline{canvas.current_revision().value + 1U};
  const bool reset_for_overlap =
      stress_400 &&
      vector_v2::restore_document_snapshot(log, canvas, overlap_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(overlap_baseline);
  const bool overlap_ready =
      reset_for_overlap && append_overlapping_scribble(log, canvas, workspace);
  const bool overlap_cold =
      overlap_ready && run_overlap_cold_gates(presenter, producer, canvas, touch, chrome);

  const DocumentRevision general_cold_baseline{canvas.current_revision().value + 1U};
  const bool reset_for_general_cold =
      overlap_ready &&
      vector_v2::restore_document_snapshot(log, canvas, general_cold_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(general_cold_baseline);
  const bool general_cold_ready =
      reset_for_general_cold && append_general_cold_document(log, canvas, workspace);
  const bool general_cold =
      general_cold_ready && run_general_cold_gates(presenter, producer, canvas, touch, chrome);

  const bool owner_document =
      general_cold_ready &&
      run_owner_document_cold_gate(presenter, producer, log, canvas, touch, chrome, workspace,
                                   blank_snapshot, conversion_storage);

  const DocumentRevision realistic_baseline{canvas.current_revision().value + 1U};
  // Continue collecting independent receipts even when the general cold timing
  // gate is red; the final verdict still requires it.
  const bool reset_for_realistic =
      owner_document &&
      vector_v2::restore_document_snapshot(log, canvas, realistic_baseline, blank_snapshot) &&
      producer.reset_uniform_baseline(realistic_baseline);

  auto realistic_strokes = allocate_external<VectorStroke>(kRealisticStrokeCapacity);
  auto realistic_samples = allocate_external<StrokeSample>(kRealisticSampleCapacity);
  const bool corpus_allocated = realistic_strokes != nullptr && realistic_samples != nullptr;
  const bool workload_ready =
      reset_for_realistic && corpus_allocated &&
      load_realistic_document(
          log, canvas, workspace, std::span(realistic_strokes.get(), kRealisticStrokeCapacity),
          std::span(realistic_samples.get(), kRealisticSampleCapacity), conversion_storage);
  // The generator slabs are startup-only. Release them before the live memory
  // and export-reserve gates so the harness measures the product allocation.
  realistic_strokes.reset();
  realistic_samples.reset();
#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
  const bool census = workload_ready && run_vector_v2_tile_census(producer, canvas, tile_scratch);
  std::printf("TINYDRAW_TILE_CENSUS_APP_DONE workload=%u census=%u revision=%lu\n", workload_ready,
              census, static_cast<unsigned long>(canvas.current_revision().value));
  std::fflush(stdout);
  return census;
#elif defined(TINYDRAW_VECTOR_V2_TEARING_PROBE)
  return workload_ready && run_tearing_probe(presenter, chrome);
#else
  constexpr int kUnalignedOrigin = vector_v2::kTileWidth - 1;
  const bool paced_cold =
      workload_ready &&
      run_paced_cold_gate(presenter, producer, canvas, touch, chrome, ZoomLevel::k400Percent,
                          kUnalignedOrigin, kUnalignedOrigin, "seed7",
                          contract::kColdViewportRequiredUs);
  const bool gate_100 =
      paced_cold && run_tile_gate(presenter, producer, log, canvas, chrome, ZoomLevel::k100Percent);
  const bool live_overlay =
      gate_100 && run_overlay_canvas_purity_gate(presenter, log, canvas, chrome, workspace) &&
      run_live_ink_overlay_gate(presenter, chrome) &&
      run_edge_ink_case(presenter, producer, log, canvas, chrome, workspace, tile_scratch);
  // Pan gates are part of the final verdict; downstream gates still key on
  // the last state-producing gate so a red pan number cannot stop later
  // receipts from reporting.
  const bool pan_100 =
      live_overlay && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k100Percent);
  const bool gate_400 = live_overlay && run_tile_gate(presenter, producer, log, canvas, chrome,
                                                      ZoomLevel::k400Percent);
  const bool pan_400 =
      gate_400 && verify_pan_adapter(presenter, producer, chrome, ZoomLevel::k400Percent);
  const bool ring_local =
      pan_400 && run_ring_locality_gate(presenter, log, canvas, chrome, workspace);
  const bool pan_sequence_100 =
      gate_400 && run_pan_sequence_gate(presenter, producer, chrome, ZoomLevel::k100Percent);
  const bool pan_sequence_400 =
      gate_400 && run_pan_sequence_gate(presenter, producer, chrome, ZoomLevel::k400Percent);
  const bool pan_sequence = pan_sequence_100 && pan_sequence_400;
  const bool pan_boundary_100 =
      gate_400 && run_pan_boundary_gate(presenter, chrome, ZoomLevel::k100Percent);
  const bool pan_boundary_400 =
      gate_400 && run_pan_boundary_gate(presenter, chrome, ZoomLevel::k400Percent);
  const bool pan_boundary = pan_boundary_100 && pan_boundary_400;
  const bool draw_fill =
      gate_400 && run_draw_while_fill_gate(presenter, producer, log, canvas, chrome, workspace,
                                           conversion_storage);
  // Cache gates run against the rich seed-7 document; the long-gesture gate
  // resets the document and therefore runs after them.
  const bool cache_retention =
      draw_fill && run_cache_retention_gate(presenter, producer, canvas, chrome);
  const bool full_world_cache = cache_retention && run_full_world_cache_gate(producer, canvas);
  const auto print_rerender_ledger = [&canvas](const char* site) {
    if (canvas.rerender_ledger() == nullptr) {
      return;
    }
    const auto ledger_totals = canvas.rerender_ledger()->totals();
    std::printf(
        "TINYDRAW_RERENDER_LEDGER site=%s renders=%lu unique=%lu amplification=%.3f "
        "cold=%lu damage=%lu evict=%lu stale=%lu unexplained=%lu\n",
        site, static_cast<unsigned long>(ledger_totals.renders),
        static_cast<unsigned long>(ledger_totals.unique_groups), ledger_totals.amplification(),
        static_cast<unsigned long>(ledger_totals.cold_miss),
        static_cast<unsigned long>(ledger_totals.expected_damage),
        static_cast<unsigned long>(ledger_totals.eviction),
        static_cast<unsigned long>(ledger_totals.stale_revision),
        static_cast<unsigned long>(ledger_totals.unexplained));
    std::fflush(stdout);
  };
  // Scope the tour receipt to the tour itself: the ledger accumulated every
  // earlier gate's renders since the last document restore.
  if (canvas.rerender_ledger() != nullptr) {
    canvas.rerender_ledger()->reset();
    std::printf("TINYDRAW_RERENDER_LEDGER_RESET site=cache_tour_start\n");
  }
  const bool cache_tour =
      full_world_cache && run_cache_tour_gate(presenter, producer, canvas, chrome);
  print_rerender_ledger("cache_tour");
  // The mixed-zoom drawing gate is part of the final verdict: warm-cache
  // interactive chunk commits must stay under the 15 ms alarm at every zoom.
  // It still must not stop later receipts when red.
  const bool mixed_draw =
      cache_tour && run_mixed_zoom_draw_gate(presenter, producer, log, canvas, chrome, workspace,
                                             conversion_storage);
  // Idle repair rides the same rich document; it discards and rebuilds its
  // own cache state, so mixed-draw's drops cannot skew it.
  const bool idle_repair =
      cache_tour &&
      run_idle_repair_gate(presenter, producer, log, canvas, chrome, workspace, conversion_storage);
  // Replays the recorded owner corpus through the production offer() path.
  // Runs after the cache gates on the deterministic post-idle-repair document
  // and before the long-gesture gate resets authority.
  const bool ink_trace_replay =
      cache_tour && run_ink_trace_replay_gate(presenter, producer, log, canvas, chrome, workspace,
                                              conversion_storage);
  // Cold timing already includes the evil hairlines. This later reset is the
  // specialized cache-capacity and repair-saturation gate for that corpus.
  const bool hairline_capacity =
      workload_ready &&
      run_hairline_gate(presenter, producer, log, canvas, touch, chrome, workspace, blank_snapshot);
  const bool long_gesture =
      cache_tour && run_long_gesture_commit_gate(presenter, producer, log, canvas, chrome,
                                                 workspace, blank_snapshot, conversion_storage);
  const bool export_encode = long_gesture && run_export_encode_gate(exporter, log);
  const bool export_reserve = export_encode && verify_export_reserve();
  // Runs last before the return so its evil-hairline history document is the
  // one left on glass for manual Undo/Redo inspection.
  const bool history_latency =
      cache_tour && run_history_latency_gate(presenter, producer, log, canvas, chrome, workspace,
                                             blank_snapshot, conversion_storage, overview_scratch);
  // Rides the evil-hairline history document: one dense window plus
  // ink-free margins at every zoom.
  const bool settle_timing =
      history_latency && run_settle_timing_gate(presenter, producer, log, canvas, chrome,
                                                settle_workspace, settle_pixels);
  const auto return_overview = presenter.set_view(ZoomLevel::k25Percent, 0, 0, chrome, now_us());
  print_rerender_ledger("final");
  std::printf(
      "TINYDRAW_GATE1_AUTOMATED_DONE native_kernels=%u minimap_navigation=%u "
      "color_dialog=%u cooperative_compose=%u stress=%u stress_100=%u stress_400=%u "
      "overlap_ready=%u "
      "overlap_cold=%u general_cold_ready=%u general_cold=%u owner_document=%u "
      "workload=%u paced_cold=%u "
      "hard_100=%u hard_400=%u pan_100=%u "
      "pan_400=%u ring_local=%u pan_seq=%u pan_boundary=%u live_overlay=%u draw_fill=%u cache=%u "
      "full_world_cache=%u "
      "cache_tour=%u mixed_draw=%u idle_repair=%u ink_trace=%u hairline_capacity=%u "
      "long_gesture=%u history_latency=%u settle_timing=%u "
      "export_encode=%u export_reserve=%u return=%u ssaa_receipt=yellow\n",
      native_kernels, minimap_navigation, color_dialog, cooperative_compose, stress_ready,
      stress_100, stress_400, overlap_ready, overlap_cold, general_cold_ready, general_cold,
      owner_document, workload_ready, paced_cold, gate_100, gate_400, pan_100, pan_400, ring_local,
      pan_sequence, pan_boundary, live_overlay, draw_fill, cache_retention, full_world_cache,
      cache_tour, mixed_draw, idle_repair, ink_trace_replay, hairline_capacity, long_gesture,
      history_latency, settle_timing, export_encode, export_reserve, return_overview.passed);
  return native_kernels && minimap_navigation && color_dialog && cooperative_compose &&
         return_overview.passed && export_reserve && overlap_cold && general_cold &&
         owner_document && mixed_draw && idle_repair && hairline_capacity && history_latency &&
         settle_timing && pan_100 && pan_400 && ring_local && pan_sequence && pan_boundary;
#endif
}

}  // namespace tinydraw::esp32
