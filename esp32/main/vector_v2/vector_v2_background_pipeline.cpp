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

// One quiet-time tick may use several work quanta; touch urgency is sampled
// before every quantum, limiting preemption latency to one 512-work slice.
constexpr std::int64_t kSettleSliceBudgetUs = 8'000;
constexpr std::size_t kSettleWorkPixels = 512U;
constexpr std::int64_t kAbsorbSliceBudgetUs = 1'500;
constexpr std::size_t kAbsorbRasterWorkPixels = 256U;
constexpr std::uint8_t kSettleRetryLimit = 3U;

struct AbsorbSliceLimit {
  TouchUrgencyProbe touch_urgency{};
  std::int64_t deadline_us = 0;

  static bool requested(const void* context) {
    const auto& limit = *static_cast<const AbsorbSliceLimit*>(context);
    return limit.touch_urgency.requested() || esp_timer_get_time() >= limit.deadline_us;
  }
};

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
  repair_pan_delta_ = {};
  repair_cursor_ = 0;
  repair_steps_ = 0;
  repair_planned_ = false;

  drain_swap_world_.reset();
  absorption_.cancel();
  drain_operations_ = 0;
  drain_slices_ = 0;
  drain_restarts_ = 0;
  drain_max_pending_ = 0;
  drain_total_us_ = 0;
  drain_max_us_ = 0;
  drain_failures_ = 0;
  drain_ready_to_present_ = false;
  history_controls_dirty_ = false;
  reset_settle_pass();
}

bool VectorV2BackgroundPipeline::drain_boundary(BackgroundDrainBoundary boundary) {
  const std::int64_t started = esp_timer_get_time();
  std::uint32_t operations = 0U;
  // A quiet-time continuation may carry the larger idle-retention policy.
  // Restarting is pixel-idempotent and keeps this synchronous boundary on its
  // own policy fingerprint.
  absorption_.cancel();
  producer_.cancel_pending_work();
  while (absorption_.active() || vector_v2::pending_operation_count(log_, canvas_) != 0U) {
    const auto absorbed = vector_v2::absorb_pending_operation_slice(
        log_, canvas_, append_workspace_, absorption_, priority_view(presenter_), {},
        {.now_us = &esp_timer_get_time, .budget_us = kInPlaceRetentionBudgetUs});
    if (absorbed.status == vector_v2::PendingAbsorptionStatus::kComplete) {
      ++operations;
    } else if (absorbed.status == vector_v2::PendingAbsorptionStatus::kIdle) {
      break;
    } else if (absorbed.status == vector_v2::PendingAbsorptionStatus::kError) {
      absorption_.cancel();
      break;
    }
  }
  const bool lockstep = log_.current_revision() == canvas_.current_revision();
  if (boundary == BackgroundDrainBoundary::kPan) {
    drain_swap_world_.reset();
    drain_ready_to_present_ = false;
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
      drain_ready_to_present_ = false;
      drain_operations_ = 0U;
      drain_slices_ = 0U;
      drain_restarts_ = 0U;
      drain_max_pending_ = 0U;
      drain_total_us_ = 0;
      drain_max_us_ = 0;
      drain_failures_ = 0U;
    }
  }
  return lockstep;
}

