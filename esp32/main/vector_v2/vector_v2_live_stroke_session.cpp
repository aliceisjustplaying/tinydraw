#include "vector_v2_live_stroke_session.h"

#include <algorithm>
#include <cstdio>

#include "esp_timer.h"
#include "tinydraw/vector_v2/incremental_document.h"
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
    case vector_v2::OperationBuilderReject::kCapacityOverflow:
      return "capacity_overflow";
  }
  return "unknown";
}

void print_rejected(const char* site, const vector_v2::OperationBuilder& builder,
                    vector_v2::OperationPoint point) {
  std::printf(
      "TINYDRAW_STROKE_REJECTED site=%s reason=%s samples=%lu x=%.3f y=%.3f radius=%.5f "
      "timestamp_us=%lu\n",
      site, reject_name(builder.last_reject()), static_cast<unsigned long>(builder.sample_count()),
      static_cast<double>(point.world_x), static_cast<double>(point.world_y),
      static_cast<double>(point.radius), static_cast<unsigned long>(point.timestamp_us));
  std::fflush(stdout);
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
                                     vector_v2::OperationLog& log, VectorV2Presenter& presenter)
    : log_(log), presenter_(presenter), builder_(builder_storage) {
  InkConfig config;
  // Owner experiment 2026-08-16: V2 uses stronger input smoothing. Raster V1
  // remains independent with its established 0.35 setting.
  config.streamline = 0.4F;
  ink_.set_config(config);
}

void LiveStrokeSession::reset_stroke_stats() {
  world_bounds_.reset();
  metrics_ = {};
  append_us_ = 0;
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
  start_touch_ = screen_point;
  last_touch_ = screen_point;
  const vector_v2::ChromePoint attracted =
      vector_v2::attract_canvas_edges({.x = screen_point.x, .y = screen_point.y}, chrome);
  last_canvas_touch_ = Point{.x = attracted.x, .y = attracted.y};
  last_ink_ = ink_.begin(
      {.x = last_canvas_touch_.x, .y = last_canvas_touch_.y, .timestamp_us = event_us});
  first_operation_ = log_.operation_count();
  reset_stroke_stats();
  const vector_v2::OperationPoint begin_point = presenter_.operation_point(last_ink_);
  if (!builder_.begin(tool, color_, begin_point, gesture_id)) {
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

bool LiveStrokeSession::commit_operation(const vector_v2::BuiltOperation& operation) {
  const std::int64_t started_us = esp_timer_get_time();
  const auto committed =
      vector_v2::append_authority_only(log_, operation, {.now_us = &esp_timer_get_time});
  const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
  append_us_ += elapsed_us;
  if (!committed.has_value()) {
    return false;
  }
  world_bounds_ = committed->affected_world_bounds;
  return true;
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
  const vector_v2::ChromePoint attracted = vector_v2::attract_canvas_edges(*clipped, chrome);
  last_canvas_touch_ = Point{.x = attracted.x, .y = attracted.y};
  last_ink_ = ink_.update(
      {.x = last_canvas_touch_.x, .y = last_canvas_touch_.y, .timestamp_us = event_us});
  const vector_v2::OperationPoint add_point = presenter_.operation_point(last_ink_);
  const RibbonUpdate update = ribbon_.append(last_ink_, true, last_canvas_touch_);
  result.geometry_us = static_cast<std::uint32_t>(esp_timer_get_time()) - event_us;
  result.presentation = presenter_.show_update(update, color_, chrome, event_us);
  result.presented = true;
  metrics_.include(result.presentation);
  if (builder_.add(add_point)) {
    return result;
  }

  print_rejected("add", builder_, add_point);
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
  const bool has_drawn_segment = builder_.sample_count() > 1U;
  if (!vector_v2::chrome_accepts_stroke_finish({.x = start_touch_.x, .y = start_touch_.y},
                                               has_drawn_segment)) {
    builder_.cancel();
    ribbon_.reset();
    ink_.end();
    const std::int64_t refresh_started = esp_timer_get_time();
    result.refresh = presenter_.refresh(chrome, event_us);
    result.refresh_wall_us = esp_timer_get_time() - refresh_started;
    result.metrics = metrics_;
    return result;
  }
  const std::int64_t finish_preview_started = esp_timer_get_time();
  last_ink_ =
      ink_.finish({.x = last_canvas_touch_.x, .y = last_canvas_touch_.y, .timestamp_us = event_us});
  result.preview = presenter_.show_update(ribbon_.finish(last_ink_), color_, chrome, event_us);
  metrics_.include(result.preview);
  result.finish_preview_us = esp_timer_get_time() - finish_preview_started;

  const std::int64_t builder_finish_started = esp_timer_get_time();
  const auto operation = builder_.finish(presenter_.operation_point(last_ink_));
  result.builder_finish_us = esp_timer_get_time() - builder_finish_started;
  result.committed = operation.has_value() && commit_operation(*operation);
  commit_failed_ = operation.has_value() && !result.committed;
  result.commit_failed = commit_failed_;
  if (!result.committed) {
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
  if (!result.committed) {
    result.refresh = presenter_.refresh(chrome, event_us);
  } else {
    result.refresh.passed = true;
  }
  result.refresh_wall_us = esp_timer_get_time() - refresh_started;
  result.world_bounds = world_bounds_;
  result.metrics = metrics_;
  result.append_us = append_us_;
  return result;
}

}  // namespace tinydraw::esp32
