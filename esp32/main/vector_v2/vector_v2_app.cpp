#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "physical_touch.h"
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
#include "vector_v2_gate_harness.h"
#endif
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/navigation_state.h"
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

struct LiftBaselineTiming {
  std::uint32_t id = 0;
  std::int64_t detected_us = 0;
  std::int64_t finish_preview_us = 0;
  std::int64_t builder_finish_us = 0;
  std::int64_t append_us = 0;
  std::int64_t refresh_wall_us = 0;
  vector_v2::PixelRect refresh_level_bounds{};
  std::int64_t stroke_logging_us = 0;
  LivePresentationTiming refresh{};
  bool committed = false;
  bool overflowed = false;
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
    records = allocate_array<OperationRecord>(vector_v2::kOperationCapacity);
    samples = allocate_array<CompactOperationSample>(vector_v2::kOperationSampleCapacity);
    input_samples = allocate_array<CompactOperationSample>(kInputSampleCapacity);
    publications = allocate_array<TileRevisionPublication>(kWorkspaceTileCapacity);
    affected_keys =
        allocate_array<TileKey>(vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles);
    if (overview == nullptr || snapshot == nullptr || frame == nullptr || tile_pixels == nullptr ||
        overview_scratch == nullptr || tile_scratch == nullptr || region_scratch == nullptr ||
        producer_supertask == nullptr || producer_packed == nullptr || uniforms == nullptr ||
        occupancy == nullptr || slots == nullptr || records == nullptr || samples == nullptr ||
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

std::optional<Point> clip_ink_point(Point previous, Point current) {
  constexpr float kCanvasBottom = static_cast<float>(vector_v2::kChromeCanvasBottom - 1);
  if (current.y <= kCanvasBottom) {
    return current;
  }
  if (previous.y > kCanvasBottom) {
    return std::nullopt;
  }
  const float vertical_distance = current.y - previous.y;
  if (vertical_distance <= 0.0F) {
    return Point{current.x, kCanvasBottom};
  }
  const float progress = (kCanvasBottom - previous.y) / vertical_distance;
  return Point{previous.x + (current.x - previous.x) * progress, kCanvasBottom};
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
      "TINYDRAW_LIVE_PRESENT kind=%s zoom=%s x=%d y=%d compose_us=%lld "
      "read_submit_us=%lld read_complete_us=%lld transfer_wait_us=%lld tile_pixels=%lu "
      "uniform_pixels=%lu overview_pixels=%lu fallback_pixels=%lu resident_tiles=%lu "
      "fallback_tiles=%lu pushes=%lu tear_wait_us=%lld tear_sync=%u frame_reused=%u pass=%u\n",
      kind, zoom_name(presenter.zoom()), presenter.level_x(), presenter.level_y(),
      static_cast<long long>(timing.compose_us), static_cast<long long>(timing.first_submit_us),
      static_cast<long long>(timing.first_complete_us), static_cast<long long>(timing.complete_us),
      static_cast<unsigned long>(timing.tile_pixels),
      static_cast<unsigned long>(timing.uniform_pixels),
      static_cast<unsigned long>(timing.overview_pixels),
      static_cast<unsigned long>(timing.fallback_pixels),
      static_cast<unsigned long>(timing.resident_tiles),
      static_cast<unsigned long>(timing.fallback_tiles), static_cast<unsigned long>(timing.pushes),
      static_cast<long long>(timing.tear_wait_us), timing.tear_synchronized, timing.frame_reused,
      timing.passed);
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
      "compute_total_us=%lld compute_max_us=%lld present_total_us=%lld "
      "present_max_us=%lld tick_max_us=%lld producer_failures=%lu "
      "presentation_failures=%lu\n",
      result, zoom_name(zoom), x, y, static_cast<unsigned long>(revision.value),
      static_cast<unsigned long>(timing.steps), static_cast<long long>(timing.compute_total_us),
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
      "append_us=%lld refresh_wall_us=%lld refresh_x0=%d refresh_y0=%d "
      "refresh_x1=%d refresh_y1=%d refresh_compose_us=%lld "
      "refresh_first_submit_us=%lld refresh_first_complete_us=%lld "
      "refresh_transfer_wait_us=%lld stroke_logging_us=%lld "
      "detected_to_poll_start_us=%lld detected_to_poll_complete_us=%lld poll_read_us=%lld "
      "unattributed_tail_us=%lld reports_dropped=%lu committed=%u refresh=%u overflow=%u\n",
      static_cast<unsigned long>(timing.id), static_cast<long long>(timing.finish_preview_us),
      static_cast<long long>(timing.builder_finish_us), static_cast<long long>(timing.append_us),
      static_cast<long long>(timing.refresh_wall_us), timing.refresh_level_bounds.x0,
      timing.refresh_level_bounds.y0, timing.refresh_level_bounds.x1,
      timing.refresh_level_bounds.y1, static_cast<long long>(timing.refresh.compose_us),
      static_cast<long long>(timing.refresh.first_submit_us),
      static_cast<long long>(timing.refresh.first_complete_us),
      static_cast<long long>(timing.refresh.complete_us),
      static_cast<long long>(timing.stroke_logging_us), static_cast<long long>(detected_to_poll_us),
      static_cast<long long>(poll_completed_us - timing.detected_us),
      static_cast<long long>(poll_completed_us - poll_started_us),
      static_cast<long long>(detected_to_poll_us - measured_phase_us),
      static_cast<unsigned long>(reports_dropped), timing.committed, timing.refresh.passed,
      timing.overflowed);
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

bool apply_chrome_action(vector_v2::ChromeAction action, Point point,
                         vector_v2::ChromeState& chrome, OperationLog& log,
                         MaterializedCanvas& canvas, vector_v2::TileProducer& producer,
                         std::span<const std::uint16_t> blank_snapshot,
                         VectorV2Presenter& presenter) {
  const auto toggle = [&](vector_v2::ChromePopup popup) {
    chrome.popup = chrome.popup == popup ? vector_v2::ChromePopup::kNone : popup;
  };
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
    case vector_v2::ChromeAction::kNewDrawing: {
      const DocumentRevision revision{canvas.current_revision().value + 1U};
      if (!vector_v2::restore_document_snapshot(log, canvas, revision, blank_snapshot) ||
          !producer.reset_uniform_baseline(revision)) {
        return false;
      }
      chrome.popup = vector_v2::ChromePopup::kNone;
      break;
    }
    case vector_v2::ChromeAction::kNone:
    case vector_v2::ChromeAction::kUndo:
    case vector_v2::ChromeAction::kRedo:
    case vector_v2::ChromeAction::kExport:
      break;
  }
  const auto timing = presenter.refresh(chrome, now_us());
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
  vector_v2::NavigationState navigation;
  VectorV2Presenter presenter(canvas, navigation, scheduler, display,
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
      .affected_keys = std::span(storage.affected_keys,
                                 vector_v2::kTileSlotCount + vector_v2::kMaximumVisibleTiles),
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

  vector_v2::ChromeState chrome;
  chrome.tool = vector_v2::ChromeTool::kDraw;
  chrome.size = vector_v2::ChromeSize::kLarge;
  InkConfig ink_config;
  ink_config.size = vector_v2::brush_size(chrome.size);
  InkStream ink(ink_config);
  CurvedRibbonStream ribbon;
  const auto initial = presenter.refresh(chrome);
  print_presentation("startup", presenter, initial);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  if (!run_vector_v2_gate_harness(presenter, producer, log, canvas, chrome, workspace,
                                  std::span(storage.snapshot, vector_v2::kOverviewPixels),
                                  std::span(storage.input_samples, kInputSampleCapacity),
                                  std::span(storage.producer_packed, vector_v2::kTilePixels))) {
    std::printf("TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=0\n");
    return;
  }
  std::printf("TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=1\n");
#endif
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
  const std::size_t live_storage_bytes =
      overview_bytes + raw_tile_bytes + tile_metadata_bytes + operation_bytes + live_scratch_bytes;
  const auto tear_signal = display.tear_signal_timing();
  std::printf(
      "TINYDRAW_VECTOR_V2_READY zoom=25 controls=toolbar button=cycle_all_zooms "
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
  PanMetrics pan_metrics{};
  std::uint32_t poll_previous_us = now_us();
  std::uint32_t poll_max_us = 0;
  std::uint32_t touch_errors = 0;
  bool button_down = false;
  ZoomLevel fill_zoom = ZoomLevel::k25Percent;
  int fill_x = 0;
  int fill_y = 0;
  DocumentRevision fill_revision = canvas.current_revision();
  bool fill_complete = true;
  FillBaselineTiming fill_timing{};
  bool fill_measurement_active = false;
  PendingFillPresentation pending_fill{};
  LiftBaselineTiming lift_timing{};
  std::uint32_t next_lift_id = 1U;
  std::uint32_t lift_reports_dropped = 0U;

  for (;;) {
    const std::uint32_t loop_us = now_us();
    poll_max_us = std::max(poll_max_us, loop_us - poll_previous_us);
    poll_previous_us = loop_us;

    const bool next_button_down = gpio_get_level(kModeButton) == 0;
    if (next_button_down && !button_down) {
      button_down = true;
    } else if (!next_button_down && button_down) {
      button_down = false;
      const ZoomLevel zoom = vector_v2::next_zoom(presenter.zoom());
      const ZoomLevel target = zoom == presenter.zoom() ? ZoomLevel::k25Percent : zoom;
      const auto timing = presenter.set_zoom(target, chrome, loop_us);
      print_presentation("zoom", presenter, timing);
    }

    Point point{};
    const bool idle_before_poll = !pressed;
    const std::int64_t poll_started_us = esp_timer_get_time();
    const TouchRead read = touch.read(point);
    const std::int64_t poll_completed_us = esp_timer_get_time();
    const bool lift_report_ready = lift_timing.pending;
    const LiftBaselineTiming lift_report = lift_timing;
    lift_timing.pending = false;
    if (read == TouchRead::kError) {
      ++touch_errors;
    }
    const bool touching = read == TouchRead::kPoint;
    if (touching) {
      lift_reads = 0;
      if (!pressed) {
        pressed = true;
        last_touch = point;
        if (vector_v2::chrome_contains({point.x, point.y}, chrome)) {
          toolbar_pressed = true;
          toolbar_sum = point;
          toolbar_samples = 1;
        } else if (chrome.tool == vector_v2::ChromeTool::kPan) {
          panning = true;
          pan_metrics.reset();
          pan_start = point;
          pan_start_x = presenter.level_x();
          pan_start_y = presenter.level_y();
        } else {
          ink_config.size = vector_v2::brush_size(chrome.size);
          ink.set_config(ink_config);
          last_ink = ink.begin({.x = point.x, .y = point.y, .timestamp_us = loop_us});
          const OperationTool tool = chrome.tool == vector_v2::ChromeTool::kErase
                                         ? OperationTool::kEraser
                                         : OperationTool::kPen;
          const std::uint16_t color =
              tool == OperationTool::kEraser ? 0xFFFFU : vector_v2::selected_color(chrome);
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
            presenter.pan_from(pan_start_x, pan_start_y, pan_start, point, chrome, loop_us);
        pan_metrics.include(timing);
      } else if (ink.active() && (point.x != last_touch.x || point.y != last_touch.y)) {
        const auto canvas_point = clip_ink_point(last_touch, point);
        last_touch = point;
        if (canvas_point.has_value()) {
          last_ink =
              ink.update({.x = canvas_point->x, .y = canvas_point->y, .timestamp_us = loop_us});
          if (!builder.add(presenter.operation_point(last_ink))) {
            builder.cancel();
            ribbon.reset();
            ink.end();
          } else {
            const std::uint16_t color = chrome.tool == vector_v2::ChromeTool::kErase
                                            ? 0xFFFFU
                                            : vector_v2::selected_color(chrome);
            live_metrics.include(
                presenter.show_update(ribbon.append(last_ink, false), color, loop_us));
          }
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
        if (vector_v2::chrome_contains({tap.x, tap.y}, chrome)) {
          static_cast<void>(apply_chrome_action(
              vector_v2::chrome_action_at({tap.x, tap.y}, chrome), tap, chrome, log, canvas,
              producer, std::span(storage.snapshot, vector_v2::kOverviewPixels), presenter));
        }
      } else if (panning) {
        panning = false;
        print_pan_baseline(presenter, pan_metrics);
        std::fflush(stdout);
      } else if (ink.active()) {
        LiftBaselineTiming measured_lift{
            .id = next_lift_id++,
            .detected_us = esp_timer_get_time(),
        };
        const std::uint32_t finished_us = now_us();
        const std::int64_t finish_preview_started = esp_timer_get_time();
        last_ink = ink.finish(
            {.x = last_ink.position.x, .y = last_ink.position.y, .timestamp_us = finished_us});
        const std::uint16_t color = chrome.tool == vector_v2::ChromeTool::kErase
                                        ? 0xFFFFU
                                        : vector_v2::selected_color(chrome);
        live_metrics.include(presenter.show_update(ribbon.finish(last_ink), color, finished_us));
        measured_lift.finish_preview_us = esp_timer_get_time() - finish_preview_started;

        const std::int64_t builder_finish_started = esp_timer_get_time();
        const auto append = builder.finish(presenter.operation_point(last_ink));
        measured_lift.builder_finish_us = esp_timer_get_time() - builder_finish_started;
        if (append.has_value()) {
          const std::int64_t append_started = esp_timer_get_time();
          const vector_v2::ViewRequest priority_view{
              .zoom = presenter.zoom(),
              .level_pixels = {presenter.level_x(), presenter.level_y(),
                               presenter.level_x() + vector_v2::kOverviewWidth,
                               presenter.level_y() + vector_v2::kOverviewHeight},
          };
          const auto committed = vector_v2::append_incrementally(
              log, canvas, *append, workspace,
              {.priority_view = presenter.zoom() == ZoomLevel::k25Percent
                                    ? std::optional<vector_v2::ViewRequest>{}
                                    : std::optional{priority_view},
               .publication_scope = presenter.zoom() == ZoomLevel::k25Percent
                                        ? vector_v2::IncrementalPublicationScope::kAllMaterialized
                                        : vector_v2::IncrementalPublicationScope::kPriorityView});
          measured_lift.committed = committed.has_value();
          if (committed.has_value()) {
            measured_lift.refresh_level_bounds = vector_v2::operation_level_bounds(
                committed->affected_world_bounds, presenter.zoom());
          }
          measured_lift.append_us = esp_timer_get_time() - append_started;
        }
        measured_lift.overflowed = builder.overflowed();
        builder.cancel();
        ribbon.reset();
        const std::int64_t refresh_started = esp_timer_get_time();
        measured_lift.refresh =
            measured_lift.committed
                ? presenter.refresh_region(measured_lift.refresh_level_bounds, finished_us)
                : LivePresentationTiming{};
        measured_lift.refresh_wall_us = esp_timer_get_time() - refresh_started;
        const std::int64_t logging_started = esp_timer_get_time();
        print_stroke(log, canvas, live_metrics, measured_lift.append_us, measured_lift.refresh,
                     poll_max_us, touch_errors);
        std::printf("TINYDRAW_LIVE_STROKE_DONE committed=%u refresh=%u overflow=%u\n",
                    measured_lift.committed, measured_lift.refresh.passed,
                    measured_lift.overflowed);
        std::fflush(stdout);
        measured_lift.stroke_logging_us = esp_timer_get_time() - logging_started;
        measured_lift.pending = true;
        lift_timing = measured_lift;
        live_metrics = {};
        poll_max_us = 0;
        touch_errors = 0;
      }
    }

    const bool fill_view_available =
        presenter.zoom() != ZoomLevel::k25Percent && chrome.popup == vector_v2::ChromePopup::kNone;
    // The commit tick already performed bounded immediate publication and its
    // display update. Defer cold replay until input has had another poll.
    const bool fill_allowed = !pressed && fill_view_available && !lift_timing.pending;
    if (!fill_view_available && fill_measurement_active) {
      print_fill_baseline("paused", fill_zoom, fill_x, fill_y, fill_revision, fill_timing);
      fill_timing.reset();
      fill_measurement_active = false;
    }
    if (fill_allowed) {
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
        fill_measurement_active = true;
        pending_fill = {};
      }
      if (pending_fill.pending) {
        const bool still_current = pending_fill.zoom == fill_view.zoom &&
                                   pending_fill.x == fill_view.level_pixels.x0 &&
                                   pending_fill.y == fill_view.level_pixels.y0;
        if (!still_current) {
          pending_fill = {};
        } else {
          const std::int64_t present_started = esp_timer_get_time();
          const auto presentation = presenter.refresh_region(pending_fill.level_bounds);
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
          fill_measurement_active = true;
        }
        const std::int64_t fill_tick_started = esp_timer_get_time();
        const std::int64_t compute_started = esp_timer_get_time();
        const auto step = producer.produce_next(fill_view);
        const std::int64_t compute_us = esp_timer_get_time() - compute_started;
        ++fill_timing.steps;
        fill_timing.compute_total_us += compute_us;
        fill_timing.compute_max_us = std::max(fill_timing.compute_max_us, compute_us);
        if (step.has_value()) {
          if (step->tiles_published != 0U) {
            pending_fill = {.level_bounds = step->level_bounds,
                            .zoom = fill_view.zoom,
                            .x = fill_view.level_pixels.x0,
                            .y = fill_view.level_pixels.y0,
                            .pending = true};
          }
          fill_complete = step->complete;
        } else {
          ++fill_timing.producer_failures;
        }
        fill_timing.tick_max_us =
            std::max(fill_timing.tick_max_us, esp_timer_get_time() - fill_tick_started);
        if (step.has_value() && fill_complete && !pending_fill.pending) {
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
      }
    }
    if (lift_report_ready) {
      if (idle_before_poll && !touching) {
        print_lift_baseline(lift_report, poll_started_us, poll_completed_us, lift_reports_dropped);
        std::fflush(stdout);
        lift_reports_dropped = 0U;
        // Exclude this diagnostic write from the pre-existing stroke poll-gap
        // metric. The physical input may still observe the write, so the lift
        // record reports stroke_logging_us explicitly.
        poll_previous_us = now_us();
      } else {
        ++lift_reports_dropped;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace tinydraw::esp32

extern "C" void app_main() { tinydraw::esp32::run_vector_v2_app(); }
