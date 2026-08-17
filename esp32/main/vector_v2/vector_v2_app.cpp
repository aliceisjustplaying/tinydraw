#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <span>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#include "power_manager.h"
#include "rtc_clock.h"
#include "time_sync.h"
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
#include "vector_v2_gate_harness.h"
#ifdef TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE
#include "vector_v2_ink_trace_capture.h"
#endif
#endif
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/live_ink_coordinator.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "tinydraw/vector_v2/settled_tile.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_export.h"
#include "vector_v2_presenter.h"
#include "vector_v2_touch_sampler.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::ChainedOperationBuilder;
using vector_v2::ChainedOperationStatus;
using vector_v2::CompactOperationSample;
using vector_v2::DisplayScheduler;
using vector_v2::DisplayStrip;
using vector_v2::DocumentRevision;
using vector_v2::IncrementalDocumentWorkspace;
using vector_v2::MaterializedCanvas;
using vector_v2::MaterializedSlotStorage;
using vector_v2::MaterializedUniformStorage;
using vector_v2::OperationLog;
using vector_v2::OperationRecord;
using vector_v2::OperationTool;
using vector_v2::TileKey;
using vector_v2::TileRevisionPublication;
using vector_v2::ZoomLevel;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr gpio_num_t kModeButton = GPIO_NUM_0;
constexpr std::uint32_t kPowerRefreshUs = 30'000'000U;
constexpr std::size_t kInputSampleCapacity = 4'096;
constexpr std::size_t kWorkspaceTileCapacity = vector_v2::kMaximumVisibleTiles;

struct LiftBaselineTiming {
  std::uint32_t id = 0;
  std::int64_t detected_us = 0;
  std::int64_t finish_preview_us = 0;
  std::int64_t builder_finish_us = 0;
  std::int64_t append_us = 0;
  std::int64_t append_max_us = 0;
  std::int64_t refresh_wall_us = 0;
  vector_v2::PixelRect refresh_level_bounds{};
  std::int64_t stroke_logging_us = 0;
  LivePresentationTiming refresh{};
  std::uint32_t chunks = 0;
  bool committed = false;
  bool commit_failed = false;
  bool pending = false;
};

struct PendingFillPresentation {
  vector_v2::PixelRect level_bounds{};
  ZoomLevel zoom = ZoomLevel::k25Percent;
  int x = 0;
  int y = 0;
  bool pending = false;
};

struct FillBaselineTiming {
  std::int64_t started_us = 0;
  std::uint32_t steps = 0;
  std::int64_t compute_total_us = 0;
  std::int64_t compute_max_us = 0;
  std::int64_t present_total_us = 0;
  std::int64_t present_max_us = 0;
  std::int64_t tick_max_us = 0;
  std::uint32_t producer_failures = 0;
  std::uint32_t presentation_failures = 0;

  void reset() { *this = {}; }
};

struct PanMetrics {
  std::uint64_t compose_total_us = 0;
  std::uint64_t present_total_us = 0;
  std::uint64_t tear_wait_total_us = 0;
  std::uint32_t frames = 0;
  std::uint32_t reused_frames = 0;
  std::uint32_t compose_max_us = 0;
  std::uint32_t present_max_us = 0;
  std::uint32_t tear_wait_max_us = 0;
  std::uint32_t failures = 0;

  void include(const LivePresentationTiming& timing) {
    if (!timing.passed) {
      ++failures;
      return;
    }
    const auto compose = static_cast<std::uint32_t>(timing.compose_us);
    const auto present = static_cast<std::uint32_t>(timing.complete_us);
    const auto tear_wait = static_cast<std::uint32_t>(timing.tear_wait_us);
    compose_total_us += compose;
    present_total_us += present;
    tear_wait_total_us += tear_wait;
    ++frames;
    reused_frames += timing.frame_reused;
    compose_max_us = std::max(compose_max_us, compose);
    present_max_us = std::max(present_max_us, present);
    tear_wait_max_us = std::max(tear_wait_max_us, tear_wait);
  }

  void reset() { *this = {}; }
};

struct LiveMetrics {
  std::uint64_t submit_total_us = 0;
  std::uint64_t complete_total_us = 0;
  std::uint32_t samples = 0;
  std::uint32_t submit_max_us = 0;
  std::uint32_t complete_max_us = 0;
  std::uint32_t submit_over_16ms = 0;
  std::uint32_t complete_over_33ms = 0;
  std::uint32_t failures = 0;

  void include(const LivePresentationTiming& timing) {
    if (!timing.passed) {
      ++failures;
      return;
    }
    if (timing.first_submit_us <= 0 || timing.first_complete_us <= 0) {
      return;
    }
    const auto submit = static_cast<std::uint32_t>(timing.first_submit_us);
    const auto complete = static_cast<std::uint32_t>(timing.first_complete_us);
    submit_total_us += submit;
    complete_total_us += complete;
    ++samples;
    submit_max_us = std::max(submit_max_us, submit);
    complete_max_us = std::max(complete_max_us, complete);
    submit_over_16ms += submit > 16'667U;
    complete_over_33ms += complete > 33'333U;
  }
};

struct PendingStrokeReport {
  DocumentRevision revision{};
  std::size_t operation_count = 0;
  std::size_t sample_count = 0;
  std::int64_t append_us = 0;
  std::int64_t append_max_us = 0;
  LivePresentationTiming refresh{};
  LiveMetrics metrics{};
  std::uint32_t poll_max_us = 0;
  TouchSamplerMetrics touch{};
  vector_v2::InPlaceAppendPhases phase_max{};
  vector_v2::InPlaceRetainDrops drops{};
  std::uint32_t chunks = 0;
  std::size_t free_psram = 0;
  std::size_t largest_psram = 0;
  bool authority_match = false;
  bool committed = false;
  bool commit_failed = false;
  bool pending = false;
};

struct AppStorage {
  std::uint16_t* overview = nullptr;
  std::uint16_t* snapshot = nullptr;
  std::uint16_t* frame = nullptr;
  std::uint16_t* tile_pixels = nullptr;
  std::uint16_t* overview_scratch = nullptr;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // The transactional reference append used by harness corpus construction
  // still stages whole tiles; the product interactive path commits in place
  // and no longer funds this scratch.
  std::uint16_t* tile_scratch = nullptr;
#endif
  std::uint16_t* region_scratch = nullptr;
  std::uint16_t* chrome_cache = nullptr;
  std::uint16_t* producer_supertask = nullptr;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // Harness-only compose/census scratch for one 64x64 tile. The producer
  // publishes straight from the supertask surface and no longer stages here.
  std::uint16_t* harness_tile_scratch = nullptr;
#endif
  bool supertask_internal = false;
  std::uint8_t* producer_mask = nullptr;
  std::uint16_t* producer_summary_rows = nullptr;
  std::uint32_t* producer_summary_words = nullptr;
  std::uint32_t* producer_chord_plans = nullptr;
  std::uint8_t* chunk_mask = nullptr;
  // Settled-AA workspace (40 KiB PSRAM — exactly five 8 KiB dcache ways, so
  // downstream allocations keep their measured cache sets; internal SRAM is
  // off-limits here per the panel-init DMA razor note below): per-op union
  // alpha, accumulated alpha, exact 16-bit channel accumulators, and the
  // output staging tile. Settling is idle-budget work; PSRAM latency is
  // acceptable.
  std::uint8_t* settle_op_alpha = nullptr;
  std::uint8_t* settle_accumulated = nullptr;
  std::uint16_t* settle_red = nullptr;
  std::uint16_t* settle_green = nullptr;
  std::uint16_t* settle_blue = nullptr;
  std::uint16_t* settle_pixels = nullptr;
  MaterializedUniformStorage* uniforms = nullptr;
  std::uint8_t* occupancy = nullptr;
  MaterializedSlotStorage* slots = nullptr;
  std::uint16_t* raw_slot_directory = nullptr;
  bool slot_directory_internal = false;
  OperationRecord* records = nullptr;
  CompactOperationSample* samples = nullptr;
  CompactOperationSample* input_samples = nullptr;
  vector_v2::RerenderLedgerEntry* rerender_entries = nullptr;
  vector_v2::TouchEvent* touch_events = nullptr;
#ifdef TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE
  InkTraceCaptureRecord* ink_trace_records = nullptr;
#endif
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  TileRevisionPublication* publications = nullptr;
#endif
  TileKey* affected_keys = nullptr;

