#include "vector_v2_live_stroke_session.h"

#include <algorithm>
#include <cstdio>

#include "esp_timer.h"
#include "tinydraw/vector_v2/live_ink_coordinator.h"

namespace tinydraw::esp32 {
namespace {

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

void print_rejected(const char* site, const vector_v2::ChainedOperationBuilder& builder,
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

}  // namespace

void LiveStrokeMetrics::include(const LivePresentationTiming& timing) {
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

LiveStrokeSession::LiveStrokeSession(std::span<vector_v2::CompactOperationSample> builder_storage,
                                     vector_v2::OperationLog& log,
                                     vector_v2::MaterializedCanvas& canvas,
                                     const vector_v2::InPlaceAppendWorkspace& workspace,
                                     VectorV2Presenter& presenter)
    : log_(log),
      canvas_(canvas),
      workspace_(workspace),
      presenter_(presenter),
      builder_(builder_storage, kInteractiveChunkSampleLimit) {
  InkConfig config;
  // Owner experiment 2026-08-16: V2 uses stronger input smoothing. Raster V1
  // remains independent with its established 0.35 setting.
  config.streamline = 0.4F;
  ink_.set_config(config);
}

std::optional<vector_v2::ViewRequest> LiveStrokeSession::priority_view() const {
  if (presenter_.zoom() == vector_v2::ZoomLevel::k25Percent) {
    return std::nullopt;
  }
  return vector_v2::ViewRequest{
      .zoom = presenter_.zoom(),
      .level_pixels = {presenter_.level_x(), presenter_.level_y(),
                       presenter_.level_x() + vector_v2::kOverviewWidth,
                       presenter_.level_y() + vector_v2::kOverviewHeight},
  };
}

std::optional<vector_v2::IncrementalAppendResult> LiveStrokeSession::absorb_one(
    std::int64_t budget_us) {
  return vector_v2::absorb_pending_operation(
      log_, canvas_, workspace_, priority_view(),
      {.now_us = &esp_timer_get_time, .budget_us = budget_us});
}

void LiveStrokeSession::reset_stroke_stats() {
  world_bounds_.reset();
  metrics_ = {};
  phase_max_ = {};
  drops_ = {};
  chunks_ = 0;
  append_us_ = 0;
  append_max_us_ = 0;
  commit_failed_ = false;
}

LiveStrokeStartResult LiveStrokeSession::begin(Point screen_point, std::uint32_t event_us,
                                               float brush_size, vector_v2::OperationTool tool,
                                               std::uint16_t color, std::uint16_t gesture_id,
                                               const vector_v2::ChromeState& chrome) {
  InkConfig config = ink_.config();
  config.size = brush_size;
  ink_.set_config(config);
  color_ = color;
  last_touch_ = screen_point;
  last_canvas_touch_ = screen_point;
  last_ink_ = ink_.begin({.x = screen_point.x, .y = screen_point.y, .timestamp_us = event_us});
  first_operation_ = log_.operation_count();
  reset_stroke_stats();
  const vector_v2::OperationPoint begin_point = presenter_.operation_point(last_ink_);
  if (!builder_.begin(tool, color_, gesture_id, begin_point)) {
    print_rejected("begin", builder_, begin_point);
    ink_.end();
    return {};
  }
  ribbon_.reset();
  static_cast<void>(ribbon_.append(last_ink_, true));
  const LivePresentationTiming timing = presenter_.show_start(last_ink_, color_, chrome, event_us);
  metrics_.include(timing);
  return {.presentation = timing, .accepted = true};
}

std::optional<vector_v2::ChainedOperationStatus> LiveStrokeSession::commit_ready_chunk() {
  const auto append = builder_.pending_append();
  if (!append.has_value()) {
    return std::nullopt;
  }
  const std::int64_t started_us = esp_timer_get_time();
  // Pending authority stays presentation-exact through the committed overlay.
  // The background pipeline now absorbs it cooperatively between touch samples,
  // so the input path never enters the former synchronous high-water drain.
  const auto committed =
      vector_v2::append_authority_only(log_, *append, {.now_us = &esp_timer_get_time});
  const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
  append_us_ += elapsed_us;
  append_max_us_ = std::max(append_max_us_, elapsed_us);
  if (!committed.has_value()) {
    return std::nullopt;
  }
  include_phase_maxima(phase_max_, committed->phases);
  include_retain_drops(drops_, committed->drops);
  include_bounds(world_bounds_, committed->affected_world_bounds);
  ++chunks_;
  return builder_.acknowledge_commit();
}

LiveStrokeMoveResult LiveStrokeSession::move(Point screen_point, std::uint32_t event_us,
                                             const vector_v2::ChromeState& chrome) {
  LiveStrokeMoveResult result;
  if (!active() || (screen_point.x == last_touch_.x && screen_point.y == last_touch_.y)) {
    return result;
  }
  const auto clipped = vector_v2::clip_canvas_segment(
      {.x = last_touch_.x, .y = last_touch_.y}, {.x = screen_point.x, .y = screen_point.y}, chrome);
  last_touch_ = screen_point;
  if (!clipped.has_value()) {
    return result;
  }
  last_canvas_touch_ = Point{.x = clipped->x, .y = clipped->y};
  last_ink_ = ink_.update({.x = clipped->x, .y = clipped->y, .timestamp_us = event_us});
  const vector_v2::OperationPoint add_point = presenter_.operation_point(last_ink_);
  const std::uint32_t chunks_before = chunks_;
  const auto move_result = vector_v2::process_live_ink_move(
      ribbon_, builder_, last_ink_, add_point, last_canvas_touch_, event_us,
      [&](const RibbonUpdate& update, std::uint32_t visual_event_us) {
        result.geometry_us = static_cast<std::uint32_t>(esp_timer_get_time()) - event_us;
        result.presentation = presenter_.show_update(update, color_, chrome, visual_event_us);
        result.presented = true;
        metrics_.include(result.presentation);
        return result.presentation.passed;
      },
      [&] { return commit_ready_chunk(); });
  result.chunk_committed = chunks_ != chunks_before;
  result.commit_failed = move_result.commit_failed;
  commit_failed_ = commit_failed_ || move_result.commit_failed;
  if (move_result.status == vector_v2::ChainedOperationStatus::kAccepted) {
    return result;
  }

  if (move_result.commit_failed) {
    std::printf("TINYDRAW_STROKE_REJECTED site=commit reason=document_capacity\n");
    std::fflush(stdout);
  } else {
    print_rejected("add", builder_, add_point);
  }
  builder_.cancel();
  ribbon_.reset();
  ink_.end();
  result.rejection_refresh = presenter_.refresh(chrome, event_us);
  result.rejected = true;
  return result;
}

LiveStrokeFinishResult LiveStrokeSession::finish(std::uint32_t event_us,
                                                 const vector_v2::ChromeState& chrome) {
  LiveStrokeFinishResult result{};
  result.first_operation = first_operation_;
  if (!active()) {
    return result;
  }
  const std::int64_t finish_preview_started = esp_timer_get_time();
  last_ink_ =
      ink_.finish({.x = last_canvas_touch_.x, .y = last_canvas_touch_.y, .timestamp_us = event_us});
  result.preview = presenter_.show_update(ribbon_.finish(last_ink_), color_, chrome, event_us);
  metrics_.include(result.preview);
  result.finish_preview_us = esp_timer_get_time() - finish_preview_started;

  const std::int64_t builder_finish_started = esp_timer_get_time();
  vector_v2::ChainedOperationStatus finish_status =
      builder_.finish(presenter_.operation_point(last_ink_));
  result.builder_finish_us = esp_timer_get_time() - builder_finish_started;
  while (finish_status == vector_v2::ChainedOperationStatus::kChunkReady ||
         finish_status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
    const auto continued = commit_ready_chunk();
    if (!continued.has_value()) {
      commit_failed_ = true;
      finish_status = vector_v2::ChainedOperationStatus::kRejected;
      break;
    }
    finish_status = *continued;
  }
  result.committed = finish_status == vector_v2::ChainedOperationStatus::kComplete;
  result.commit_failed = commit_failed_;
  if (finish_status == vector_v2::ChainedOperationStatus::kRejected) {
    if (commit_failed_) {
      std::printf("TINYDRAW_STROKE_REJECTED site=commit reason=document_capacity\n");
      std::fflush(stdout);
    } else {
      print_rejected("finish", builder_, presenter_.operation_point(last_ink_));
    }
  }
  builder_.cancel();
  ribbon_.reset();

  const std::int64_t refresh_started = esp_timer_get_time();
  if (finish_status == vector_v2::ChainedOperationStatus::kRejected) {
    result.refresh = presenter_.refresh(chrome, event_us);
  } else {
    result.refresh.passed = true;
  }
  result.refresh_wall_us = esp_timer_get_time() - refresh_started;
  result.world_bounds = world_bounds_;
  result.metrics = metrics_;
  result.phase_max = phase_max_;
  result.drops = drops_;
  result.chunks = chunks_;
  result.append_us = append_us_;
  result.append_max_us = append_max_us_;
  return result;
}

}  // namespace tinydraw::esp32
