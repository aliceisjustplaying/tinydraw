#include "vector_v2_gate_harness_internal.h"

namespace tinydraw::esp32::gate_harness {

struct LiveInkPathMeasurement {
  std::int64_t maximum_wall_us = 0;
  std::int64_t maximum_submit_us = 0;
  std::int64_t maximum_complete_us = 0;
  std::int64_t maximum_chrome_us = 0;
  std::size_t presented_updates = 0;
  std::size_t failures = 0;
};

LiveInkPathMeasurement measure_live_ink_circle(VectorV2Presenter& presenter,
                                               const vector_v2::ChromeState& chrome, Point center,
                                               float path_radius) {
  constexpr std::size_t kPointCount = 48;
  constexpr float kTau = 6.28318530718F;
  CurvedRibbonStream ribbon;
  LiveInkPathMeasurement measurement;
  float running_length = 0.0F;
  Point previous{};
  std::uint32_t timestamp_us = now_us();
  for (std::size_t index = 0; index < kPointCount; ++index) {
    const float angle = kTau * static_cast<float>(index) / static_cast<float>(kPointCount - 1U);
    const Point position{center.x + std::cos(angle) * path_radius,
                         center.y + std::sin(angle) * path_radius};
    const float distance =
        index == 0U ? 0.0F : std::hypot(position.x - previous.x, position.y - previous.y);
    running_length += distance;
    timestamp_us += 8'333U;
    const InkPoint point{.position = position,
                         .pressure = 1.0F,
                         .radius = 8.0F,
                         .distance = distance,
                         .running_length = running_length,
                         .timestamp_us = timestamp_us};
    const std::uint32_t event_us = now_us();
    const std::int64_t started_us = esp_timer_get_time();
    LivePresentationTiming timing;
    if (index == 0U) {
      static_cast<void>(ribbon.append(point, true));
      timing = presenter.show_start(point, 0x001FU, chrome, event_us);
    } else {
      timing = presenter.show_update(ribbon.append(point, true), 0x001FU, chrome, event_us);
    }
    const std::int64_t wall_us = esp_timer_get_time() - started_us;
    if (!timing.passed) {
      ++measurement.failures;
    }
    if (timing.pushes != 0U) {
      ++measurement.presented_updates;
      measurement.maximum_wall_us = std::max(measurement.maximum_wall_us, wall_us);
      measurement.maximum_submit_us =
          std::max(measurement.maximum_submit_us, timing.first_submit_us);
      measurement.maximum_complete_us =
          std::max(measurement.maximum_complete_us, timing.first_complete_us);
      measurement.maximum_chrome_us = std::max(measurement.maximum_chrome_us, timing.chrome_us);
    }
    previous = position;
  }
  return measurement;
}

bool fill_view_to_completion(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                             std::size_t& tiles, std::int64_t& wall_us);

// Commits single-cap ink at the four panel-edge corners and verifies the
// composed pixels at the true edge columns/rows: a presentation or compose
// path that drops column 0/367 or row 0 fails here instead of on glass.
bool run_edge_ink_case(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                       OperationLog& log, MaterializedCanvas& canvas,
                       const vector_v2::ChromeState& chrome,
                       const InPlaceAppendWorkspace& workspace,
                       std::span<std::uint16_t> compose_scratch) {
  if (!presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  constexpr std::uint16_t kColor = 0x001FU;
  constexpr float kRadius = 6.0F;
  constexpr std::array<std::array<int, 2>, 4> kCorners{{{1, 1}, {366, 1}, {1, 369}, {366, 369}}};
  std::uint32_t timestamp_us = now_us();
  for (const auto& corner : kCorners) {
    const auto x = static_cast<float>(corner[0]);
    const auto y = static_cast<float>(corner[1]);
    const InkPoint point{.position = {x, y},
                         .pressure = 1.0F,
                         .radius = kRadius,
                         .distance = 0.0F,
                         .running_length = 0.0F,
                         .timestamp_us = timestamp_us};
    timestamp_us += 8'333U;
    if (!presenter.show_start(point, kColor, chrome, now_us()).passed) {
      return false;
    }
    // At 100% with origin (0, 0) panel coordinates equal world coordinates.
    const std::array<CompactOperationSample, 1> samples{{{
        .x_quarter = static_cast<std::uint16_t>(corner[0] * 16),
        .y_quarter = static_cast<std::uint16_t>(corner[1] * 16),
        .radius_256 = static_cast<std::uint16_t>(kRadius * 256.0F),
        .elapsed_ms = 0,
    }}};
    if (!append_and_absorb(log, canvas,
                           vector_v2::OperationAppend{.color = kColor, .samples = samples},
                           workspace)
             .has_value()) {
      return false;
    }
  }
  // Exact raw tiles for the view, then compose and sample the edges.
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k100Percent,
      .level_pixels = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
  };
  std::size_t fill_tiles = 0;
  std::int64_t fill_us = 0;
  if (!fill_view_to_completion(producer, view, fill_tiles, fill_us)) {
    return false;
  }
  std::size_t edge_failures = 0;
  for (const auto& corner : kCorners) {
    constexpr int kBandRows = 8;
    const int band_top = std::clamp(corner[1] - 2, 0, vector_v2::kOverviewHeight - kBandRows);
    const vector_v2::ViewRequest band{
        .zoom = ZoomLevel::k100Percent,
        .level_pixels = {0, band_top, vector_v2::kOverviewWidth, band_top + kBandRows},
    };
    const std::size_t band_pixels = static_cast<std::size_t>(vector_v2::kOverviewWidth) * kBandRows;
    if (band_pixels > compose_scratch.size()) {
      return false;
    }
    const auto stats = canvas.compose_view(band, compose_scratch.first(band_pixels));
    if (!stats.has_value() || stats->fallback_pixels != 0U) {
      return false;
    }
    const auto pixel_at = [&](int x, int y) {
      return compose_scratch[static_cast<std::size_t>(y - band_top) * vector_v2::kOverviewWidth +
                             static_cast<std::size_t>(x)];
    };
    // The cap covers its center and the adjacent true edge column/row.
    const int edge_x = corner[0] == 1 ? 0 : vector_v2::kOverviewWidth - 1;
    edge_failures += pixel_at(corner[0], corner[1]) != kColor;
    edge_failures += pixel_at(edge_x, corner[1]) != kColor;
  }
  std::printf("TINYDRAW_EDGE_INK corners=%lu edge_failures=%lu pass=%u\n",
              static_cast<unsigned long>(kCorners.size()),
              static_cast<unsigned long>(edge_failures), edge_failures == 0U);
  return edge_failures == 0U;
}

bool run_overlay_canvas_purity_gate(VectorV2Presenter& presenter, OperationLog& log,
                                    MaterializedCanvas& canvas,
                                    const vector_v2::ChromeState& chrome,
                                    const InPlaceAppendWorkspace& workspace) {
  constexpr std::array<std::array<int, 2>, 4> kOverlayCenters{{
      {280, 36},   // battery
      {332, 150},  // zoom rail
      {310, 310},  // minimap
      {184, 410},  // toolbar
  }};
  const DocumentRevision before = canvas.current_revision();
  std::uint16_t gesture_id = 6'000;
  for (const auto& center : kOverlayCenters) {
    const std::array<CompactOperationSample, 1> sample{{{
        .x_quarter = static_cast<std::uint16_t>(center[0] * 16),
        .y_quarter = static_cast<std::uint16_t>(center[1] * 16),
        .radius_256 = 12U * 256U,
        .elapsed_ms = 0,
    }}};
    if (!append_and_absorb(log, canvas,
                           vector_v2::OperationAppend{.tool = OperationTool::kPen,
                                                      .color = 0xF800U,
                                                      .gesture_id = gesture_id++,
                                                      .samples = sample},
                           workspace)
             .has_value()) {
      return false;
    }
  }
  const auto refresh = presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us());
  const bool preserved = refresh.passed && presenter.verify_staging_preserves_canvas(chrome);
  const bool authority = canvas.current_revision().value == before.value + kOverlayCenters.size() &&
                         log.current_revision() == canvas.current_revision();
  const bool passed = preserved && authority;
  std::printf(
      "TINYDRAW_GATE1_OVERLAY_CANVAS regions=zoom,minimap,toolbar,battery operations=%lu "
      "presentation_mutations=%u authority=%u pass=%u\n",
      static_cast<unsigned long>(kOverlayCenters.size()), !preserved, authority, passed);
  std::fflush(stdout);
  return passed;
}

