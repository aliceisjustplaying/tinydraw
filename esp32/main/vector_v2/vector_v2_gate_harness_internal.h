#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/adversarial_tapered_corpus.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/ink_trace.h"
#include "tinydraw/vector_v2/live_ink_coordinator.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/raster_census.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "vector_v2_app_diagnostics.h"
#include "vector_v2_gate_harness.h"
#include "vector_v2_live_stroke_session.h"
#include "vector_v2_ship_contract.h"

namespace tinydraw::esp32::gate_harness {

using vector_v2::CompactOperationSample;
using vector_v2::DocumentRevision;
using vector_v2::InPlaceAppendWorkspace;
using vector_v2::MaterializedCanvas;
using vector_v2::OperationLog;
using vector_v2::OperationTool;
using vector_v2::ZoomLevel;
namespace contract = vector_v2_ship_contract;

inline constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
inline constexpr std::uint32_t kStressOperations = 1'000;
inline constexpr std::uint32_t kStressSamplesPerOperation = 20;
inline constexpr std::size_t kRealisticStrokeCapacity = 1'000;
inline constexpr std::size_t kRealisticSampleCapacity = 24'576;
inline constexpr std::int64_t kMixedDrawAbsorbSliceBudgetUs = 1'500;
inline constexpr std::size_t kMixedDrawAbsorbRasterWorkPixels = 256U;
inline constexpr std::int64_t kMixedDrawAbsorbSliceGuardUs = 4'000;
inline constexpr std::int64_t kInkTraceAbsorbSliceBudgetUs = 1'500;
inline constexpr std::size_t kInkTraceAbsorbRasterWorkPixels = 256U;
inline constexpr std::int64_t kInkTraceAbsorbSliceGuardUs = 4'000;

struct MixedDrawAbsorbLimit {
  std::int64_t deadline_us = 0;

  static bool requested(const void* context) {
    return esp_timer_get_time() >= static_cast<const MixedDrawAbsorbLimit*>(context)->deadline_us;
  }
};

struct MixedDrawCensus {
  std::size_t raw = 0;
  std::size_t uniform = 0;
};

template <typename Type>
[[nodiscard]] std::unique_ptr<Type, decltype(&heap_caps_free)> allocate_external(
    std::size_t count) {
  return {static_cast<Type*>(heap_caps_malloc(count * sizeof(Type), kExternalCaps)),
          &heap_caps_free};
}

inline std::uint32_t now_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

template <typename Operation>
std::optional<vector_v2::IncrementalAppendResult> append_and_absorb(
    OperationLog& log, MaterializedCanvas& canvas, const Operation& operation,
    const InPlaceAppendWorkspace& workspace,
    std::optional<vector_v2::ViewRequest> priority_view = std::nullopt,
    vector_v2::InPlaceRetentionBudget budget = {}) {
  if (vector_v2::pending_operation_count(log, canvas) != 0U ||
      !vector_v2::append_authority_only(log, operation, budget).has_value()) {
    return std::nullopt;
  }
  return vector_v2::absorb_pending_operation(log, canvas, workspace, priority_view, budget);
}

[[gnu::noinline]] bool classify_minimap_navigation(VectorV2Presenter& presenter,
                                                   const vector_v2::ChromeState& chrome);
void print_gate_presentation(const char* kind, const VectorV2Presenter& presenter,
                             const LivePresentationTiming& timing);
bool load_realistic_document(OperationLog& log, MaterializedCanvas& canvas,
                             const InPlaceAppendWorkspace& workspace,
                             std::span<VectorStroke> stroke_storage,
                             std::span<StrokeSample> sample_storage,
                             std::span<CompactOperationSample> conversion_storage);

bool run_tearing_probe(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome);
bool run_cooperative_compose_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                  OperationLog& log, MaterializedCanvas& canvas,
                                  const InPlaceAppendWorkspace& workspace,
                                  const vector_v2::ChromeState& chrome);
bool run_tile_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                   OperationLog& log, MaterializedCanvas& canvas,
                   const vector_v2::ChromeState& chrome, ZoomLevel zoom);
bool run_paced_cold_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                         MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                         const vector_v2::ChromeState& chrome, ZoomLevel zoom, int level_x,
                         int level_y, const char* corpus, std::int64_t maximum_wall_us);
bool append_overlapping_scribble(OperationLog& log, MaterializedCanvas& canvas,
                                 const InPlaceAppendWorkspace& workspace);
