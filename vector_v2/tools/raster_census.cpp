// Host measurement + equality rig; fuzz implementations live in raster_census_fuzz.cpp.
// Counters require a library built with TINYDRAW_VECTOR_V2_RASTER_CENSUS.

#include "tinydraw/vector_v2/raster_census.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "raster_census_fuzz.h"
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/vector_v2/adversarial_tapered_corpus.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace v2 = tinydraw::vector_v2;

namespace {

struct Rig {
  std::vector<v2::OperationRecord> records;
  std::vector<v2::CompactOperationSample> samples;
  std::vector<std::uint16_t> overview;
  std::vector<std::uint16_t> snapshot;
  std::unique_ptr<std::array<v2::MaterializedUniformStorage, v2::kMaterializedTileIdentityCount>>
      uniforms;
  std::vector<std::uint8_t> occupancy;
  std::vector<v2::MaterializedSlotStorage> slots;
  std::vector<std::uint16_t> tile_pool;
  std::vector<std::uint16_t> raw_slot_directory;
  std::vector<std::uint16_t> supertask;
  std::vector<std::uint8_t> mask;
  std::vector<std::uint16_t> summary_rows;
  std::vector<std::uint32_t> summary_words;
  std::vector<std::uint32_t> chord_plans;
  std::vector<std::uint64_t> spatial_cells;
  std::vector<std::uint64_t> spatial_large;
  std::vector<std::uint16_t> candidates;
  // Authority absorption workspace (mirrors the app).
  std::vector<std::uint16_t> overview_scratch;
  std::vector<std::uint8_t> append_mask;
  std::vector<v2::TileKey> affected_keys;

  v2::OperationSpatialIndex spatial_index;
  v2::OperationLog log;
  v2::MaterializedCanvas canvas;
  v2::TileProducer producer;

  Rig(std::size_t record_capacity, std::size_t sample_capacity)
      : records(record_capacity),
        samples(sample_capacity),
        overview(v2::kOverviewPixels, 0xFFFFU),
        snapshot(v2::kOverviewPixels, 0xFFFFU),
        uniforms(std::make_unique<
                 std::array<v2::MaterializedUniformStorage, v2::kMaterializedTileIdentityCount>>()),
        occupancy(v2::kOccupancyBytes, 0U),
        slots(v2::kTileSlotCount),
        tile_pool(v2::kTileSlotCount * v2::kTilePixels),
        raw_slot_directory(v2::kMaterializedTileIdentityCount),
        supertask(v2::kTileProducerPixels),
        mask(v2::kTileProducerMaskBytes),
        summary_rows(v2::kTileProducerSummaryRows),
        summary_words(v2::kTileProducerSummaryWords),
        chord_plans(v2::kOperationChordStorageBytes / 4U),
        spatial_cells(v2::operation_spatial_cell_word_count(record_capacity)),
        spatial_large(v2::operation_spatial_word_count(record_capacity)),
        candidates(record_capacity),
        overview_scratch(v2::kOverviewPixels),
        append_mask(v2::kInPlaceTileMaskBytes),
        affected_keys(v2::kTileSlotCount + v2::kMaximumVisibleTiles),
        spatial_index(record_capacity, spatial_cells, spatial_large),
        log(records, samples, &spatial_index),
        canvas({.overview_pixels = overview,
                .uniform_catalog = *uniforms,
                .occupancy_bits = occupancy,
                .slots = slots,
                .tile_pixels = tile_pool,
                .initial_revision = {},
                .raw_slot_directory = raw_slot_directory}),
        producer(log, canvas,
                 {.supertask_pixels = supertask,
                  .finalized_pixels = mask,
                  .summary_row_unset = summary_rows,
                  .summary_saturated_words = summary_words,
                  .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans)),
                  .candidate_indices = candidates},
                 {}, 0xFFFFU) {}

  [[nodiscard]] v2::InPlaceAppendWorkspace workspace() {
    return {
        .overview_scratch = overview_scratch,
        .affected_keys = affected_keys,
        .tile_mask = append_mask,
    };
  }

  [[nodiscard]] bool append(const v2::OperationAppend& operation) {
    return v2::pending_operation_count(log, canvas) == 0U &&
           v2::append_authority_only(log, operation).has_value() &&
           v2::absorb_pending_operation(log, canvas, workspace()).has_value();
  }

  [[nodiscard]] bool reset_blank(v2::DocumentRevision revision) {
    return v2::restore_document_snapshot(log, canvas, revision, snapshot) &&
           producer.reset_uniform_baseline(revision);
  }
};

struct CandidateDiscoveryCounters {
  std::size_t operations_in_authority = 0;
  std::size_t index_candidates = 0;
  std::size_t deduplicated_candidates = 0;
  std::size_t operations_scanned = 0;
  std::size_t operations_intersecting = 0;
  std::size_t groups_published = 0;
};

struct SweepResult {
  std::size_t steps = 0;
  std::size_t tiles = 0;
  double wall_ms = 0.0;
  CandidateDiscoveryCounters candidates{};
  bool ok = false;
};

SweepResult run_cold_fill(Rig& rig, const v2::ViewRequest& view) {
  SweepResult result{};
  const auto started = std::chrono::steady_clock::now();
  while (true) {
    const auto step = rig.producer.produce_next(view);
    if (!step.has_value()) {
      std::fprintf(stderr, "produce_next failed at step %zu\n", result.steps);
      return result;
    }
    ++result.steps;
    result.tiles += step->tiles_published;
    result.candidates.operations_in_authority += step->operations_in_authority;
    result.candidates.index_candidates += step->index_candidates;
    result.candidates.deduplicated_candidates += step->deduplicated_candidates;
    result.candidates.operations_scanned += step->operations_scanned;
    result.candidates.operations_intersecting += step->operations_intersecting;
    result.candidates.groups_published += step->groups_published;
    if (step->complete) {
      break;
    }
  }
  result.wall_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  result.ok = true;
  return result;
}