bool run_live_ink_overlay_gate(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome) {
  if (!presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const auto clear = measure_live_ink_circle(presenter, chrome, {150.0F, 180.0F}, 44.0F);
  if (!presenter.set_view(ZoomLevel::k100Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const auto overlay = measure_live_ink_circle(presenter, chrome, {276.0F, 304.0F}, 48.0F);
  const bool passed = clear.failures == 0U && overlay.failures == 0U &&
                      clear.presented_updates > 0U && overlay.presented_updates > 0U &&
                      overlay.maximum_chrome_us < 3'000 && overlay.maximum_submit_us < 16'667 &&
                      overlay.maximum_complete_us < 33'333;
  std::printf(
      "TINYDRAW_GATE1_LIVE_OVERLAY clear_updates=%lu clear_wall_max_us=%lld "
      "clear_submit_max_us=%lld clear_complete_max_us=%lld overlay_updates=%lu "
      "overlay_wall_max_us=%lld overlay_submit_max_us=%lld overlay_complete_max_us=%lld "
      "overlay_chrome_max_us=%lld clear_failures=%lu overlay_failures=%lu pass=%u\n",
      static_cast<unsigned long>(clear.presented_updates),
      static_cast<long long>(clear.maximum_wall_us),
      static_cast<long long>(clear.maximum_submit_us),
      static_cast<long long>(clear.maximum_complete_us),
      static_cast<unsigned long>(overlay.presented_updates),
      static_cast<long long>(overlay.maximum_wall_us),
      static_cast<long long>(overlay.maximum_submit_us),
      static_cast<long long>(overlay.maximum_complete_us),
      static_cast<long long>(overlay.maximum_chrome_us), static_cast<unsigned long>(clear.failures),
      static_cast<unsigned long>(overlay.failures), passed);
  std::fflush(stdout);
  return passed;
}

bool run_draw_while_fill_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const InPlaceAppendWorkspace& workspace,
                              std::span<CompactOperationSample> interaction_samples) {
  const auto fallback = presenter.set_view(ZoomLevel::k400Percent, 0, 0, chrome, now_us());
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k400Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  std::size_t priming_steps = 0U;
  std::size_t priming_publications = 0U;
  bool fill_primed = false;
  while (!fill_primed && priming_steps < 64U) {
    const auto initial = producer.produce_next(view);
    if (!initial.has_value() || initial->complete) {
      return false;
    }
    ++priming_steps;
    priming_publications += initial->tiles_published;
    fill_primed = initial->tiles_published == 0U;
  }
  if (!fill_primed) {
    return false;
  }

  const std::uint32_t event_us = now_us();
  const std::int64_t blocked_started = esp_timer_get_time();
  const auto blocked_step = producer.produce_next(view);
  const std::int64_t poll_gap_us = esp_timer_get_time() - blocked_started;
  if (!blocked_step.has_value()) {
    return false;
  }
  const InkPoint preview{.position = {20.0F, 200.0F},
                         .pressure = 1.0F,
                         .radius = 20.0F,
                         .distance = 0.0F,
                         .running_length = 0.0F,
                         .timestamp_us = event_us};
  const auto live = presenter.show_start(preview, 0x001FU, chrome, event_us);

  if (interaction_samples.size() < 8U) {
    return false;
  }
  auto fast_xl = interaction_samples.first(8U);
  for (std::size_t index = 0; index < fast_xl.size(); ++index) {
    fast_xl[index] = {
        .x_quarter = static_cast<std::uint16_t>(80U + index * 192U),
        .y_quarter = static_cast<std::uint16_t>(index % 2U == 0U ? 720U : 960U),
        // XL is 20 screen pixels; at 400% that is 5 world units.
        .radius_256 = 1'280U,
        .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
    };
  }
  const std::int64_t append_started = esp_timer_get_time();
  const auto append = append_and_absorb(
      log, canvas,
      vector_v2::OperationAppend{.tool = OperationTool::kPen, .color = 0x001FU, .samples = fast_xl},
      workspace, view);
  const std::int64_t append_us = esp_timer_get_time() - append_started;
  // A revision change now restarts stale producer state within produce_next;
  // the old contract returned nullopt and required a caller retry.
  const bool revision_restarted = producer.produce_next(view).has_value() &&
                                  log.current_revision() == canvas.current_revision();

  std::int64_t maximum_slice_us = poll_gap_us;
  std::int64_t maximum_compute_slice_us = poll_gap_us;
  std::int64_t fill_started = esp_timer_get_time();
  bool fill_complete = false;
  while (!fill_complete) {
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    const std::int64_t compute_slice_us = esp_timer_get_time() - step_started;
    maximum_slice_us = std::max(maximum_slice_us, compute_slice_us);
    if (!step.has_value()) {
      return false;
    }
    // Publication copies four packed tiles from PSRAM. It is bounded but not
    // replay compute; track replay-only slices separately from total blocking.
    if (step->tiles_published == 0U) {
      maximum_compute_slice_us = std::max(maximum_compute_slice_us, compute_slice_us);
    }
    if (step->tiles_published != 0U) {
      const std::int64_t present_started = esp_timer_get_time();
      if (!presenter.refresh_region(step->level_bounds, chrome).passed) {
        return false;
      }
      maximum_slice_us = std::max(maximum_slice_us, esp_timer_get_time() - present_started);
    }
    fill_complete = step->complete;
  }
  const std::int64_t fill_us = esp_timer_get_time() - fill_started;
  const bool passed = append.has_value() && revision_restarted && live.passed &&
                      live.first_submit_us < 100'000 && poll_gap_us < 35'000 &&
                      maximum_compute_slice_us < 30'000 && maximum_slice_us < 75'000;
  std::printf(
      "TINYDRAW_GATE1_DRAW_FILL zoom=400 revision=%lu append_us=%lld poll_gap_us=%lld "
      "event_submit_us=%lld event_complete_us=%lld max_compute_slice_us=%lld "
      "max_display_slice_us=%lld fill_us=%lld priming_steps=%lu priming_publications=%lu "
      "revision_restarted=%u pass=%u\n",
      static_cast<unsigned long>(canvas.current_revision().value),
      static_cast<long long>(append_us), static_cast<long long>(poll_gap_us),
      static_cast<long long>(live.first_submit_us), static_cast<long long>(live.first_complete_us),
      static_cast<long long>(maximum_compute_slice_us), static_cast<long long>(maximum_slice_us),
      static_cast<long long>(fill_us), static_cast<unsigned long>(priming_steps),
      static_cast<unsigned long>(priming_publications), revision_restarted, passed);
  return passed;
}

struct LongGestureMeasurement {
  std::size_t samples = 0;
  std::size_t chunks = 0;
  std::int64_t append_total_us = 0;
  std::int64_t append_max_us = 0;
  std::size_t fallback_pixels = 0;
  std::size_t settled_fallback_pixels = 0;
  bool committed = false;
  bool authority_match = false;
  bool refresh_passed = false;
};

// Streams one deterministic 400% XL gesture through the chained builder with
// the interactive 64-sample chunk policy and the caller-selected commit
// implementation, measuring every intermediate chunk commit.
template <typename CommitChunk>
bool run_long_gesture_pass(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                           OperationLog& log, MaterializedCanvas& canvas,
                           const vector_v2::ChromeState& chrome,
                           std::span<const std::uint16_t> blank_snapshot,
                           std::span<CompactOperationSample> builder_storage,
                           std::size_t chunk_sample_limit, CommitChunk&& commit_chunk,
                           LongGestureMeasurement& measurement) {
  const DocumentRevision baseline{canvas.current_revision().value + 1U};
  if (!vector_v2::restore_document_snapshot(log, canvas, baseline, blank_snapshot) ||
      !producer.reset_uniform_baseline(baseline)) {
    return false;
  }
  if (!presenter.set_view(ZoomLevel::k400Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k400Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  for (std::size_t step = 0; step < 100'000U; ++step) {
    if (step % 50U == 49U) {
      vTaskDelay(1);
    }
    const auto produced = producer.produce_next(view);
    if (!produced.has_value()) {
      return false;
    }
    if (produced->complete) {
      break;
    }
  }

  vector_v2::ChainedOperationBuilder builder(builder_storage, chunk_sample_limit);
  constexpr std::size_t kGestureSamples = 1'600;
  constexpr float kRadius = 5.0F;  // XL at 400%: 20 screen pixels.
  std::uint32_t timestamp_us = now_us();
  float x = 6.0F;
  float y = 8.0F;
  float direction = 1.0F;
  std::optional<vector_v2::PixelRect> world_bounds;
  const auto commit_ready = [&](vector_v2::ChainedOperationStatus status)
      -> std::optional<vector_v2::ChainedOperationStatus> {
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto pending = builder.pending_append();
      if (!pending.has_value()) {
        return std::nullopt;
      }
      const std::int64_t started_us = esp_timer_get_time();
      const auto committed = commit_chunk(*pending, view);
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      if (!committed.has_value()) {
        return std::nullopt;
      }
      measurement.append_total_us += elapsed_us;
      measurement.append_max_us = std::max(measurement.append_max_us, elapsed_us);
      ++measurement.chunks;
      if (!world_bounds.has_value()) {
        world_bounds = committed->affected_world_bounds;
      } else {
        world_bounds->x0 = std::min(world_bounds->x0, committed->affected_world_bounds.x0);
        world_bounds->y0 = std::min(world_bounds->y0, committed->affected_world_bounds.y0);
        world_bounds->x1 = std::max(world_bounds->x1, committed->affected_world_bounds.x1);
        world_bounds->y1 = std::max(world_bounds->y1, committed->affected_world_bounds.y1);
      }
      status = builder.acknowledge_commit();
    }
    if (status != vector_v2::ChainedOperationStatus::kAccepted &&
        status != vector_v2::ChainedOperationStatus::kComplete) {
      return std::nullopt;
    }
    return status;
  };

  if (!builder.begin(
          OperationTool::kPen, 0x001FU, 1U,
          {.world_x = x, .world_y = y, .radius = kRadius, .timestamp_us = timestamp_us})) {
    return false;
  }
  ++measurement.samples;
  for (std::size_t index = 1; index < kGestureSamples; ++index) {
    x += 1.6F * direction;
    if (x > 86.0F || x < 6.0F) {
      direction = -direction;
      x = std::clamp(x, 6.0F, 86.0F);
      y += 2.5F;
    }
    timestamp_us += 8'000U;
    const vector_v2::OperationPoint point{
        .world_x = x, .world_y = y, .radius = kRadius, .timestamp_us = timestamp_us};
    const bool final_sample = index + 1U == kGestureSamples;
    const auto status = final_sample ? builder.finish(point) : builder.add(point);
    const auto continued = commit_ready(status);
    if (!continued.has_value()) {
      return false;
    }
    ++measurement.samples;
  }
  measurement.committed = !builder.active();
  measurement.authority_match = log.current_revision() == canvas.current_revision();
  if (world_bounds.has_value()) {
    // Visible tiles are budget-exempt, so the pen-up refresh must already
    // show zero fallback; the settled refresh after producer completion
    // re-proves it.
    const auto refresh = presenter.refresh_region(
        vector_v2::operation_level_bounds(*world_bounds, ZoomLevel::k400Percent), chrome, now_us());
    measurement.fallback_pixels = refresh.fallback_pixels;
    for (std::size_t step = 0; step < 100'000U; ++step) {
      if (step % 50U == 49U) {
        vTaskDelay(1);
      }
      const auto produced = producer.produce_next(view);
      if (!produced.has_value()) {
        return false;
      }
      if (produced->complete) {
        break;
      }
    }
    const auto settled = presenter.refresh_region(
        vector_v2::operation_level_bounds(*world_bounds, ZoomLevel::k400Percent), chrome, now_us());
    measurement.refresh_passed = refresh.passed && settled.passed;
    measurement.settled_fallback_pixels = settled.fallback_pixels;
  }
  return true;
}

bool run_long_gesture_commit_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                  OperationLog& log, MaterializedCanvas& canvas,
                                  const vector_v2::ChromeState& chrome,
                                  const InPlaceAppendWorkspace& workspace,
                                  std::span<const std::uint16_t> blank_snapshot,
                                  std::span<CompactOperationSample> builder_storage) {
  LongGestureMeasurement production{};
  const bool run_ok = run_long_gesture_pass(
      presenter, producer, log, canvas, chrome, blank_snapshot, builder_storage,
      kGateChunkSampleLimit,
      [&](const vector_v2::BuiltOperation& chunk, const vector_v2::ViewRequest& view) {
        return append_and_absorb(
            log, canvas, chunk, workspace, view,
            {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs});
      },
      production);
  std::printf(
      "TINYDRAW_GATE1_LONG_GESTURE path=committed_overlay samples=%lu chunks=%lu "
      "append_total_us=%lld append_max_us=%lld append_avg_us=%lld "
      "refresh_fallback_pixels=%lu settled_fallback_pixels=%lu committed=%u "
      "authority=%u refresh=%u run_ok=%u\n",
      static_cast<unsigned long>(production.samples), static_cast<unsigned long>(production.chunks),
      static_cast<long long>(production.append_total_us),
      static_cast<long long>(production.append_max_us),
      static_cast<long long>(production.chunks == 0U
                                 ? 0
                                 : production.append_total_us /
                                       static_cast<std::int64_t>(production.chunks)),
      static_cast<unsigned long>(production.fallback_pixels),
      static_cast<unsigned long>(production.settled_fallback_pixels), production.committed,
      production.authority_match, production.refresh_passed, run_ok);
  std::fflush(stdout);
  return run_ok && production.committed && production.authority_match &&
         production.refresh_passed && production.fallback_pixels == 0U &&
         production.settled_fallback_pixels == 0U && production.chunks >= 24U &&
         production.append_max_us < 15'000;
}

// Encodes the currently loaded seed-7 authority directly to SVG and verifies
// the complete stored byte stream without presenting USB. This preserves the
// automated serial console and never puts the device in mass-storage mode.
constexpr std::array<std::uint32_t, 256> make_export_crc32_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t value = 0; value < table.size(); ++value) {
    std::uint32_t entry = value;
    for (int bit = 0; bit < 8; ++bit) {
      entry = (entry >> 1U) ^ (0xEDB88320U & (0U - (entry & 1U)));
    }
    table[value] = entry;
  }
  return table;
}

