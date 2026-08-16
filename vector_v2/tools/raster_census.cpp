// Host measurement + equality rig for the Vector V2 cold producer.
//
// Modes:
//   (default)            adversarial-corpus census sweep across zooms with a
//                        bit-exact check against direct forward painter replay
//   --fuzz-collinear N   randomized collinear constant-radius runs: producer
//                        per-segment replay vs direct forward replay
//   --fuzz-docs N        randomized mixed documents (tapered/constant/collinear,
//                        erasers): producer vs direct forward replay
//
// Counters require the library to be built with TINYDRAW_VECTOR_V2_RASTER_CENSUS.

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
#include <random>
#include <span>
#include <vector>

#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/vector_v2/adversarial_tapered_corpus.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace v2 = tinydraw::vector_v2;

namespace {

constexpr std::size_t kWorkspaceTileCapacity = v2::kMaximumVisibleTiles;

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
  std::vector<std::uint32_t> replay_index_words;
  std::vector<v2::RenderAccountingEntry> accounting_entries;
  // append_incrementally workspace (mirrors the app).
  std::vector<std::uint16_t> overview_scratch;
  std::vector<std::uint16_t> tile_scratch;
  std::vector<v2::TileRevisionPublication> publications;
  std::vector<v2::TileKey> affected_keys;

  v2::OperationLog log;
  v2::MaterializedCanvas canvas;
  v2::RenderAccounting accounting;
  v2::TileProducer producer;

  Rig(std::size_t record_capacity, std::size_t sample_capacity, bool indexed = true)
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
        replay_index_words(indexed ? v2::kReplayIndexWords : 0U),
        accounting_entries(v2::kMaximumVisibleTiles),
        overview_scratch(v2::kOverviewPixels),
        tile_scratch(kWorkspaceTileCapacity * v2::kTilePixels),
        publications(kWorkspaceTileCapacity),
        affected_keys(v2::kTileSlotCount + v2::kMaximumVisibleTiles),
        log(records, samples),
        canvas(overview, *uniforms, occupancy, slots, tile_pool, {}, raw_slot_directory),
        accounting(accounting_entries),
        producer(log, canvas,
                 {.supertask_pixels = supertask,
                  .finalized_pixels = mask,
                  .summary_row_unset = summary_rows,
                  .summary_saturated_words = summary_words,
                  .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans)),
                  .replay_index_words = replay_index_words},
                 {}, 0xFFFFU, &accounting) {}

  [[nodiscard]] v2::IncrementalDocumentWorkspace workspace() {
    return {
        .overview_scratch = overview_scratch,
        .tile_scratch = tile_scratch,
        .publications = publications,
        .affected_keys = affected_keys,
    };
  }

  [[nodiscard]] bool reset_blank(v2::DocumentRevision revision) {
    return v2::restore_document_snapshot(log, canvas, revision, snapshot) &&
           producer.reset_uniform_baseline(revision);
  }
};

struct SweepResult {
  std::size_t steps = 0;
  std::size_t tiles = 0;
  double wall_ms = 0.0;
  v2::CandidateDiscoveryCounters candidates{};
  v2::RenderAccountingTotals accounting{};
  bool ok = false;
};

SweepResult run_cold_fill(Rig& rig, const v2::ViewRequest& view) {
  SweepResult result{};
  rig.producer.reset_candidate_counters();
  rig.accounting.reset();
  const auto started = std::chrono::steady_clock::now();
  while (true) {
    const auto step = rig.producer.produce_next(view);
    if (!step.has_value()) {
      std::fprintf(stderr, "produce_next failed at step %zu\n", result.steps);
      return result;
    }
    ++result.steps;
    result.tiles += step->tiles_published;
    if (step->complete) {
      break;
    }
  }
  result.wall_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  result.candidates = rig.producer.candidate_counters();
  // One immediate revisit exercises reuse accounting without affecting cold
  // wall time or candidate-discovery totals.
  if (!rig.producer.produce_next(view).has_value()) {
    return result;
  }
  result.accounting = rig.accounting.totals();
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
  const auto workspace = rig.workspace();
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
    if (!v2::append_incrementally(
            rig.log, rig.canvas,
            {.tool = stroke.tool == tinydraw::VectorTool::kEraser ? v2::OperationTool::kEraser
                                                                  : v2::OperationTool::kPen,
             .color = stroke.color,
             .samples = std::span(converted).first(input.size())},
            workspace)) {
      return false;
    }
  }
  return true;
}

