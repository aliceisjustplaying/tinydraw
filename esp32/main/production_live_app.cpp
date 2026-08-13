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
#include "production_live_presenter.h"
#include "tinydraw/document/realistic_workload.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/production/incremental_document.h"
#include "tinydraw/production/memory_layout.h"
#include "tinydraw/production/operation_builder.h"
#include "tinydraw/production/operation_log.h"
#include "tinydraw/production/tile_producer.h"
#include "tinydraw/ui/toolbar.h"

namespace tinydraw::esp32 {
namespace {

using production::CompactOperationSample;
using production::DisplayScheduler;
using production::DisplayStrip;
using production::DocumentRevision;
using production::IncrementalDocumentWorkspace;
using production::MaterializedCanvas;
using production::MaterializedSlotStorage;
using production::OperationBuilder;
using production::OperationLog;
using production::OperationPoint;
using production::OperationRecord;
using production::OperationTool;
using production::TileKey;
using production::TileRevisionPublication;
using production::ZoomLevel;

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr gpio_num_t kModeButton = GPIO_NUM_0;
constexpr int kLiftReads = 2;
constexpr std::size_t kInputSampleCapacity = 1'024;
constexpr std::size_t kWorkspaceTileCapacity = 16;
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
  MaterializedSlotStorage* slots = nullptr;
  VectorStroke* realistic_strokes = nullptr;
  StrokeSample* realistic_samples = nullptr;
  OperationRecord* records = nullptr;
  CompactOperationSample* samples = nullptr;
  CompactOperationSample* input_samples = nullptr;