  [[nodiscard]] bool allocate() {
    overview = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    snapshot = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    frame = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    tile_pixels = allocate_array<std::uint16_t>(vector_v2::kTileSlotCount * vector_v2::kTilePixels);
    overview_scratch = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
    tile_scratch = allocate_array<std::uint16_t>(kWorkspaceTileCapacity * vector_v2::kTilePixels);
#endif
    region_scratch = allocate_array<std::uint16_t>(kLiveRegionScratchPixels);
    chrome_cache = allocate_array<std::uint16_t>(vector_v2::kChromeStagingCachePixels);
    // The producer paint scratch is the hottest pixel memory in cold replay:
    // every group starts with a 32 KiB fill and ends with a 32 KiB publish
    // read, with masked span writes in between. Internal SRAM removes the
    // PSRAM round-trips; the PSRAM fallback keeps allocation infallible.
    producer_supertask = allocate_internal<std::uint16_t>(vector_v2::kTileProducerPixels);
    supertask_internal = producer_supertask != nullptr;
    if (producer_supertask == nullptr) {
      producer_supertask = allocate_array<std::uint16_t>(vector_v2::kTileProducerPixels);
    }
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
    harness_tile_scratch = allocate_internal<std::uint16_t>(vector_v2::kTilePixels);
    if (harness_tile_scratch == nullptr) {
      harness_tile_scratch = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
    }
#endif
    producer_mask = allocate_internal<std::uint8_t>(vector_v2::kTileProducerMaskBytes);
    producer_summary_rows = allocate_internal<std::uint16_t>(vector_v2::kTileProducerSummaryRows);
    producer_summary_words = allocate_internal<std::uint32_t>(vector_v2::kTileProducerSummaryWords);
    // One operation's prepared chord batch (H7 sweep), read once per chord
    // per swept row: internal SRAM keeps it off the PSRAM dcache path.
    producer_chord_plans =
        allocate_internal<std::uint32_t>(vector_v2::kOperationChordStorageBytes / 4U);
    chunk_mask = allocate_internal<std::uint8_t>(vector_v2::kInPlaceTileMaskBytes);
    uniforms =
        allocate_array<MaterializedUniformStorage>(vector_v2::kMaterializedTileIdentityCount);
    occupancy = allocate_array<std::uint8_t>(vector_v2::kOccupancyBytes);
    slots = allocate_array<MaterializedSlotStorage>(vector_v2::kTileSlotCount);
    records = allocate_array<OperationRecord>(vector_v2::kOperationCapacity);
    samples = allocate_array<CompactOperationSample>(vector_v2::kOperationSampleCapacity);
    input_samples = allocate_array<CompactOperationSample>(kInputSampleCapacity);
    rerender_entries =
        allocate_array<vector_v2::RerenderLedgerEntry>(vector_v2::kRerenderLedgerEntryCount);
    touch_events = allocate_internal<vector_v2::TouchEvent>(kVectorV2TouchEventCapacity);
#ifdef TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE
    ink_trace_records = allocate_array<InkTraceCaptureRecord>(kInkTraceCaptureCapacity);
    const bool ink_trace_ready = ink_trace_records != nullptr;
#else
    const bool ink_trace_ready = true;
#endif
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
    publications = allocate_array<TileRevisionPublication>(kWorkspaceTileCapacity);
    const bool harness_workspace_ready =
        tile_scratch != nullptr && publications != nullptr && harness_tile_scratch != nullptr;
#else
    const bool harness_workspace_ready = true;
#endif
    affected_keys =
        allocate_array<TileKey>(vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles);
    // O(1) find_tile metadata (27,384 B used), allocated LAST and padded to
    // 32 KiB. Placement receipts (2026-08-16): internal SRAM shifted the
    // panel-init DMA staging buffers (+50 us pan strip staging, pan_seq
    // red); an unpadded PSRAM allocation shifted every later heap address by
    // 27.4 KB onto presentation-hostile dcache sets (+40 us strip staging in
    // seven straight build variants). Padding to a multiple of the 8 KiB
    // dcache way size keeps downstream allocations on the same cache sets
    // that every pan optical receipt was measured on.
    constexpr std::size_t kDirectoryPaddedEntries = (32U * 1024U) / sizeof(std::uint16_t);
    static_assert(kDirectoryPaddedEntries >= vector_v2::kMaterializedTileIdentityCount);
    raw_slot_directory = allocate_array<std::uint16_t>(kDirectoryPaddedEntries);
    // Settled-AA workspace, allocated dead LAST so it shifts no other
    // allocation's cache sets (the first settle-build battery measured the
    // 400% cold wall +9 ms with these placed mid-heap).
    settle_op_alpha = allocate_array<std::uint8_t>(vector_v2::kTilePixels);
    settle_accumulated = allocate_array<std::uint8_t>(vector_v2::kTilePixels);
    settle_red = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
    settle_green = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
    settle_blue = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
    settle_pixels = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
    slot_directory_internal = false;
    if (overview == nullptr || snapshot == nullptr || frame == nullptr || tile_pixels == nullptr ||
        overview_scratch == nullptr || !harness_workspace_ready || region_scratch == nullptr ||
        chrome_cache == nullptr || producer_supertask == nullptr || producer_mask == nullptr ||
        producer_summary_rows == nullptr || producer_summary_words == nullptr ||
        producer_chord_plans == nullptr || chunk_mask == nullptr || uniforms == nullptr ||
        occupancy == nullptr || slots == nullptr || raw_slot_directory == nullptr ||
        records == nullptr || samples == nullptr || input_samples == nullptr ||
        rerender_entries == nullptr || touch_events == nullptr || !ink_trace_ready ||
        affected_keys == nullptr || settle_op_alpha == nullptr || settle_accumulated == nullptr ||
        settle_red == nullptr || settle_green == nullptr || settle_blue == nullptr ||
        settle_pixels == nullptr) {
      return false;
    }
    for (std::size_t index = 0; index < vector_v2::kMaterializedTileIdentityCount; ++index) {
      std::construct_at(uniforms + index);
    }
    for (std::size_t index = 0; index < vector_v2::kTileSlotCount; ++index) {
      std::construct_at(slots + index);
    }
    return true;
  }

 private:
  template <typename Type>
  [[nodiscard]] static Type* allocate_array(std::size_t count) {
    return static_cast<Type*>(heap_caps_malloc(count * sizeof(Type), kExternalCaps));
  }

  template <typename Type>
  [[nodiscard]] static Type* allocate_internal(std::size_t count) {
    return static_cast<Type*>(
        heap_caps_malloc(count * sizeof(Type), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
};

std::uint32_t now_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

const char* reject_name(vector_v2::OperationBuilderReject reject) {
  switch (reject) {
    case vector_v2::OperationBuilderReject::kNone:
      return "none";
    case vector_v2::OperationBuilderReject::kNotActive:
      return "not_active";
    case vector_v2::OperationBuilderReject::kInvalidPoint:
      return "invalid_point";
    case vector_v2::OperationBuilderReject::kTimestampRegression:
      return "timestamp_regression";
    case vector_v2::OperationBuilderReject::kElapsedOverflow:
      return "elapsed_overflow";
    case vector_v2::OperationBuilderReject::kCapacityOverflow:
      return "capacity_overflow";
  }
  return "unknown";
}

void print_stroke_rejected(const char* site, const ChainedOperationBuilder& builder,
                           vector_v2::OperationPoint point) {
  std::printf(
      "TINYDRAW_STROKE_REJECTED site=%s reason=%s samples=%lu x=%.3f y=%.3f radius=%.5f "
      "timestamp_us=%lu\n",
      site, reject_name(builder.last_reject()), static_cast<unsigned long>(builder.sample_count()),
      static_cast<double>(point.world_x), static_cast<double>(point.world_y),
      static_cast<double>(point.radius), static_cast<unsigned long>(point.timestamp_us));
  std::fflush(stdout);
}

void include_bounds(std::optional<vector_v2::PixelRect>& accumulated, vector_v2::PixelRect bounds) {
  if (!accumulated.has_value()) {
    accumulated = bounds;
    return;
  }
  accumulated->x0 = std::min(accumulated->x0, bounds.x0);
  accumulated->y0 = std::min(accumulated->y0, bounds.y0);
  accumulated->x1 = std::max(accumulated->x1, bounds.x1);
  accumulated->y1 = std::max(accumulated->y1, bounds.y1);
}

std::optional<vector_v2::ViewRequest> current_priority_view(const VectorV2Presenter& presenter) {
  if (presenter.zoom() == ZoomLevel::k25Percent) {
    return std::nullopt;
  }
  return vector_v2::ViewRequest{
      .zoom = presenter.zoom(),
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
}

std::optional<vector_v2::IncrementalAppendResult> commit_pending_chunk(
    ChainedOperationBuilder& builder, OperationLog& log, MaterializedCanvas& canvas,
    const vector_v2::InPlaceAppendWorkspace& workspace, const VectorV2Presenter& presenter) {
  const auto append = builder.pending_append();
  if (!append.has_value()) {
    return std::nullopt;
  }
  // Deferred commit (VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md §3): the input
  // path publishes authority only; idle slices absorb the pending range and
  // presentation patches pending ink into every compose. The high-water
  // fallback bounds the range (and the per-present overlay cost) by paying
  // one synchronous absorption — today's behavior as the worst case.
  if (vector_v2::pending_operation_count(log, canvas) >= kPendingOperationHighWater) {
    static_cast<void>(vector_v2::absorb_pending_operation(
        log, canvas, workspace, current_priority_view(presenter),
        {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs}));
  }
  return vector_v2::append_authority_only(log, *append, {.now_us = &esp_timer_get_time});
}

void include_phase_maxima(vector_v2::InPlaceAppendPhases& maxima,
                          const vector_v2::InPlaceAppendPhases& sample) {
  maxima.prepare_us = std::max(maxima.prepare_us, sample.prepare_us);
  maxima.overview_us = std::max(maxima.overview_us, sample.overview_us);
  maxima.enumerate_us = std::max(maxima.enumerate_us, sample.enumerate_us);
  maxima.uniform_retain_us = std::max(maxima.uniform_retain_us, sample.uniform_retain_us);
  maxima.raw_retain_us = std::max(maxima.raw_retain_us, sample.raw_retain_us);
  maxima.offscreen_retain_us = std::max(maxima.offscreen_retain_us, sample.offscreen_retain_us);
  maxima.commit_us = std::max(maxima.commit_us, sample.commit_us);
}

void include_retain_drops(vector_v2::InPlaceRetainDrops& total,
                          const vector_v2::InPlaceRetainDrops& sample) {
  total.visible_uniform_no_slot += sample.visible_uniform_no_slot;
  total.visible_uniform_paint_fail += sample.visible_uniform_paint_fail;
  total.visible_raw_edit_fail += sample.visible_raw_edit_fail;
  total.visible_raw_paint_fail += sample.visible_raw_paint_fail;
  total.offscreen_skipped += sample.offscreen_skipped;
}

std::optional<ChainedOperationStatus> commit_ready_chunk(
    ChainedOperationBuilder& builder, OperationLog& log, MaterializedCanvas& canvas,
    const vector_v2::InPlaceAppendWorkspace& workspace, const VectorV2Presenter& presenter,
    std::optional<vector_v2::PixelRect>& accumulated_bounds, std::uint32_t& chunks,
    std::int64_t& append_us, std::int64_t& append_max_us,
    vector_v2::InPlaceAppendPhases& phase_maxima, vector_v2::InPlaceRetainDrops& drops) {
  const std::int64_t started_us = esp_timer_get_time();
  const auto committed = commit_pending_chunk(builder, log, canvas, workspace, presenter);
  const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
  append_us += elapsed_us;
  append_max_us = std::max(append_max_us, elapsed_us);
  if (!committed.has_value()) {
    return std::nullopt;
  }
  include_phase_maxima(phase_maxima, committed->phases);
  include_retain_drops(drops, committed->drops);
  include_bounds(accumulated_bounds, committed->affected_world_bounds);
  ++chunks;
  return builder.acknowledge_commit();
}

const char* zoom_name(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return "25";
    case ZoomLevel::k50Percent:
      return "50";
    case ZoomLevel::k100Percent:
      return "100";
    case ZoomLevel::k200Percent:
      return "200";
    case ZoomLevel::k400Percent:
      return "400";
  }
  return "unknown";
}

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld scroll_us=%lld "
      "exposed_compose_us=%lld chrome_us=%lld chrome_prepare_us=%lld chrome_stage_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld "
      "transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu pushes=%lu tear_wait_us=%lld tear_edge_isr_to_resume_us=%lu "
      "tear_edge_observed=%u tear_edge_wait_resumed=%u tear_edge_timeout=%u "
      "tear_heal_attempted=%u "
      "tear_heal_command_sent=%u presentation_experiment=%s te_edge=%s "
      "requested_clock_mhz=%d effective_clock_mhz=%d "
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
      static_cast<unsigned long>(timing.fallback_tiles), static_cast<unsigned long>(timing.pushes),
      static_cast<long long>(timing.tear_wait_us),
      static_cast<unsigned long>(timing.tear_edge_isr_to_resume_us), timing.tear_edge_observed,
      timing.tear_edge_wait_resumed, timing.tear_edge_timed_out, timing.tear_heal_attempted,
      timing.tear_heal_command_sent, presentation_experiment_name(), selected_tear_edge_name(),
      requested_panel_clock_mhz(), effective_panel_clock_mhz(), timing.frame_reused, timing.passed);
}

void print_pan_baseline(const VectorV2Presenter& presenter, const PanMetrics& metrics) {
  std::printf(
      "TINYDRAW_PAN_BASELINE zoom=%s x=%d y=%d frames=%lu reused=%lu "
      "compose_avg_us=%llu compose_max_us=%lu present_avg_us=%llu present_max_us=%lu "
      "tear_wait_avg_us=%llu tear_wait_max_us=%lu failures=%lu\n",
      zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<unsigned long>(metrics.frames), static_cast<unsigned long>(metrics.reused_frames),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.compose_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.compose_max_us),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.present_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.present_max_us),
      static_cast<unsigned long long>(
          metrics.frames == 0U ? 0U : metrics.tear_wait_total_us / metrics.frames),
      static_cast<unsigned long>(metrics.tear_wait_max_us),
      static_cast<unsigned long>(metrics.failures));
}

