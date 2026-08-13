#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
#include "vector_v2_tile_census.h"
#endif
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::CompactOperationSample;
using vector_v2::DisplayScheduler;
using vector_v2::DisplayStrip;
using vector_v2::DocumentRevision;
using vector_v2::IncrementalDocumentWorkspace;
using vector_v2::MaterializedCanvas;
using vector_v2::MaterializedSlotStorage;
using vector_v2::MaterializedUniformStorage;
using vector_v2::OperationBuilder;
using vector_v2::OperationLog;
using vector_v2::OperationPoint;
using vector_v2::OperationRecord;
using vector_v2::OperationTool;
using vector_v2::TileKey;
using vector_v2::TileRevisionPublication;
using vector_v2::ZoomLevel;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr gpio_num_t kModeButton = GPIO_NUM_0;
constexpr int kLiftReads = 2;
constexpr std::size_t kInputSampleCapacity = 1'024;
constexpr std::size_t kWorkspaceTileCapacity = vector_v2::kMaximumVisibleTiles;
constexpr std::uint32_t kStressOperations = 1'000;
constexpr std::uint32_t kStressSamplesPerOperation = 20;
constexpr std::size_t kRealisticStrokeCapacity = 1'000;
constexpr std::size_t kRealisticSampleCapacity = 24'576;
constexpr std::uint32_t kButtonLongPressUs = 1'200'000U;

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

struct AppStorage {
  std::uint16_t* overview = nullptr;
  std::uint16_t* snapshot = nullptr;
  std::uint16_t* frame = nullptr;
  std::uint16_t* tile_pixels = nullptr;
  std::uint16_t* overview_scratch = nullptr;
  std::uint16_t* tile_scratch = nullptr;
  std::uint16_t* region_scratch = nullptr;
  std::uint16_t* producer_supertask = nullptr;
  std::uint16_t* producer_packed = nullptr;
  MaterializedUniformStorage* uniforms = nullptr;
  std::uint8_t* occupancy = nullptr;
  MaterializedSlotStorage* slots = nullptr;
  VectorStroke* realistic_strokes = nullptr;
  StrokeSample* realistic_samples = nullptr;
  OperationRecord* records = nullptr;
  CompactOperationSample* samples = nullptr;
  CompactOperationSample* input_samples = nullptr;
  TileRevisionPublication* publications = nullptr;
  TileKey* affected_keys = nullptr;

  [[nodiscard]] bool allocate() {
    overview = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    snapshot = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    frame = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    tile_pixels = allocate_array<std::uint16_t>(vector_v2::kTileSlotCount * vector_v2::kTilePixels);
    overview_scratch = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
    tile_scratch = allocate_array<std::uint16_t>(kWorkspaceTileCapacity * vector_v2::kTilePixels);
    region_scratch = allocate_array<std::uint16_t>(kLiveRegionScratchPixels);
    producer_supertask = allocate_array<std::uint16_t>(vector_v2::kTileProducerPixels);
    producer_packed = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
    uniforms =
        allocate_array<MaterializedUniformStorage>(vector_v2::kMaterializedTileIdentityCount);
    occupancy = allocate_array<std::uint8_t>(vector_v2::kOccupancyBytes);
    slots = allocate_array<MaterializedSlotStorage>(vector_v2::kTileSlotCount);
    realistic_strokes = allocate_array<VectorStroke>(kRealisticStrokeCapacity);
    realistic_samples = allocate_array<StrokeSample>(kRealisticSampleCapacity);
    records = allocate_array<OperationRecord>(vector_v2::kOperationCapacity);
    samples = allocate_array<CompactOperationSample>(vector_v2::kOperationSampleCapacity);
    input_samples = allocate_array<CompactOperationSample>(kInputSampleCapacity);
    publications = allocate_array<TileRevisionPublication>(kWorkspaceTileCapacity);
    affected_keys = allocate_array<TileKey>(vector_v2::kTileSlotCount);
    if (overview == nullptr || snapshot == nullptr || frame == nullptr || tile_pixels == nullptr ||
        overview_scratch == nullptr || tile_scratch == nullptr || region_scratch == nullptr ||
        producer_supertask == nullptr || producer_packed == nullptr || uniforms == nullptr ||
        occupancy == nullptr || slots == nullptr || realistic_strokes == nullptr ||
        realistic_samples == nullptr || records == nullptr || samples == nullptr ||
        input_samples == nullptr || publications == nullptr || affected_keys == nullptr) {
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
};

std::uint32_t now_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

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

ZoomLevel next_test_zoom(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return ZoomLevel::k100Percent;
    case ZoomLevel::k100Percent:
      return ZoomLevel::k400Percent;
    default:
      return ZoomLevel::k25Percent;
  }
}

void print_presentation(const char* kind, const VectorV2Presenter& presenter,
                        const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu pushes=%lu frame_reused=%u pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.tile_pixels),
      static_cast<unsigned long>(timing.uniform_pixels),
      static_cast<unsigned long>(timing.overview_pixels),
      static_cast<unsigned long>(timing.fallback_pixels),
      static_cast<unsigned long>(timing.resident_tiles),
      static_cast<unsigned long>(timing.fallback_tiles), static_cast<unsigned long>(timing.pushes),
      timing.frame_reused, timing.passed);
}