  [[nodiscard]] bool allocate() {
    overview = allocate_array<std::uint16_t>(production::kOverviewPixels);
    snapshot = allocate_array<std::uint16_t>(production::kOverviewPixels);
    frame = allocate_array<std::uint16_t>(production::kOverviewPixels);
    tile_pixels =
        allocate_array<std::uint16_t>(production::kTileSlotCount * production::kTilePixels);
    overview_scratch = allocate_array<std::uint16_t>(production::kOverviewPixels);
    tile_scratch = allocate_array<std::uint16_t>(kWorkspaceTileCapacity * production::kTilePixels);
    region_scratch = allocate_array<std::uint16_t>(production::kTileProducerPixels);
    producer_supertask = allocate_array<std::uint16_t>(production::kTileProducerPixels);
    producer_packed = allocate_array<std::uint16_t>(production::kTilePixels);
    slots = allocate_array<MaterializedSlotStorage>(production::kTileSlotCount);
    realistic_strokes = allocate_array<VectorStroke>(kRealisticStrokeCapacity);
    realistic_samples = allocate_array<StrokeSample>(kRealisticSampleCapacity);
    records = allocate_array<OperationRecord>(production::kOperationCapacity);
    samples = allocate_array<CompactOperationSample>(production::kOperationSampleCapacity);
    input_samples = allocate_array<CompactOperationSample>(kInputSampleCapacity);
    if (overview == nullptr || snapshot == nullptr || frame == nullptr || tile_pixels == nullptr ||
        overview_scratch == nullptr || tile_scratch == nullptr || region_scratch == nullptr ||
        producer_supertask == nullptr || producer_packed == nullptr || slots == nullptr ||
        realistic_strokes == nullptr || realistic_samples == nullptr || records == nullptr ||
        samples == nullptr || input_samples == nullptr) {
      return false;
    }
    for (std::size_t index = 0; index < production::kTileSlotCount; ++index) {
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

void print_presentation(const char* kind, const ProductionLivePresenter& presenter,
                        const LivePresentationTiming& timing) {
  std::printf(
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld transfer_wait_us=%lld pushes=%lu pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.pushes), timing.passed);
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
                          std::span<const std::uint16_t> blank_snapshot,
                          ProductionLivePresenter& presenter) {
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
      if (!production::restore_document_snapshot(log, canvas, revision, blank_snapshot)) {
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
                   .x1 = static_cast<float>(production::kWorldWidth),
                   .y1 = static_cast<float>(production::kWorldHeight)};
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
    const auto result = production::append_incrementally(
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

bool run_tile_gate(ProductionLivePresenter& presenter, production::TileProducer& producer,
                   OperationLog& log, MaterializedCanvas& canvas, const ToolbarState& toolbar,
                   ZoomLevel zoom) {
  // The seed-7 corpus fills lines from the upper left. Fixing the origin makes
  // both zooms measure real ink rather than a potentially blank center crop.
  const auto fallback = presenter.set_view(zoom, 0, 0, toolbar, now_us());
  print_presentation("gate_fallback", presenter, fallback);
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const production::ViewRequest view{
      .zoom = zoom,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + production::kOverviewWidth,
                       presenter.level_y() + production::kOverviewHeight},
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
  std::printf(
      "TINYDRAW_GATE1_HARD zoom=%s cold=1 operations=%lu samples=%lu steps=%lu tiles=%lu "
      "scanned=%lu rendered=%lu max_supertask_us=%lld presentation_us=%lld total_us=%lld "
      "pass=%u\n",
      zoom_name(zoom), static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<unsigned long>(steps),
      static_cast<unsigned long>(tiles_published), static_cast<unsigned long>(operations_scanned),
      static_cast<unsigned long>(operations_rendered), static_cast<long long>(maximum_supertask_us),
      static_cast<long long>(presentation_us), static_cast<long long>(total_us),
      total_us < 500'000 && maximum_supertask_us < 30'000);
  return true;
}

bool run_ssaa_gate(ProductionLivePresenter& presenter, production::TileProducer& producer,
                   OperationLog& log, MaterializedCanvas& canvas, const ToolbarState& toolbar) {
  const auto fallback = presenter.set_view(ZoomLevel::k100Percent, 0, 0, toolbar, now_us());
  if (!fallback.passed || !canvas.discard_tiles()) {
    return false;
  }
  const production::ViewRequest view{
      .zoom = ZoomLevel::k100Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + production::kOverviewWidth,
                       presenter.level_y() + production::kOverviewHeight},
  };
  const std::int64_t started = esp_timer_get_time();
  std::int64_t maximum_tile_us = 0;
  std::int64_t presentation_us = 0;
  std::size_t steps = 0;
  std::size_t operations_scanned = 0;
  std::size_t operations_rendered = 0;
  while (true) {
    const std::int64_t step_started = esp_timer_get_time();
    const auto step = producer.produce_next_2x_aa_100(view);
    maximum_tile_us = std::max(maximum_tile_us, esp_timer_get_time() - step_started);
    if (!step.has_value()) {
      return false;
    }
    if (step->tiles_published != 0U) {
      const auto present_started = esp_timer_get_time();
      if (!presenter.refresh_region(step->level_bounds).passed) {
        return false;
      }
      presentation_us += esp_timer_get_time() - present_started;
    }
    ++steps;
    operations_scanned += step->operations_scanned;
    operations_rendered += step->operations_rendered;
    if (step->complete) {
      break;
    }
  }
  const std::int64_t total_us = esp_timer_get_time() - started;
  const bool passed = total_us < 500'000;
  std::printf(
      "TINYDRAW_GATE1_SSAA zoom=100 samples_per_pixel=4 cold=1 operations=%lu samples=%lu "
      "steps=%lu scanned=%lu rendered=%lu max_tile_us=%lld presentation_us=%lld total_us=%lld "
      "pass=%u\n",
      static_cast<unsigned long>(log.operation_count()),
      static_cast<unsigned long>(log.sample_count()), static_cast<unsigned long>(steps),
      static_cast<unsigned long>(operations_scanned),
      static_cast<unsigned long>(operations_rendered), static_cast<long long>(maximum_tile_us),
      static_cast<long long>(presentation_us), static_cast<long long>(total_us), passed);
  return passed;
}

bool verify_pan_adapter(ProductionLivePresenter& presenter, const ToolbarState& toolbar,
                        ZoomLevel zoom) {
  const auto setup = presenter.set_view(zoom, 0, 0, toolbar, now_us());
  const int before_x = presenter.level_x();
  const int before_y = presenter.level_y();
  const auto pan =
      presenter.pan_from(before_x, before_y, {240.0F, 240.0F}, {120.0F, 120.0F}, toolbar, now_us());
  const bool moved = presenter.level_x() > before_x && presenter.level_y() > before_y;
  std::printf(
      "TINYDRAW_GATE1_PAN zoom=%s from_x=%d from_y=%d to_x=%d to_y=%d setup=%u present=%u "
      "moved=%u pass=%u\n",
      zoom_name(zoom), before_x, before_y, presenter.level_x(), presenter.level_y(), setup.passed,
      pan.passed, moved, setup.passed && pan.passed && moved);
  return setup.passed && pan.passed && moved;
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
                                 static_cast<float>(production::kWorldWidth));
      const int wave = static_cast<int>((operation + index) % 9U) - 4;
      const float y = std::clamp(base_y + static_cast<float>(wave * 3), 0.0F,
                                 static_cast<float>(production::kWorldHeight));
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>(x * 4.0F),
          .y_quarter = static_cast<std::uint16_t>(y * 4.0F),
          .radius_256 = static_cast<std::uint16_t>((3U + operation % 6U) * 256U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    const std::int64_t append_started = esp_timer_get_time();
    const auto result = production::append_incrementally(
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

void run_production_live_app() {
  AppStorage storage;
  if (!storage.allocate()) {
    std::printf("TINYDRAW_LIVE_FAIL reason=allocation free_psram=%lu largest_psram=%lu\n",
                static_cast<unsigned long>(heap_caps_get_free_size(kExternalCaps)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(kExternalCaps)));
    return;
  }
  std::fill_n(storage.snapshot, production::kOverviewPixels, 0xFFFFU);
  std::fill_n(storage.overview, production::kOverviewPixels, 0xFFFFU);

  MaterializedCanvas canvas(
      std::span(storage.overview, production::kOverviewPixels),
      std::span(storage.slots, production::kTileSlotCount),
      std::span(storage.tile_pixels, production::kTileSlotCount * production::kTilePixels));
  OperationLog log(std::span(storage.records, production::kOperationCapacity),
                   std::span(storage.samples, production::kOperationSampleCapacity));
  std::array<DisplayStrip, 3> queue{};
  DisplayScheduler scheduler(queue);
  Co5300PanelTransport display;
  PhysicalTouch touch;
  ProductionLivePresenter presenter(
      canvas, scheduler, display, std::span(storage.frame, production::kOverviewPixels),
      std::span(storage.region_scratch, production::kTileProducerPixels));
  OperationBuilder builder(std::span(storage.input_samples, kInputSampleCapacity));
  production::TileProducer producer(
      log, canvas,
      {.supertask_pixels = std::span(storage.producer_supertask, production::kTileProducerPixels),
       .packed_tile_pixels = std::span(storage.producer_packed, production::kTilePixels)});
  std::array<TileRevisionPublication, kWorkspaceTileCapacity> publications{};
  std::array<TileKey, production::kTileSlotCount> affected_keys{};
  const IncrementalDocumentWorkspace workspace{
      .overview_scratch = std::span(storage.overview_scratch, production::kOverviewPixels),
      .tile_scratch =
          std::span(storage.tile_scratch, kWorkspaceTileCapacity * production::kTilePixels),
      .publications = publications,
      .affected_keys = affected_keys,
  };
  if (!canvas.publish_overview({0}, std::span(storage.snapshot, production::kOverviewPixels)) ||
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
  const bool workload_ready = load_realistic_document(
      log, canvas, workspace, std::span(storage.realistic_strokes, kRealisticStrokeCapacity),
      std::span(storage.realistic_samples, kRealisticSampleCapacity),
      std::span(storage.input_samples, kInputSampleCapacity));
  const bool gate_100 = workload_ready && run_tile_gate(presenter, producer, log, canvas, toolbar,
                                                        ZoomLevel::k100Percent);
  const bool gate_400 =
      gate_100 && run_tile_gate(presenter, producer, log, canvas, toolbar, ZoomLevel::k400Percent);
  const bool ssaa_100 = gate_400 && run_ssaa_gate(presenter, producer, log, canvas, toolbar);
  const bool pan_100 = ssaa_100 && verify_pan_adapter(presenter, toolbar, ZoomLevel::k100Percent);
  const bool pan_400 = pan_100 && verify_pan_adapter(presenter, toolbar, ZoomLevel::k400Percent);
  const auto return_overview = presenter.set_zoom(ZoomLevel::k25Percent, toolbar, now_us());
  std::printf(
      "TINYDRAW_GATE1_AUTOMATED_DONE workload=%u hard_100=%u hard_400=%u ssaa_100=%u "
      "pan_100=%u pan_400=%u return=%u\n",
      workload_ready, gate_100, gate_400, ssaa_100, pan_100, pan_400, return_overview.passed);
  std::printf(
      "TINYDRAW_LIVE_READY zoom=25 controls=toolbar button=cycle_25_100_400 "
      "long_button=load_1000 operations_capacity=%lu samples_capacity=%lu free_psram=%lu "
      "largest_psram=%lu\n",
      static_cast<unsigned long>(log.operation_capacity()),
      static_cast<unsigned long>(log.sample_capacity()),
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
        const auto timing = presenter.set_zoom(zoom, toolbar, loop_us);
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
              toolbar_action_at(tap, toolbar), tap, toolbar, log, canvas,
              std::span(storage.snapshot, production::kOverviewPixels), presenter));
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
          committed = production::append_incrementally(log, canvas, *append, workspace).has_value();
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
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_production_live_app(); }