void print_fill_baseline(const char* result, ZoomLevel zoom, int x, int y,
                         DocumentRevision revision, const FillBaselineTiming& timing) {
  if (timing.steps == 0U) {
    return;
  }
  std::printf(
      "TINYDRAW_FILL_BASELINE result=%s zoom=%s x=%d y=%d revision=%lu steps=%lu "
      "wall_us=%lld compute_total_us=%lld compute_max_us=%lld present_total_us=%lld "
      "present_max_us=%lld tick_max_us=%lld producer_failures=%lu "
      "presentation_failures=%lu\n",
      result, zoom_name(zoom), x, y, static_cast<unsigned long>(revision.value),
      static_cast<unsigned long>(timing.steps),
      static_cast<long long>(timing.started_us == 0 ? 0 : esp_timer_get_time() - timing.started_us),
      static_cast<long long>(timing.compute_total_us),
      static_cast<long long>(timing.compute_max_us),
      static_cast<long long>(timing.present_total_us),
      static_cast<long long>(timing.present_max_us), static_cast<long long>(timing.tick_max_us),
      static_cast<unsigned long>(timing.producer_failures),
      static_cast<unsigned long>(timing.presentation_failures));
}

void print_lift_baseline(const LiftBaselineTiming& timing, std::int64_t poll_started_us,
                         std::int64_t poll_completed_us, std::uint32_t reports_dropped) {
  const std::int64_t measured_phase_us = timing.finish_preview_us + timing.builder_finish_us +
                                         timing.append_us + timing.refresh_wall_us +
                                         timing.stroke_logging_us;
  const std::int64_t detected_to_poll_us = poll_started_us - timing.detected_us;
  std::printf(
      "TINYDRAW_LIFT_BASELINE id=%lu finish_preview_us=%lld builder_finish_us=%lld "
      "append_us=%lld append_max_us=%lld refresh_wall_us=%lld refresh_x0=%d refresh_y0=%d "
      "refresh_x1=%d refresh_y1=%d refresh_compose_us=%lld "
      "refresh_first_submit_us=%lld refresh_first_complete_us=%lld "
      "refresh_transfer_wait_us=%lld stroke_logging_us=%lld "
      "detected_to_poll_start_us=%lld detected_to_poll_complete_us=%lld poll_read_us=%lld "
      "unattributed_tail_us=%lld reports_dropped=%lu chunks=%lu committed=%u refresh=%u "
      "commit_failed=%u\n",
      static_cast<unsigned long>(timing.id), static_cast<long long>(timing.finish_preview_us),
      static_cast<long long>(timing.builder_finish_us), static_cast<long long>(timing.append_us),
      static_cast<long long>(timing.append_max_us), static_cast<long long>(timing.refresh_wall_us),
      timing.refresh_level_bounds.x0, timing.refresh_level_bounds.y0,
      timing.refresh_level_bounds.x1, timing.refresh_level_bounds.y1,
      static_cast<long long>(timing.refresh.compose_us),
      static_cast<long long>(timing.refresh.first_submit_us),
      static_cast<long long>(timing.refresh.first_complete_us),
      static_cast<long long>(timing.refresh.complete_us),
      static_cast<long long>(timing.stroke_logging_us), static_cast<long long>(detected_to_poll_us),
      static_cast<long long>(poll_completed_us - timing.detected_us),
      static_cast<long long>(poll_completed_us - poll_started_us),
      static_cast<long long>(detected_to_poll_us - measured_phase_us),
      static_cast<unsigned long>(reports_dropped), static_cast<unsigned long>(timing.chunks),
      timing.committed, timing.refresh.passed, timing.commit_failed);
}

void print_stroke(const PendingStrokeReport& report) {
  std::printf(
      "TINYDRAW_LIVE_STROKE revision=%lu operations=%lu samples=%lu append_us=%lld "
      "append_max_us=%lld refresh_compose_us=%lld refresh_complete_us=%lld ink_samples=%lu "
      "read_submit_avg_us=%llu read_submit_max_us=%lu read_complete_avg_us=%llu "
      "read_complete_max_us=%lu submit_over_16ms=%lu complete_over_33ms=%lu "
      "presentation_failures=%lu poll_max_us=%lu touch_errors=%lu touch_overflows=%lu "
      "touch_moves_coalesced=%lu touch_events=%lu touch_down=%lu touch_up=%lu "
      "touch_events_ge_8ms=%lu touch_event_age_max_us=%lu chunks=%lu "
      "ph_prepare_max_us=%lld ph_overview_max_us=%lld ph_enumerate_max_us=%lld "
      "ph_uniform_max_us=%lld ph_raw_max_us=%lld ph_offscreen_max_us=%lld "
      "ph_commit_max_us=%lld "
      "drop_uni_slot=%lu drop_uni_paint=%lu drop_raw_edit=%lu drop_raw_paint=%lu "
      "off_skip=%lu free_psram=%lu "
      "largest_psram=%lu authority_match=%u\n",
      static_cast<unsigned long>(report.revision.value),
      static_cast<unsigned long>(report.operation_count),
      static_cast<unsigned long>(report.sample_count), static_cast<long long>(report.append_us),
      static_cast<long long>(report.append_max_us),
      static_cast<long long>(report.refresh.compose_us),
      static_cast<long long>(report.refresh.complete_us),
      static_cast<unsigned long>(report.metrics.samples),
      static_cast<unsigned long long>(
          report.metrics.samples == 0U ? 0U
                                       : report.metrics.submit_total_us / report.metrics.samples),
      static_cast<unsigned long>(report.metrics.submit_max_us),
      static_cast<unsigned long long>(
          report.metrics.samples == 0U ? 0U
                                       : report.metrics.complete_total_us / report.metrics.samples),
      static_cast<unsigned long>(report.metrics.complete_max_us),
      static_cast<unsigned long>(report.metrics.submit_over_16ms),
      static_cast<unsigned long>(report.metrics.complete_over_33ms),
      static_cast<unsigned long>(report.metrics.failures),
      static_cast<unsigned long>(report.poll_max_us),
      static_cast<unsigned long>(report.touch.errors),
      static_cast<unsigned long>(report.touch.queue_overflows),
      static_cast<unsigned long>(report.touch.moves_coalesced),
      static_cast<unsigned long>(report.touch.events_consumed),
      static_cast<unsigned long>(report.touch.down_events),
      static_cast<unsigned long>(report.touch.up_events),
      static_cast<unsigned long>(report.touch.events_at_least_8ms_old),
      static_cast<unsigned long>(report.touch.maximum_event_age_us),
      static_cast<unsigned long>(report.chunks),
      static_cast<long long>(report.phase_max.prepare_us),
      static_cast<long long>(report.phase_max.overview_us),
      static_cast<long long>(report.phase_max.enumerate_us),
      static_cast<long long>(report.phase_max.uniform_retain_us),
      static_cast<long long>(report.phase_max.raw_retain_us),
      static_cast<long long>(report.phase_max.offscreen_retain_us),
      static_cast<long long>(report.phase_max.commit_us),
      static_cast<unsigned long>(report.drops.visible_uniform_no_slot),
      static_cast<unsigned long>(report.drops.visible_uniform_paint_fail),
      static_cast<unsigned long>(report.drops.visible_raw_edit_fail),
      static_cast<unsigned long>(report.drops.visible_raw_paint_fail),
      static_cast<unsigned long>(report.drops.offscreen_skipped),
      static_cast<unsigned long>(report.free_psram),
      static_cast<unsigned long>(report.largest_psram), report.authority_match);
}

struct ExportProgressContext {
  vector_v2::ChromeState* chrome = nullptr;
  VectorV2Presenter* presenter = nullptr;
  int last_percentage = 0;
};

void present_export_progress(std::size_t completed_operations, std::size_t total_operations,
                             void* raw_context) {
  auto& context = *static_cast<ExportProgressContext*>(raw_context);
  if (context.chrome == nullptr || context.presenter == nullptr || total_operations == 0U) {
    return;
  }
  const int percentage =
      static_cast<int>(std::min<std::size_t>(completed_operations * 100U / total_operations, 100U));
  if (percentage < 100 && percentage < context.last_percentage + 5) {
    return;
  }
  context.last_percentage = percentage;
  context.chrome->export_progress = static_cast<std::uint8_t>(percentage);
  const auto timing = context.presenter->refresh(*context.chrome, now_us());
  print_presentation("export-progress", *context.presenter, timing);
  // Encoding is intentionally blocking. Five-percent UI steps avoid making
  // panel refreshes dominate the now-fast path export; the flash sink also
  // yields periodically for maximum-capacity documents.
  vTaskDelay(pdMS_TO_TICKS(1));
}

bool run_export(VectorV2Export& exporter, const OperationLog& log, vector_v2::ChromeState& chrome,
                VectorV2Presenter& presenter) {
  chrome.popup = vector_v2::ChromePopup::kNone;
  chrome.confirm_new = false;
  chrome.export_status = vector_v2::ChromeExportStatus::kSaving;
  chrome.export_progress = 0;
  const auto started = presenter.refresh(chrome, now_us());
  print_presentation("export-start", presenter, started);

  exporter.prepare_reencode();
  ExportProgressContext progress{.chrome = &chrome, .presenter = &presenter};
  const VectorV2ExportStats stats = exporter.encode(log, present_export_progress, &progress);
  chrome.export_progress = stats.encoded ? 100 : chrome.export_progress;
  chrome.export_status =
      stats.encoded ? vector_v2::ChromeExportStatus::kSaved : vector_v2::ChromeExportStatus::kError;
  const auto finished = presenter.refresh(chrome, now_us());
  print_presentation("export-finish", presenter, finished);
  std::printf(
      "TINYDRAW_V2_EXPORT format=svg encoded=%u bytes=%lu elapsed_us=%lld "
      "workspace_bytes=%lu operations=%lu sink_calls=%lu flash_pages=%lu crc32=%08lx "
      "free_psram=%lu free_internal=%lu usb_attempt=%u\n",
      stats.encoded, static_cast<unsigned long>(stats.bytes),
      static_cast<long long>(stats.elapsed_us), static_cast<unsigned long>(stats.workspace_bytes),
      static_cast<unsigned long>(stats.operation_count),
      static_cast<unsigned long>(stats.sink_calls), static_cast<unsigned long>(stats.flash_pages),
      static_cast<unsigned long>(stats.content_crc32),
      static_cast<unsigned long>(stats.free_psram_after),
      static_cast<unsigned long>(stats.free_internal_after), stats.encoded);
  std::fflush(stdout);
  if (!stats.encoded) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(150));
  const bool usb_ready = exporter.present_usb();
  if (!usb_ready) {
    chrome.export_status = vector_v2::ChromeExportStatus::kError;
    static_cast<void>(presenter.refresh(chrome, now_us()));
  }
  return usb_ready;
}

vector_v2::ChromeTimeSyncStatus chrome_time_sync_status(TimeSyncStatus status) {
  switch (status) {
    case TimeSyncStatus::kIdle:
      return vector_v2::ChromeTimeSyncStatus::kIdle;
    case TimeSyncStatus::kConnecting:
      return vector_v2::ChromeTimeSyncStatus::kConnecting;
    case TimeSyncStatus::kSynchronizing:
      return vector_v2::ChromeTimeSyncStatus::kSynchronizing;
    case TimeSyncStatus::kSucceeded:
      return vector_v2::ChromeTimeSyncStatus::kSaved;
    case TimeSyncStatus::kFailed:
      return vector_v2::ChromeTimeSyncStatus::kError;
  }
  return vector_v2::ChromeTimeSyncStatus::kError;
}

