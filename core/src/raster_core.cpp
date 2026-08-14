#include "tinydraw/app/raster_core.h"

#include <algorithm>
#include <cmath>

namespace tinydraw {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;

// InkStream intentionally stores uint32_t microseconds. Its unsigned delta
// subtraction (covered by the wrap-around unit test) makes this truncation
// safe across the normal uint32_t wrap; intervals of at least 2^31 us are
// classified as backward and receive the nominal dt. RasterCore and platform
// shells retain the full monotonic timestamp for all other work.
std::uint32_t ink_timestamp(std::uint64_t timestamp_us) {
  return static_cast<std::uint32_t>(timestamp_us);
}

bool valid_storage(const RasterCoreStorage& storage) {
  return storage.committed.size() >= RasterCore::kPixelCount &&
         storage.visible.size() >= RasterCore::kPixelCount &&
         storage.active_coverage.size() >= RasterCore::kPixelCount &&
         storage.undo.size() >= TileUndoHistory::kRequiredPixels &&
         storage.world.size() >= WorldCanvas::kRequiredPixels;
}

}  // namespace

void RasterCore::ToolbarDisplay::push_rect(int x, int y, int width, int height,
                                           const std::uint16_t*, int) {
  // This adapter supports only the visible-framebuffer-backed configuration.
  if (framebuffer_.size() < RasterCore::kPixelCount || x < 0 || y < 0 || width <= 0 ||
      height <= 0 || width > kCanvasWidth || height > kCanvasHeight || x > kCanvasWidth - width ||
      y > kCanvasHeight - height) {
    return;
  }
  draw_toolbar(framebuffer_, kCanvasWidth, kCanvasHeight, toolbar_);
  const auto offset = static_cast<std::size_t>(y * kCanvasWidth + x);
  downstream_.push_rect(x, y, width, height, framebuffer_.data() + offset, kCanvasWidth);
}

RasterCore::RasterCore(RasterCoreStorage storage, DisplayBackend& display)
    : storage_(storage),
      downstream_(display),
      toolbar_display_(storage_.visible, toolbar_, downstream_),
      world_(storage_.world),
      ink_({.size = brush_size(toolbar_.size)}) {
  ready_ = valid_storage(storage_) && world_.valid();
  if (!ready_) {
    return;
  }
  undo_history_.emplace(storage_.undo);
  raster_.emplace(storage_.committed, storage_.visible, storage_.active_coverage, toolbar_display_);
  static_cast<void>(world_.clear(storage_.committed, storage_.visible));
  draw_toolbar(storage_.visible, kCanvasWidth, kCanvasHeight, toolbar_);
  downstream_.push_rect(0, 0, kCanvasWidth, kCanvasHeight, storage_.visible.data(), kCanvasWidth);
}

void RasterCore::close_popups() {
  toolbar_.tools_open = false;
  toolbar_.colors_open = false;
  toolbar_.sizes_open = false;
}

void RasterCore::reset_stroke() {
  ink_.end();
  ribbon_.reset();
  raster_->cancel();
}

void RasterCore::refresh_toolbar() {
  if (!ready_) {
    return;
  }
  std::copy_n(storage_.committed.begin(), kPixelCount, storage_.visible.begin());
  draw_toolbar(storage_.visible, kCanvasWidth, kCanvasHeight, toolbar_);
  downstream_.push_rect(0, 0, kCanvasWidth, kCanvasHeight, storage_.visible.data(), kCanvasWidth);
}

void RasterCore::push_full() {
  if (!ready_) {
    return;
  }
  draw_toolbar(storage_.visible, kCanvasWidth, kCanvasHeight, toolbar_);
  downstream_.push_rect(0, 0, kCanvasWidth, kCanvasHeight, storage_.visible.data(), kCanvasWidth);
}

void RasterCore::select_size(PenSize size) {
  toolbar_.size = size;
  InkConfig config = ink_.config();
  config.size = brush_size(size);
  ink_.set_config(config);
  close_popups();
}