bool append_overlap_corpus(Rig& rig) {
  constexpr std::size_t kStrokeCount = 8;
  constexpr std::size_t kSamplesPerStroke = 150;
  std::array<v2::CompactOperationSample, kSamplesPerStroke> samples{};
  const auto workspace = rig.workspace();
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
    if (!v2::append_incrementally(rig.log, rig.canvas,
                                  {.tool = v2::OperationTool::kPen,
                                   .color = static_cast<std::uint16_t>(0x001FU + stroke * 0x111U),
                                   .samples = samples},
                                  workspace)) {
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
    bool indexed = true,
    std::size_t operation_count = v2::test_support::kAdversarialTaperedOperationCount,
    std::size_t samples_per_operation = v2::test_support::kAdversarialTaperedSamplesPerOperation) {
  Rig rig(operation_count, operation_count * samples_per_operation, indexed);
  if (!rig.log.ready() || !rig.canvas.ready() || !rig.producer.ready()) {
    std::fprintf(stderr, "rig not ready\n");
    return 1;
  }
  if (!rig.reset_blank({1})) {
    std::fprintf(stderr, "baseline reset failed\n");
    return 1;
  }
  const auto workspace = rig.workspace();
  const bool appended = v2::test_support::emit_adversarial_tapered_corpus(
      [&](const v2::OperationAppend& operation) {
        return v2::append_incrementally(rig.log, rig.canvas, operation, workspace).has_value();
      },
      nullptr, operation_count, samples_per_operation);
  if (!appended) {
    std::fprintf(stderr, "corpus append failed\n");
    return 1;
  }

  constexpr std::array zooms{v2::ZoomLevel::k50Percent, v2::ZoomLevel::k100Percent,
                             v2::ZoomLevel::k200Percent, v2::ZoomLevel::k400Percent};
  constexpr std::array zoom_names{"50", "100", "200", "400"};

  std::printf("== adversarial cold sweep mode=%s ==\n", indexed ? "indexed" : "linear");
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
        "zoom=%s steps=%zu tiles=%zu wall_ms=%.3f ops_scanned=%zu ops_intersecting=%zu "
        "groups=%zu ops_per_group=%.2f attempts=%zu completions=%zu reuses=%zu discards=%zu "
        "amplification=%.3f exact=1\n",
        zoom_names[zoom_index], sweep.steps, sweep.tiles, sweep.wall_ms,
        sweep.candidates.operations_scanned, sweep.candidates.operations_intersecting,
        sweep.candidates.groups_published, operations_per_group, sweep.accounting.attempts,
        sweep.accounting.completions, sweep.accounting.reuses, sweep.accounting.discards,
        sweep.accounting.amplification());
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

std::optional<ScorecardResult> measure_scorecard_corpus(ScorecardCorpus corpus, bool indexed) {
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
  Rig rig(operation_capacity, sample_capacity, indexed);
  if (!rig.reset_blank({1})) {
    return std::nullopt;
  }
  bool appended = false;
  if (corpus == ScorecardCorpus::kSeed7) {
    appended = append_realistic_seed7(rig);
  } else if (corpus == ScorecardCorpus::kAdversarial) {
    const auto workspace = rig.workspace();
    appended = v2::test_support::emit_adversarial_tapered_corpus(
        [&](const v2::OperationAppend& operation) {
          return v2::append_incrementally(rig.log, rig.canvas, operation, workspace).has_value();
        });
  } else {
    appended = append_overlap_corpus(rig);
  }
  if (!appended || !rig.producer.sync_replay_index()) {
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
    for (const bool indexed : {false, true}) {
      const auto result = measure_scorecard_corpus(corpora[corpus], indexed);
      if (!result.has_value()) {
        std::fprintf(stderr, "scorecard failed corpus=%s mode=%s\n", names[corpus],
                     indexed ? "indexed" : "linear");
        return 1;
      }
      const auto& candidates = result->sweep.candidates;
      const double operations_per_group =
          candidates.groups_published == 0U ? 0.0
                                            : static_cast<double>(candidates.operations_scanned) /
                                                  static_cast<double>(candidates.groups_published);
      const auto& accounting = result->sweep.accounting;
      std::printf(
          "SCORE corpus=%s mode=%s ops_scanned=%zu ops_intersecting=%zu groups=%zu "
          "ops_per_group=%.2f wall_ms=%.3f attempts=%zu completions=%zu reuses=%zu "
          "discards=%zu amplification=%.3f exact=%u\n",
          names[corpus], indexed ? "indexed" : "linear", candidates.operations_scanned,
          candidates.operations_intersecting, candidates.groups_published, operations_per_group,
          result->sweep.wall_ms, accounting.attempts, accounting.completions, accounting.reuses,
          accounting.discards, accounting.amplification(), result->exact);
    }
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

bool append_hairline_stroke(Rig& rig, const v2::IncrementalDocumentWorkspace& workspace,
                            HairlineRandom& random, float radius, std::uint16_t color,
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
    if (!v2::append_incrementally(rig.log, rig.canvas,
                                  {.tool = tool,
                                   .color = color,
                                   .gesture_id = gesture_id,
                                   .samples = std::span(chunk.data(), count)},
                                  workspace)
             .has_value()) {
      return false;
    }
  }
  return true;
}

bool append_hairline_document(Rig& rig) {
  constexpr std::array<std::uint16_t, 6> kColors{0x0000U, 0x001FU, 0xF800U,
                                                 0x07E0U, 0x4208U, 0x8010U};
  const auto workspace = rig.workspace();
  HairlineRandom random;
  std::uint16_t gesture_id = 7'000;
  for (int stroke = 0; stroke < 220; ++stroke) {
    const bool eraser = (random.next() % 12U) == 0U;
    if (!append_hairline_stroke(rig, workspace, random, random.range(1.3F, 2.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? v2::OperationTool::kEraser : v2::OperationTool::kPen,
                                gesture_id++, random.range(300.0F, 1'400.0F))) {
      return false;
    }
  }
  for (int stroke = 0; stroke < 60; ++stroke) {
    if (!append_hairline_stroke(rig, workspace, random, random.range(3.5F, 4.7F),
                                kColors[random.next() % kColors.size()], v2::OperationTool::kPen,
                                gesture_id++, random.range(200.0F, 800.0F))) {
      return false;
    }
  }
  for (int stroke = 0; stroke < 10; ++stroke) {
    const bool eraser = stroke == 4 || stroke == 9;
    if (!append_hairline_stroke(rig, workspace, random, random.range(40.0F, 80.0F),
                                kColors[random.next() % kColors.size()],
                                eraser ? v2::OperationTool::kEraser : v2::OperationTool::kPen,
                                gesture_id++, random.range(800.0F, 2'000.0F))) {
      return false;
    }
  }
  return true;
}

int run_general_sweep(bool indexed, std::size_t runs) {
  Rig rig(1'024, 16'384, indexed);
  if (!rig.log.ready() || !rig.canvas.ready() || !rig.producer.ready()) {
    std::fprintf(stderr, "rig not ready\n");
    return 1;
  }
  if (!rig.reset_blank({1})) {
    std::fprintf(stderr, "baseline reset failed\n");
    return 1;
  }
  const auto workspace = rig.workspace();
  const bool appended =
      v2::test_support::emit_adversarial_tapered_corpus(
          [&](const v2::OperationAppend& operation) {
            return v2::append_incrementally(rig.log, rig.canvas, operation, workspace).has_value();
          },
          nullptr) &&
      append_hairline_document(rig);
  if (!appended) {
    std::fprintf(stderr, "corpus append failed\n");
    return 1;
  }
  std::printf("== general cold sweep mode=%s operations=%zu samples=%zu ==\n",
              indexed ? "indexed" : "linear", rig.log.operation_count(), rig.log.sample_count());
  constexpr std::array zooms{v2::ZoomLevel::k50Percent, v2::ZoomLevel::k100Percent,
                             v2::ZoomLevel::k200Percent, v2::ZoomLevel::k400Percent};
  constexpr std::array zoom_names{"50", "100", "200", "400"};
  for (std::size_t zoom_index = 0; zoom_index < zooms.size(); ++zoom_index) {
    const v2::ViewRequest zoom_view{
        .zoom = zooms[zoom_index],
        .level_pixels = {0, 0, v2::kOverviewWidth, v2::kOverviewHeight},
    };
    std::vector<double> zoom_walls;
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
    std::printf("GENERAL zoom=%s median_wall_ms=%.3f\n", zoom_names[zoom_index],
                zoom_walls[zoom_walls.size() / 2U]);
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
        "run=%zu steps=%zu tiles=%zu wall_ms=%.3f ops_scanned=%zu ops_intersecting=%zu "
        "groups=%zu\n",
        run, sweep.steps, sweep.tiles, sweep.wall_ms, sweep.candidates.operations_scanned,
        sweep.candidates.operations_intersecting, sweep.candidates.groups_published);
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

// ---------------------------------------------------------------------------

struct FuzzRig {
  std::vector<v2::OperationRecord> records;
  std::vector<v2::CompactOperationSample> samples;
  std::vector<std::uint16_t> overview;
  std::vector<v2::MaterializedSlotStorage> slots;
  std::vector<std::uint16_t> tile_pool;
  std::vector<std::uint16_t> supertask;
  std::vector<std::uint8_t> mask;
  std::vector<std::uint16_t> summary_rows;
  std::vector<std::uint32_t> summary_words;
  std::vector<std::uint32_t> chord_plans;
  std::vector<std::uint32_t> replay_index_words;
  v2::OperationLog log;
  v2::MaterializedCanvas canvas;
  v2::TileProducer producer;

  FuzzRig()
      : records(64),
        samples(4'096),
        overview(v2::kOverviewPixels, 0xFFFFU),
        slots(64),
        tile_pool(slots.size() * v2::kTilePixels),
        supertask(v2::kTileProducerPixels),
        mask(v2::kTileProducerMaskBytes),
        summary_rows(v2::kTileProducerSummaryRows),
        summary_words(v2::kTileProducerSummaryWords),
        chord_plans(v2::kOperationChordStorageBytes / 4U),
        replay_index_words(v2::kReplayIndexWords),
        log(records, samples),
        canvas(overview, slots, tile_pool),
        producer(log, canvas,
                 {.supertask_pixels = supertask,
                  .finalized_pixels = mask,
                  .summary_row_unset = summary_rows,
                  .summary_saturated_words = summary_words,
                  .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans)),
                  .replay_index_words = replay_index_words}) {}
};

bool replay_and_compare(FuzzRig& rig, const v2::ViewRequest& view, std::uint32_t case_index,
                        const char* label) {
  std::vector<std::uint16_t> refreshed(v2::kOverviewPixels, 0xFFFFU);
  if (!rig.canvas.publish_overview({rig.log.current_revision()}, refreshed)) {
    std::fprintf(stderr, "[%s %u] overview publish failed\n", label, case_index);
    return false;
  }
  std::size_t steps = 0;
  while (true) {
    const auto step = rig.producer.produce_next(view);
    if (!step.has_value()) {
      std::fprintf(stderr, "[%s %u] produce failed at step %zu\n", label, case_index, steps);
      return false;
    }
    ++steps;
    if (step->complete) {
      break;
    }
    if (steps > 200'000) {
      std::fprintf(stderr, "[%s %u] runaway\n", label, case_index);
      return false;
    }
  }
  const int width = view.level_pixels.x1 - view.level_pixels.x0;
  const int height = view.level_pixels.y1 - view.level_pixels.y0;
  std::vector<std::uint16_t> composed(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
  if (!rig.canvas.compose_view(view, composed).has_value()) {
    std::fprintf(stderr, "[%s %u] compose failed\n", label, case_index);
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
      std::fprintf(stderr, "[%s %u] forward replay failed\n", label, case_index);
      return false;
    }
  }
  if (composed != direct) {
    std::size_t mismatches = 0;
    std::size_t first_mismatch = 0;
    for (std::size_t i = 0; i < composed.size(); ++i) {
      if (composed[i] != direct[i]) {
        if (mismatches == 0) {
          first_mismatch = i;
        }
        ++mismatches;
      }
    }
    std::fprintf(
        stderr, "[%s %u] MISMATCH %zu pixels, first at (%d,%d) got=%04x want=%04x ops=%zu\n", label,
        case_index, mismatches, view.level_pixels.x0 + static_cast<int>(first_mismatch) % width,
        view.level_pixels.y0 + static_cast<int>(first_mismatch) / width, composed[first_mismatch],
        direct[first_mismatch], rig.log.operation_count());
    // Dump the document for reproduction.
    for (std::size_t index = 0; index < rig.log.operation_count(); ++index) {
      const auto operation = rig.log.operation(index);
      std::fprintf(stderr, "  op %zu tool=%d color=%04x:", index, static_cast<int>(operation->tool),
                   operation->color);
      for (const auto sample : operation->samples) {
        std::fprintf(stderr, " (%u,%u,r%u)", sample.x_quarter, sample.y_quarter, sample.radius_256);
      }
      std::fprintf(stderr, "\n");
    }
    return false;
  }
  return true;
}

int run_fuzz_collinear(std::uint32_t cases, std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uint32_t failures = 0;
  for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
    FuzzRig rig;
    // Random collinear run, constant radius, optional non-collinear tail/head.
    const int dir_x = static_cast<int>(rng() % 13U) - 6;
    const int dir_y = static_cast<int>(rng() % 13U) - 6;
    if (dir_x == 0 && dir_y == 0) {
      continue;
    }
    const int step_scale = 1 + static_cast<int>(rng() % 6U);
    const int run_length = 3 + static_cast<int>(rng() % 30U);
    const std::uint16_t radius = static_cast<std::uint16_t>(64U + rng() % 3'000U);
    const int origin_x = 200 + static_cast<int>(rng() % 600U);
    const int origin_y = 200 + static_cast<int>(rng() % 600U);
    std::vector<v2::CompactOperationSample> op_samples;
    const bool head = (rng() % 2U) == 0U;
    if (head) {
      op_samples.push_back({.x_quarter = static_cast<std::uint16_t>((origin_x - 40) * 4),
                            .y_quarter = static_cast<std::uint16_t>((origin_y + 30) * 4),
                            .radius_256 = radius});
    }
    for (int i = 0; i < run_length; ++i) {
      const int x = origin_x + i * dir_x * step_scale;
      const int y = origin_y + i * dir_y * step_scale;
      if (x < 0 || y < 0 || x > v2::kWorldWidth * 4 || y > v2::kWorldHeight * 4) {
        break;
      }
      op_samples.push_back({.x_quarter = static_cast<std::uint16_t>(x * 4),
                            .y_quarter = static_cast<std::uint16_t>(y * 4),
                            .radius_256 = radius,
                            .elapsed_ms = static_cast<std::uint16_t>(op_samples.size())});
    }
    if (op_samples.size() < 3U) {
      continue;
    }
    for (std::size_t i = 0; i < op_samples.size(); ++i) {
      op_samples[i].elapsed_ms = static_cast<std::uint16_t>(i);
    }
    if (!rig.log.append(
            {.tool = v2::OperationTool::kPen, .color = 0x001FU, .samples = op_samples})) {
      continue;
    }
    // View around the run at a random zoom, tile-aligned 128x128.
    constexpr std::array zooms{v2::ZoomLevel::k100Percent, v2::ZoomLevel::k200Percent,
                               v2::ZoomLevel::k400Percent};
    const v2::ZoomLevel zoom = zooms[rng() % zooms.size()];
    const int percent = v2::zoom_percent(zoom);
    const int level_x = (origin_x / 4) * percent / 100;
    const int level_y = (origin_y / 4) * percent / 100;
    const int level_width = v2::kWorldWidth * percent / 100;
    const int level_height = v2::kWorldHeight * percent / 100;
    const int view_x = std::clamp((level_x / 64) * 64 - 64, 0, std::max(0, level_width - 128));
    const int view_y = std::clamp((level_y / 64) * 64 - 64, 0, std::max(0, level_height - 128));
    const v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {view_x, view_y, std::min(view_x + 128, level_width),
                         std::min(view_y + 128, level_height)},
    };
    if (!replay_and_compare(rig, view, case_index, "collinear")) {
      ++failures;
      if (failures >= 5U) {
        return 1;
      }
    }
  }
  std::printf(failures == 0U ? "FUZZ_COLLINEAR_OK cases=%u\n" : "FUZZ_COLLINEAR_FAIL cases=%u\n",
              cases);
  return failures == 0U ? 0 : 1;
}

int run_fuzz_docs(std::uint32_t cases, std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uint32_t failures = 0;
  for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
    {
      std::mt19937 doc_rng(seed ^ (case_index * 2654435761U));
      FuzzRig rig;
      const std::size_t op_count = 2U + doc_rng() % 10U;
      bool appended = true;
      for (std::size_t op = 0; op < op_count && appended; ++op) {
        const std::size_t sample_count = 2U + doc_rng() % 24U;
        const bool eraser = doc_rng() % 5U == 0U;
        const bool constant_radius = doc_rng() % 3U == 0U;
        const std::uint16_t base_radius = static_cast<std::uint16_t>(96U + doc_rng() % 2'000U);
        int x = 300 + static_cast<int>(doc_rng() % 500U);
        int y = 300 + static_cast<int>(doc_rng() % 500U);
        std::vector<v2::CompactOperationSample> op_samples;
        for (std::size_t i = 0; i < sample_count; ++i) {
          op_samples.push_back(
              {.x_quarter = static_cast<std::uint16_t>(std::clamp(x, 0, v2::kWorldWidth * 4) * 4),
               .y_quarter = static_cast<std::uint16_t>(std::clamp(y, 0, v2::kWorldHeight * 4) * 4),
               .radius_256 = constant_radius ? base_radius
                                             : static_cast<std::uint16_t>(
                                                   64U + (base_radius + i * 173U) % 2'400U),
               .elapsed_ms = static_cast<std::uint16_t>(i * 4U)});
          x += static_cast<int>(doc_rng() % 90U) - 45;
          y += static_cast<int>(doc_rng() % 90U) - 45;
        }
        appended =
            rig.log
                .append({.tool = eraser ? v2::OperationTool::kEraser : v2::OperationTool::kPen,
                         .color = static_cast<std::uint16_t>(doc_rng() & 0xFFFFU),
                         .samples = op_samples})
                .has_value();
      }
      if (!appended) {
        continue;
      }
      const v2::ViewRequest view{
          .zoom = v2::ZoomLevel::k400Percent,
          .level_pixels = {256, 256, 512, 512},
      };
      if (!replay_and_compare(rig, view, case_index, "docs")) {
        ++failures;
        if (failures >= 5U) {
          return 1;
        }
      }
    }
  }
  std::printf(failures == 0U ? "FUZZ_DOCS_OK cases=%u\n" : "FUZZ_DOCS_FAIL cases=%u\n", cases);
  return failures == 0U ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--fuzz-collinear") == 0) {
    const auto cases = static_cast<std::uint32_t>(argc >= 3 ? std::atoi(argv[2]) : 2'000);
    const auto seed = static_cast<std::uint32_t>(argc >= 4 ? std::atoi(argv[3]) : 0xC0FFEE);
    return run_fuzz_collinear(cases, seed);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--fuzz-docs") == 0) {
    const auto cases = static_cast<std::uint32_t>(argc >= 3 ? std::atoi(argv[2]) : 400);
    const auto seed = static_cast<std::uint32_t>(argc >= 4 ? std::atoi(argv[3]) : 0xBEEF);
    return run_fuzz_docs(cases, seed);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--linear") == 0) {
    return run_adversarial_sweep(false);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--adversarial-ops") == 0) {
    const auto operation_count =
        static_cast<std::size_t>(argc >= 3 ? std::max(1, std::atoi(argv[2])) : 1);
    const auto samples_per_operation =
        static_cast<std::size_t>(argc >= 4 ? std::max(2, std::atoi(argv[3])) : 32);
    return run_adversarial_sweep(true, operation_count, samples_per_operation);
  }
  if (argc >= 2 && std::strcmp(argv[1], "--cold-scorecard") == 0) {
    return run_cold_scorecard();
  }
  if (argc >= 2 && std::strcmp(argv[1], "--general") == 0) {
    const bool linear = argc >= 3 && std::strcmp(argv[2], "linear") == 0;
    const auto runs = static_cast<std::size_t>(argc >= 4 ? std::max(1, std::atoi(argv[3])) : 5);
    return run_general_sweep(!linear, runs);
  }
  return run_adversarial_sweep();
}