bool apply_chrome_action(vector_v2::ChromeAction action, Point point,
                         vector_v2::ChromeState& chrome, OperationLog& log,
                         MaterializedCanvas& canvas, vector_v2::TileProducer& producer,
                         std::span<const std::uint16_t> blank_snapshot,
                         VectorV2Presenter& presenter, VectorV2Export& exporter,
                         TimeSyncController& time_sync) {
  const auto toggle = [&](vector_v2::ChromePopup popup) {
    chrome.popup = chrome.popup == popup ? vector_v2::ChromePopup::kNone : popup;
  };
  const bool palette_only_refresh = action == vector_v2::ChromeAction::kSelectColor ||
                                    action == vector_v2::ChromeAction::kToggleColors ||
                                    action == vector_v2::ChromeAction::kPreviousPalette ||
                                    action == vector_v2::ChromeAction::kNextPalette;
  switch (action) {
    case vector_v2::ChromeAction::kSelectDraw:
      chrome.tool = vector_v2::ChromeTool::kDraw;
      chrome.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSelectErase:
      chrome.tool = vector_v2::ChromeTool::kErase;
      chrome.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSelectPan:
      chrome.tool = vector_v2::ChromeTool::kPan;
      chrome.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSelectColor:
      if (const auto color = vector_v2::chrome_color_at({point.x, point.y}, chrome);
          color.has_value()) {
        chrome.color_index = *color;
        chrome.tool = vector_v2::ChromeTool::kDraw;
        chrome.popup = vector_v2::ChromePopup::kNone;
      }
      break;
    case vector_v2::ChromeAction::kToggleTools:
      toggle(vector_v2::ChromePopup::kTools);
      break;
    case vector_v2::ChromeAction::kToggleColors:
      toggle(vector_v2::ChromePopup::kColors);
      break;
    case vector_v2::ChromeAction::kToggleSizes:
      toggle(vector_v2::ChromePopup::kSizes);
      break;
    case vector_v2::ChromeAction::kToggleDocument:
      toggle(vector_v2::ChromePopup::kDocument);
      break;
    case vector_v2::ChromeAction::kSelectSmall:
    case vector_v2::ChromeAction::kSelectMedium:
    case vector_v2::ChromeAction::kSelectLarge:
    case vector_v2::ChromeAction::kSelectExtraLarge:
      chrome.size =
          action == vector_v2::ChromeAction::kSelectSmall    ? vector_v2::ChromeSize::kSmall
          : action == vector_v2::ChromeAction::kSelectMedium ? vector_v2::ChromeSize::kMedium
          : action == vector_v2::ChromeAction::kSelectLarge  ? vector_v2::ChromeSize::kLarge
                                                             : vector_v2::ChromeSize::kExtraLarge;
      chrome.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kPreviousPalette:
      chrome.palette_page = 0;
      break;
    case vector_v2::ChromeAction::kNextPalette:
      chrome.palette_page = 1;
      break;
    case vector_v2::ChromeAction::kNewDrawing:
      chrome.popup = vector_v2::ChromePopup::kNone;
      chrome.confirm_new = true;
      break;
    case vector_v2::ChromeAction::kCancelNewDrawing:
      chrome.confirm_new = false;
      break;
    case vector_v2::ChromeAction::kConfirmNewDrawing: {
      // UINT32_MAX is the declared terminal revision; never wrap past it.
      if (canvas.current_revision().value >= std::numeric_limits<std::uint32_t>::max() - 1U) {
        chrome.confirm_new = false;
        break;
      }
      const DocumentRevision revision{canvas.current_revision().value + 1U};
      if (!vector_v2::restore_document_snapshot(log, canvas, revision, blank_snapshot) ||
          !producer.reset_uniform_baseline(revision)) {
        return false;
      }
      chrome.confirm_new = false;
      chrome.popup = vector_v2::ChromePopup::kNone;
      break;
    }
    case vector_v2::ChromeAction::kExport:
      // Blocking by design, like Raster V1. Progress is operation-based;
      // activating USB then ends serial until the next reset.
      return chrome.can_export && run_export(exporter, log, chrome, presenter);
    case vector_v2::ChromeAction::kSyncTime:
      chrome.popup = vector_v2::ChromePopup::kNone;
      if (chrome.can_sync_time) {
        static_cast<void>(time_sync.start());
        chrome.time_sync_status = chrome_time_sync_status(time_sync.status());
      }
      break;
    case vector_v2::ChromeAction::kZoomIn: {
      const ZoomLevel target = vector_v2::next_zoom(presenter.zoom());
      if (target == presenter.zoom()) {
        return true;
      }
      const auto timing = presenter.set_zoom(target, chrome, now_us());
      print_presentation("zoom-ui", presenter, timing);
      return timing.passed;
    }
    case vector_v2::ChromeAction::kZoomOut: {
      const ZoomLevel target = vector_v2::previous_zoom(presenter.zoom());
      if (target == presenter.zoom()) {
        return true;
      }
      const auto timing = presenter.set_zoom(target, chrome, now_us());
      print_presentation("zoom-ui", presenter, timing);
      return timing.passed;
    }
    case vector_v2::ChromeAction::kNone:
    case vector_v2::ChromeAction::kUndo:
    case vector_v2::ChromeAction::kRedo:
      break;
  }
  auto timing =
      palette_only_refresh
          ? presenter.present_frame_region(
                {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome, now_us())
          : LivePresentationTiming{};
  if (!palette_only_refresh || !timing.passed) {
    // A cached pan leaves frame_ ring-addressed; only a full compose may
    // materialize it before ordinary linear presentation.
    timing = presenter.refresh(chrome, now_us());
  }
  print_presentation("chrome", presenter, timing);
  return timing.passed;
}

}  // namespace

void run_vector_v2_app() {
  AppStorage storage;
  if (!storage.allocate()) {
    std::printf("TINYDRAW_LIVE_FAIL reason=allocation free_psram=%lu largest_psram=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
    return;
  }
  std::printf(
      "TINYDRAW_PRODUCER_SCRATCH supertask_internal=%u slot_directory_internal=%u "
      "free_internal=%lu free_psram=%lu\n",
      storage.supertask_internal, storage.slot_directory_internal,
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)));
  std::fill_n(storage.snapshot, vector_v2::kOverviewPixels, 0xFFFFU);
  std::fill_n(storage.overview, vector_v2::kOverviewPixels, 0xFFFFU);

  MaterializedCanvas canvas(
      std::span(storage.overview, vector_v2::kOverviewPixels),
      std::span(storage.uniforms, vector_v2::kMaterializedTileIdentityCount),
      std::span(storage.occupancy, vector_v2::kOccupancyBytes),
      std::span(storage.slots, vector_v2::kTileSlotCount),
      std::span(storage.tile_pixels, vector_v2::kTileSlotCount * vector_v2::kTilePixels),
      DocumentRevision{},
      std::span(storage.raw_slot_directory, vector_v2::kMaterializedTileIdentityCount));
  OperationLog log(std::span(storage.records, vector_v2::kOperationCapacity),
                   std::span(storage.samples, vector_v2::kOperationSampleCapacity));
  std::array<DisplayStrip, 3> queue{};
  DisplayScheduler scheduler(queue);
  Co5300PanelTransport display;
  PhysicalTouch touch;
  PowerManager power(touch.bus());
  RtcClock clock(touch.bus());
  TimeSyncController time_sync(clock);
  VectorV2TouchSampler touch_sampler(touch,
                                     std::span(storage.touch_events, kVectorV2TouchEventCapacity));
#ifdef TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE
  InkTraceCaptureRing ink_trace_ring(
      std::span(storage.ink_trace_records, kInkTraceCaptureCapacity));
  touch_sampler.set_capture_ring(&ink_trace_ring);
  std::printf("TINYDRAW_INKTRACE_CAPTURE_READY capacity=%u\n",
              static_cast<unsigned>(kInkTraceCaptureCapacity));
#endif
  // Navigation lives for the app's entire lifetime. Keep remembered zoom
  // views out of the latency-sensitive 16 KiB main-task stack.
  static vector_v2::NavigationState navigation;
  VectorV2Export exporter;
  VectorV2Presenter presenter(
      canvas, navigation, scheduler, display, std::span(storage.frame, vector_v2::kOverviewPixels),
      std::span(storage.region_scratch, kLiveRegionScratchPixels),
      std::span(storage.chrome_cache, vector_v2::kChromeStagingCachePixels));
  presenter.attach_authority(log);
  ChainedOperationBuilder builder(std::span(storage.input_samples, kInputSampleCapacity),
                                  kInteractiveChunkSampleLimit);
  vector_v2::TileProducer producer(
      log, canvas,
      {.supertask_pixels = std::span(storage.producer_supertask, vector_v2::kTileProducerPixels),
       .finalized_pixels = std::span(storage.producer_mask, vector_v2::kTileProducerMaskBytes),
       .summary_row_unset =
           std::span(storage.producer_summary_rows, vector_v2::kTileProducerSummaryRows),
       .summary_saturated_words =
           std::span(storage.producer_summary_words, vector_v2::kTileProducerSummaryWords),
       .operation_chord_plans = std::as_writable_bytes(
           std::span(storage.producer_chord_plans, vector_v2::kOperationChordStorageBytes / 4U))});
  // Re-render truth: every completed group render is classified against the
  // damage/eviction state the canvas reports (déjà-vu oracle; ~27.5 KiB).
  // Déjà-vu campaign step 1: the spatial re-render ledger speaks during
  // glass sessions. Deltas since the previous print attribute each
  // transition's re-renders to a cause; cumulative amplification rides along.
  vector_v2::RerenderLedgerTotals ledger_printed{};
  vector_v2::RerenderLedger rerender_ledger(
      std::span(storage.rerender_entries, vector_v2::kRerenderLedgerEntryCount));
  canvas.set_rerender_ledger(&rerender_ledger);
  const auto print_live_ledger = [&rerender_ledger, &ledger_printed](const char* site) {
    const auto totals = rerender_ledger.totals();
    std::printf(
        "TINYDRAW_LIVE_LEDGER site=%s renders=%lu cold=%lu damage=%lu evict=%lu stale=%lu "
        "unexplained=%lu total_renders=%lu unique=%lu amplification=%.3f\n",
        site, static_cast<unsigned long>(totals.renders - ledger_printed.renders),
        static_cast<unsigned long>(totals.cold_miss - ledger_printed.cold_miss),
        static_cast<unsigned long>(totals.expected_damage - ledger_printed.expected_damage),
        static_cast<unsigned long>(totals.eviction - ledger_printed.eviction),
        static_cast<unsigned long>(totals.stale_revision - ledger_printed.stale_revision),
        static_cast<unsigned long>(totals.unexplained - ledger_printed.unexplained),
        static_cast<unsigned long>(totals.renders),
        static_cast<unsigned long>(totals.unique_groups), totals.amplification());
    std::fflush(stdout);
    ledger_printed = totals;
  };
  producer.set_rerender_ledger(&rerender_ledger);
  const vector_v2::InPlaceAppendWorkspace workspace{
      .overview_scratch = std::span(storage.overview_scratch, vector_v2::kOverviewPixels),
      .affected_keys = std::span(storage.affected_keys,
                                 vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles),
      .tile_mask = std::span(storage.chunk_mask, vector_v2::kInPlaceTileMaskBytes),
  };
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  const IncrementalDocumentWorkspace harness_workspace{
      .overview_scratch = std::span(storage.overview_scratch, vector_v2::kOverviewPixels),
      .tile_scratch =
          std::span(storage.tile_scratch, kWorkspaceTileCapacity * vector_v2::kTilePixels),
      .publications = std::span(storage.publications, kWorkspaceTileCapacity),
      .affected_keys = std::span(storage.affected_keys,
                                 vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles),
  };