bool append_adversarial_tapered_document(OperationLog& log, MaterializedCanvas& canvas,
                                         const InPlaceAppendWorkspace& workspace);
bool run_overlap_cold_gates(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                            MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                            const vector_v2::ChromeState& chrome);
bool run_general_cold_gates(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                            MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                            const vector_v2::ChromeState& chrome);

bool run_edge_ink_case(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                       OperationLog& log, MaterializedCanvas& canvas,
                       const vector_v2::ChromeState& chrome,
                       const InPlaceAppendWorkspace& workspace,
                       std::span<std::uint16_t> compose_scratch);
bool run_overlay_canvas_purity_gate(VectorV2Presenter& presenter, OperationLog& log,
                                    MaterializedCanvas& canvas,
                                    const vector_v2::ChromeState& chrome,
                                    const InPlaceAppendWorkspace& workspace);
bool run_live_ink_overlay_gate(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome);
bool run_draw_while_fill_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const InPlaceAppendWorkspace& workspace,
                              std::span<CompactOperationSample> interaction_samples);
bool run_long_gesture_commit_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                  OperationLog& log, MaterializedCanvas& canvas,
                                  const vector_v2::ChromeState& chrome,
                                  const InPlaceAppendWorkspace& workspace,
                                  std::span<const std::uint16_t> blank_snapshot,
                                  std::span<CompactOperationSample> builder_storage);
bool run_history_latency_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const InPlaceAppendWorkspace& workspace,
                              std::span<const std::uint16_t> blank_snapshot,
                              std::span<CompactOperationSample> builder_storage,
                              std::span<std::uint16_t> overview_scratch);
bool run_settle_timing_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                            OperationLog& log, MaterializedCanvas& canvas,
                            const vector_v2::ChromeState& chrome,
                            const vector_v2::SettledTileWorkspace& settle_workspace,
                            std::span<std::uint16_t> settle_pixels);
bool run_export_encode_gate(VectorV2Export& exporter, OperationLog& log);

bool run_cache_tour_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                         MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome);
bool run_mixed_zoom_draw_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const InPlaceAppendWorkspace& workspace,
                              std::span<CompactOperationSample> builder_storage);
MixedDrawCensus census_zoom_tiles(const MaterializedCanvas& canvas, ZoomLevel zoom);
const char* absorption_unit_name(vector_v2::PendingAbsorptionWorkUnit unit);
std::size_t count_zoom_fallback(MaterializedCanvas& canvas, ZoomLevel zoom);

bool verify_pan_adapter(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                        const vector_v2::ChromeState& chrome, ZoomLevel zoom);
bool run_ring_locality_gate(VectorV2Presenter& presenter, OperationLog& log,
                            MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome,
                            const InPlaceAppendWorkspace& workspace);
bool run_pan_sequence_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                           const vector_v2::ChromeState& chrome, ZoomLevel zoom);
bool run_pan_boundary_gate(VectorV2Presenter& presenter, const vector_v2::ChromeState& chrome,
                           ZoomLevel zoom);
bool run_cache_retention_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              MaterializedCanvas& canvas, const vector_v2::ChromeState& chrome);
bool run_full_world_cache_gate(vector_v2::TileProducer& producer, MaterializedCanvas& canvas);

bool append_general_cold_document(OperationLog& log, MaterializedCanvas& canvas,
                                  const InPlaceAppendWorkspace& workspace);
bool run_hairline_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                       OperationLog& log, MaterializedCanvas& canvas, VectorV2TouchSampler& touch,
                       const vector_v2::ChromeState& chrome,
                       const InPlaceAppendWorkspace& workspace,
                       std::span<const std::uint16_t> blank_snapshot);
bool run_idle_repair_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                          OperationLog& log, MaterializedCanvas& canvas,
                          const vector_v2::ChromeState& chrome,
                          const InPlaceAppendWorkspace& workspace,
                          std::span<CompactOperationSample> builder_storage);
bool verify_export_reserve();
bool append_stress_document(OperationLog& log, MaterializedCanvas& canvas,
                            const InPlaceAppendWorkspace& workspace);

bool run_ink_trace_replay_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                               OperationLog& log, MaterializedCanvas& canvas,
                               const vector_v2::ChromeState& chrome,
                               const InPlaceAppendWorkspace& in_place_workspace,
                               std::span<CompactOperationSample> builder_storage);

}  // namespace tinydraw::esp32::gate_harness