void print_stroke(const OperationLog& log, const MaterializedCanvas& canvas,
                  const LiveMetrics& metrics, std::int64_t append_us,
                  const LivePresentationTiming& refresh, std::uint32_t poll_max_us,
                  std::uint32_t touch_errors) {
  std::printf(
      "TINYDRAW_LIVE_STROKE revision=%lu operations=%lu samples=%lu append_us=%lld "
      "refresh_compose_us=%lld refresh_complete_us=%lld ink_samples=%lu "
      "read_submit_avg_us=%llu read_submit_max_us=%lu read_complete_avg_us=%llu "
      "read_complete_max_us=%lu submit_over_16ms=%lu complete_over_33ms=%lu "
      "presentation_failures=%lu poll_max_us=%lu touch_errors=%lu free_psram=%lu "
      "largest_psram=%lu authority_match=%u\n",
      static_cast<unsigned long>(canvas.current_revision().value),
      static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<long long>(append_us),
      static_cast<long long>(refresh.compose_us), static_cast<long long>(refresh.complete_us),
      static_cast<unsigned long>(metrics.samples),
      static_cast<unsigned long long>(
          metrics.samples == 0U ? 0U : metrics.submit_total_us / metrics.samples),
      static_cast<unsigned long>(metrics.submit_max_us),
      static_cast<unsigned long long>(
          metrics.samples == 0U ? 0U : metrics.complete_total_us / metrics.samples),
      static_cast<unsigned long>(metrics.complete_max_us),
      static_cast<unsigned long>(metrics.submit_over_16ms),
      static_cast<unsigned long>(metrics.complete_over_33ms),
      static_cast<unsigned long>(metrics.failures), static_cast<unsigned long>(poll_max_us),
      static_cast<unsigned long>(touch_errors),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)),
      log.current_revision() == canvas.current_revision());
}

bool apply_toolbar_action(ToolbarAction action, Point point, ToolbarState& toolbar,
                          OperationLog& log, MaterializedCanvas& canvas,
                          vector_v2::TileProducer& producer,
                          std::span<const std::uint16_t> blank_snapshot,
                          VectorV2Presenter& presenter) {
  const auto close = [&]() {
    toolbar.tools_open = false;
    toolbar.colors_open = false;
    toolbar.sizes_open = false;
  };
  switch (action) {
    case ToolbarAction::kSelectPen:
      toolbar.tool = DrawingTool::kPen;
      close();
      break;
    case ToolbarAction::kSelectPan:
      toolbar.tool = DrawingTool::kPan;
      close();
      break;
    case ToolbarAction::kSelectEraser:
      toolbar.tool = DrawingTool::kEraser;
      close();
      break;
    case ToolbarAction::kSelectColor:
      if (const auto color = toolbar_color_at(point, toolbar); color.has_value()) {
        toolbar.color = *color;
        toolbar.tool = DrawingTool::kPen;
      }
      close();
      break;
    case ToolbarAction::kToggleTools:
      toolbar.tools_open = !toolbar.tools_open;
      toolbar.colors_open = false;
      toolbar.sizes_open = false;
      break;
    case ToolbarAction::kToggleColors:
      toolbar.colors_open = !toolbar.colors_open;
      toolbar.tools_open = false;
      toolbar.sizes_open = false;
      break;
    case ToolbarAction::kToggleSizes:
      toolbar.sizes_open = !toolbar.sizes_open;
      toolbar.tools_open = false;
      toolbar.colors_open = false;
      break;
    case ToolbarAction::kSelectSmall:
    case ToolbarAction::kSelectMedium:
    case ToolbarAction::kSelectLarge:
    case ToolbarAction::kSelectExtraLarge:
      toolbar.size = action == ToolbarAction::kSelectSmall    ? PenSize::kSmall
                     : action == ToolbarAction::kSelectMedium ? PenSize::kMedium
                     : action == ToolbarAction::kSelectLarge  ? PenSize::kLarge
                                                              : PenSize::kExtraLarge;
      close();
      break;
    case ToolbarAction::kNewDrawing: {
      const DocumentRevision revision{canvas.current_revision().value + 1U};
      if (!vector_v2::restore_document_snapshot(log, canvas, revision, blank_snapshot) ||
          !producer.reset_uniform_baseline(revision)) {
        return false;
      }
      close();
      break;
    }
    case ToolbarAction::kNone:
    case ToolbarAction::kUndo:
    case ToolbarAction::kExport:
    case ToolbarAction::kCancelNewDrawing:
    case ToolbarAction::kConfirmNewDrawing:
      break;
  }
  const auto timing = presenter.refresh(toolbar, now_us());
  print_presentation("toolbar", presenter, timing);
  return timing.passed;
}