void RasterCore::undo() {
  if (!undo_history_->can_undo()) {
    return;
  }
  reset_stroke();
  static_cast<void>(world_.capture(storage_.committed));
  const auto undo_origin = undo_history_->next_undo_origin();
  const bool view_changed = undo_origin.has_value() && *undo_origin != world_.origin();
  if (view_changed) {
    static_cast<void>(world_.show(*undo_origin, storage_.committed, storage_.visible));
  }
  static_cast<void>(undo_history_->undo(storage_.committed, storage_.visible));
  static_cast<void>(world_.capture(storage_.committed));
  toolbar_.can_undo = undo_history_->can_undo();
  close_popups();
  push_full();
}

void RasterCore::new_drawing() {
  reset_stroke();
  undo_history_->begin_entry(world_.origin());
  undo_history_->capture_canvas(storage_.committed);
  static_cast<void>(undo_history_->commit_entry());
  static_cast<void>(world_.clear(storage_.committed, storage_.visible));
  toolbar_.confirm_new = false;
  toolbar_.can_undo = undo_history_->can_undo();
  close_popups();
  push_full();
}

void RasterCore::apply_toolbar_action(Point point) {
  const ToolbarAction action = toolbar_action_at(point, toolbar_);
  switch (action) {
    case ToolbarAction::kSelectPen:
      toolbar_.tool = DrawingTool::kPen;
      close_popups();
      break;
    case ToolbarAction::kSelectPan:
      toolbar_.tool = DrawingTool::kPan;
      close_popups();
      break;
    case ToolbarAction::kSelectEraser:
      toolbar_.tool = DrawingTool::kEraser;
      close_popups();
      break;
    case ToolbarAction::kSelectColor:
      toolbar_.color = toolbar_color_at(point, toolbar_).value_or(toolbar_.color);
      toolbar_.tool = DrawingTool::kPen;
      close_popups();
      break;
    case ToolbarAction::kToggleTools:
      toolbar_.tools_open = !toolbar_.tools_open;
      toolbar_.colors_open = false;
      toolbar_.sizes_open = false;
      break;
    case ToolbarAction::kToggleColors:
      toolbar_.colors_open = !toolbar_.colors_open;
      toolbar_.tools_open = false;
      toolbar_.sizes_open = false;
      break;
    case ToolbarAction::kToggleSizes:
      toolbar_.sizes_open = !toolbar_.sizes_open;
      toolbar_.tools_open = false;
      toolbar_.colors_open = false;
      break;
    case ToolbarAction::kSelectSmall:
      select_size(PenSize::kSmall);
      break;
    case ToolbarAction::kSelectMedium:
      select_size(PenSize::kMedium);
      break;
    case ToolbarAction::kSelectLarge:
      select_size(PenSize::kLarge);
      break;
    case ToolbarAction::kSelectExtraLarge:
      select_size(PenSize::kExtraLarge);
      break;
    case ToolbarAction::kUndo:
      undo();
      return;
    case ToolbarAction::kNewDrawing:
      close_popups();
      toolbar_.confirm_new = true;
      break;
    case ToolbarAction::kCancelNewDrawing:
      toolbar_.confirm_new = false;
      break;
    case ToolbarAction::kConfirmNewDrawing:
      new_drawing();
      return;
    case ToolbarAction::kExport:
    case ToolbarAction::kNone:
      break;
  }
  toolbar_.can_undo = undo_history_->can_undo();
  refresh_toolbar();
}

void RasterCore::pan_to(Point point) {
  const int delta_x = static_cast<int>(std::lround(point.x - pan_start_touch_.x));
  const int delta_y = static_cast<int>(std::lround(point.y - pan_start_touch_.y));
  const ViewOrigin requested{pan_start_origin_.x - delta_x, pan_start_origin_.y - delta_y};
  if (!world_.show(requested, storage_.committed, storage_.visible)) {
    return;
  }
  push_full();
}