bool compose_equals_forward(Rig& rig, const v2::ViewRequest& view,
                            std::vector<std::uint16_t>* composed_out) {
  const int width = view.level_pixels.x1 - view.level_pixels.x0;
  const int height = view.level_pixels.y1 - view.level_pixels.y0;
  std::vector<std::uint16_t> composed(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
  if (!rig.canvas.compose_view(view, composed).has_value()) {
    std::fprintf(stderr, "compose_view failed\n");
    return false;
  }
  std::vector<std::uint16_t> direct(composed.size(), 0xFFFFU);
  for (std::size_t index = 0; index < rig.log.operation_count(); ++index) {
    const auto operation = rig.log.operation(index);
    if (!operation.has_value() ||
        !v2::apply_incremental_operation(
            {.tool = operation->tool, .color = operation->color, .samples = operation->samples},
            {.zoom = view.zoom,
             .level_bounds = view.level_pixels,
             .pixels = direct,
             .stride = width})) {
      std::fprintf(stderr, "forward replay failed\n");
      return false;
    }
  }
  if (composed_out != nullptr) {
    *composed_out = composed;
  }
  if (composed != direct) {
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < composed.size(); ++i) {
      mismatches += composed[i] != direct[i];
    }
    std::fprintf(stderr, "MISMATCH producer-vs-forward: %zu pixels differ\n", mismatches);
    return false;
  }
  return true;
}