bool load_realistic_document(OperationLog& log, MaterializedCanvas& canvas,
                             const IncrementalDocumentWorkspace& workspace,
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
          .x_quarter = static_cast<std::uint16_t>(std::lround(input[index].x * 4.0F)),
          .y_quarter = static_cast<std::uint16_t>(std::lround(input[index].y * 4.0F)),
          .radius_256 = static_cast<std::uint16_t>(std::lround(input[index].radius * 256.0F)),
          .elapsed_ms = static_cast<std::uint16_t>(index * 15U),
      };
    }
    const auto result = vector_v2::append_incrementally(
        log, canvas,
        {.tool = stroke.tool == VectorTool::kEraser ? OperationTool::kEraser : OperationTool::kPen,
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

bool run_tile_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                   OperationLog& log, MaterializedCanvas& canvas, const ToolbarState& toolbar,
                   ZoomLevel zoom) {
  // The seed-7 corpus fills lines from the upper left. Fixing the origin makes
  // both zooms measure real ink rather than a potentially blank center crop.
  const auto fallback = presenter.set_view(zoom, 0, 0, toolbar, now_us());
  print_presentation("gate_fallback", presenter, fallback);
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = zoom,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  const std::int64_t started = esp_timer_get_time();
  std::int64_t maximum_supertask_us = 0;
  std::int64_t presentation_us = 0;
  std::size_t steps = 0;
  std::size_t operations_scanned = 0;
  std::size_t operations_rendered = 0;
  std::size_t tiles_published = 0;
  while (true) {
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    const std::int64_t step_us = esp_timer_get_time() - step_started;
    maximum_supertask_us = std::max(maximum_supertask_us, step_us);
    if (!step.has_value()) {
      return false;
    }
    if (step->tiles_published != 0U) {
      const auto present_started = esp_timer_get_time();
      const auto timing = presenter.refresh_region(step->level_bounds);
      presentation_us += esp_timer_get_time() - present_started;
      if (!timing.passed) {
        return false;
      }
    }
    ++steps;
    operations_scanned += step->operations_scanned;
    operations_rendered += step->operations_rendered;
    tiles_published += step->tiles_published;
    if (step->complete) {
      break;
    }
  }
  const std::int64_t total_us = esp_timer_get_time() - started;
  // The current hard-edged cold path is explicitly accepted up to 0.75 s;
  // cache hits and interaction slices retain their tighter gates.
  const bool passed = total_us < 750'000 && maximum_supertask_us < 30'000;
  std::printf(
      "TINYDRAW_GATE1_HARD zoom=%s cold=1 operations=%lu samples=%lu steps=%lu tiles=%lu "
      "scanned=%lu rendered=%lu max_supertask_us=%lld presentation_us=%lld total_us=%lld "
      "pass=%u\n",
      zoom_name(zoom), static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<unsigned long>(steps),
      static_cast<unsigned long>(tiles_published), static_cast<unsigned long>(operations_scanned),
      static_cast<unsigned long>(operations_rendered), static_cast<long long>(maximum_supertask_us),
      static_cast<long long>(presentation_us), static_cast<long long>(total_us), passed);
  return passed;
}

bool run_draw_while_fill_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const ToolbarState& toolbar,
                              const IncrementalDocumentWorkspace& workspace,
                              std::span<CompactOperationSample> interaction_samples) {
  const auto fallback = presenter.set_view(ZoomLevel::k400Percent, 0, 0, toolbar, now_us());
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const vector_v2::ViewRequest view{
      .zoom = ZoomLevel::k400Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  const auto initial = producer.produce_next(view);
  if (!initial.has_value() || initial->complete || initial->tiles_published != 0U) {
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
  const auto live = presenter.show_start(preview, 0x001FU, event_us);

  if (interaction_samples.size() < 8U) {
    return false;
  }
  auto fast_xl = interaction_samples.first(8U);
  for (std::size_t index = 0; index < fast_xl.size(); ++index) {
    fast_xl[index] = {
        .x_quarter = static_cast<std::uint16_t>(20U + index * 48U),
        .y_quarter = static_cast<std::uint16_t>(index % 2U == 0U ? 180U : 240U),
        // XL is 20 screen pixels; at 400% that is 5 world units.
        .radius_256 = 1'280U,
        .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
    };
  }
  const std::int64_t append_started = esp_timer_get_time();
  const auto append = vector_v2::append_incrementally(
      log, canvas, {.tool = OperationTool::kPen, .color = 0x001FU, .samples = fast_xl}, workspace,
      {.priority_view = view});
  const std::int64_t append_us = esp_timer_get_time() - append_started;
  const bool stale_rejected = !producer.produce_next(view).has_value();

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
      if (!presenter.refresh_region(step->level_bounds).passed) {
        return false;
      }
      maximum_slice_us = std::max(maximum_slice_us, esp_timer_get_time() - present_started);
    }
    fill_complete = step->complete;
  }
  const std::int64_t fill_us = esp_timer_get_time() - fill_started;
  const bool passed = append.has_value() && stale_rejected && live.passed &&
                      live.first_submit_us < 100'000 && poll_gap_us < 35'000 &&
                      maximum_compute_slice_us < 30'000 && maximum_slice_us < 75'000;
  std::printf(
      "TINYDRAW_GATE1_DRAW_FILL zoom=400 revision=%lu append_us=%lld poll_gap_us=%lld "
      "event_submit_us=%lld event_complete_us=%lld max_compute_slice_us=%lld "
      "max_display_slice_us=%lld fill_us=%lld "
      "stale_rejected=%u pass=%u\n",
      static_cast<unsigned long>(canvas.current_revision().value),
      static_cast<long long>(append_us), static_cast<long long>(poll_gap_us),
      static_cast<long long>(live.first_submit_us), static_cast<long long>(live.first_complete_us),
      static_cast<long long>(maximum_compute_slice_us), static_cast<long long>(maximum_slice_us),
      static_cast<long long>(fill_us), stale_rejected, passed);
  return passed;
}

