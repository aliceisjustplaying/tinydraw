#include "vector_v2_chrome_controller.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <limits>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc_clock.h"
#include "time_sync.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_app_diagnostics.h"
#include "vector_v2_export.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {
namespace {

std::uint32_t now_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

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

bool run_export(VectorV2Export& exporter, const vector_v2::OperationLog& log,
                vector_v2::ChromeState& chrome, VectorV2Presenter& presenter, RtcClock& clock) {
  chrome.popup = vector_v2::ChromePopup::kNone;
  chrome.confirm_new = false;
  chrome.export_status = vector_v2::ChromeExportStatus::kSaving;
  chrome.export_progress = 0;
  const auto started = presenter.refresh(chrome, now_us());
  print_presentation("export-start", presenter, started);

  if (!exporter.prepare_reencode()) {
    chrome.export_status = vector_v2::ChromeExportStatus::kExitError;
    static_cast<void>(presenter.refresh(chrome, now_us()));
    return false;
  }
  FatDateTime modified_time;
  if (clock.read(modified_time)) {
    exporter.set_modified_time(modified_time);
    std::printf("TINYDRAW_V2_EXPORT_TIME local=%04u-%02u-%02uT%02u:%02u:%02u\n", modified_time.year,
                modified_time.month, modified_time.day, modified_time.hour, modified_time.minute,
                modified_time.second);
  }
  ExportProgressContext progress{.chrome = &chrome, .presenter = &presenter};
  const VectorV2ExportStats stats = exporter.encode(log, present_export_progress, &progress);
  chrome.export_progress = stats.encoded ? 100 : chrome.export_progress;
  chrome.export_status =
      stats.encoded ? vector_v2::ChromeExportStatus::kSaved : vector_v2::ChromeExportStatus::kError;
  const auto finished = presenter.refresh(chrome, now_us());
  print_presentation("export-finish", presenter, finished);
  std::printf(
      "TINYDRAW_V2_EXPORT formats=svg,png encoded=%u svg_bytes=%lu png_bytes=%lu "
      "elapsed_us=%lld svg_workspace_bytes=%lu png_workspace_bytes=%lu "
      "render_workspace_bytes=%lu peak_workspace_bytes=%lu operations=%lu sink_calls=%lu "
      "flash_pages=%lu crc32=%08lx free_psram=%lu free_internal=%lu usb_attempt=%u\n",
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
      static_cast<unsigned long>(stats.free_internal_after), stats.encoded);
  std::fflush(stdout);
  if (!stats.encoded) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(150));
  const bool usb_ready = exporter.present_usb();
  chrome.export_status =
      usb_ready ? vector_v2::ChromeExportStatus::kPresented : vector_v2::ChromeExportStatus::kError;
  static_cast<void>(presenter.refresh(chrome, now_us()));
  return usb_ready;
}

}  // namespace

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

bool sync_history_controls(vector_v2::ChromeState& chrome, const vector_v2::OperationLog& log) {
  const bool can_undo = log.can_undo();
  const bool can_redo = log.can_redo();
  const bool changed = chrome.can_undo != can_undo || chrome.can_redo != can_redo;
  chrome.can_undo = can_undo;
  chrome.can_redo = can_redo;
  return changed;
}