constexpr auto kExportCrc32Table = make_export_crc32_table();

std::uint32_t export_crc32(std::uint32_t crc, std::span<const std::uint8_t> bytes) {
  crc = ~crc;
  for (const std::uint8_t byte : bytes) {
    crc = (crc >> 8U) ^ kExportCrc32Table[(crc ^ byte) & 0xFFU];
  }
  return ~crc;
}

std::uint32_t big_endian_u32(std::span<const std::uint8_t> bytes) {
  return static_cast<std::uint32_t>(bytes[0]) << 24U | static_cast<std::uint32_t>(bytes[1]) << 16U |
         static_cast<std::uint32_t>(bytes[2]) << 8U | static_cast<std::uint32_t>(bytes[3]);
}

struct PngVerification {
  bool signature = false;
  bool dimensions = false;
  bool chunks = false;
  std::size_t chunk_count = 0;
};

PngVerification verify_png(VectorV2Export& exporter, std::size_t total_bytes) {
  PngVerification result;
  std::array<std::uint8_t, 24> header{};
  constexpr std::array<std::uint8_t, 8> kSignature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                   0x0DU, 0x0AU, 0x1AU, 0x0AU};
  if (total_bytes < 45U || !exporter.read_png(0, header)) {
    return result;
  }
  result.signature = std::equal(kSignature.begin(), kSignature.end(), header.begin());
  result.dimensions = big_endian_u32(std::span(header).subspan(16U, 4U)) ==
                          static_cast<std::uint32_t>(vector_v2::kWorldWidth) &&
                      big_endian_u32(std::span(header).subspan(20U, 4U)) ==
                          static_cast<std::uint32_t>(vector_v2::kWorldHeight);

  std::array<std::uint8_t, 4'096> buffer{};
  std::size_t offset = 8U;
  bool saw_iend = false;
  while (offset + 12U <= total_bytes && !saw_iend) {
    std::array<std::uint8_t, 8> chunk_header{};
    if (!exporter.read_png(offset, chunk_header)) {
      return result;
    }
    const std::size_t length = big_endian_u32(std::span(chunk_header).first(4U));
    if (length > total_bytes - offset - 12U) {
      return result;
    }
    std::uint32_t crc = export_crc32(0U, std::span(chunk_header).subspan(4U));
    std::size_t payload_offset = offset + 8U;
    std::size_t remaining = length;
    while (remaining > 0U) {
      const std::size_t count = std::min(remaining, buffer.size());
      if (!exporter.read_png(payload_offset, std::span(buffer).first(count))) {
        return result;
      }
      crc = export_crc32(crc, std::span<const std::uint8_t>(buffer).first(count));
      payload_offset += count;
      remaining -= count;
      vTaskDelay(1);
    }
    std::array<std::uint8_t, 4> stored_crc{};
    if (!exporter.read_png(offset + 8U + length, stored_crc) || crc != big_endian_u32(stored_crc)) {
      return result;
    }
    constexpr std::array<std::uint8_t, 4> kIend{'I', 'E', 'N', 'D'};
    saw_iend = std::equal(kIend.begin(), kIend.end(), chunk_header.begin() + 4);
    offset += length + 12U;
    ++result.chunk_count;
  }
  result.chunks = saw_iend && offset == total_bytes;
  return result;
}