bool verify_pan_adapter(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                        const ToolbarState& toolbar, ZoomLevel zoom) {
  constexpr int kPanDelta = 24;
  const vector_v2::ViewRequest destination{
      .zoom = zoom,
      .level_pixels = {kPanDelta, kPanDelta, kPanDelta + vector_v2::kOverviewWidth,
                       kPanDelta + vector_v2::kOverviewHeight},
  };
  while (true) {
    const auto remaining = producer.visible_tiles_remaining(destination);
    if (!remaining.has_value()) {
      return false;
    }
    if (*remaining == 0U) {
      break;
    }
    if (!producer.produce_next(destination).has_value()) {
      return false;
    }
  }
  const auto setup = presenter.set_view(zoom, 0, 0, toolbar, now_us());
  const int before_x = presenter.level_x();
  const int before_y = presenter.level_y();
  const auto pan = presenter.pan_from(before_x, before_y, {240.0F, 240.0F},
                                      {240.0F - kPanDelta, 240.0F - kPanDelta}, toolbar, now_us());
  const bool moved = presenter.level_x() > before_x && presenter.level_y() > before_y;
  std::printf(
      "TINYDRAW_GATE1_PAN zoom=%s from_x=%d from_y=%d to_x=%d to_y=%d compose_us=%lld "
      "event_submit_us=%lld event_complete_us=%lld transfer_us=%lld setup=%u present=%u moved=%u "
      "frame_reused=%u pass=%u\n",
      zoom_name(zoom), before_x, before_y, presenter.level_x(), presenter.level_y(),
      static_cast<long long>(pan.compose_us), static_cast<long long>(pan.first_submit_us),
      static_cast<long long>(pan.first_complete_us), static_cast<long long>(pan.complete_us),
      setup.passed, pan.passed, moved, pan.frame_reused,
      setup.passed && pan.passed && moved && pan.frame_reused && pan.first_complete_us < 35'000);
  return setup.passed && pan.passed && moved && pan.frame_reused && pan.first_complete_us < 35'000;
}

bool run_cache_retention_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              MaterializedCanvas& canvas, const ToolbarState& toolbar) {
  constexpr std::array zooms{
      ZoomLevel::k50Percent,
      ZoomLevel::k100Percent,
      ZoomLevel::k200Percent,
      ZoomLevel::k400Percent,
  };
  constexpr int kUnalignedOrigin = vector_v2::kTileWidth - 1;
  constexpr int kDisjointOrigin = 9 * vector_v2::kTileWidth - 1;
  const auto fill = [&](ZoomLevel zoom, int x, int y) {
    const auto fallback = presenter.set_view(zoom, x, y, toolbar, now_us());
    if (!fallback.passed) {
      return false;
    }
    const vector_v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {presenter.level_x(), presenter.level_y(),
                         presenter.level_x() + vector_v2::kOverviewWidth,
                         presenter.level_y() + vector_v2::kOverviewHeight},
    };
    const std::int64_t started = esp_timer_get_time();
    std::size_t published = 0;
    while (true) {
      const auto step = producer.produce_next(view);
      if (!step.has_value()) {
        return false;
      }
      if (step->tiles_published != 0U) {
        published += step->tiles_published;
        if (!presenter.refresh_region(step->level_bounds).passed) {
          return false;
        }
      }
      if (step->complete) {
        break;
      }
    }
    const std::int64_t elapsed_us = esp_timer_get_time() - started;
    std::printf(
        "TINYDRAW_GATE1_CACHE_FILL zoom=%s x=%d y=%d published=%lu total_us=%lld "
        "within_cold_gate=%u complete=1\n",
        zoom_name(zoom), presenter.level_x(), presenter.level_y(),
        static_cast<unsigned long>(published), static_cast<long long>(elapsed_us),
        elapsed_us < 500'000);
    return true;
  };

  bool passed = canvas.discard_tiles();
  for (const ZoomLevel zoom : zooms) {
    passed = fill(zoom, kUnalignedOrigin, kUnalignedOrigin) && passed;
  }
  for (const ZoomLevel zoom : zooms) {
    const vector_v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {kUnalignedOrigin, kUnalignedOrigin,
                         kUnalignedOrigin + vector_v2::kOverviewWidth,
                         kUnalignedOrigin + vector_v2::kOverviewHeight},
    };
    const auto remaining = producer.visible_tiles_remaining(view);
    const auto revisit =
        presenter.set_view(zoom, kUnalignedOrigin, kUnalignedOrigin, toolbar, now_us());
    const bool hit = remaining == 0U && revisit.passed && revisit.fallback_pixels == 0U;
    std::printf(
        "TINYDRAW_GATE1_CACHE_REVISIT zoom=%s remaining=%lu tile_pixels=%lu fallback_pixels=%lu "
        "compose_us=%lld complete_us=%lld hit=%u\n",
        zoom_name(zoom), static_cast<unsigned long>(remaining.value_or(999U)),
        static_cast<unsigned long>(revisit.tile_pixels),
        static_cast<unsigned long>(revisit.fallback_pixels),
        static_cast<long long>(revisit.compose_us), static_cast<long long>(revisit.complete_us),
        hit);
    passed = hit && passed;
  }

  passed = fill(ZoomLevel::k400Percent, kDisjointOrigin, kDisjointOrigin) && passed;
  bool every_round_trip_hit = true;
  for (const ZoomLevel zoom : zooms) {
    const vector_v2::ViewRequest origin{
        .zoom = zoom,
        .level_pixels = {kUnalignedOrigin, kUnalignedOrigin,
                         kUnalignedOrigin + vector_v2::kOverviewWidth,
                         kUnalignedOrigin + vector_v2::kOverviewHeight},
    };
    const auto origin_remaining = producer.visible_tiles_remaining(origin);
    const auto round_trip =
        presenter.set_view(zoom, kUnalignedOrigin, kUnalignedOrigin, toolbar, now_us());
    const bool hit =
        origin_remaining == 0U && round_trip.passed && round_trip.fallback_pixels == 0U;
    std::printf(
        "TINYDRAW_GATE1_CACHE_ROUND_TRIP zoom=%s from_x=%d from_y=%d via_x=%d via_y=%d "
        "remaining=%lu tile_pixels=%lu fallback_pixels=%lu compose_us=%lld complete_us=%lld "
        "hit=%u\n",
        zoom_name(zoom), kUnalignedOrigin, kUnalignedOrigin, kDisjointOrigin, kDisjointOrigin,
        static_cast<unsigned long>(origin_remaining.value_or(999U)),
        static_cast<unsigned long>(round_trip.tile_pixels),
        static_cast<unsigned long>(round_trip.fallback_pixels),
        static_cast<long long>(round_trip.compose_us),
        static_cast<long long>(round_trip.complete_us), hit);
    every_round_trip_hit = hit && every_round_trip_hit;
  }
  passed = every_round_trip_hit && passed;
  std::printf("TINYDRAW_GATE1_CACHE_RETENTION pass=%u slots=%lu revision=%lu\n", passed,
              static_cast<unsigned long>(canvas.slot_capacity()),
              static_cast<unsigned long>(canvas.current_revision().value));
  return passed;
}