LivePresentationTiming present_history_controls(VectorV2Presenter& presenter,
                                                const vector_v2::ChromeState& chrome,
                                                std::uint32_t event_us) {
  auto timing =
      presenter.present_frame_region({0, vector_v2::chrome_canvas_bottom(chrome),
                                      vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
                                     chrome, event_us);
  if (!timing.passed) {
    timing = presenter.refresh(chrome, event_us);
  }
  return timing;
}

VectorV2ChromeController::VectorV2ChromeController(
    vector_v2::ChromeState& chrome, vector_v2::OperationLog& log,
    vector_v2::MaterializedCanvas& canvas, vector_v2::TileProducer& producer,
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
    std::span<const std::uint16_t> blank_snapshot,
#endif
    std::span<std::uint16_t> history_scratch, VectorV2Presenter& presenter,
    VectorV2Export& exporter, TimeSyncController& time_sync, RtcClock& clock)
    : chrome_(chrome),
      log_(log),
      canvas_(canvas),
      producer_(producer),
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
      blank_snapshot_(blank_snapshot),
#endif
      history_scratch_(history_scratch),
      presenter_(presenter),
      exporter_(exporter),
      time_sync_(time_sync),
      clock_(clock) {
}

bool VectorV2ChromeController::apply(vector_v2::ChromeAction action, Point point) {
  const auto toggle = [this](vector_v2::ChromePopup popup) {
    chrome_.popup = chrome_.popup == popup ? vector_v2::ChromePopup::kNone : popup;
  };
  const bool palette_only_refresh = action == vector_v2::ChromeAction::kSelectColor ||
                                    action == vector_v2::ChromeAction::kToggleColors ||
                                    action == vector_v2::ChromeAction::kPreviousPalette ||
                                    action == vector_v2::ChromeAction::kNextPalette;
  switch (action) {
    case vector_v2::ChromeAction::kSelectDraw:
      chrome_.tool = vector_v2::ChromeTool::kDraw;
      chrome_.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSelectErase:
      chrome_.tool = vector_v2::ChromeTool::kErase;
      chrome_.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSelectPan:
      chrome_.tool = vector_v2::ChromeTool::kPan;
      chrome_.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSelectColor:
      if (const auto color = vector_v2::chrome_color_at({point.x, point.y}, chrome_);
          color.has_value()) {
        chrome_.color_index = *color;
        chrome_.tool = vector_v2::ChromeTool::kDraw;
        chrome_.popup = vector_v2::ChromePopup::kNone;
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
      chrome_.size =
          action == vector_v2::ChromeAction::kSelectSmall    ? vector_v2::ChromeSize::kSmall
          : action == vector_v2::ChromeAction::kSelectMedium ? vector_v2::ChromeSize::kMedium
          : action == vector_v2::ChromeAction::kSelectLarge  ? vector_v2::ChromeSize::kLarge
                                                             : vector_v2::ChromeSize::kExtraLarge;
      chrome_.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kPreviousPalette:
      chrome_.palette_page = 0;
      break;
    case vector_v2::ChromeAction::kNextPalette:
      chrome_.palette_page = 1;
      break;
    case vector_v2::ChromeAction::kNewDrawing:
      chrome_.popup = vector_v2::ChromePopup::kNone;
      chrome_.confirm_new = true;
      break;
    case vector_v2::ChromeAction::kCancelNewDrawing:
      chrome_.confirm_new = false;
      break;
    case vector_v2::ChromeAction::kConfirmNewDrawing: {
      const std::uint32_t current_generation =
          std::max(log_.current_revision().value, canvas_.current_revision().value);
      if (current_generation == std::numeric_limits<std::uint32_t>::max()) {
        chrome_.confirm_new = false;
        break;
      }
      const vector_v2::DocumentRevision revision{current_generation + 1U};
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
      const bool reset =
          vector_v2::restore_document_snapshot(log_, canvas_, revision, blank_snapshot_);
#else
      const bool reset = vector_v2::reset_blank_document(log_, canvas_, revision);
#endif
      if (!reset || !producer_.reset_uniform_baseline(revision)) {
        return false;
      }
      static_cast<void>(sync_history_controls(chrome_, log_));
      chrome_.confirm_new = false;
      chrome_.popup = vector_v2::ChromePopup::kNone;
      break;
    }
    case vector_v2::ChromeAction::kExport:
      return chrome_.can_export && run_export(exporter_, log_, chrome_, presenter_, clock_);
    case vector_v2::ChromeAction::kExitExport:
      chrome_.export_status = exporter_.stop_usb() ? vector_v2::ChromeExportStatus::kIdle
                                                   : vector_v2::ChromeExportStatus::kExitError;
      chrome_.popup = vector_v2::ChromePopup::kNone;
      break;
    case vector_v2::ChromeAction::kSyncTime:
      chrome_.popup = vector_v2::ChromePopup::kNone;
      if (chrome_.can_sync_time) {
        static_cast<void>(time_sync_.start());
        chrome_.time_sync_status = chrome_time_sync_status(time_sync_.status());
      }
      break;
    case vector_v2::ChromeAction::kZoomIn: {
      const vector_v2::ZoomLevel target = vector_v2::next_zoom(presenter_.zoom());
      if (target == presenter_.zoom()) {
        return true;
      }
      const auto timing = presenter_.set_zoom(target, chrome_, now_us());
      print_presentation("zoom-ui", presenter_, timing);
      return timing.passed;
    }
    case vector_v2::ChromeAction::kZoomOut: {
      const vector_v2::ZoomLevel target = vector_v2::previous_zoom(presenter_.zoom());
      if (target == presenter_.zoom()) {
        return true;
      }
      const auto timing = presenter_.set_zoom(target, chrome_, now_us());
      print_presentation("zoom-ui", presenter_, timing);
      return timing.passed;
    }
    case vector_v2::ChromeAction::kUndo:
    case vector_v2::ChromeAction::kRedo: {
      const bool undo = action == vector_v2::ChromeAction::kUndo;
      const bool enabled = undo ? chrome_.can_undo : chrome_.can_redo;
      const bool authority_enabled = undo ? log_.can_undo() : log_.can_redo();
      if (!enabled || !authority_enabled) {
        if (!sync_history_controls(chrome_, log_)) {
          return true;
        }
        const auto timing = present_history_controls(presenter_, chrome_, now_us());
        print_presentation("history-stale-guard", presenter_, timing);
        return timing.passed;
      }
      const auto change = vector_v2::move_history_incrementally(
          log_, canvas_,
          undo ? vector_v2::HistoryDirection::kUndo : vector_v2::HistoryDirection::kRedo,
          history_scratch_);
      if (!change.has_value()) {
        static_cast<void>(sync_history_controls(chrome_, log_));
        return false;
      }
      chrome_.popup = vector_v2::ChromePopup::kNone;
      static_cast<void>(sync_history_controls(chrome_, log_));
      // The damaged canvas keeps its retained pixels: the background refill
      // presents the region exactly once when it completes (owner-directed
      // hold-back), showing the busy hourglass only if it runs long.
      history_damage_ =
          vector_v2::operation_level_bounds(change->affected_world_bounds, presenter_.zoom());
      // The hourglass appears synchronously with the tap (owner direction);
      // the background hold erases it with the exact swap.
      chrome_.history_busy = true;
      const vector_v2::ChromeRect busy = vector_v2::chrome_history_busy_region();
      static_cast<void>(presenter_.present_frame_region({busy.x0, busy.y0, busy.x1, busy.y1},
                                                        chrome_, now_us()));
      const auto dock_timing = present_history_controls(presenter_, chrome_, now_us());
      print_presentation(undo ? "undo-dock" : "redo-dock", presenter_, dock_timing);
      return dock_timing.passed;
    }
    case vector_v2::ChromeAction::kNone:
      break;
  }
  auto timing =
      palette_only_refresh
          ? presenter_.present_frame_region(
                {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight}, chrome_, now_us())
          : LivePresentationTiming{};
  if (!palette_only_refresh || !timing.passed) {
    timing = presenter_.refresh(chrome_, now_us());
  }
  print_presentation("chrome", presenter_, timing);
  return timing.passed;
}

std::optional<vector_v2::PixelRect> VectorV2ChromeController::take_history_damage() {
  const auto damage = history_damage_;
  history_damage_.reset();
  return damage;
}

}  // namespace tinydraw::esp32