void VectorV2BackgroundPipeline::reset_settle_pass() {
  settle_render_.cursor.cancel();
  settle_render_ = {};
  settle_cursor_ = 0;
  settle_complete_ = false;
  settle_tiles_ = 0;
  settle_slices_ = 0;
  settle_work_ = 0;
  settle_total_us_ = 0;
  settle_max_us_ = 0;
  settle_failures_ = 0;
  settle_retry_count_ = 0;
  settle_permanent_failures_ = 0;
  pending_settle_ = {};
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

void VectorV2BackgroundPipeline::run_fill(const ViewRequest& view,
                                          TouchUrgencyProbe touch_urgency) {
  const bool view_changed = fill_zoom_ != view.zoom || fill_x_ != view.level_pixels.x0 ||
                            fill_y_ != view.level_pixels.y0 ||
                            fill_revision_ != canvas_.current_revision();
  if (view_changed) {
    if (fill_measurement_active_) {
      print_fill("superseded");
    }
    repair_pan_delta_ = fill_zoom_ == view.zoom && fill_revision_ == canvas_.current_revision()
                            ? vector_v2::IdleRepairPanDelta{.x = view.level_pixels.x0 - fill_x_,
                                                            .y = view.level_pixels.y0 - fill_y_}
                            : vector_v2::IdleRepairPanDelta{};
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
    if (touch_urgency.requested()) {
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
    if (touch_urgency.requested()) {
      break;
    }
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

void VectorV2BackgroundPipeline::run_repair(const ViewRequest& view,
                                            TouchUrgencyProbe touch_urgency) {
  if (!repair_planned_) {
    repair_plan_ = vector_v2::plan_idle_repair(view, canvas_.recent_views(), repair_pan_delta_);
    repair_pan_delta_ = {};
    repair_cursor_ = 0;
    repair_steps_ = 0;
    repair_planned_ = true;
  }
  const std::int64_t tick_started = esp_timer_get_time();
  while (repair_cursor_ < repair_plan_.count &&
         esp_timer_get_time() - tick_started < kColdFillSliceDeadlineUs) {
    if (touch_urgency.requested()) {
      break;
    }
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

void VectorV2BackgroundPipeline::run_settle(std::uint32_t loop_us,
                                            TouchUrgencyProbe touch_urgency) {
  const bool overview = presenter_.zoom() == ZoomLevel::k25Percent;
  if (pending_settle_.pending) {
    if (touch_urgency.requested()) {
      return;
    }
    const auto presentation =
        pending_settle_.overview
            ? presenter_.present_frame_region(pending_settle_.level_bounds, chrome_, loop_us)
            : presenter_.refresh_region(pending_settle_.level_bounds, chrome_, loop_us);
    if (!presentation.passed) {
      return;
    }
    pending_settle_ = {};
  }
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
    if (touch_urgency.requested()) {
      break;
    }
    if (!settle_render_.active) {
      const int column = static_cast<int>(settle_cursor_ % columns);
      const int row = static_cast<int>(settle_cursor_ / columns);
      settle_render_.overview = overview;
      if (overview) {
        settle_render_.level_bounds = {
            column * vector_v2::kTileWidth, row * vector_v2::kTileHeight,
            std::min((column + 1) * vector_v2::kTileWidth, vector_v2::kOverviewWidth),
            std::min((row + 1) * vector_v2::kTileHeight, vector_v2::kOverviewHeight)};
      } else {
        settle_render_.key = {presenter_.zoom(), static_cast<std::uint16_t>(first_column + column),
                              static_cast<std::uint16_t>(first_row + row)};
        const auto source = canvas_.lookup(settle_render_.key);
        if (!source.has_value() || source->kind != vector_v2::SourceKind::kTileSlot ||
            source->quality >= vector_v2::MaterializationQuality::kSettled) {
          ++settle_cursor_;
          settle_retry_count_ = 0U;
          continue;
        }
        settle_render_.level_bounds = vector_v2::tile_pixel_bounds(settle_render_.key);
      }
      settle_render_.cursor.cancel();
      settle_render_.active = true;
    }

    const std::int64_t render_started = esp_timer_get_time();
    const auto slice = vector_v2::render_settled_window_slice(
        log_, settle_render_.overview ? ZoomLevel::k25Percent : settle_render_.key.zoom,
        settle_render_.level_bounds, settle_workspace_, settle_pixels_, settle_render_.cursor,
        kSettleWorkPixels);
    const PixelRect bounds = settle_render_.level_bounds;
    bool rendered = false;
    if (slice.status == vector_v2::SettledRenderStatus::kComplete) {
      rendered =
          settle_render_.overview
              ? presenter_.stage_settled_pixels(bounds, settle_pixels_, bounds.x1 - bounds.x0)
              : canvas_
                    .publish_tile(settle_render_.key, canvas_.current_revision(),
                                  vector_v2::MaterializationQuality::kSettled, settle_pixels_)
                    .has_value();
    }
    const std::int64_t render_us = esp_timer_get_time() - render_started;
    ++settle_slices_;
    settle_work_ += slice.work_px;
    settle_total_us_ += render_us;
    settle_max_us_ = std::max(settle_max_us_, render_us);
    if (slice.status == vector_v2::SettledRenderStatus::kInProgress) {
      continue;
    }

    settle_render_.cursor.cancel();
    settle_render_.active = false;
    if (rendered) {
      include_bounds(batch_bounds, bounds);
      ++settle_tiles_;
      ++settle_cursor_;
      settle_retry_count_ = 0U;
    } else {
      ++settle_failures_;
      ++settle_retry_count_;
      if (settle_retry_count_ >= kSettleRetryLimit) {
        ++settle_permanent_failures_;
        ++settle_cursor_;
        settle_retry_count_ = 0U;
      }
      // A transient failure keeps this item at the durable cursor for the
      // next quiet-time slice. Do not spin on a failing tile in one slice.
      break;
    }
  }
  if (batch_bounds.has_value()) {
    pending_settle_ = {
        .level_bounds = *batch_bounds,
        .overview = overview,
        .pending = true,
    };
    if (!touch_urgency.requested()) {
      const auto presentation =
          overview ? presenter_.present_frame_region(*batch_bounds, chrome_, loop_us)
                   : presenter_.refresh_region(*batch_bounds, chrome_, loop_us);
      if (presentation.passed) {
        pending_settle_ = {};
      }
    }
  }
  if (settle_cursor_ >= total && !pending_settle_.pending) {
    settle_complete_ = true;
    if (settle_tiles_ != 0U || settle_failures_ != 0U) {
      std::printf(
          "TINYDRAW_LIVE_SETTLE zoom=%s tiles=%lu slices=%lu total_us=%lld "
          "max_slice_us=%lld work=%llu failures=%lu permanent_failures=%lu\n",
          zoom_name(presenter_.zoom()), static_cast<unsigned long>(settle_tiles_),
          static_cast<unsigned long>(settle_slices_), static_cast<long long>(settle_total_us_),
          static_cast<long long>(settle_max_us_), static_cast<unsigned long long>(settle_work_),
          static_cast<unsigned long>(settle_failures_),
          static_cast<unsigned long>(settle_permanent_failures_));
      std::fflush(stdout);
    }
  }
}

BackgroundSliceResult VectorV2BackgroundPipeline::run_slice(const BackgroundSliceInput& input) {
  BackgroundSliceResult result{};
  const bool fill_view_available =
      presenter_.zoom() != ZoomLevel::k25Percent && chrome_.popup == vector_v2::ChromePopup::kNone;
  const bool fill_allowed =
      !input.pressed && !input.sample_ready && fill_view_available && !input.lift_report_pending;
  if (!input.pressed) {
    reset_settle_fingerprint();
  }

  std::size_t pending = vector_v2::pending_operation_count(log_, canvas_);
  drain_max_pending_ = std::max(drain_max_pending_, pending);
  if (pending != 0U) {
    drain_ready_to_present_ = false;
  }
  const bool absorption_allowed = !input.panning && !input.sample_ready &&
                                  !input.lift_report_pending && drain_failures_ < 16U &&
                                  !input.touch_urgency.requested();
  if ((absorption_.active() || pending != 0U) && absorption_allowed) {
    if (!absorption_.active()) {
      // A producer group targets the canvas revision absorption is about to
      // replace, so its unpublished work cannot survive the commit. Dropping
      // it also makes the shared chord workspace exclusively available.
      producer_.cancel_pending_work();
    }
    const std::int64_t started = esp_timer_get_time();
    const AbsorbSliceLimit limit{.touch_urgency = input.touch_urgency,
                                 .deadline_us = started + kAbsorbSliceBudgetUs};
    const auto absorbed = vector_v2::absorb_pending_operation_slice(
        log_, canvas_, append_workspace_, absorption_, priority_view(presenter_),
        {.requested = &AbsorbSliceLimit::requested,
         .context = &limit,
         .raster_work_px = kAbsorbRasterWorkPixels},
        {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs});
    const std::int64_t elapsed = esp_timer_get_time() - started;
    ++drain_slices_;
    drain_total_us_ += elapsed;
    drain_max_us_ = std::max(drain_max_us_, elapsed);
    if (absorbed.status == vector_v2::PendingAbsorptionStatus::kComplete) {
      ++drain_operations_;
      pending = vector_v2::pending_operation_count(log_, canvas_);
      if (pending == 0U) {
        drain_ready_to_present_ = true;
      }
    } else if (absorbed.status == vector_v2::PendingAbsorptionStatus::kError) {
      const bool superseded = absorption_.active();
      absorption_.cancel();
      if (superseded) {
        ++drain_restarts_;
      } else {
        ++drain_failures_;
      }
    }
  }

  const bool drain_presentation_allowed =
      !input.pressed && !input.panning && !input.sample_ready && !input.lift_report_pending;
  if (drain_ready_to_present_ && pending == 0U && !absorption_.active() &&
      drain_presentation_allowed && !input.touch_urgency.requested()) {
    LivePresentationTiming swap{};
    swap.passed = true;
    std::int64_t swap_wall_us = 0;
    if (drain_swap_world_.has_value()) {
      const std::int64_t swap_started = esp_timer_get_time();
      swap = presenter_.refresh_region(
          vector_v2::operation_level_bounds(*drain_swap_world_, presenter_.zoom()), chrome_,
          input.loop_us);
      swap_wall_us = esp_timer_get_time() - swap_started;
      if (swap.passed) {
        drain_swap_world_.reset();
      }
    }
    if (history_controls_dirty_ && swap.passed && !input.touch_urgency.requested()) {
      const auto dock = present_history_controls(presenter_, chrome_, input.loop_us);
      print_presentation("history-dock", presenter_, dock);
      history_controls_dirty_ = !dock.passed;
    }
    if (swap.passed && !history_controls_dirty_) {
      std::printf(
          "TINYDRAW_LIVE_DRAIN ops=%lu slices=%lu restarts=%lu max_pending=%lu total_us=%lld "
          "max_slice_us=%lld failures=%lu swap_wall_us=%lld swap_pass=%u\n",
          static_cast<unsigned long>(drain_operations_), static_cast<unsigned long>(drain_slices_),
          static_cast<unsigned long>(drain_restarts_),
          static_cast<unsigned long>(drain_max_pending_), static_cast<long long>(drain_total_us_),
          static_cast<long long>(drain_max_us_), static_cast<unsigned long>(drain_failures_),
          static_cast<long long>(swap_wall_us), swap.passed);
      std::fflush(stdout);
      drain_operations_ = 0U;
      drain_slices_ = 0U;
      drain_restarts_ = 0U;
      drain_max_pending_ = 0U;
      drain_total_us_ = 0;
      drain_max_us_ = 0;
      drain_failures_ = 0U;
      drain_ready_to_present_ = false;
      result.drain_completed = true;
    }
  }

  if (!fill_view_available && fill_measurement_active_) {
    print_fill("paused");
    fill_timing_ = {};
    fill_measurement_active_ = false;
  }
  pending = vector_v2::pending_operation_count(log_, canvas_);
  if (fill_allowed && pending == 0U && !absorption_.active()) {
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
      run_fill(view, input.touch_urgency);
    } else if (!repair_planned_ || repair_cursor_ < repair_plan_.count) {
      run_repair(view, input.touch_urgency);
    } else if (!settle_complete_) {
      run_settle(input.loop_us, input.touch_urgency);
    }
  } else if (!input.pressed && !input.panning && !input.sample_ready &&
             !input.lift_report_pending && !settle_complete_ &&
             presenter_.zoom() == ZoomLevel::k25Percent &&
             chrome_.popup == vector_v2::ChromePopup::kNone &&
             vector_v2::pending_operation_count(log_, canvas_) == 0U) {
    run_settle(input.loop_us, input.touch_urgency);
  }
  result.fill_busy = absorption_.active() || pending != 0U ||
                     (fill_allowed && (!fill_complete_ || pending_fill_.pending));
  return result;
}

}  // namespace tinydraw::esp32