#endif
  if (!canvas.publish_overview({0}, std::span(storage.snapshot, vector_v2::kOverviewPixels)) ||
      !log.ready() || !presenter.ready() || !touch.ready() || !touch_sampler.start() ||
      !builder.ready() || !producer.ready()) {
    std::printf(
        "TINYDRAW_LIVE_FAIL reason=bootstrap canvas=%u log=%u presenter=%u touch=%u builder=%u "
        "producer=%u\n",
        canvas.ready(), log.ready(), presenter.ready(), touch.ready(), builder.ready(),
        producer.ready());
    return;
  }

  gpio_config_t button_config{};
  button_config.pin_bit_mask = 1ULL << static_cast<unsigned>(kModeButton);
  button_config.mode = GPIO_MODE_INPUT;
  button_config.pull_up_en = GPIO_PULLUP_ENABLE;
  static_cast<void>(gpio_config(&button_config));

  const PowerStatus initial_power = power.read();
  PowerStatus current_power = initial_power;
  vector_v2::ChromeState chrome;
  chrome.tool = vector_v2::ChromeTool::kDraw;
  chrome.size = vector_v2::ChromeSize::kLarge;
  chrome.can_export = exporter.ready();
  chrome.can_sync_time = time_sync.available();
  chrome.battery_percentage = initial_power.percentage;
  chrome.battery_charging = initial_power.charging;
  InkConfig ink_config;
  ink_config.size = vector_v2::brush_size(chrome.size);
  // Owner experiment 2026-08-16: stronger input smoothing for V2 (default
  // 0.35 stays for the Raster V1 fallback). Receipts:
  // benchmark-results/settled-aa-prototype/ streamline sweep.
  ink_config.streamline = 0.4F;
  InkStream ink(ink_config);
  CurvedRibbonStream ribbon;
  const auto initial = presenter.refresh(chrome);
  print_presentation("startup", presenter, initial);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // The harness leaves the realistic document loaded for manual glass
  // testing, so a red verdict must not exit the app: with standing known-red
  // gates (mixed_draw budget, the 400% cold wall) an early return made every
  // harness boot end before a human could touch the glass. The verdict
  // stays in the DONE line; automation keys on that, not on liveness.
  const bool harness_verdict = run_vector_v2_gate_harness(
      presenter, producer, log, canvas, touch_sampler, chrome, harness_workspace, workspace,
      exporter, std::span(storage.snapshot, vector_v2::kOverviewPixels),
      std::span(storage.input_samples, kInputSampleCapacity),
      std::span(storage.harness_tile_scratch, vector_v2::kTilePixels));
  std::printf("TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=%u\n", harness_verdict);
#endif
  const std::size_t overview_bytes = vector_v2::kOverviewPixels * 4U * sizeof(std::uint16_t);
  const std::size_t raw_tile_bytes =
      vector_v2::kTileSlotCount * vector_v2::kTilePixels * sizeof(std::uint16_t);
  const std::size_t tile_metadata_bytes =
      vector_v2::kTileSlotCount * sizeof(MaterializedSlotStorage) +
      vector_v2::kMaterializedTileIdentityCount * sizeof(MaterializedUniformStorage) +
      vector_v2::kMaterializedTileIdentityCount * sizeof(std::uint16_t) +
      vector_v2::kOccupancyBytes;
  const std::size_t operation_bytes =
      vector_v2::kOperationCapacity * sizeof(OperationRecord) +
      vector_v2::kOperationSampleCapacity * sizeof(CompactOperationSample);
  const std::size_t live_scratch_bytes =
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
      kWorkspaceTileCapacity * vector_v2::kTilePixels * sizeof(std::uint16_t) +
      kWorkspaceTileCapacity * sizeof(TileRevisionPublication) +
      vector_v2::kTilePixels * sizeof(std::uint16_t) +
