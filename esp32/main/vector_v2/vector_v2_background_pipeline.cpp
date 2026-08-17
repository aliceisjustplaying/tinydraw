#include "vector_v2_background_pipeline.h"

#include <algorithm>
#include <cstdio>

#include "esp_timer.h"
#include "vector_v2_app_diagnostics.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::PixelRect;
using vector_v2::ViewRequest;
using vector_v2::ZoomLevel;

constexpr std::int64_t kSettleSliceBudgetUs = 8'000;

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

void include_bounds(std::optional<PixelRect>& accumulated, PixelRect bounds) {
  if (!accumulated.has_value()) {
    accumulated = bounds;
    return;
  }
  accumulated->x0 = std::min(accumulated->x0, bounds.x0);
  accumulated->y0 = std::min(accumulated->y0, bounds.y0);
  accumulated->x1 = std::max(accumulated->x1, bounds.x1);
  accumulated->y1 = std::max(accumulated->y1, bounds.y1);
}

std::optional<ViewRequest> priority_view(const VectorV2Presenter& presenter) {
  if (presenter.zoom() == ZoomLevel::k25Percent) {
    return std::nullopt;
  }
  return ViewRequest{
      .zoom = presenter.zoom(),
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
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

}  // namespace

VectorV2BackgroundPipeline::VectorV2BackgroundPipeline(
    vector_v2::OperationLog& log, vector_v2::MaterializedCanvas& canvas,
    vector_v2::TileProducer& producer, vector_v2::InPlaceAppendWorkspace append_workspace,
    vector_v2::SettledTileWorkspace settle_workspace, std::span<std::uint16_t> settle_pixels,
    VectorV2Presenter& presenter, vector_v2::ChromeState& chrome)
    : log_(log),
      canvas_(canvas),
      producer_(producer),
      append_workspace_(append_workspace),
      settle_workspace_(settle_workspace),
      settle_pixels_(settle_pixels),
      presenter_(presenter),
      chrome_(chrome),
      fill_revision_(canvas.current_revision()) {}

void VectorV2BackgroundPipeline::note_committed_bounds(PixelRect world_bounds) {
  include_bounds(drain_swap_world_, world_bounds);
}

void VectorV2BackgroundPipeline::mark_history_controls_dirty() { history_controls_dirty_ = true; }

void VectorV2BackgroundPipeline::history_controls_presented() { history_controls_dirty_ = false; }

void VectorV2BackgroundPipeline::reset_document_state() {
  if (fill_measurement_active_) {
    print_fill("superseded");
  }
  fill_zoom_ = presenter_.zoom();
  fill_x_ = presenter_.level_x();
  fill_y_ = presenter_.level_y();
  fill_revision_ = canvas_.current_revision();
  fill_complete_ = false;
  fill_timing_ = {};
  fill_measurement_active_ = false;
  pending_fill_ = {};

  repair_plan_ = {};
  repair_cursor_ = 0;
  repair_steps_ = 0;
  repair_planned_ = false;

  drain_swap_world_.reset();
  drain_operations_ = 0;
  drain_total_us_ = 0;
  drain_max_us_ = 0;
  drain_failures_ = 0;
  history_controls_dirty_ = false;
  reset_settle_pass();
}

bool VectorV2BackgroundPipeline::drain_boundary(BackgroundDrainBoundary boundary) {
  const std::int64_t started = esp_timer_get_time();
  std::uint32_t operations = 0U;
  while (vector_v2::pending_operation_count(log_, canvas_) != 0U) {
    if (!vector_v2::absorb_pending_operation(
             log_, canvas_, append_workspace_, priority_view(presenter_),
             {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs})
             .has_value()) {
      break;
    }
    ++operations;
  }
  const bool lockstep = log_.current_revision() == canvas_.current_revision();
  if (boundary == BackgroundDrainBoundary::kPan) {
    drain_swap_world_.reset();
    std::printf("TINYDRAW_LIVE_DRAIN_BOUNDARY site=pan ops=%lu wall_us=%lld pending=%lu\n",
                static_cast<unsigned long>(operations),
                static_cast<long long>(esp_timer_get_time() - started),
                static_cast<unsigned long>(vector_v2::pending_operation_count(log_, canvas_)));
  } else {
    std::printf(
        "TINYDRAW_LIVE_DRAIN_BOUNDARY site=history ops=%lu wall_us=%lld pending=%lu "
        "lockstep=%u\n",
        static_cast<unsigned long>(operations),
        static_cast<long long>(esp_timer_get_time() - started),
        static_cast<unsigned long>(vector_v2::pending_operation_count(log_, canvas_)), lockstep);
    if (lockstep) {
      drain_swap_world_.reset();
      drain_operations_ = 0U;
      drain_total_us_ = 0;
      drain_max_us_ = 0;
      drain_failures_ = 0U;
    }
  }
  return lockstep;
}

void VectorV2BackgroundPipeline::reset_settle_pass() {
  settle_cursor_ = 0;
  settle_complete_ = false;
  settle_tiles_ = 0;
  settle_total_us_ = 0;
  settle_max_us_ = 0;
  settle_failures_ = 0;
}

void VectorV2BackgroundPipeline::reset_settle_fingerprint() {
  if (settle_zoom_ == presenter_.zoom() && settle_revision_ == canvas_.current_revision() &&
      settle_x_ == presenter_.level_x() && settle_y_ == presenter_.level_y()) {
    return;
  }
  settle_zoom_ = presenter_.zoom();
  settle_revision_ = canvas_.current_revision();
  settle_x_ = presenter_.level_x();
  settle_y_ = presenter_.level_y();
  reset_settle_pass();
}

void VectorV2BackgroundPipeline::print_fill(const char* result) const {
  if (fill_timing_.steps == 0U) {
    return;
  }
  std::printf(
      "TINYDRAW_FILL_BASELINE result=%s zoom=%s x=%d y=%d revision=%lu steps=%lu "
      "wall_us=%lld compute_total_us=%lld compute_max_us=%lld present_total_us=%lld "
      "present_max_us=%lld tick_max_us=%lld producer_failures=%lu "
      "presentation_failures=%lu\n",
      result, zoom_name(fill_zoom_), fill_x_, fill_y_,
      static_cast<unsigned long>(fill_revision_.value),
      static_cast<unsigned long>(fill_timing_.steps),
      static_cast<long long>(
          fill_timing_.started_us == 0 ? 0 : esp_timer_get_time() - fill_timing_.started_us),
      static_cast<long long>(fill_timing_.compute_total_us),
      static_cast<long long>(fill_timing_.compute_max_us),
      static_cast<long long>(fill_timing_.present_total_us),
      static_cast<long long>(fill_timing_.present_max_us),
      static_cast<long long>(fill_timing_.tick_max_us),
      static_cast<unsigned long>(fill_timing_.producer_failures),
      static_cast<unsigned long>(fill_timing_.presentation_failures));
}

void VectorV2BackgroundPipeline::run_fill(const ViewRequest& view) {
  const bool view_changed = fill_zoom_ != view.zoom || fill_x_ != view.level_pixels.x0 ||
                            fill_y_ != view.level_pixels.y0 ||
                            fill_revision_ != canvas_.current_revision();
  if (view_changed) {
    if (fill_measurement_active_) {
      print_fill("superseded");
    }
    fill_zoom_ = view.zoom;
    fill_x_ = view.level_pixels.x0;
    fill_y_ = view.level_pixels.y0;
    fill_revision_ = canvas_.current_revision();
    fill_complete_ = false;
    fill_timing_ = {};
    fill_timing_.started_us = esp_timer_get_time();
    fill_measurement_active_ = true;
    pending_fill_ = {};
    repair_planned_ = false;
    reset_settle_pass();
  }

  if (pending_fill_.pending) {
    const bool still_current = pending_fill_.zoom == view.zoom &&
                               pending_fill_.x == view.level_pixels.x0 &&
                               pending_fill_.y == view.level_pixels.y0;
    if (!still_current) {
      pending_fill_ = {};
      return;
    }
    const std::int64_t started = esp_timer_get_time();
    const auto presentation = presenter_.refresh_region(pending_fill_.level_bounds, chrome_);
    const std::int64_t elapsed = esp_timer_get_time() - started;
    fill_timing_.present_total_us += elapsed;
    fill_timing_.present_max_us = std::max(fill_timing_.present_max_us, elapsed);
    fill_timing_.presentation_failures += !presentation.passed;
    if (presentation.passed) {
      pending_fill_ = {};
    }
    return;
  }

  if (fill_complete_) {
    if (fill_measurement_active_) {
      print_fill("complete");
      std::printf("TINYDRAW_LIVE_FILL_DONE zoom=%s x=%d y=%d revision=%lu\n", zoom_name(fill_zoom_),
                  fill_x_, fill_y_, static_cast<unsigned long>(fill_revision_.value));
      fill_measurement_active_ = false;
    }
    return;
  }

  if (!fill_measurement_active_) {
    fill_timing_ = {};
    fill_timing_.started_us = esp_timer_get_time();
    fill_measurement_active_ = true;
  }
  const std::int64_t tick_started = esp_timer_get_time();
  do {
    const std::int64_t compute_started = esp_timer_get_time();
    const auto step = producer_.produce_next(view);
    const std::int64_t compute_us = esp_timer_get_time() - compute_started;
    ++fill_timing_.steps;
    fill_timing_.compute_total_us += compute_us;
    fill_timing_.compute_max_us = std::max(fill_timing_.compute_max_us, compute_us);
    if (!step.has_value()) {
      ++fill_timing_.producer_failures;
      break;
    }
    if (step->tiles_published != 0U) {
      pending_fill_ = {.level_bounds = step->level_bounds,
                       .zoom = view.zoom,
                       .x = view.level_pixels.x0,
                       .y = view.level_pixels.y0,
                       .pending = true};
    }
    fill_complete_ = step->complete;
  } while (!fill_complete_ && !pending_fill_.pending &&
           esp_timer_get_time() - tick_started < kColdFillSliceDeadlineUs);
  fill_timing_.tick_max_us =
      std::max(fill_timing_.tick_max_us, esp_timer_get_time() - tick_started);
  if (fill_complete_ && !pending_fill_.pending) {
    print_fill("complete");
    std::printf("TINYDRAW_LIVE_FILL_DONE zoom=%s x=%d y=%d revision=%lu\n", zoom_name(fill_zoom_),
                fill_x_, fill_y_, static_cast<unsigned long>(fill_revision_.value));
    fill_measurement_active_ = false;
  }
}

void VectorV2BackgroundPipeline::run_repair(const ViewRequest& view) {
  if (!repair_planned_) {
    repair_plan_ = vector_v2::plan_idle_repair(view, canvas_.recent_views());
    repair_cursor_ = 0;
    repair_steps_ = 0;
    repair_planned_ = true;
  }
  const std::int64_t tick_started = esp_timer_get_time();
  while (repair_cursor_ < repair_plan_.count &&
         esp_timer_get_time() - tick_started < kColdFillSliceDeadlineUs) {
    if (repair_cursor_ >= repair_plan_.grid_start &&
        canvas_.resident_raw_tiles() + kRepairSaturationHeadroomTiles >= canvas_.slot_capacity()) {
      repair_cursor_ = repair_plan_.count;
      break;
    }
    const auto step = producer_.produce_next(repair_plan_.views[repair_cursor_]);
    if (!step.has_value()) {
      std::printf("TINYDRAW_LIVE_REPAIR_ABANDON view=%u\n", static_cast<unsigned>(repair_cursor_));
      repair_cursor_ = repair_plan_.count;
      break;
    }
    ++repair_steps_;
    if (step->complete) {
      ++repair_cursor_;
    }
  }
  if (repair_planned_ && repair_cursor_ >= repair_plan_.count && repair_plan_.count != 0U) {
    std::printf("TINYDRAW_LIVE_REPAIR views=%u steps=%u\n",
                static_cast<unsigned>(repair_plan_.count), static_cast<unsigned>(repair_steps_));
  }
}

void VectorV2BackgroundPipeline::run_settle(std::uint32_t loop_us) {
  const bool overview = presenter_.zoom() == ZoomLevel::k25Percent;
  const int first_column = overview ? 0 : presenter_.level_x() / vector_v2::kTileWidth;
  const int first_row = overview ? 0 : presenter_.level_y() / vector_v2::kTileHeight;
  const std::size_t columns =
      overview
          ? (static_cast<std::size_t>(vector_v2::kOverviewWidth) + vector_v2::kTileWidth - 1U) /
                vector_v2::kTileWidth
          : static_cast<std::size_t>((presenter_.level_x() + vector_v2::kOverviewWidth - 1) /
                                         vector_v2::kTileWidth -
                                     first_column + 1);
  const std::size_t rows =
      overview
          ? (static_cast<std::size_t>(vector_v2::kOverviewHeight) + vector_v2::kTileHeight - 1U) /
                vector_v2::kTileHeight
          : static_cast<std::size_t>((presenter_.level_y() + vector_v2::kOverviewHeight - 1) /
                                         vector_v2::kTileHeight -
                                     first_row + 1);
  const std::size_t total = columns * rows;
  const std::int64_t slice_started = esp_timer_get_time();
  std::optional<PixelRect> batch_bounds;
  while (settle_cursor_ < total && esp_timer_get_time() - slice_started < kSettleSliceBudgetUs) {
    const int column = static_cast<int>(settle_cursor_ % columns);
    const int row = static_cast<int>(settle_cursor_ / columns);
    ++settle_cursor_;
    const std::int64_t tile_started = esp_timer_get_time();
    vector_v2::SettledTileStats stats{};
    PixelRect bounds{};
    bool rendered = false;
    if (overview) {
      bounds = {column * vector_v2::kTileWidth, row * vector_v2::kTileHeight,
                std::min((column + 1) * vector_v2::kTileWidth, vector_v2::kOverviewWidth),
                std::min((row + 1) * vector_v2::kTileHeight, vector_v2::kOverviewHeight)};
      rendered = vector_v2::render_settled_window(log_, ZoomLevel::k25Percent, bounds,
                                                  settle_workspace_, settle_pixels_, &stats) &&
                 presenter_.stage_settled_pixels(bounds, settle_pixels_, bounds.x1 - bounds.x0);
    } else {
      const vector_v2::TileKey key{presenter_.zoom(),
                                   static_cast<std::uint16_t>(first_column + column),
                                   static_cast<std::uint16_t>(first_row + row)};
      const auto source = canvas_.lookup(key);
      if (!source.has_value() || source->kind != vector_v2::SourceKind::kTileSlot ||
          source->quality >= vector_v2::MaterializationQuality::kSettled) {
        continue;
      }
      bounds = vector_v2::tile_pixel_bounds(key);
      rendered =
          vector_v2::render_settled_tile(log_, key, settle_workspace_, settle_pixels_, &stats) &&
          canvas_
              .publish_tile(key, canvas_.current_revision(),
                            vector_v2::MaterializationQuality::kSettled, settle_pixels_)
              .has_value();
    }
    if (rendered) {
      const std::int64_t tile_us = esp_timer_get_time() - tile_started;
      include_bounds(batch_bounds, bounds);
      ++settle_tiles_;
      settle_total_us_ += tile_us;
      settle_max_us_ = std::max(settle_max_us_, tile_us);
    } else {
      ++settle_failures_;
    }
  }
  if (batch_bounds.has_value()) {
    if (overview) {
      static_cast<void>(presenter_.present_frame_region(*batch_bounds, chrome_, loop_us));
    } else {
      static_cast<void>(presenter_.refresh_region(*batch_bounds, chrome_, loop_us));
    }
  }
  if (settle_cursor_ >= total) {
    settle_complete_ = true;
    if (settle_tiles_ != 0U || settle_failures_ != 0U) {
      std::printf(
          "TINYDRAW_LIVE_SETTLE zoom=%s tiles=%lu total_us=%lld max_tile_us=%lld "
          "failures=%lu\n",
          zoom_name(presenter_.zoom()), static_cast<unsigned long>(settle_tiles_),
          static_cast<long long>(settle_total_us_), static_cast<long long>(settle_max_us_),
          static_cast<unsigned long>(settle_failures_));
      std::fflush(stdout);
    }
  }
}

BackgroundSliceResult VectorV2BackgroundPipeline::run_slice(const BackgroundSliceInput& input) {
  BackgroundSliceResult result{};
  const bool fill_view_available =
      presenter_.zoom() != ZoomLevel::k25Percent && chrome_.popup == vector_v2::ChromePopup::kNone;
  const bool fill_allowed = !input.pressed && fill_view_available && !input.lift_report_pending;
  if (!input.pressed) {
    reset_settle_fingerprint();
  }

  const std::size_t idle_pending = !input.pressed && !input.panning && !input.sample_ready
                                       ? vector_v2::pending_operation_count(log_, canvas_)
                                       : 0U;
  if (idle_pending != 0U && drain_failures_ < 16U) {
    const std::int64_t started = esp_timer_get_time();
    const auto absorbed = vector_v2::absorb_pending_operation(
        log_, canvas_, append_workspace_, priority_view(presenter_),
        {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs});
    const std::int64_t elapsed = esp_timer_get_time() - started;
    if (absorbed.has_value()) {
      ++drain_operations_;
      drain_total_us_ += elapsed;
      drain_max_us_ = std::max(drain_max_us_, elapsed);
      if (vector_v2::pending_operation_count(log_, canvas_) == 0U) {
        LivePresentationTiming swap{};
        swap.passed = true;
        std::int64_t swap_wall_us = 0;
        if (drain_swap_world_.has_value()) {
          const std::int64_t swap_started = esp_timer_get_time();
          swap = presenter_.refresh_region(
              vector_v2::operation_level_bounds(*drain_swap_world_, presenter_.zoom()), chrome_,
              input.loop_us);
          swap_wall_us = esp_timer_get_time() - swap_started;
          drain_swap_world_.reset();
        }
        if (history_controls_dirty_ && swap.passed) {
          const auto dock = present_history_controls(presenter_, chrome_, input.loop_us);
          print_presentation("history-dock", presenter_, dock);
          history_controls_dirty_ = !dock.passed;
        }
        std::printf(
            "TINYDRAW_LIVE_DRAIN ops=%lu total_us=%lld max_us=%lld failures=%lu "
            "swap_wall_us=%lld swap_pass=%u\n",
            static_cast<unsigned long>(drain_operations_), static_cast<long long>(drain_total_us_),
            static_cast<long long>(drain_max_us_), static_cast<unsigned long>(drain_failures_),
            static_cast<long long>(swap_wall_us), swap.passed);
        std::fflush(stdout);
        drain_operations_ = 0U;
        drain_total_us_ = 0;
        drain_max_us_ = 0;
        drain_failures_ = 0U;
        result.drain_completed = true;
      }
    } else {
      ++drain_failures_;
    }
  }

  if (!fill_view_available && fill_measurement_active_) {
    print_fill("paused");
    fill_timing_ = {};
    fill_measurement_active_ = false;
  }
  if (fill_allowed && idle_pending == 0U) {
    const ViewRequest view{
        .zoom = presenter_.zoom(),
        .level_pixels = {presenter_.level_x(), presenter_.level_y(),
                         presenter_.level_x() + vector_v2::kOverviewWidth,
                         presenter_.level_y() + vector_v2::kOverviewHeight},
    };
    const bool needs_fill = fill_zoom_ != view.zoom || fill_x_ != view.level_pixels.x0 ||
                            fill_y_ != view.level_pixels.y0 ||
                            fill_revision_ != canvas_.current_revision() || !fill_complete_ ||
                            pending_fill_.pending || fill_measurement_active_;
    if (needs_fill) {
      run_fill(view);
    } else if (!repair_planned_ || repair_cursor_ < repair_plan_.count) {
      run_repair(view);
    } else if (!settle_complete_) {
      run_settle(input.loop_us);
    }
  } else if (!input.pressed && !input.panning && !input.lift_report_pending && !settle_complete_ &&
             presenter_.zoom() == ZoomLevel::k25Percent &&
             chrome_.popup == vector_v2::ChromePopup::kNone &&
             vector_v2::pending_operation_count(log_, canvas_) == 0U) {
    run_settle(input.loop_us);
  }
  result.fill_busy = fill_allowed && (!fill_complete_ || pending_fill_.pending);
  return result;
}

}  // namespace tinydraw::esp32