bool run_full_world_cache_gate(vector_v2::TileProducer& producer, MaterializedCanvas& canvas) {
  if (!canvas.discard_tiles()) {
    return false;
  }
  constexpr ZoomLevel kZoom = ZoomLevel::k100Percent;
  constexpr int kGroupPixels = vector_v2::kTileProducerWidth;
  const std::int64_t started = esp_timer_get_time();
  for (int y = 0; y < vector_v2::kWorldHeight; y += kGroupPixels) {
    for (int x = 0; x < vector_v2::kWorldWidth; x += kGroupPixels) {
      const vector_v2::ViewRequest view{
          .zoom = kZoom,
          .level_pixels = {x, y, std::min(x + kGroupPixels, vector_v2::kWorldWidth),
                           std::min(y + kGroupPixels, vector_v2::kWorldHeight)},
      };
      while (true) {
        const auto step = producer.produce_next(view);
        if (!step.has_value()) {
          return false;
        }
        if (step->complete) {
          break;
        }
      }
    }
  }

  const vector_v2::TileGrid grid = vector_v2::tile_grid(kZoom);
  std::size_t raw = 0;
  std::size_t uniform = 0;
  std::size_t fallback = 0;
  for (int row = 0; row < grid.rows; ++row) {
    for (int column = 0; column < grid.columns; ++column) {
      const auto source = canvas.lookup(
          {kZoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      if (!source.has_value() || source->kind == vector_v2::SourceKind::kOverview) {
        ++fallback;
      } else if (source->kind == vector_v2::SourceKind::kUniform) {
        ++uniform;
      } else {
        ++raw;
      }
    }
  }
  const std::size_t identities =
      static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows);
  const bool passed =
      fallback == 0U && raw <= canvas.slot_capacity() && raw + uniform == identities;
  std::printf(
      "TINYDRAW_PAPER_SWEEP zoom=100 identities=%lu raw=%lu uniform=%lu fallback=%lu "
      "slots=%lu total_us=%lld pass=%u\n",
      static_cast<unsigned long>(identities), static_cast<unsigned long>(raw),
      static_cast<unsigned long>(uniform), static_cast<unsigned long>(fallback),
      static_cast<unsigned long>(canvas.slot_capacity()),
      static_cast<long long>(esp_timer_get_time() - started), passed);
  return passed;
}

bool verify_export_reserve() {
  const std::size_t free_before = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_before = heap_caps_get_largest_free_block(kExternalCaps);
  void* reserve = heap_caps_malloc(vector_v2::kTargetContiguousReserveBytes, kExternalCaps);
  const std::size_t free_held = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_held = heap_caps_get_largest_free_block(kExternalCaps);
  const bool passed = reserve != nullptr;
  heap_caps_free(reserve);
  std::printf(
      "TINYDRAW_EXPORT_RESERVE requested=%lu free_before=%lu largest_before=%lu free_held=%lu "
      "largest_held=%lu pass=%u\n",
      static_cast<unsigned long>(vector_v2::kTargetContiguousReserveBytes),
      static_cast<unsigned long>(free_before), static_cast<unsigned long>(largest_before),
      static_cast<unsigned long>(free_held), static_cast<unsigned long>(largest_held), passed);
  return passed;
}

bool append_stress_document(OperationLog& log, MaterializedCanvas& canvas,
                            const IncrementalDocumentWorkspace& workspace) {
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
          .x_quarter = static_cast<std::uint16_t>(x * 4.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 4.0F),
          .radius_256 = static_cast<std::uint16_t>((3U + operation % 6U) * 256U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    const std::int64_t append_started = esp_timer_get_time();
    const auto result = vector_v2::append_incrementally(
        log, canvas,
        {.tool = operation % 11U == 10U ? OperationTool::kEraser : OperationTool::kPen,
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

}  // namespace

void run_vector_v2_app() {
  AppStorage storage;
  if (!storage.allocate()) {
    std::printf("TINYDRAW_LIVE_FAIL reason=allocation free_psram=%lu largest_psram=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
    return;
  }
  std::fill_n(storage.snapshot, vector_v2::kOverviewPixels, 0xFFFFU);
  std::fill_n(storage.overview, vector_v2::kOverviewPixels, 0xFFFFU);

  MaterializedCanvas canvas(
      std::span(storage.overview, vector_v2::kOverviewPixels),
      std::span(storage.uniforms, vector_v2::kMaterializedTileIdentityCount),
      std::span(storage.occupancy, vector_v2::kOccupancyBytes),
      std::span(storage.slots, vector_v2::kTileSlotCount),
      std::span(storage.tile_pixels, vector_v2::kTileSlotCount * vector_v2::kTilePixels));
  OperationLog log(std::span(storage.records, vector_v2::kOperationCapacity),
                   std::span(storage.samples, vector_v2::kOperationSampleCapacity));
  std::array<DisplayStrip, 3> queue{};
  DisplayScheduler scheduler(queue);
  Co5300PanelTransport display;
  PhysicalTouch touch;
  VectorV2Presenter presenter(canvas, scheduler, display,
                              std::span(storage.frame, vector_v2::kOverviewPixels),
                              std::span(storage.region_scratch, kLiveRegionScratchPixels));
  OperationBuilder builder(std::span(storage.input_samples, kInputSampleCapacity));
  vector_v2::TileProducer producer(
      log, canvas,
      {.supertask_pixels = std::span(storage.producer_supertask, vector_v2::kTileProducerPixels),
       .packed_tile_pixels = std::span(storage.producer_packed, vector_v2::kTilePixels)});
  const IncrementalDocumentWorkspace workspace{
      .overview_scratch = std::span(storage.overview_scratch, vector_v2::kOverviewPixels),
      .tile_scratch =
          std::span(storage.tile_scratch, kWorkspaceTileCapacity * vector_v2::kTilePixels),
      .publications = std::span(storage.publications, kWorkspaceTileCapacity),
      .affected_keys = std::span(storage.affected_keys, vector_v2::kTileSlotCount),
  };
  if (!canvas.publish_overview({0}, std::span(storage.snapshot, vector_v2::kOverviewPixels)) ||
      !log.ready() || !presenter.ready() || !touch.ready() || !builder.ready() ||
      !producer.ready()) {
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

  ToolbarState toolbar;
  toolbar.tool = DrawingTool::kPen;
  toolbar.color = InkColor::kBlue;
  toolbar.size = PenSize::kLarge;
  InkConfig ink_config;
  ink_config.size = brush_size(toolbar.size);
  InkStream ink(ink_config);
  CurvedRibbonStream ribbon;
  const auto initial = presenter.refresh(toolbar);
  print_presentation("startup", presenter, initial);
  const bool stress_ready = append_stress_document(log, canvas, workspace);
  const bool stress_100 = stress_ready && run_tile_gate(presenter, producer, log, canvas, toolbar,
                                                        ZoomLevel::k100Percent);
  const bool stress_400 = stress_100 && run_tile_gate(presenter, producer, log, canvas, toolbar,
                                                      ZoomLevel::k400Percent);
  const DocumentRevision realistic_baseline{canvas.current_revision().value + 1U};
  const bool reset_for_realistic = stress_400 &&
                                   vector_v2::restore_document_snapshot(
                                       log, canvas, realistic_baseline,
                                       std::span(storage.snapshot, vector_v2::kOverviewPixels)) &&
                                   producer.reset_uniform_baseline(realistic_baseline);
  const bool workload_ready =
      reset_for_realistic &&
      load_realistic_document(log, canvas, workspace,
                              std::span(storage.realistic_strokes, kRealisticStrokeCapacity),
                              std::span(storage.realistic_samples, kRealisticSampleCapacity),
                              std::span(storage.input_samples, kInputSampleCapacity));
#ifdef TINYDRAW_VECTOR_V2_TILE_CENSUS
  const bool census =
      workload_ready &&
      run_vector_v2_tile_census(producer, canvas,
                                std::span(storage.producer_packed, vector_v2::kTilePixels));
  std::printf("TINYDRAW_TILE_CENSUS_APP_DONE workload=%u census=%u revision=%lu\n", workload_ready,
              census, static_cast<unsigned long>(canvas.current_revision().value));
  std::fflush(stdout);
  return;
#endif
  const bool gate_100 = workload_ready && run_tile_gate(presenter, producer, log, canvas, toolbar,
                                                        ZoomLevel::k100Percent);
  const bool pan_100 =
      gate_100 && verify_pan_adapter(presenter, producer, toolbar, ZoomLevel::k100Percent);
  const bool gate_400 =
      pan_100 && run_tile_gate(presenter, producer, log, canvas, toolbar, ZoomLevel::k400Percent);
  const bool pan_400 =
      gate_400 && verify_pan_adapter(presenter, producer, toolbar, ZoomLevel::k400Percent);
  const bool draw_fill =
      pan_400 && run_draw_while_fill_gate(presenter, producer, log, canvas, toolbar, workspace,
                                          std::span(storage.input_samples, kInputSampleCapacity));
  const bool cache_retention =
      draw_fill && run_cache_retention_gate(presenter, producer, canvas, toolbar);
  const bool full_world_cache = cache_retention && run_full_world_cache_gate(producer, canvas);
  const bool export_reserve = full_world_cache && verify_export_reserve();
  const auto return_overview = presenter.set_view(ZoomLevel::k25Percent, 0, 0, toolbar, now_us());
  const std::size_t overview_bytes = vector_v2::kOverviewPixels * 4U * sizeof(std::uint16_t);
  const std::size_t raw_tile_bytes =
      vector_v2::kTileSlotCount * vector_v2::kTilePixels * sizeof(std::uint16_t);
  const std::size_t tile_metadata_bytes =
      vector_v2::kTileSlotCount * sizeof(MaterializedSlotStorage) +
      vector_v2::kMaterializedTileIdentityCount * sizeof(MaterializedUniformStorage) +
      vector_v2::kOccupancyBytes;
  const std::size_t operation_bytes =
      vector_v2::kOperationCapacity * sizeof(OperationRecord) +
      vector_v2::kOperationSampleCapacity * sizeof(CompactOperationSample);
  const std::size_t live_scratch_bytes =
      kWorkspaceTileCapacity * vector_v2::kTilePixels * sizeof(std::uint16_t) +
      (vector_v2::kTileProducerPixels + kLiveRegionScratchPixels) * sizeof(std::uint16_t) +
      vector_v2::kTilePixels * sizeof(std::uint16_t) +
      kInputSampleCapacity * sizeof(CompactOperationSample) +
      kWorkspaceTileCapacity * sizeof(TileRevisionPublication) +
      vector_v2::kTileSlotCount * sizeof(TileKey);
  const std::size_t corpus_bytes = kRealisticStrokeCapacity * sizeof(VectorStroke) +
                                   kRealisticSampleCapacity * sizeof(StrokeSample);
  const std::size_t live_storage_bytes = overview_bytes + raw_tile_bytes + tile_metadata_bytes +
                                         operation_bytes + live_scratch_bytes + corpus_bytes;
  std::printf(
      "TINYDRAW_GATE1_AUTOMATED_DONE stress=%u stress_100=%u stress_400=%u workload=%u "
      "hard_100=%u hard_400=%u pan_100=%u pan_400=%u draw_fill=%u cache=%u "
      "full_world_cache=%u export_reserve=%u return=%u ssaa_receipt=yellow\n",
      stress_ready, stress_100, stress_400, workload_ready, gate_100, gate_400, pan_100, pan_400,
      draw_fill, cache_retention, full_world_cache, export_reserve, return_overview.passed);
  std::printf(
      "TINYDRAW_LIVE_READY zoom=25 controls=toolbar button=cycle_25_100_400 "
      "long_button=load_1000 operations_capacity=%lu samples_capacity=%lu live_storage_bytes=%lu "
      "overview_bytes=%lu raw_tile_bytes=%lu tile_metadata_bytes=%lu operation_bytes=%lu "
      "live_scratch_bytes=%lu corpus_bytes=%lu free_psram=%lu largest_psram=%lu\n",
      static_cast<unsigned long>(log.operation_capacity()),
      static_cast<unsigned long>(log.sample_capacity()),
      static_cast<unsigned long>(live_storage_bytes), static_cast<unsigned long>(overview_bytes),
      static_cast<unsigned long>(raw_tile_bytes), static_cast<unsigned long>(tile_metadata_bytes),
      static_cast<unsigned long>(operation_bytes), static_cast<unsigned long>(live_scratch_bytes),
      static_cast<unsigned long>(corpus_bytes),
      static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
  std::fflush(stdout);

  bool pressed = false;
  bool toolbar_pressed = false;
  bool panning = false;
  int lift_reads = 0;
  Point last_touch{};
  Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  Point pan_start{};
  int pan_start_x = 0;
  int pan_start_y = 0;
  InkPoint last_ink{};
  LiveMetrics live_metrics{};
  std::uint32_t poll_previous_us = now_us();
  std::uint32_t poll_max_us = 0;
  std::uint32_t touch_errors = 0;
  bool button_down = false;
  bool button_long_handled = false;
  std::uint32_t button_down_us = 0;
  ZoomLevel fill_zoom = ZoomLevel::k25Percent;
  int fill_x = 0;
  int fill_y = 0;
  DocumentRevision fill_revision = canvas.current_revision();
  bool fill_complete = true;

  for (;;) {
    const std::uint32_t loop_us = now_us();
    poll_max_us = std::max(poll_max_us, loop_us - poll_previous_us);
    poll_previous_us = loop_us;

    const bool next_button_down = gpio_get_level(kModeButton) == 0;
    if (next_button_down && !button_down) {
      button_down = true;
      button_long_handled = false;
      button_down_us = loop_us;
    } else if (button_down && next_button_down && !button_long_handled &&
               loop_us - button_down_us >= kButtonLongPressUs) {
      button_long_handled = true;
      const bool loaded = append_stress_document(log, canvas, workspace);
      const auto timing = presenter.refresh(toolbar, loop_us);
      print_presentation("stress", presenter, timing);
      std::printf("TINYDRAW_LIVE_STRESS_DONE pass=%u authority_match=%u\n", loaded && timing.passed,
                  log.current_revision() == canvas.current_revision());
    } else if (!next_button_down && button_down) {
      button_down = false;
      if (!button_long_handled) {
        const ZoomLevel zoom = next_test_zoom(presenter.zoom());
        const auto timing = presenter.set_view(zoom, 0, 0, toolbar, loop_us);
        print_presentation("zoom", presenter, timing);
      }
    }

    Point point{};
    const TouchRead read = touch.read(point);
    if (read == TouchRead::kError) {
      ++touch_errors;
    }
    const bool touching = read == TouchRead::kPoint;
    if (touching) {
      lift_reads = 0;
      if (!pressed) {
        pressed = true;
        last_touch = point;
        if (toolbar_contains(point, toolbar)) {
          toolbar_pressed = true;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (toolbar.tool == DrawingTool::kPan) {
          panning = true;
          pan_start = point;
          pan_start_x = presenter.level_x();
          pan_start_y = presenter.level_y();
        } else {
          ink_config.size = brush_size(toolbar.size);
          ink.set_config(ink_config);
          last_ink = ink.begin({.x = point.x, .y = point.y, .timestamp_us = loop_us});
          const OperationTool tool =
              toolbar.tool == DrawingTool::kEraser ? OperationTool::kEraser : OperationTool::kPen;
          const std::uint16_t color =
              tool == OperationTool::kEraser ? 0xFFFFU : rgb565(toolbar.color);
          if (!builder.begin(tool, color, presenter.operation_point(last_ink))) {
            ink.end();
          } else {
            ribbon.reset();
            static_cast<void>(ribbon.append(last_ink, false));
            live_metrics.include(presenter.show_start(last_ink, color, loop_us));
          }
        }
      } else if (toolbar_pressed && (point.x != last_touch.x || point.y != last_touch.y)) {
        toolbar_sum.x += point.x;
        toolbar_sum.y += point.y;
        ++toolbar_samples;
        last_touch = point;
      } else if (panning && (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        const auto timing =
            presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, toolbar, loop_us);
        if (timing.passed) {
          print_presentation("pan", presenter, timing);
        }
      } else if (ink.active() && (point.x != last_touch.x || point.y != last_touch.y)) {
        last_touch = point;
        last_ink = ink.update({.x = point.x, .y = point.y, .timestamp_us = loop_us});
        if (!builder.add(presenter.operation_point(last_ink))) {
          builder.cancel();
          ribbon.reset();
          ink.end();
        } else {
          const std::uint16_t color =
              toolbar.tool == DrawingTool::kEraser ? 0xFFFFU : rgb565(toolbar.color);
          live_metrics.include(
              presenter.show_update(ribbon.append(last_ink, false), color, loop_us));
        }
      }
    } else if (pressed && ++lift_reads >= kLiftReads) {
      pressed = false;
      lift_reads = 0;
      if (toolbar_pressed) {
        toolbar_pressed = false;
        const float divisor = static_cast<float>(std::max<std::uint32_t>(1U, toolbar_samples));
        const Point tap{toolbar_sum.x / divisor, toolbar_sum.y / divisor};
        toolbar_samples = 0;
        if (toolbar_contains(tap, toolbar)) {
          static_cast<void>(apply_toolbar_action(
              toolbar_action_at(tap, toolbar), tap, toolbar, log, canvas, producer,
              std::span(storage.snapshot, vector_v2::kOverviewPixels), presenter));
        }
      } else if (panning) {
        panning = false;
      } else if (ink.active()) {
        const std::uint32_t finished_us = now_us();
        last_ink = ink.finish({.x = last_touch.x, .y = last_touch.y, .timestamp_us = finished_us});
        const std::uint16_t color =
            toolbar.tool == DrawingTool::kEraser ? 0xFFFFU : rgb565(toolbar.color);
        live_metrics.include(presenter.show_update(ribbon.finish(last_ink), color, finished_us));
        const auto append = builder.finish(presenter.operation_point(last_ink));
        std::int64_t append_us = 0;
        bool committed = false;
        if (append.has_value()) {
          const std::int64_t append_started = esp_timer_get_time();
          const vector_v2::ViewRequest priority_view{
              .zoom = presenter.zoom(),
              .level_pixels = {presenter.level_x(), presenter.level_y(),
                               presenter.level_x() + vector_v2::kOverviewWidth,
                               presenter.level_y() + vector_v2::kOverviewHeight},
          };
          committed = vector_v2::append_incrementally(
                          log, canvas, *append, workspace,
                          {.priority_view = presenter.zoom() == ZoomLevel::k25Percent
                                                ? std::optional<vector_v2::ViewRequest>{}
                                                : std::optional{priority_view}})
                          .has_value();
          append_us = esp_timer_get_time() - append_started;
        }
        builder.cancel();
        ribbon.reset();
        const auto refresh = presenter.refresh(toolbar, finished_us);
        print_stroke(log, canvas, live_metrics, append_us, refresh, poll_max_us, touch_errors);
        std::printf("TINYDRAW_LIVE_STROKE_DONE committed=%u refresh=%u overflow=%u\n", committed,
                    refresh.passed, builder.overflowed());
        live_metrics = {};
        poll_max_us = 0;
        touch_errors = 0;
        std::fflush(stdout);
      }
    }

    if (!pressed && presenter.zoom() != ZoomLevel::k25Percent && !toolbar.tools_open &&
        !toolbar.colors_open && !toolbar.sizes_open && !toolbar.confirm_new) {
      const vector_v2::ViewRequest fill_view{
          .zoom = presenter.zoom(),
          .level_pixels = {presenter.level_x(), presenter.level_y(),
                           presenter.level_x() + vector_v2::kOverviewWidth,
                           presenter.level_y() + vector_v2::kOverviewHeight},
      };
      if (fill_zoom != fill_view.zoom || fill_x != fill_view.level_pixels.x0 ||
          fill_y != fill_view.level_pixels.y0 || fill_revision != canvas.current_revision()) {
        fill_zoom = fill_view.zoom;
        fill_x = fill_view.level_pixels.x0;
        fill_y = fill_view.level_pixels.y0;
        fill_revision = canvas.current_revision();
        fill_complete = false;
      }
      if (!fill_complete) {
        const auto step = producer.produce_next(fill_view);
        if (step.has_value()) {
          if (step->tiles_published != 0U) {
            static_cast<void>(presenter.refresh_region(step->level_bounds));
          }
          fill_complete = step->complete;
          if (fill_complete) {
            std::printf("TINYDRAW_LIVE_FILL_DONE zoom=%s x=%d y=%d revision=%lu\n",
                        zoom_name(fill_zoom), fill_x, fill_y,
                        static_cast<unsigned long>(fill_revision.value));
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_vector_v2_app(); }