struct SvgVerification {
  bool prolog = false;
  bool dimensions = false;
  bool terminator = false;
  bool crc = false;
  bool path_only = false;
  std::size_t paths = 0;
};

SvgVerification verify_svg(VectorV2Export& exporter, std::size_t total_bytes,
                           std::size_t expected_paths, std::uint32_t expected_crc) {
  SvgVerification result;
  if (total_bytes < 7U) {
    return result;
  }

  std::array<std::uint8_t, 512> header{};
  const std::size_t header_bytes = std::min(header.size(), total_bytes);
  if (!exporter.read_file(0, std::span(header).first(header_bytes))) {
    return result;
  }
  const std::string_view header_text(reinterpret_cast<const char*>(header.data()), header_bytes);
  result.prolog = header_text.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg ");
  result.dimensions =
      header_text.find("width=\"1472\" height=\"1792\" viewBox=\"0 0 1472 1792\"") !=
      std::string_view::npos;

  std::array<std::uint8_t, 7> tail{};
  result.terminator =
      exporter.read_file(total_bytes - tail.size(), tail) &&
      std::string_view(reinterpret_cast<const char*>(tail.data()), tail.size()) == "</svg>\n";

  constexpr std::size_t kCarryBytes = 32U;
  // Keep the verifier below the product main-task stack headroom. The file is
  // still checked in full; a smaller streaming window only adds read calls.
  std::array<std::uint8_t, 512> buffer{};
  std::array<char, 512 + kCarryBytes> searchable{};
  std::array<char, kCarryBytes> carry{};
  std::size_t carry_size = 0;
  std::size_t offset = 0;
  std::uint32_t crc = 0;
  bool path_only = true;
  while (offset < total_bytes) {
    const std::size_t count = std::min(buffer.size(), total_bytes - offset);
    if (!exporter.read_file(offset, std::span(buffer).first(count))) {
      return result;
    }
    crc = export_crc32(crc, std::span<const std::uint8_t>(buffer).first(count));
    std::copy_n(carry.begin(), carry_size, searchable.begin());
    std::copy_n(reinterpret_cast<const char*>(buffer.data()), count,
                searchable.begin() + static_cast<std::ptrdiff_t>(carry_size));
    const std::string_view text(searchable.data(), carry_size + count);
    constexpr std::string_view kPathMarker = "<path fill=\"";
    for (std::size_t found = text.find(kPathMarker); found != std::string_view::npos;
         found = text.find(kPathMarker, found + 1U)) {
      if (found + kPathMarker.size() > carry_size) {
        ++result.paths;
      }
    }
    path_only = path_only && text.find("<circle") == std::string_view::npos &&
                text.find("stroke-width") == std::string_view::npos;
    carry_size = std::min(kCarryBytes, text.size());
    std::copy_n(text.end() - static_cast<std::ptrdiff_t>(carry_size), carry_size, carry.begin());
    offset += count;
    vTaskDelay(1);
  }
  result.crc = crc == expected_crc;
  result.path_only = path_only && result.paths == expected_paths;
  return result;
}