void RasterCore::touch(bool touching, Point point, std::uint64_t timestamp_us) {
  if (!ready_) {
    return;
  }
  point.x = std::clamp(point.x, 0.0F, static_cast<float>(kCanvasWidth - 1));
  point.y = std::clamp(point.y, 0.0F, static_cast<float>(kCanvasHeight - 1));

  if (touching && !pressed_) {
    pressed_ = true;
    last_touch_ = point;
    if (toolbar_.confirm_new) {
      const ToolbarAction action = toolbar_action_at(point, toolbar_);
      if (action == ToolbarAction::kCancelNewDrawing ||
          action == ToolbarAction::kConfirmNewDrawing) {
        apply_toolbar_action(point);
        return;
      }
    }
    if (toolbar_contains(point, toolbar_)) {
      toolbar_pressed_ = true;
      toolbar_sum_ = point;
      toolbar_samples_ = 1U;
      return;
    }
    if (toolbar_.tools_open || toolbar_.colors_open || toolbar_.sizes_open) {
      close_popups();
      refresh_toolbar();
    }
    if (toolbar_.tool == DrawingTool::kPan) {
      static_cast<void>(world_.capture(storage_.committed));
      pan_start_touch_ = point;
      pan_start_origin_ = world_.origin();
      panning_ = true;
      return;
    }
    stroke_color_ = toolbar_.tool == DrawingTool::kEraser ? kBackground : rgb565(toolbar_.color);
    InkConfig config = ink_.config();
    config.size = brush_size(toolbar_.size);
    ink_.set_config(config);
    last_ink_ =
        ink_.begin({.x = point.x, .y = point.y, .timestamp_us = ink_timestamp(timestamp_us)});
    static_cast<void>(raster_->update(ribbon_.append(last_ink_), stroke_color_));
    return;
  }

  if (touching && pressed_ && (point.x != last_touch_.x || point.y != last_touch_.y)) {
    last_touch_ = point;
    if (toolbar_pressed_) {
      if (toolbar_contains(point, toolbar_)) {
        toolbar_sum_.x += point.x;
        toolbar_sum_.y += point.y;
        ++toolbar_samples_;
      }
      return;
    }
    if (panning_) {
      pan_to(point);
      return;
    }
    if (ink_.active()) {
      last_ink_ =
          ink_.update({.x = point.x, .y = point.y, .timestamp_us = ink_timestamp(timestamp_us)});
      static_cast<void>(raster_->update(ribbon_.append(last_ink_), stroke_color_));
    }
    return;
  }

  if (touching || !pressed_) {
    return;
  }

  pressed_ = false;
  if (toolbar_pressed_) {
    toolbar_pressed_ = false;
    const float divisor = static_cast<float>(toolbar_samples_ == 0U ? 1U : toolbar_samples_);
    const Point tap{.x = toolbar_sum_.x / divisor, .y = toolbar_sum_.y / divisor};
    toolbar_samples_ = 0U;
    if (toolbar_contains(tap, toolbar_)) {
      apply_toolbar_action(tap);
    }
    return;
  }
  if (panning_) {
    pan_to(point);
    panning_ = false;
    static_cast<void>(world_.show(world_.origin(), storage_.committed, storage_.visible));
    push_full();
    return;
  }
  if (ink_.active()) {
    last_ink_ = ink_.finish(
        {.x = last_touch_.x, .y = last_touch_.y, .timestamp_us = ink_timestamp(timestamp_us)});
    static_cast<void>(raster_->finish(ribbon_.finish(last_ink_), stroke_color_, &*undo_history_,
                                      world_.origin()));
    static_cast<void>(world_.capture(storage_.committed));
    toolbar_.can_undo = undo_history_->can_undo();
    refresh_toolbar();
  }
}

void RasterCore::tick(std::uint64_t now_us) { static_cast<void>(now_us); }

}  // namespace tinydraw