#endif
      (vector_v2::kTileProducerPixels + kLiveRegionScratchPixels) * sizeof(std::uint16_t) +
      vector_v2::kTileProducerMaskBytes +
      vector_v2::kTileProducerSummaryRows * sizeof(std::uint16_t) +
      vector_v2::kTileProducerSummaryWords * sizeof(std::uint32_t) +
      vector_v2::kInPlaceTileMaskBytes + kInputSampleCapacity * sizeof(CompactOperationSample) +
      kVectorV2TouchEventCapacity * sizeof(vector_v2::TouchEvent) +
      (vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles) * sizeof(TileKey);
  const std::size_t live_storage_bytes =
      overview_bytes + raw_tile_bytes + tile_metadata_bytes + operation_bytes + live_scratch_bytes;
  const auto tear_signal = display.tear_signal_timing();
  std::printf(
      "TINYDRAW_VECTOR_V2_READY zoom=25 controls=chrome button=cycle_all_zooms "
      "operations_capacity=%lu samples_capacity=%lu live_storage_bytes=%lu "
      "overview_bytes=%lu raw_tile_bytes=%lu tile_metadata_bytes=%lu operation_bytes=%lu "
      "live_scratch_bytes=%lu free_psram=%lu largest_psram=%lu "
      "te_edges=%lu te_period_us=%lld te_high_us=%lld te_level=%u main_stack_free=%lu\n",
      static_cast<unsigned long>(log.operation_capacity()),
      static_cast<unsigned long>(log.sample_capacity()),
      static_cast<unsigned long>(live_storage_bytes), static_cast<unsigned long>(overview_bytes),
      static_cast<unsigned long>(raw_tile_bytes), static_cast<unsigned long>(tile_metadata_bytes),
      static_cast<unsigned long>(operation_bytes), static_cast<unsigned long>(live_scratch_bytes),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)),
      static_cast<unsigned long>(tear_signal.rising_edges),
      static_cast<long long>(tear_signal.period_us), static_cast<long long>(tear_signal.high_us),
      tear_signal.level, static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  std::fflush(stdout);

  bool pressed = false;
  bool toolbar_pressed = false;
  bool minimap_pressed = false;
  bool popup_dismissed_press = false;
  bool panning = false;
  Point last_touch{};
  Point toolbar_start{};
  Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  Point pan_start{};
  int pan_start_x = 0;
  int pan_start_y = 0;
  InkPoint last_ink{};
  LiveMetrics live_metrics{};
  PanMetrics pan_metrics{};
  std::uint32_t poll_previous_us = now_us();
  std::uint32_t poll_max_us = 0;
  std::uint32_t power_sampled_us = now_us();
  bool button_down = false;
  ZoomLevel fill_zoom = ZoomLevel::k25Percent;
  int fill_x = 0;
  int fill_y = 0;
  DocumentRevision fill_revision = canvas.current_revision();
  bool fill_complete = true;
  FillBaselineTiming fill_timing{};
  bool fill_measurement_active = false;
  PendingFillPresentation pending_fill{};
  vector_v2::IdleRepairPlan repair_plan{};
  std::size_t repair_cursor = 0;
  std::size_t repair_steps = 0;
  std::uint32_t repair_failures = 0;
  bool repair_planned = false;
  // Settled-AA idle pass state: one viewport sweep per (view, revision),
  // fingerprinted so any zoom/origin/revision change restarts the sweep.
  std::size_t settle_cursor = 0;
  bool settle_complete = false;
  std::uint32_t settle_tiles = 0;
  std::int64_t settle_total_us = 0;
  std::int64_t settle_max_us = 0;
  std::uint32_t settle_failures = 0;
  DocumentRevision settle_pass_revision{};
  ZoomLevel settle_pass_zoom = ZoomLevel::k25Percent;
  int settle_pass_x = -1;
  int settle_pass_y = -1;
  const auto settle_fingerprint_reset = [&]() {
    if (settle_pass_zoom == presenter.zoom() && settle_pass_revision == canvas.current_revision() &&
        settle_pass_x == presenter.level_x() && settle_pass_y == presenter.level_y()) {
      return;
    }
    settle_pass_zoom = presenter.zoom();
    settle_pass_revision = canvas.current_revision();
    settle_pass_x = presenter.level_x();
    settle_pass_y = presenter.level_y();
    settle_cursor = 0;
    settle_complete = false;
    settle_tiles = 0;
    settle_total_us = 0;
    settle_max_us = 0;
    settle_failures = 0;
  };
  LiftBaselineTiming lift_timing{};
  std::uint32_t next_lift_id = 1U;
  std::uint16_t next_gesture_id = 1U;
  std::optional<vector_v2::PixelRect> stroke_world_bounds;
  std::uint32_t stroke_chunks = 0U;
  std::int64_t stroke_append_us = 0;
  std::int64_t stroke_append_max_us = 0;
  vector_v2::InPlaceAppendPhases stroke_phase_max{};
  vector_v2::InPlaceRetainDrops stroke_drops{};
  bool stroke_commit_failed = false;
  // Committed-overlay drain state: world bounds awaiting the exact swap
  // refresh once the pending range empties, plus per-drain receipts.
  std::optional<vector_v2::PixelRect> drain_swap_world;
  std::uint32_t drain_ops = 0U;
  std::int64_t drain_total_us = 0;
  std::int64_t drain_max_us = 0;
  std::uint32_t drain_failures = 0U;
  std::uint32_t lift_reports_dropped = 0U;
  PendingStrokeReport stroke_report{};
  std::uint8_t background_ticks = 0U;

  const auto begin_pan = [&](Point start) {
    panning = true;
    pan_metrics.reset();
    // Boundary drain (committed-overlay design §3.4): the ring-reuse pan path
    // composes exposed strips without the pending overlay, so the canvas must
    // reach authority before the first pan present.
    if (vector_v2::pending_operation_count(log, canvas) != 0U) {
      const std::int64_t boundary_started = esp_timer_get_time();
      std::uint32_t boundary_ops = 0U;
      while (vector_v2::pending_operation_count(log, canvas) != 0U) {
        if (!vector_v2::absorb_pending_operation(
                 log, canvas, workspace, current_priority_view(presenter),
                 {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs})
                 .has_value()) {
          break;
        }
        ++boundary_ops;
      }
      drain_swap_world.reset();
      std::printf("TINYDRAW_LIVE_DRAIN_BOUNDARY site=pan ops=%lu wall_us=%lld pending=%lu\n",
                  static_cast<unsigned long>(boundary_ops),
                  static_cast<long long>(esp_timer_get_time() - boundary_started),
                  static_cast<unsigned long>(vector_v2::pending_operation_count(log, canvas)));
    }
    pan_start = start;
    pan_start_x = presenter.level_x();
    pan_start_y = presenter.level_y();
  };

  for (;;) {
    const std::uint32_t loop_us = now_us();
    poll_max_us = std::max(poll_max_us, loop_us - poll_previous_us);
    poll_previous_us = loop_us;

    const auto next_time_sync_status = chrome_time_sync_status(time_sync.status());
    if (next_time_sync_status != chrome.time_sync_status) {
      chrome.time_sync_status = next_time_sync_status;
      chrome.popup = vector_v2::ChromePopup::kNone;
      const auto timing = presenter.refresh(chrome, loop_us);
      print_presentation("time-sync", presenter, timing);
    }

    // The battery redraw is a full-frame present (60-140 ms on dense
    // content); it is cosmetic and must never ride the post-lift window or
    // the drain (owner glass receipt: it was the 85 ms "lift spike").
    if (!pressed && !lift_timing.pending && !stroke_report.pending &&
        vector_v2::pending_operation_count(log, canvas) == 0U && power.ready() &&
        loop_us - power_sampled_us >= kPowerRefreshUs) {
      const PowerStatus next_power = power.read();
      power_sampled_us = loop_us;
      if (next_power.valid && next_power != current_power) {
        current_power = next_power;
        chrome.battery_percentage = current_power.percentage;
        chrome.battery_charging = current_power.charging;
        // A battery change re-presents only the battery overlay region
        // (owner question 2026-08-16: the full-frame refresh here cost
        // 60-140 ms on dense content for a cosmetic glyph).
        const vector_v2::ChromeRect battery = vector_v2::chrome_battery_region();
        const auto timing = presenter.refresh_region(
            {presenter.level_x() + battery.x0, presenter.level_y() + battery.y0,
             presenter.level_x() + battery.x1, presenter.level_y() + battery.y1},
            chrome, loop_us);
        print_presentation("power", presenter, timing);
      }
    }

    const bool next_button_down = gpio_get_level(kModeButton) == 0;
    if (next_button_down && !button_down) {
      button_down = true;
    } else if (!next_button_down && button_down) {
      button_down = false;
      const ZoomLevel zoom = vector_v2::next_zoom(presenter.zoom());
      const ZoomLevel target = zoom == presenter.zoom() ? ZoomLevel::k25Percent : zoom;
      const auto timing = presenter.set_zoom(target, chrome, loop_us);
      print_presentation("zoom", presenter, timing);
      print_live_ledger("zoom");
    }

    Point point{};
    const bool idle_before_poll = !pressed;
    const std::int64_t poll_started_us = esp_timer_get_time();
    const auto sampled_touch = touch_sampler.read_next();
    const std::int64_t poll_completed_us = esp_timer_get_time();
    const bool sample_ready = sampled_touch.has_value();
    if (sample_ready) {
      point = sampled_touch->point;
    }
    const bool lift_event = sample_ready && sampled_touch->kind == vector_v2::TouchEventKind::kUp;
    const bool point_event = sample_ready && (!lift_event || pressed);
    const std::uint32_t event_us = sample_ready ? sampled_touch->timestamp_us : loop_us;
    if (point_event) {
      if (!pressed && sampled_touch->kind == vector_v2::TouchEventKind::kDown) {
        pressed = true;
        last_touch = point;
        const bool export_toast = chrome.export_status == vector_v2::ChromeExportStatus::kSaved ||
                                  chrome.export_status == vector_v2::ChromeExportStatus::kError;
        const bool time_toast =
            chrome.time_sync_status == vector_v2::ChromeTimeSyncStatus::kSaved ||
            chrome.time_sync_status == vector_v2::ChromeTimeSyncStatus::kError;
        if (export_toast || time_toast) {
          chrome.export_status = vector_v2::ChromeExportStatus::kIdle;
          if (time_toast) {
            time_sync.dismiss();
          }
          chrome.time_sync_status = vector_v2::ChromeTimeSyncStatus::kIdle;
          popup_dismissed_press = true;
          static_cast<void>(presenter.refresh(chrome, loop_us));
        }
        if (popup_dismissed_press) {
          // Consume the complete gesture that dismisses a terminal toast so
          // it cannot also begin a stroke or navigation gesture beneath it.
        } else if (vector_v2::chrome_minimap_contains({point.x, point.y}, chrome)) {
          // Minimap navigation owns its frame for every tool. A stationary
          // gesture jumps on Up; the first movement enters continuous pan.
          minimap_pressed = true;
          toolbar_start = point;
        } else if (vector_v2::chrome_contains({point.x, point.y}, chrome)) {
          toolbar_pressed = true;
          toolbar_start = point;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (chrome.popup != vector_v2::ChromePopup::kNone) {
          // A tap outside a compact popup dismisses it and consumes the
          // complete gesture. It must never leak through as a stroke or pan.
          chrome.popup = vector_v2::ChromePopup::kNone;
          popup_dismissed_press = true;
          const auto timing = presenter.refresh(chrome, loop_us);
          print_presentation("chrome-dismiss", presenter, timing);
        } else if (chrome.tool == vector_v2::ChromeTool::kPan) {
          begin_pan(point);
        } else {
          ink_config.size = vector_v2::brush_size(chrome.size);
          ink.set_config(ink_config);
          last_ink = ink.begin({.x = point.x, .y = point.y, .timestamp_us = event_us});
          const OperationTool tool = chrome.tool == vector_v2::ChromeTool::kErase
                                         ? OperationTool::kEraser
                                         : OperationTool::kPen;
          const std::uint16_t color =
              tool == OperationTool::kEraser ? 0xFFFFU : vector_v2::selected_color(chrome);
          const vector_v2::OperationPoint begin_point = presenter.operation_point(last_ink);
          const std::uint16_t gesture_id = next_gesture_id++;
          if (next_gesture_id == 0U) {
            next_gesture_id = 1U;
          }
          stroke_world_bounds.reset();
          stroke_chunks = 0U;
          stroke_append_us = 0;
          stroke_append_max_us = 0;
          stroke_phase_max = {};
          stroke_drops = {};
          stroke_commit_failed = false;
          if (!builder.begin(tool, color, gesture_id, begin_point)) {
            print_stroke_rejected("begin", builder, begin_point);
            ink.end();
          } else {
            ribbon.reset();
            static_cast<void>(ribbon.append(last_ink, true));
            live_metrics.include(presenter.show_start(last_ink, color, chrome, event_us));
          }
        }
      } else if (minimap_pressed && (point.x != last_touch.x || point.y != last_touch.y)) {
        if (!panning && vector_v2::chrome_promotes_minimap_drag(
                            {toolbar_start.x, toolbar_start.y}, {point.x, point.y}, chrome,
                            vector_v2::zoom_percent(presenter.zoom()))) {
          begin_pan(toolbar_start);
        }
        last_touch = point;
        if (panning) {
          pan_metrics.include(presenter.pan_minimap_from(pan_start_x, pan_start_y, pan_start, point,
                                                         chrome, event_us));
        }
      } else if (toolbar_pressed && (point.x != last_touch.x || point.y != last_touch.y)) {
        if (vector_v2::chrome_promotes_pan_drag({toolbar_start.x, toolbar_start.y},
                                                {point.x, point.y}, chrome)) {
          toolbar_pressed = false;
          toolbar_samples = 0;
          begin_pan(toolbar_start);
          last_touch = point;
          pan_metrics.include(
              presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, event_us));
        } else {
          toolbar_sum.x += point.x;
          toolbar_sum.y += point.y;
          ++toolbar_samples;
          last_touch = point;
        }
      } else if (panning && (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        const auto timing =
            presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, event_us);
        pan_metrics.include(timing);
      } else if (ink.active() && (point.x != last_touch.x || point.y != last_touch.y)) {
        const auto clipped = vector_v2::clip_canvas_segment({last_touch.x, last_touch.y},
                                                            {point.x, point.y}, chrome);
        const auto canvas_point = clipped.has_value() ? std::optional{Point{clipped->x, clipped->y}}
                                                      : std::optional<Point>{};
        last_touch = point;
        if (canvas_point.has_value()) {
          last_ink =
              ink.update({.x = canvas_point->x, .y = canvas_point->y, .timestamp_us = event_us});
          const vector_v2::OperationPoint add_point = presenter.operation_point(last_ink);
          const std::uint16_t color = chrome.tool == vector_v2::ChromeTool::kErase
                                          ? 0xFFFFU
                                          : vector_v2::selected_color(chrome);
          const auto move = vector_v2::process_live_ink_move(
              ribbon, builder, last_ink, add_point, event_us,
              [&](const RibbonUpdate& update, std::uint32_t visual_event_us) {
                const auto timing = presenter.show_update(update, color, chrome, visual_event_us);
                live_metrics.include(timing);
                return timing.passed;
              },
              [&] {
                return commit_ready_chunk(builder, log, canvas, workspace, presenter,
                                          stroke_world_bounds, stroke_chunks, stroke_append_us,
                                          stroke_append_max_us, stroke_phase_max, stroke_drops);
              });
          const ChainedOperationStatus add_status = move.status;
          stroke_commit_failed = move.commit_failed;
          if (add_status != ChainedOperationStatus::kAccepted) {
            // Accepted streaming policy: chunks already committed stay in
            // the document like physical ink; only the uncommitted tail of
            // the gesture is discarded on capacity rejection.
            if (stroke_commit_failed) {
              std::printf("TINYDRAW_STROKE_REJECTED site=commit reason=document_capacity\n");
              std::fflush(stdout);
            } else {
              print_stroke_rejected("add", builder, add_point);
            }
            builder.cancel();
            ribbon.reset();
            ink.end();
            // Restore authority beneath the discarded transient tail.
            static_cast<void>(presenter.refresh(chrome, event_us));
          }
        }
      }
    }
    if (lift_event && pressed) {
      pressed = false;
      if (popup_dismissed_press) {
        popup_dismissed_press = false;
      } else if (minimap_pressed) {
        minimap_pressed = false;
        if (panning) {
          panning = false;
          print_pan_baseline(presenter, pan_metrics);
          print_live_ledger("minimap_pan_end");
          std::fflush(stdout);
        } else {
          const auto timing = presenter.jump_from_minimap(toolbar_start, chrome, event_us);
          print_presentation("minimap-tap", presenter, timing);
          print_live_ledger("minimap_tap");
        }
      } else if (toolbar_pressed) {
        toolbar_pressed = false;
        const float divisor = static_cast<float>(std::max<std::uint32_t>(1U, toolbar_samples));
        const Point tap{toolbar_sum.x / divisor, toolbar_sum.y / divisor};
        toolbar_samples = 0;
        if (vector_v2::chrome_contains({tap.x, tap.y}, chrome)) {
          static_cast<void>(apply_chrome_action(
              vector_v2::chrome_action_at({tap.x, tap.y}, chrome), tap, chrome, log, canvas,
              producer, std::span(storage.snapshot, vector_v2::kOverviewPixels), presenter,
              exporter, time_sync));
        }
      } else if (panning) {
        panning = false;
        print_pan_baseline(presenter, pan_metrics);
        print_live_ledger("pan_end");
        std::fflush(stdout);
      } else if (ink.active()) {
        LiftBaselineTiming measured_lift{
            .id = next_lift_id++,
            .detected_us = esp_timer_get_time(),
        };
        const std::uint32_t finished_us = event_us;
        const std::int64_t finish_preview_started = esp_timer_get_time();
        last_ink = ink.finish(
            {.x = last_ink.position.x, .y = last_ink.position.y, .timestamp_us = finished_us});
        const std::uint16_t color = chrome.tool == vector_v2::ChromeTool::kErase
                                        ? 0xFFFFU
                                        : vector_v2::selected_color(chrome);
        live_metrics.include(
            presenter.show_update(ribbon.finish(last_ink), color, chrome, finished_us));
        measured_lift.finish_preview_us = esp_timer_get_time() - finish_preview_started;

        const std::int64_t builder_finish_started = esp_timer_get_time();
        ChainedOperationStatus finish_status = builder.finish(presenter.operation_point(last_ink));
        measured_lift.builder_finish_us = esp_timer_get_time() - builder_finish_started;
        while (finish_status == ChainedOperationStatus::kChunkReady ||
               finish_status == ChainedOperationStatus::kFinalChunkReady) {
          const auto continued = commit_ready_chunk(
              builder, log, canvas, workspace, presenter, stroke_world_bounds, stroke_chunks,
              stroke_append_us, stroke_append_max_us, stroke_phase_max, stroke_drops);
          if (!continued.has_value()) {
            stroke_commit_failed = true;
            finish_status = ChainedOperationStatus::kRejected;
            break;
          }
          finish_status = *continued;
        }
        measured_lift.committed = finish_status == ChainedOperationStatus::kComplete;
        measured_lift.commit_failed = stroke_commit_failed;
        measured_lift.chunks = stroke_chunks;
        measured_lift.append_us = stroke_append_us;
        measured_lift.append_max_us = stroke_append_max_us;
        if (stroke_world_bounds.has_value()) {
          measured_lift.refresh_level_bounds =
              vector_v2::operation_level_bounds(*stroke_world_bounds, presenter.zoom());
        }
        if (finish_status == ChainedOperationStatus::kRejected) {
          if (stroke_commit_failed) {
            std::printf("TINYDRAW_STROKE_REJECTED site=commit reason=document_capacity\n");
            std::fflush(stdout);
          } else {
            print_stroke_rejected("finish", builder, presenter.operation_point(last_ink));
          }
        }
        builder.cancel();
        ribbon.reset();
        const std::int64_t refresh_started = esp_timer_get_time();
        if (finish_status == ChainedOperationStatus::kRejected) {
          // A rejected finish leaves uncommitted preview ink beyond the
          // committed bounds; only a full repaint clears it. The patched
          // compose keeps already-committed pending chunks visible.
          measured_lift.refresh = presenter.refresh(chrome, finished_us);
        } else if (stroke_world_bounds.has_value()) {
          // Committed-overlay lift (design §5.4): no synchronous refresh.
          // Glass keeps the preview; the idle drain absorbs the pending
          // range and then runs one exact swap refresh over the union of
          // undrained stroke bounds.
          include_bounds(drain_swap_world, *stroke_world_bounds);
          measured_lift.refresh = {};
          measured_lift.refresh.passed = true;
        }
        measured_lift.refresh_wall_us = esp_timer_get_time() - refresh_started;
        if (stroke_report.pending || lift_timing.pending) {
          ++lift_reports_dropped;
        }
        const TouchSamplerMetrics touch_metrics = touch_sampler.take_metrics();
        stroke_report = {
            .revision = canvas.current_revision(),
            .operation_count = log.operation_count(),
            .sample_count = log.sample_count(),
            .append_us = measured_lift.append_us,
            .append_max_us = measured_lift.append_max_us,
            .refresh = measured_lift.refresh,
            .metrics = live_metrics,
            .poll_max_us = poll_max_us,
            .touch = touch_metrics,
            .phase_max = stroke_phase_max,
            .drops = stroke_drops,
            .chunks = measured_lift.chunks,
            .free_psram = heap_caps_get_free_size(kExternalCaps),
            .largest_psram = heap_caps_get_largest_free_block(kExternalCaps),
            .authority_match = log.current_revision() == canvas.current_revision(),
            .committed = measured_lift.committed,
            .commit_failed = measured_lift.commit_failed,
            .pending = true,
        };
        measured_lift.pending = true;
        lift_timing = measured_lift;
        live_metrics = {};
        poll_max_us = 0;
      }
    }

#ifdef TINYDRAW_VECTOR_V2_INK_TRACE_CAPTURE
    // Dump a finished capture only when the owner has been hands-off long
    // enough that the serial burst cannot perturb a gesture.
    if (!pressed && !ink_trace_ring.touching() && ink_trace_ring.size() != 0U &&
        loop_us - ink_trace_ring.last_activity_us() > 2'000'000U) {
      ink_trace_ring.dump_and_reset();
    }
#endif

    const bool fill_view_available =
        presenter.zoom() != ZoomLevel::k25Percent && chrome.popup == vector_v2::ChromePopup::kNone;
    // The commit tick already performed bounded immediate publication and its
    // display update. Defer cold replay until input has had another poll.
    const bool fill_allowed = !pressed && fill_view_available && !lift_timing.pending;
    if (!pressed) {
      settle_fingerprint_reset();
    }
    // Committed-overlay idle drain: absorbing pending authority outranks
    // fill and repair — fill tiles produced at a stale revision would be
    // invalidated by the very next absorption anyway. One operation per
    // slice; the exact swap refresh runs when the range empties.
    // Drain only on truly idle iterations: a slice must never ride an
    // iteration that just consumed input (the glass receipt showed the
    // first absorption inside the lift iteration as a 29 ms unattributed
    // tail). While events flow, the high-water fallback bounds the range.
    const std::size_t idle_pending_ops = !pressed && !panning && !sample_ready
                                             ? vector_v2::pending_operation_count(log, canvas)
                                             : 0U;
    // After repeated absorb failures the drain stands down: the overlay keeps
    // glass exact indefinitely and the failure counter surfaces in receipts.
    if (idle_pending_ops != 0U && drain_failures < 16U) {
      const std::int64_t absorb_started = esp_timer_get_time();
      const auto absorbed = vector_v2::absorb_pending_operation(
          log, canvas, workspace, current_priority_view(presenter),
          {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs});
      const std::int64_t absorb_us = esp_timer_get_time() - absorb_started;
      if (absorbed.has_value()) {
        ++drain_ops;
        drain_total_us += absorb_us;
        drain_max_us = std::max(drain_max_us, absorb_us);
        if (vector_v2::pending_operation_count(log, canvas) == 0U) {
          LivePresentationTiming swap{};
          swap.passed = true;
          std::int64_t swap_wall_us = 0;
          if (drain_swap_world.has_value()) {
            const std::int64_t swap_started = esp_timer_get_time();
            swap = presenter.refresh_region(
                vector_v2::operation_level_bounds(*drain_swap_world, presenter.zoom()), chrome,
                loop_us);
            swap_wall_us = esp_timer_get_time() - swap_started;
            drain_swap_world.reset();
          }
          std::printf(
              "TINYDRAW_LIVE_DRAIN ops=%lu total_us=%lld max_us=%lld failures=%lu "
              "swap_wall_us=%lld swap_pass=%u\n",
              static_cast<unsigned long>(drain_ops), static_cast<long long>(drain_total_us),
              static_cast<long long>(drain_max_us), static_cast<unsigned long>(drain_failures),
              static_cast<long long>(swap_wall_us), swap.passed);
          std::fflush(stdout);
          print_live_ledger("drain");
          drain_ops = 0U;
          drain_total_us = 0;
          drain_max_us = 0;
          drain_failures = 0U;
        }
      } else {
        ++drain_failures;
      }
    }
    if (!fill_view_available && fill_measurement_active) {
      print_fill_baseline("paused", fill_zoom, fill_x, fill_y, fill_revision, fill_timing);
      fill_timing.reset();
      fill_measurement_active = false;
    }
    if (fill_allowed && idle_pending_ops == 0U) {
      const vector_v2::ViewRequest fill_view{
          .zoom = presenter.zoom(),
          .level_pixels = {presenter.level_x(), presenter.level_y(),
                           presenter.level_x() + vector_v2::kOverviewWidth,
                           presenter.level_y() + vector_v2::kOverviewHeight},
      };
      const bool fill_view_changed =
          fill_zoom != fill_view.zoom || fill_x != fill_view.level_pixels.x0 ||
          fill_y != fill_view.level_pixels.y0 || fill_revision != canvas.current_revision();
      if (fill_view_changed) {
        if (fill_measurement_active) {
          print_fill_baseline("superseded", fill_zoom, fill_x, fill_y, fill_revision, fill_timing);
        }
        fill_zoom = fill_view.zoom;
        fill_x = fill_view.level_pixels.x0;
        fill_y = fill_view.level_pixels.y0;
        fill_revision = canvas.current_revision();
        fill_complete = false;
        fill_timing.reset();
        fill_timing.started_us = esp_timer_get_time();
        fill_measurement_active = true;
        pending_fill = {};
        repair_planned = false;
        repair_failures = 0;
        settle_cursor = 0;
        settle_complete = false;
        settle_tiles = 0;
        settle_total_us = 0;
        settle_max_us = 0;
        settle_failures = 0;
      }
      if (pending_fill.pending) {
        const bool still_current = pending_fill.zoom == fill_view.zoom &&
                                   pending_fill.x == fill_view.level_pixels.x0 &&
                                   pending_fill.y == fill_view.level_pixels.y0;
        if (!still_current) {
          pending_fill = {};
        } else {
          const std::int64_t present_started = esp_timer_get_time();
          const auto presentation = presenter.refresh_region(pending_fill.level_bounds, chrome);
          const std::int64_t present_us = esp_timer_get_time() - present_started;
          fill_timing.present_total_us += present_us;
          fill_timing.present_max_us = std::max(fill_timing.present_max_us, present_us);
          fill_timing.presentation_failures += !presentation.passed;
          if (presentation.passed) {
            pending_fill = {};
          }
        }
      } else if (!fill_complete) {
        if (!fill_measurement_active) {
          fill_timing.reset();
          fill_timing.started_us = esp_timer_get_time();
          fill_measurement_active = true;
        }
        const std::int64_t fill_tick_started = esp_timer_get_time();
        // Fill the slice up to a deadline instead of taking one bounded
        // producer step per tick: the per-step work budget under-predicts
        // masked-replay cost by an order of magnitude, so single-step ticks
        // waste most of the slice on fixed loop overhead and idle pacing.
        // Resumable produce_next keeps each inner step bounded, so the worst
        // tick stays under the 15 ms input-poll alarm.
        do {
          const std::int64_t compute_started = esp_timer_get_time();
          const auto step = producer.produce_next(fill_view);
          const std::int64_t compute_us = esp_timer_get_time() - compute_started;
          ++fill_timing.steps;
          fill_timing.compute_total_us += compute_us;
          fill_timing.compute_max_us = std::max(fill_timing.compute_max_us, compute_us);
          if (!step.has_value()) {
            ++fill_timing.producer_failures;
            break;
          }
          if (step->tiles_published != 0U) {
            pending_fill = {.level_bounds = step->level_bounds,
                            .zoom = fill_view.zoom,
                            .x = fill_view.level_pixels.x0,
                            .y = fill_view.level_pixels.y0,
                            .pending = true};
          }
          fill_complete = step->complete;
        } while (!fill_complete && !pending_fill.pending &&
                 esp_timer_get_time() - fill_tick_started < kColdFillSliceDeadlineUs);
        fill_timing.tick_max_us =
            std::max(fill_timing.tick_max_us, esp_timer_get_time() - fill_tick_started);
        // fill_complete only becomes true through a successful producer step.
        if (fill_complete && !pending_fill.pending) {
          print_fill_baseline("complete", fill_zoom, fill_x, fill_y, fill_revision, fill_timing);
          std::printf("TINYDRAW_LIVE_FILL_DONE zoom=%s x=%d y=%d revision=%lu\n",
                      zoom_name(fill_zoom), fill_x, fill_y,
                      static_cast<unsigned long>(fill_revision.value));
          fill_measurement_active = false;
        }
      } else if (!pending_fill.pending && fill_measurement_active) {
        print_fill_baseline("complete", fill_zoom, fill_x, fill_y, fill_revision, fill_timing);
        std::printf("TINYDRAW_LIVE_FILL_DONE zoom=%s x=%d y=%d revision=%lu\n",
                    zoom_name(fill_zoom), fill_x, fill_y,
                    static_cast<unsigned long>(fill_revision.value));
        fill_measurement_active = false;
      } else if (!repair_planned || repair_cursor < repair_plan.count) {
        // Idle cache repair: the visible fill is complete and input is
        // quiet, so pre-produce the views a pan or zoom return will hit
        // next. Publications never present from here; they only make future
        // composition sharp. Bounded by the same slice deadline as fill.
        if (!repair_planned) {
          repair_plan = vector_v2::plan_idle_repair(fill_view, canvas.recent_views());
          repair_cursor = 0;
          repair_steps = 0;
          repair_planned = true;
        }
        const std::int64_t repair_tick_started = esp_timer_get_time();
        while (repair_cursor < repair_plan.count &&
               esp_timer_get_time() - repair_tick_started < kColdFillSliceDeadlineUs) {
          // The level sweep stops when the pool saturates: past that point
          // every publication evicts warmer tiles (dense documents exceed
          // pool capacity at 100%), which churned instead of repairing.
          if (repair_cursor >= repair_plan.grid_start &&
              canvas.resident_raw_tiles() + kRepairSaturationHeadroomTiles >=
                  canvas.slot_capacity()) {
            repair_cursor = repair_plan.count;
            break;
          }
          const auto step = producer.produce_next(repair_plan.views[repair_cursor]);
          if (!step.has_value()) {
            // Producer failure: replan and retry on the next quiet slice
            // (leaving the cursor exhausted silently disabled repair for
            // this camera). Three failures for the same view/revision mean
            // something structural; stop until the fingerprint changes.
            ++repair_failures;
            if (repair_failures < 3U) {
              repair_planned = false;
            } else {
              repair_cursor = repair_plan.count;
            }
            std::printf("TINYDRAW_LIVE_REPAIR_ABANDON view=%u failures=%u\n",
                        static_cast<unsigned>(repair_cursor),
                        static_cast<unsigned>(repair_failures));
            break;
          }
          ++repair_steps;
          if (step->complete) {
            ++repair_cursor;
          }
        }
        if (repair_planned && repair_cursor >= repair_plan.count && repair_plan.count != 0U) {
          std::printf("TINYDRAW_LIVE_REPAIR views=%u steps=%u\n",
                      static_cast<unsigned>(repair_plan.count),
                      static_cast<unsigned>(repair_steps));
        }
      } else if (!settle_complete && presenter.zoom() != ZoomLevel::k25Percent) {
        // Settled-AA pass (ship contract §4): with drain, fill, and repair
        // quiet, re-render one visible tile per slice with analytic
        // coverage and republish it at the settled quality tier. The live
        // path stays hard-edged; fresh ink demotes tiles back to immediate
        // quality so they re-settle on the next quiet stretch.
        const int first_column = presenter.level_x() / vector_v2::kTileWidth;
        const int last_column =
            (presenter.level_x() + vector_v2::kOverviewWidth - 1) / vector_v2::kTileWidth;
        const int first_row = presenter.level_y() / vector_v2::kTileHeight;
        const int last_row =
            (presenter.level_y() + vector_v2::kOverviewHeight - 1) / vector_v2::kTileHeight;
        const std::size_t columns = static_cast<std::size_t>(last_column - first_column + 1);
        const std::size_t total = columns * static_cast<std::size_t>(last_row - first_row + 1);
        constexpr std::int64_t kSettleSliceBudgetUs = 8'000;
        const std::int64_t slice_started = esp_timer_get_time();
        std::optional<vector_v2::PixelRect> batch_bounds;
        while (settle_cursor < total &&
               esp_timer_get_time() - slice_started < kSettleSliceBudgetUs) {
          const vector_v2::TileKey key{
              presenter.zoom(),
              static_cast<std::uint16_t>(first_column + static_cast<int>(settle_cursor % columns)),
              static_cast<std::uint16_t>(first_row + static_cast<int>(settle_cursor / columns))};
          ++settle_cursor;
          const auto source = canvas.lookup(key);
          if (!source.has_value() || source->kind != vector_v2::SourceKind::kTileSlot ||
              source->identity.quality >= vector_v2::MaterializationQuality::kSettled) {
            continue;
          }
          const std::int64_t tile_started = esp_timer_get_time();
          vector_v2::SettledTileStats tile_stats{};
          const vector_v2::SettledTileWorkspace settle_workspace{
              .operation_alpha = std::span(storage.settle_op_alpha, vector_v2::kTilePixels),
              .accumulated_alpha = std::span(storage.settle_accumulated, vector_v2::kTilePixels),
              .red = std::span(storage.settle_red, vector_v2::kTilePixels),
              .green = std::span(storage.settle_green, vector_v2::kTilePixels),
              .blue = std::span(storage.settle_blue, vector_v2::kTilePixels),
          };
          const auto pixels = std::span(storage.settle_pixels, vector_v2::kTilePixels);
          if (vector_v2::render_settled_tile(log, key, settle_workspace, pixels, &tile_stats) &&
              canvas
                  .publish_tile(key, canvas.current_revision(),
                                vector_v2::MaterializationQuality::kSettled, pixels)
                  .has_value()) {
            const std::int64_t tile_us = esp_timer_get_time() - tile_started;
            include_bounds(batch_bounds, vector_v2::tile_pixel_bounds(key));
            ++settle_tiles;
            settle_total_us += tile_us;
            settle_max_us = std::max(settle_max_us, tile_us);
          } else {
            ++settle_failures;
          }
        }
        if (batch_bounds.has_value()) {
          // One present per settled batch instead of one per tile.
          static_cast<void>(presenter.refresh_region(*batch_bounds, chrome, loop_us));
        }
        if (settle_cursor >= total) {
          settle_complete = true;
          if (settle_tiles != 0U || settle_failures != 0U) {
            std::printf(
                "TINYDRAW_LIVE_SETTLE zoom=%s tiles=%lu total_us=%lld max_tile_us=%lld "
                "failures=%lu\n",
                zoom_name(presenter.zoom()), static_cast<unsigned long>(settle_tiles),
                static_cast<long long>(settle_total_us), static_cast<long long>(settle_max_us),
                static_cast<unsigned long>(settle_failures));
            std::fflush(stdout);
          }
        }
      }
    } else if (!pressed && !panning && !lift_timing.pending && !settle_complete &&
               presenter.zoom() == ZoomLevel::k25Percent &&
               chrome.popup == vector_v2::ChromePopup::kNone &&
               vector_v2::pending_operation_count(log, canvas) == 0U) {
      // The 25% idle settle runs outside the fill chain (25% has no cold
      // fill by design) and settles PRESENTATION pixels only: the overview
      // is replay authority and must stay hard-edged. A refresh or new ink
      // resets the pass via the fill-view fingerprint.
      constexpr std::int64_t kSettleSliceBudgetUs = 8'000;
      const std::size_t columns =
          (static_cast<std::size_t>(vector_v2::kOverviewWidth) + vector_v2::kTileWidth - 1U) /
          vector_v2::kTileWidth;
      const std::size_t rows =
          (static_cast<std::size_t>(vector_v2::kOverviewHeight) + vector_v2::kTileHeight - 1U) /
          vector_v2::kTileHeight;
      const std::size_t total = columns * rows;
      const std::int64_t slice_started = esp_timer_get_time();
      const vector_v2::SettledTileWorkspace settle_workspace{
          .operation_alpha = std::span(storage.settle_op_alpha, vector_v2::kTilePixels),
          .accumulated_alpha = std::span(storage.settle_accumulated, vector_v2::kTilePixels),
          .red = std::span(storage.settle_red, vector_v2::kTilePixels),
          .green = std::span(storage.settle_green, vector_v2::kTilePixels),
          .blue = std::span(storage.settle_blue, vector_v2::kTilePixels),
      };
      const auto pixels = std::span(storage.settle_pixels, vector_v2::kTilePixels);
      std::optional<vector_v2::PixelRect> batch_bounds;
      while (settle_cursor < total && esp_timer_get_time() - slice_started < kSettleSliceBudgetUs) {
        const int column = static_cast<int>(settle_cursor % columns);
        const int row = static_cast<int>(settle_cursor / columns);
        ++settle_cursor;
        const vector_v2::PixelRect window{
            column * vector_v2::kTileWidth, row * vector_v2::kTileHeight,
            std::min((column + 1) * vector_v2::kTileWidth, vector_v2::kOverviewWidth),
            std::min((row + 1) * vector_v2::kTileHeight, vector_v2::kOverviewHeight)};
        const std::int64_t tile_started = esp_timer_get_time();
        vector_v2::SettledTileStats tile_stats{};
        if (vector_v2::render_settled_window(log, ZoomLevel::k25Percent, window, settle_workspace,
                                             pixels, &tile_stats) &&
            presenter.stage_settled_pixels(window, pixels, window.x1 - window.x0)) {
          const std::int64_t tile_us = esp_timer_get_time() - tile_started;
          include_bounds(batch_bounds, window);
          ++settle_tiles;
          settle_total_us += tile_us;
          settle_max_us = std::max(settle_max_us, tile_us);
        } else {
          ++settle_failures;
        }
      }
      if (batch_bounds.has_value()) {
        static_cast<void>(presenter.present_frame_region(*batch_bounds, chrome, loop_us));
      }
      if (settle_cursor >= total) {
        settle_complete = true;
        if (settle_tiles != 0U || settle_failures != 0U) {
          std::printf(
              "TINYDRAW_LIVE_SETTLE zoom=25 tiles=%lu total_us=%lld max_tile_us=%lld "
              "failures=%lu\n",
              static_cast<unsigned long>(settle_tiles), static_cast<long long>(settle_total_us),
              static_cast<long long>(settle_max_us), static_cast<unsigned long>(settle_failures));
          std::fflush(stdout);
        }
      }
    }
    if (lift_timing.pending && stroke_report.pending && idle_before_poll && !pressed) {
      print_stroke(stroke_report);
      std::printf("TINYDRAW_LIVE_STROKE_DONE committed=%u refresh=%u commit_failed=%u\n",
                  stroke_report.committed, stroke_report.refresh.passed,
                  stroke_report.commit_failed);
      print_lift_baseline(lift_timing, poll_started_us, poll_completed_us, lift_reports_dropped);
      std::fflush(stdout);
      lift_reports_dropped = 0U;
      stroke_report.pending = false;
      lift_timing.pending = false;
      // This diagnostic write happens only after a completed input poll. Keep
      // it out of the pre-existing stroke poll-gap metric.
      poll_previous_us = now_us();
    }
    const bool background_busy = fill_allowed && (!fill_complete || pending_fill.pending);
    if (background_busy) {
      // Producer calls are bounded input-poll slices. Avoid paying a fixed
      // two-millisecond tax after every slice, but periodically unblock idle.
      if (++background_ticks == 8U) {
        background_ticks = 0U;
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    } else {
      background_ticks = 0U;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_vector_v2_app(); }