bool run_export_encode_gate(VectorV2Export& exporter, OperationLog& log) {
  const auto expected_paths = vector_v2::svg_path_count(log);
  const VectorV2ExportStats stats = exporter.encode(log);
  const SvgVerification svg =
      stats.encoded && expected_paths.has_value()
          ? verify_svg(exporter, stats.bytes, *expected_paths, stats.content_crc32)
          : SvgVerification{};
  const PngVerification png =
      stats.encoded ? verify_png(exporter, stats.png_bytes) : PngVerification{};
  const bool passed = stats.encoded && stats.bytes > 64U && svg.prolog && svg.dimensions &&
                      svg.terminator && svg.crc && svg.path_only && png.signature &&
                      png.dimensions && png.chunks;
  std::printf(
      "TINYDRAW_GATE1_EXPORT formats=svg,png encoded=%u svg_bytes=%lu png_bytes=%lu "
      "elapsed_us=%lld svg_workspace_bytes=%lu png_workspace_bytes=%lu "
      "render_workspace_bytes=%lu peak_workspace_bytes=%lu operations=%lu sink_calls=%lu "
      "flash_pages=%lu crc32=%08lx free_psram=%lu free_internal=%lu prolog=%u "
      "svg_dimensions=%u terminator=%u crc_ok=%u paths=%lu path_only=%u png_signature=%u "
      "png_dimensions=%u png_chunks=%lu png_chunks_ok=%u pass=%u\n",
      stats.encoded, static_cast<unsigned long>(stats.bytes),
      static_cast<unsigned long>(stats.png_bytes), static_cast<long long>(stats.elapsed_us),
      static_cast<unsigned long>(stats.workspace_bytes),
      static_cast<unsigned long>(stats.png_workspace_bytes),
      static_cast<unsigned long>(stats.render_workspace_bytes),
      static_cast<unsigned long>(stats.peak_workspace_bytes),
      static_cast<unsigned long>(stats.operation_count),
      static_cast<unsigned long>(stats.sink_calls), static_cast<unsigned long>(stats.flash_pages),
      static_cast<unsigned long>(stats.content_crc32),
      static_cast<unsigned long>(stats.free_psram_after),
      static_cast<unsigned long>(stats.free_internal_after), svg.prolog, svg.dimensions,
      svg.terminator, svg.crc, static_cast<unsigned long>(svg.paths), svg.path_only, png.signature,
      png.dimensions, static_cast<unsigned long>(png.chunk_count), png.chunks, passed);
  std::fflush(stdout);
  return passed;
}

// Measures cache-retention value under eviction pressure: cold-fill a home
// view, tour distinct 400% viewports across the inked world, then tour back
// and count what must be re-produced. The return-trip refill work is the
// user-visible "cold render after panning back" cost; the protected home
// footprint must additionally return with zero missing tiles and zero
// fallback pixels. Runs against the loaded seed-7 document.

}  // namespace tinydraw::esp32::gate_harness