bool append_realistic_seed7(Rig& rig) {
  constexpr std::size_t kStrokeCount = 1'000;
  std::vector<tinydraw::VectorStroke> strokes(kStrokeCount);
  std::vector<tinydraw::StrokeSample> source_samples(24'576);
  tinydraw::VectorDocument document{strokes, source_samples};
  if (!tinydraw::populate_realistic_handwriting(document, 7U, kStrokeCount,
                                                {.x0 = 0.0F,
                                                 .y0 = 0.0F,
                                                 .x1 = static_cast<float>(v2::kWorldWidth),
                                                 .y1 = static_cast<float>(v2::kWorldHeight)})) {
    return false;
  }
  std::vector<v2::CompactOperationSample> converted(200);
  for (const auto& stroke : document.strokes()) {
    const auto input = document.samples(stroke);
    if (input.empty() || input.size() > converted.size()) {
      return false;
    }
    for (std::size_t index = 0; index < input.size(); ++index) {
      converted[index] = {
          .x_quarter = static_cast<std::uint16_t>(std::lround(input[index].x * 16.0F)),
          .y_quarter = static_cast<std::uint16_t>(std::lround(input[index].y * 16.0F)),
          .radius_256 = static_cast<std::uint16_t>(std::lround(input[index].radius * 256.0F)),
          .elapsed_ms = static_cast<std::uint16_t>(index * 15U),
      };
    }
    if (!rig.append({.tool = stroke.tool == tinydraw::VectorTool::kEraser
                                 ? v2::OperationTool::kEraser
                                 : v2::OperationTool::kPen,
                     .color = stroke.color,
                     .samples = std::span(converted).first(input.size())})) {
      return false;
    }
  }
  return true;
}

bool append_overlap_corpus(Rig& rig) {
  constexpr std::size_t kStrokeCount = 8;
  constexpr std::size_t kSamplesPerStroke = 150;
  std::array<v2::CompactOperationSample, kSamplesPerStroke> samples{};
  for (std::size_t stroke = 0; stroke < kStrokeCount; ++stroke) {
    for (std::size_t index = 0; index < samples.size(); ++index) {
      const std::size_t phase = (index + stroke * 7U) % 64U;
      const std::size_t triangle = phase <= 32U ? phase : 64U - phase;
      const std::size_t x =
          64U + index * static_cast<std::size_t>(v2::kWorldWidth - 128) / (kSamplesPerStroke - 1U);
      const std::size_t y = 320U + triangle * 32U + stroke * 3U;
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>(x * 16U),
          .y_quarter = static_cast<std::uint16_t>(y * 16U),
          .radius_256 = static_cast<std::uint16_t>(80U * 256U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    if (!rig.append({.tool = v2::OperationTool::kPen,
                     .color = static_cast<std::uint16_t>(0x001FU + stroke * 0x111U),
                     .samples = samples})) {
      return false;
    }
  }
  return true;
}

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
void print_census(const char* label) {
  const auto& c = v2::g_raster_census;
  std::printf("  %s ops_rejected=%" PRIu64 " segs_painted=%" PRIu64 " segs_rejected=%" PRIu64
              "\n    ops_saturation_skipped=%" PRIu64 " segs_saturation_skipped=%" PRIu64
              " groups_saturated_early=%" PRIu64 "\n    rows_scanned=%" PRIu64
              " rows_prefinal=%" PRIu64 " rows_empty=%" PRIu64 " span_px=%" PRIu64
              "\n    mask_skips=%" PRIu64 " covers_calls=%" PRIu64 " covers_hits=%" PRIu64
              " const_span_px=%" PRIu64 " const_mask_skips=%" PRIu64
              "\n    const_rows_scanned=%" PRIu64 " const_search_calls=%" PRIu64
              " const_search_last=%" PRIu64 " const_rows_probed_empty=%" PRIu64
              "\n    remaining_scans=%" PRIu64 " remaining_scan_ms=%.2f\n",
              label, c.operations_bbox_rejected, c.segments_painted, c.segments_bbox_rejected,
              c.operations_saturation_skipped, c.segments_saturation_skipped,
              c.groups_saturated_early, c.rows_scanned, c.rows_prefinalized, c.rows_empty_span,
              c.span_pixels, c.mask_skips, c.covers_calls, c.covers_hits, c.const_span_pixels,
              c.const_mask_skips, c.const_rows_scanned, c.const_search_calls,
              c.const_search_last_calls, c.const_rows_probed_empty, c.remaining_scans,
              static_cast<double>(c.remaining_scan_ns) / 1e6);
}
#else
void print_census(const char*) {}
#endif

int run_adversarial_sweep(
    std::size_t operation_count = v2::test_support::kAdversarialTaperedOperationCount,
    std::size_t samples_per_operation = v2::test_support::kAdversarialTaperedSamplesPerOperation) {
  Rig rig(operation_count, operation_count * samples_per_operation);
  if (!rig.log.ready() || !rig.canvas.ready() || !rig.producer.ready()) {
    std::fprintf(stderr, "rig not ready\n");
    return 1;
  }
  if (!rig.reset_blank({1})) {
    std::fprintf(stderr, "baseline reset failed\n");
    return 1;
  }
  const bool appended = v2::test_support::emit_adversarial_tapered_corpus(
      [&](const v2::OperationAppend& operation) { return rig.append(operation); }, nullptr,
      operation_count, samples_per_operation);
  if (!appended) {
    std::fprintf(stderr, "corpus append failed\n");
    return 1;
  }

  constexpr std::array zooms{v2::ZoomLevel::k50Percent, v2::ZoomLevel::k100Percent,
                             v2::ZoomLevel::k200Percent, v2::ZoomLevel::k400Percent};
  constexpr std::array zoom_names{"50", "100", "200", "400"};

  std::printf("== adversarial cold sweep ==\n");
  for (std::size_t zoom_index = 0; zoom_index < zooms.size(); ++zoom_index) {
    const v2::ViewRequest view{
        .zoom = zooms[zoom_index],
        .level_pixels = {0, 0, v2::kOverviewWidth, v2::kOverviewHeight},
    };
    if (!rig.canvas.discard_tiles()) {
      std::fprintf(stderr, "discard_tiles failed\n");
      return 1;
    }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    v2::g_raster_census.reset();
#endif
    const SweepResult sweep = run_cold_fill(rig, view);
    if (!sweep.ok) {
      return 1;
    }
    if (!compose_equals_forward(rig, view, nullptr)) {
      std::fprintf(stderr, "zoom %s NOT EXACT vs forward replay\n", zoom_names[zoom_index]);
      return 1;
    }
    const double operations_per_group =
        sweep.candidates.groups_published == 0U
            ? 0.0
            : static_cast<double>(sweep.candidates.operations_scanned) /
                  static_cast<double>(sweep.candidates.groups_published);
    std::printf(
        "zoom=%s steps=%zu tiles=%zu wall_ms=%.3f ops_authority=%zu index_candidates=%zu "
        "dedup_candidates=%zu ops_scanned=%zu ops_intersecting=%zu "
        "groups=%zu ops_per_group=%.2f exact=1\n",
        zoom_names[zoom_index], sweep.steps, sweep.tiles, sweep.wall_ms,
        sweep.candidates.operations_in_authority, sweep.candidates.index_candidates,
        sweep.candidates.deduplicated_candidates, sweep.candidates.operations_scanned,
        sweep.candidates.operations_intersecting, sweep.candidates.groups_published,
        operations_per_group);
    print_census("census");
  }
  std::printf("ADVERSARIAL_SWEEP_OK\n");
  return 0;
}

enum class ScorecardCorpus { kSeed7, kAdversarial, kOverlap };

struct ScorecardResult {
  SweepResult sweep{};
  bool exact = false;
};

std::optional<ScorecardResult> measure_scorecard_corpus(ScorecardCorpus corpus) {
  const std::size_t operation_capacity =
      corpus == ScorecardCorpus::kSeed7 ? 1'000U
                                        : (corpus == ScorecardCorpus::kAdversarial
                                               ? v2::test_support::kAdversarialTaperedOperationCount
                                               : 8U);
  const std::size_t sample_capacity = corpus == ScorecardCorpus::kSeed7
                                          ? 24'576U
                                          : (corpus == ScorecardCorpus::kAdversarial
                                                 ? v2::test_support::kAdversarialTaperedSampleCount
                                                 : 8U * 150U);
  Rig rig(operation_capacity, sample_capacity);
  if (!rig.reset_blank({1})) {
    return std::nullopt;
  }
  bool appended = false;
  if (corpus == ScorecardCorpus::kSeed7) {
    appended = append_realistic_seed7(rig);
  } else if (corpus == ScorecardCorpus::kAdversarial) {
    appended = v2::test_support::emit_adversarial_tapered_corpus(
        [&](const v2::OperationAppend& operation) { return rig.append(operation); });
  } else {
    appended = append_overlap_corpus(rig);
  }
  if (!appended) {
    return std::nullopt;
  }

  const int level_width = v2::kWorldWidth * 4;
  const int level_height = v2::kWorldHeight * 4;
  int x = 0;
  int y = 0;
  if (corpus == ScorecardCorpus::kSeed7) {
    x = 63;
    y = 63;
  } else if (corpus == ScorecardCorpus::kOverlap) {
    x = std::clamp(level_width / 2 - v2::kOverviewWidth / 2 + 31, 0,
                   level_width - v2::kOverviewWidth);
    y = std::clamp(level_height / 2 - v2::kOverviewHeight / 2 + 31, 0,
                   level_height - v2::kOverviewHeight);
  }
  const v2::ViewRequest view{
      .zoom = v2::ZoomLevel::k400Percent,
      .level_pixels = {x, y, x + v2::kOverviewWidth, y + v2::kOverviewHeight},
  };

  constexpr std::size_t kRuns = 9;
  std::array<double, kRuns> wall_times{};
  ScorecardResult result{};
  for (std::size_t run = 0; run < kRuns; ++run) {
    if (!rig.canvas.discard_tiles()) {
      return std::nullopt;
    }
    result.sweep = run_cold_fill(rig, view);
    if (!result.sweep.ok || !compose_equals_forward(rig, view, nullptr)) {
      return std::nullopt;
    }
    wall_times[run] = result.sweep.wall_ms;
  }
  std::sort(wall_times.begin(), wall_times.end());
  result.sweep.wall_ms = wall_times[kRuns / 2U];
  result.exact = true;
  return result;
}

int run_cold_scorecard() {
  constexpr std::array corpora{ScorecardCorpus::kSeed7, ScorecardCorpus::kAdversarial,
                               ScorecardCorpus::kOverlap};
  constexpr std::array names{"seed7", "adversarial-tapered", "overlap"};
  std::printf("== cold replay scorecard zoom=400 runs=9 statistic=median ==\n");
  for (std::size_t corpus = 0; corpus < corpora.size(); ++corpus) {
    const auto result = measure_scorecard_corpus(corpora[corpus]);
    if (!result.has_value()) {
      std::fprintf(stderr, "scorecard failed corpus=%s\n", names[corpus]);
      return 1;
    }
    const auto& candidates = result->sweep.candidates;
    const double operations_per_group = candidates.groups_published == 0U
                                            ? 0.0
                                            : static_cast<double>(candidates.operations_scanned) /
                                                  static_cast<double>(candidates.groups_published);
    std::printf(
        "SCORE corpus=%s ops_authority=%zu index_candidates=%zu dedup_candidates=%zu "
        "ops_scanned=%zu ops_intersecting=%zu groups=%zu "
        "ops_per_group=%.2f wall_ms=%.3f exact=%u\n",
        names[corpus], candidates.operations_in_authority, candidates.index_candidates,
        candidates.deduplicated_candidates, candidates.operations_scanned,
        candidates.operations_intersecting, candidates.groups_published, operations_per_group,
        result->sweep.wall_ms, result->exact);
  }
  std::printf("COLD_SCORECARD_OK\n");
  return 0;
}

// ---------------------------------------------------------------------------
// Combined general cold corpus: host mirror of the device gate harness
// (`append_general_cold_document`). Deterministic xorshift stream 0x5EED7;
// same call order as the firmware so the document matches the frozen device
// corpus up to libm float cos/sin last-ulp drift.

struct HairlineRandom {
  std::uint32_t state = 0x5EED7u;
  std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  float range(float low, float high) {
    return low + (high - low) * (static_cast<float>(next() & 0xFFFFFFu) / 16'777'216.0F);
  }
};

bool append_hairline_stroke(Rig& rig, HairlineRandom& random, float radius, std::uint16_t color,
                            v2::OperationTool tool, std::uint16_t gesture_id, float length) {
  constexpr std::size_t kChunkSamples = 12;
  const float margin = radius + 2.0F;
  float x = random.range(margin, static_cast<float>(v2::kWorldWidth) - margin);
  float y = random.range(margin, static_cast<float>(v2::kWorldHeight) - margin);
  float angle = random.range(0.0F, 6.2831853F);
  float remaining = length;
  std::uint16_t elapsed_ms = 0;
  bool continuing = false;
  std::array<v2::CompactOperationSample, kChunkSamples> chunk{};
  while (remaining > 0.0F) {
    std::size_t count = 0;
    if (continuing) {
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
                       static_cast<float>(v2::kWorldWidth) - margin);
        y = std::clamp(y + step * std::sin(angle), margin,
                       static_cast<float>(v2::kWorldHeight) - margin);
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
    if (!rig.append({.tool = tool,
                     .color = color,
                     .gesture_id = gesture_id,
                     .samples = std::span(chunk.data(), count)})) {
      return false;
    }
  }
  return true;
}

bool append_hairline_document(Rig& rig, int thin_strokes = 220, int medium_strokes = 60,
                              int thick_strokes = 10) {
  constexpr std::array<std::uint16_t, 6> kColors{0x0000U, 0x001FU, 0xF800U,
                                                 0x07E0U, 0x4208U, 0x8010U};
  HairlineRandom random;
  std::uint16_t gesture_id = 7'000;
  for (int stroke = 0; stroke < thin_strokes; ++stroke) {
    const bool eraser = (random.next() % 12U) == 0U;
    if (!append_hairline_stroke(rig, random, random.range(1.3F, 2.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? v2::OperationTool::kEraser : v2::OperationTool::kPen,
                                gesture_id++, random.range(300.0F, 1'400.0F))) {
      return false;
    }
  }
  for (int stroke = 0; stroke < medium_strokes; ++stroke) {
    if (!append_hairline_stroke(rig, random, random.range(3.5F, 4.7F),
                                kColors[random.next() % kColors.size()], v2::OperationTool::kPen,
                                gesture_id++, random.range(200.0F, 800.0F))) {
      return false;
    }
  }
  for (int stroke = 0; stroke < thick_strokes; ++stroke) {
    const bool eraser = stroke == 4 || stroke == 9;
    if (!append_hairline_stroke(rig, random, random.range(40.0F, 80.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? v2::OperationTool::kEraser : v2::OperationTool::kPen,
                                gesture_id++, random.range(800.0F, 2'000.0F))) {
      return false;
    }
  }
  return true;
}

bool append_dense_eraser_grid(Rig& rig, int lines_per_axis = 24) {
  constexpr std::size_t kSamples = 12;
  constexpr std::uint16_t kRadius = 384U;  // 1.5 world pixels
  std::array<v2::CompactOperationSample, kSamples> samples{};
  std::uint16_t gesture_id = 8'000U;
  for (int line = 0; line < lines_per_axis; ++line) {
    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
      samples[sample] = {
          .x_quarter = static_cast<std::uint16_t>((650 + static_cast<int>(sample) * 18) * 16),
          .y_quarter = static_cast<std::uint16_t>((825 + line * 6) * 16),
          .radius_256 = kRadius,
          .elapsed_ms = static_cast<std::uint16_t>(sample * 8U),
      };
    }
    if (!rig.append(
            {.tool = v2::OperationTool::kEraser, .gesture_id = gesture_id++, .samples = samples})) {
      return false;
    }
  }
  for (int line = 0; line < lines_per_axis; ++line) {
    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
      samples[sample] = {
          .x_quarter = static_cast<std::uint16_t>((675 + line * 6) * 16),
          .y_quarter = static_cast<std::uint16_t>((810 + static_cast<int>(sample) * 18) * 16),
          .radius_256 = kRadius,
          .elapsed_ms = static_cast<std::uint16_t>(sample * 8U),
      };
    }
    if (!rig.append(
            {.tool = v2::OperationTool::kEraser, .gesture_id = gesture_id++, .samples = samples})) {
      return false;
    }
  }
  return true;
}

int run_hairline_pan_repro(int thin_strokes, int medium_strokes, int thick_strokes,
                           int eraser_lines_per_axis, int warm_rows, bool trace) {
  Rig rig(1'024, 16'384);
  if (!rig.reset_blank({1}) ||
      !append_hairline_document(rig, thin_strokes, medium_strokes, thick_strokes) ||
      !append_dense_eraser_grid(rig, eraser_lines_per_axis)) {
    std::fprintf(stderr, "hairline pan setup failed\n");
    return 1;
  }
  const v2::ViewRequest target{
      .zoom = v2::ZoomLevel::k400Percent,
      .level_pixels = {2'760, 3'360, 3'128, 3'808},
  };
  std::size_t warm_steps = 0U;
  double warm_wall_ms = 0.0;
  if (warm_rows != 0) {
    const int first_tile_row = target.level_pixels.y0 / v2::kTileHeight;
    const int warm_y1 = warm_rows < 0 ? target.level_pixels.y1
                                      : std::min(target.level_pixels.y1,
                                                 (first_tile_row + warm_rows) * v2::kTileHeight);
    const v2::ViewRequest warm{
        .zoom = v2::ZoomLevel::k400Percent,
        .level_pixels = warm_rows < 0
                            ? v2::PixelRect{2'392, target.level_pixels.y0, target.level_pixels.x0,
                                            target.level_pixels.y1}
                            : v2::PixelRect{target.level_pixels.x0 - 8, target.level_pixels.y0,
                                            target.level_pixels.x0, warm_y1},
    };
    const SweepResult warm_fill = run_cold_fill(rig, warm);
    if (!warm_fill.ok || !compose_equals_forward(rig, warm, nullptr)) {
      std::fprintf(stderr, "hairline warm view failed\n");
      return 1;
    }
    warm_steps = warm_fill.steps;
    warm_wall_ms = warm_fill.wall_ms;
  }

  constexpr std::size_t kMaximumSteps = 20'000U;
  // Current device receipt: 508,346 us / 30 producer calls. Twenty-nine
  // calls are the deterministic host proxy for the first ~500 ms of work.
  constexpr std::size_t kFiveHundredMsEquivalentSteps = 29U;
  const std::size_t pixel_count =
      static_cast<std::size_t>(v2::kOverviewWidth) * static_cast<std::size_t>(v2::kOverviewHeight);
  std::vector<std::uint16_t> composed(pixel_count);
  std::size_t steps = 0U;
  std::size_t publications = 0U;
  std::size_t previous_fallback = pixel_count + 1U;
  std::size_t budget_fallback = 0U;
  std::size_t budget_missing_blocks = 0U;
  bool budget_complete = false;
  bool complete = false;
  std::optional<std::size_t> trace_remaining;
  if (trace) {
    trace_remaining = rig.producer.visible_tiles_remaining(target);
    if (!trace_remaining.has_value()) {
      std::fprintf(stderr, "hairline pan initial remaining scan failed\n");
      return 1;
    }
  }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  v2::g_raster_census.reset();
#endif
  const auto target_started = std::chrono::steady_clock::now();
  while (steps < kMaximumSteps) {
    v2::RasterCensus census_before{};
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    census_before = v2::g_raster_census;
#endif
    const auto slice_started = std::chrono::steady_clock::now();
    const auto step = rig.producer.produce_next(target);
    if (!step.has_value()) {
      std::fprintf(stderr, "hairline pan producer failed step=%zu\n", steps);
      return 1;
    }
    const double slice_wall_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - slice_started)
            .count();
    v2::RasterCensus census_after{};
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    census_after = v2::g_raster_census;
#endif
    if (trace && (step->tiles_published != 0U || step->complete)) {
      trace_remaining = step->visible_tiles_remaining;
    }
    if (trace) {
      const int group_column = step->level_bounds.x0 / v2::kTileWidth & ~1;
      const int group_row = step->level_bounds.y0 / v2::kTileHeight & ~1;
      const v2::PixelRect group_bounds{
          .x0 = group_column * v2::kTileWidth,
          .y0 = group_row * v2::kTileHeight,
          .x1 = (group_column + v2::kTileProducerColumns) * v2::kTileWidth,
          .y1 = (group_row + v2::kTileProducerRows) * v2::kTileHeight,
      };
      std::printf(
          "HAIRLINE_PAN_TRACE slice=%zu group=[%d,%d,%d,%d] wall_ms=%.3f authority=%zu "
          "index_candidates=%zu dedup_candidates=%zu ops_scanned=%zu ops_intersecting=%zu "
          "ops_rendered=%zu raster_steps=%zu raster_work=%zu groups_published=%zu "
          "tiles_published=%zu remaining=%zu gate_ticks=%llu setup_ticks=%llu "
          "paint_ticks=%llu publish_ticks=%llu remaining_scan_ns=%llu rows_scanned=%llu "
          "span_pixels=%llu mask_skips=%llu const_rows=%llu const_search_calls=%llu "
          "const_span_pixels=%llu const_mask_skips=%llu rows_prefinalized=%llu "
          "const_full_fills=%llu const_full_pixels=%llu "
          "operation_saturation_skips=%llu segment_saturation_skips=%llu "
          "groups_saturated_early=%llu complete=%u\n",
          steps + 1U, group_bounds.x0, group_bounds.y0, group_bounds.x1, group_bounds.y1,
          slice_wall_ms, step->operations_in_authority, step->index_candidates,
          step->deduplicated_candidates, step->operations_scanned, step->operations_intersecting,
          step->operations_rendered, step->raster_steps, step->raster_work, step->groups_published,
          step->tiles_published, *trace_remaining,
          static_cast<unsigned long long>(census_after.gate_ticks - census_before.gate_ticks),
          static_cast<unsigned long long>(census_after.setup_ticks - census_before.setup_ticks),
          static_cast<unsigned long long>(census_after.paint_ticks - census_before.paint_ticks),
          static_cast<unsigned long long>(census_after.publish_ticks - census_before.publish_ticks),
          static_cast<unsigned long long>(census_after.remaining_scan_ns -
                                          census_before.remaining_scan_ns),
          static_cast<unsigned long long>(census_after.rows_scanned - census_before.rows_scanned),
          static_cast<unsigned long long>(census_after.span_pixels - census_before.span_pixels),
          static_cast<unsigned long long>(census_after.mask_skips - census_before.mask_skips),
          static_cast<unsigned long long>(census_after.const_rows_scanned -
                                          census_before.const_rows_scanned),
          static_cast<unsigned long long>(census_after.const_search_calls -
                                          census_before.const_search_calls),
          static_cast<unsigned long long>(census_after.const_span_pixels -
                                          census_before.const_span_pixels),
          static_cast<unsigned long long>(census_after.const_mask_skips -
                                          census_before.const_mask_skips),
          static_cast<unsigned long long>(census_after.rows_prefinalized -
                                          census_before.rows_prefinalized),
          static_cast<unsigned long long>(census_after.const_full_surface_fills -
                                          census_before.const_full_surface_fills),
          static_cast<unsigned long long>(census_after.const_full_surface_pixels -
                                          census_before.const_full_surface_pixels),
          static_cast<unsigned long long>(census_after.operations_saturation_skipped -
                                          census_before.operations_saturation_skipped),
          static_cast<unsigned long long>(census_after.segments_saturation_skipped -
                                          census_before.segments_saturation_skipped),
          static_cast<unsigned long long>(census_after.groups_saturated_early -
                                          census_before.groups_saturated_early),
          step->complete);
    }
    if (step->tiles_published != 0U) {
      ++publications;
      const auto stats = rig.canvas.compose_view(target, composed);
      if (!stats.has_value() || stats->fallback_pixels > previous_fallback) {
        std::fprintf(stderr, "hairline pan fallback regressed step=%zu fallback=%zu previous=%zu\n",
                     steps, stats.has_value() ? stats->fallback_pixels : pixel_count,
                     previous_fallback);
        return 1;
      }
      previous_fallback = stats->fallback_pixels;
    }
    ++steps;
    if (steps == kFiveHundredMsEquivalentSteps) {
      const auto stats = rig.canvas.compose_view(target, composed);
      if (!stats.has_value()) {
        std::fprintf(stderr, "hairline budget compose failed\n");
        return 1;
      }
      budget_complete = step->complete;
      budget_fallback = stats->fallback_pixels;
      for (int row = target.level_pixels.y0 / v2::kTileHeight;
           row <= (target.level_pixels.y1 - 1) / v2::kTileHeight; ++row) {
        for (int column = target.level_pixels.x0 / v2::kTileWidth;
             column <= (target.level_pixels.x1 - 1) / v2::kTileWidth; ++column) {
          const auto source = rig.canvas.lookup(
              {target.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
          budget_missing_blocks +=
              !source.has_value() || source->kind == v2::SourceKind::kOverview ? 1U : 0U;
        }
      }
    }
    if (step->complete) {
      complete = true;
      break;
    }
  }
  const double target_wall_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - target_started)
          .count();
  v2::RasterCensus target_census{};
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  target_census = v2::g_raster_census;
#endif
  const auto final_stats = rig.canvas.compose_view(target, composed);
  if (!complete || publications == 0U || !final_stats.has_value() ||
      final_stats->fallback_pixels != 0U) {
    std::fprintf(stderr,
                 "hairline pan incomplete complete=%u steps=%zu publications=%zu fallback=%zu\n",
                 complete, steps, publications,
                 final_stats.has_value() ? final_stats->fallback_pixels : pixel_count);
    return 1;
  }
  if (steps <= kFiveHundredMsEquivalentSteps) {
    budget_complete = true;
    budget_missing_blocks = 0U;
    budget_fallback = final_stats->fallback_pixels;
  }

  std::vector<std::uint16_t> direct(pixel_count, 0xFFFFU);
  for (std::size_t index = 0; index < rig.log.operation_count(); ++index) {
    const auto operation = rig.log.operation(index);
    if (!operation.has_value() ||
        !v2::apply_incremental_operation(
            {.tool = operation->tool, .color = operation->color, .samples = operation->samples},
            {.zoom = target.zoom,
             .level_bounds = target.level_pixels,
             .pixels = direct,
             .stride = v2::kOverviewWidth})) {
      std::fprintf(stderr, "hairline direct replay failed operation=%zu\n", index);
      return 1;
    }
  }
  std::size_t mismatches = 0U;
  std::size_t white_blocks = 0U;
  for (int block_y = 0; block_y < v2::kOverviewHeight; block_y += v2::kTileHeight) {
    for (int block_x = 0; block_x < v2::kOverviewWidth; block_x += v2::kTileWidth) {
      bool composed_is_paper = true;
      bool direct_has_ink = false;
      for (int y = block_y; y < std::min(block_y + v2::kTileHeight, v2::kOverviewHeight); ++y) {
        for (int x = block_x; x < std::min(block_x + v2::kTileWidth, v2::kOverviewWidth); ++x) {
          const std::size_t at =
              static_cast<std::size_t>(y) * v2::kOverviewWidth + static_cast<std::size_t>(x);
          mismatches += composed[at] != direct[at] ? 1U : 0U;
          composed_is_paper = composed_is_paper && composed[at] == 0xFFFFU;
          direct_has_ink = direct_has_ink || direct[at] != 0xFFFFU;
        }
      }
      white_blocks += composed_is_paper && direct_has_ink ? 1U : 0U;
    }
  }
  std::printf(
      "HAIRLINE_PAN_REPRO thin=%d medium=%d thick=%d eraser_axis=%d warm_rows=%d "
      "warm_steps=%zu warm_wall_ms=%.3f target_wall_ms=%.3f operations=%zu "
      "samples=%zu budget_steps=%zu budget_complete=%u "
      "budget_missing_blocks=%zu budget_fallback=%zu steps=%zu publications=%zu fallback=%zu "
      "mismatches=%zu white_blocks=%zu setup_ms=%.3f paint_ms=%.3f "
      "complete=%u bounded=%u regression=%u\n",
      thin_strokes, medium_strokes, thick_strokes, eraser_lines_per_axis, warm_rows, warm_steps,
      warm_wall_ms, target_wall_ms, rig.log.operation_count(), rig.log.sample_count(),
      kFiveHundredMsEquivalentSteps, budget_complete, budget_missing_blocks, budget_fallback, steps,
      publications, final_stats->fallback_pixels, mismatches, white_blocks,
      static_cast<double>(target_census.setup_ticks) / 1e6,
      static_cast<double>(target_census.paint_ticks) / 1e6, complete, steps < kMaximumSteps,
      !budget_complete || budget_missing_blocks != 0U || budget_fallback != 0U);
  const bool exact = mismatches == 0U && white_blocks == 0U;
  const bool completed_in_budget =
      budget_complete && budget_missing_blocks == 0U && budget_fallback == 0U;
  return exact && completed_in_budget ? 0 : 1;
}

int run_general_sweep(std::size_t runs) {
  Rig rig(1'024, 16'384);
  if (!rig.log.ready() || !rig.canvas.ready() || !rig.producer.ready()) {
    std::fprintf(stderr, "rig not ready\n");
    return 1;
  }
  if (!rig.reset_blank({1})) {
    std::fprintf(stderr, "baseline reset failed\n");
    return 1;
  }
  const bool appended =
      v2::test_support::emit_adversarial_tapered_corpus(
          [&](const v2::OperationAppend& operation) { return rig.append(operation); }, nullptr) &&
      append_hairline_document(rig);
  if (!appended) {
    std::fprintf(stderr, "corpus append failed\n");
    return 1;
  }
  std::printf("== general cold sweep operations=%zu samples=%zu ==\n", rig.log.operation_count(),
              rig.log.sample_count());
  constexpr std::array zooms{v2::ZoomLevel::k50Percent, v2::ZoomLevel::k100Percent,
                             v2::ZoomLevel::k200Percent, v2::ZoomLevel::k400Percent};
  constexpr std::array zoom_names{"50", "100", "200", "400"};
  for (std::size_t zoom_index = 0; zoom_index < zooms.size(); ++zoom_index) {
    const v2::ViewRequest zoom_view{
        .zoom = zooms[zoom_index],
        .level_pixels = {0, 0, v2::kOverviewWidth, v2::kOverviewHeight},
    };
    std::vector<double> zoom_walls;
    CandidateDiscoveryCounters zoom_candidates{};
    for (std::size_t run = 0; run < runs; ++run) {
      if (!rig.canvas.discard_tiles()) {
        return 1;
      }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
      v2::g_raster_census.reset();
#endif
      const SweepResult sweep = run_cold_fill(rig, zoom_view);
      if (!sweep.ok) {
        return 1;
      }
      if (run == 0U) {
        zoom_candidates = sweep.candidates;
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
        const v2::RasterCensus zoom_cold_census = v2::g_raster_census;
#endif
        if (!compose_equals_forward(rig, zoom_view, nullptr)) {
          std::fprintf(stderr, "zoom %s NOT EXACT vs forward replay\n", zoom_names[zoom_index]);
          return 1;
        }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
        v2::g_raster_census = zoom_cold_census;
#endif
        print_census(zoom_names[zoom_index]);
      }
      zoom_walls.push_back(sweep.wall_ms);
    }
    std::sort(zoom_walls.begin(), zoom_walls.end());
    std::printf(
        "GENERAL zoom=%s median_wall_ms=%.3f ops_authority=%zu index_candidates=%zu "
        "dedup_candidates=%zu ops_scanned=%zu ops_intersecting=%zu\n",
        zoom_names[zoom_index], zoom_walls[zoom_walls.size() / 2U],
        zoom_candidates.operations_in_authority, zoom_candidates.index_candidates,
        zoom_candidates.deduplicated_candidates, zoom_candidates.operations_scanned,
        zoom_candidates.operations_intersecting);
  }
  const v2::ViewRequest view{
      .zoom = v2::ZoomLevel::k400Percent,
      .level_pixels = {0, 0, v2::kOverviewWidth, v2::kOverviewHeight},
  };
  std::vector<double> walls;
  for (std::size_t run = 0; run < runs; ++run) {
    if (!rig.canvas.discard_tiles()) {
      std::fprintf(stderr, "discard_tiles failed\n");
      return 1;
    }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    v2::g_raster_census.reset();
#endif
    const SweepResult sweep = run_cold_fill(rig, view);
    if (!sweep.ok) {
      return 1;
    }
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
    // Snapshot before the forward-replay equality check pollutes counters.
    const v2::RasterCensus cold_census = v2::g_raster_census;
#endif
    if (run == 0U && !compose_equals_forward(rig, view, nullptr)) {
      std::fprintf(stderr, "general corpus NOT EXACT vs forward replay\n");
      return 1;
    }
    walls.push_back(sweep.wall_ms);
    std::printf(
        "run=%zu steps=%zu tiles=%zu wall_ms=%.3f ops_authority=%zu index_candidates=%zu "
        "dedup_candidates=%zu ops_scanned=%zu ops_intersecting=%zu "
        "groups=%zu\n",
        run, sweep.steps, sweep.tiles, sweep.wall_ms, sweep.candidates.operations_in_authority,
        sweep.candidates.index_candidates, sweep.candidates.deduplicated_candidates,
        sweep.candidates.operations_scanned, sweep.candidates.operations_intersecting,
        sweep.candidates.groups_published);
    if (run == 0U) {
#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
      v2::g_raster_census = cold_census;
#endif
      print_census("census");
    }
  }
  std::sort(walls.begin(), walls.end());
  std::printf("GENERAL_SWEEP_OK median_wall_ms=%.3f\n", walls[walls.size() / 2U]);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && (std::strcmp(argv[1], "--hairline-pan-repro") == 0 ||
                    std::strcmp(argv[1], "--hairline-pan-trace") == 0)) {
    const int thin = argc >= 3 ? std::atoi(argv[2]) : 10;
    const int medium = argc >= 4 ? std::atoi(argv[3]) : 1;
    const int thick = argc >= 5 ? std::atoi(argv[4]) : 1;
    const int eraser_axis = argc >= 6 ? std::atoi(argv[5]) : 16;
    const int warm_rows = argc >= 7 ? std::atoi(argv[6]) : 8;
    const bool trace = std::strcmp(argv[1], "--hairline-pan-trace") == 0;
    return run_hairline_pan_repro(thin, medium, thick, eraser_axis, warm_rows, trace);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--fuzz-collinear") == 0) {
    const auto cases = static_cast<std::uint32_t>(argc >= 3 ? std::atoi(argv[2]) : 2'000);
    const auto seed = static_cast<std::uint32_t>(argc >= 4 ? std::atoi(argv[3]) : 0xC0FFEE);
    return tinydraw::vector_v2::raster_census_tool::run_fuzz_collinear(cases, seed);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--fuzz-docs") == 0) {
    const auto cases = static_cast<std::uint32_t>(argc >= 3 ? std::atoi(argv[2]) : 400);
    const auto seed = static_cast<std::uint32_t>(argc >= 4 ? std::atoi(argv[3]) : 0xBEEF);
    return tinydraw::vector_v2::raster_census_tool::run_fuzz_docs(cases, seed);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--adversarial-ops") == 0) {
    const auto operation_count =
        static_cast<std::size_t>(argc >= 3 ? std::max(1, std::atoi(argv[2])) : 1);
    const auto samples_per_operation =
        static_cast<std::size_t>(argc >= 4 ? std::max(2, std::atoi(argv[3])) : 32);
    return run_adversarial_sweep(operation_count, samples_per_operation);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--cold-scorecard") == 0) {
    return run_cold_scorecard();
  }
  if (argc >= 2 && std::strcmp(argv[1], "--general") == 0) {
    const auto runs = static_cast<std::size_t>(argc >= 3 ? std::max(1, std::atoi(argv[2])) : 5);
    return run_general_sweep(runs);
  }
  return run_adversarial_sweep();
}
