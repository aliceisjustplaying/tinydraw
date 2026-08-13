#include "physical_display.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr int kCompositionPixels = 8192;
constexpr int kDialogOverlayX = 26;
constexpr int kDialogOverlayTop = 124;
constexpr int kDialogOverlayWidth = 318;
constexpr int kDialogOverlayHeight = 168;
constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

int palette_overlay_top(ToolbarState state) {
  state.confirm_new = false;
  return toolbar_overlay_top(state) & ~1;
}

}  // namespace

class PhysicalDisplay::Impl {
 public:
  explicit Impl(bool enable_overlays) : overlays_enabled_(enable_overlays) {
    if (!overlays_enabled_) {
      return;
    }
    overlay_ = static_cast<std::uint16_t*>(heap_caps_malloc(
        static_cast<std::size_t>(kCanvasWidth * kCanvasHeight) * sizeof(std::uint16_t),
        kExternalCaps));
    composition_ = static_cast<std::uint16_t*>(heap_caps_malloc(
        static_cast<std::size_t>(kCompositionPixels) * sizeof(std::uint16_t), kExternalCaps));
    if (overlay_ != nullptr) {
      std::fill_n(overlay_, kCanvasWidth * kCanvasHeight, kBackground);
    }
  }

  ~Impl() {
    heap_caps_free(composition_);
    heap_caps_free(overlay_);
  }

  [[nodiscard]] bool ready() const {
    return transport_.ready() &&
           (!overlays_enabled_ || (overlay_ != nullptr && composition_ != nullptr));
  }
  void reset_timing() {
    transport_.reset_timing();
    overlay_prepare_us_ = 0;
  }
  [[nodiscard]] std::int64_t prepare_us() const {
    return transport_.prepare_us() + overlay_prepare_us_;
  }
  [[nodiscard]] std::int64_t transfer_us() const { return transport_.transfer_us(); }
  [[nodiscard]] std::uint32_t push_count() const { return transport_.push_count(); }
  [[nodiscard]] std::uint32_t rejected_push_count() const {
    return transport_.rejected_push_count();
  }
  [[nodiscard]] std::uint32_t submit_count() const { return transport_.submit_count(); }
  [[nodiscard]] std::uint32_t complete_count() const { return transport_.complete_count(); }
  [[nodiscard]] std::int64_t complete_time_us(std::uint32_t sequence) const {
    return transport_.complete_time_us(sequence);
  }

  void set_toolbar(const ToolbarState& toolbar) {
    if (!overlays_enabled_) {
      return;
    }
    const bool main_changed = toolbar_.tool != toolbar.tool || toolbar_.color != toolbar.color ||
                              toolbar_.size != toolbar.size ||
                              toolbar_.can_undo != toolbar.can_undo ||
                              toolbar_.recording != toolbar.recording;
    const bool battery_changed = toolbar_.battery_percentage != toolbar.battery_percentage ||
                                 toolbar_.battery_charging != toolbar.battery_charging ||
                                 toolbar_.external_power != toolbar.external_power;
    const bool toast_changed = toolbar_.export_toast != toolbar.export_toast ||
                               ((toolbar_.export_toast || toolbar.export_toast) &&
                                toolbar_.export_ready != toolbar.export_ready);
    const bool palette_changed =
        toolbar_.tools_open != toolbar.tools_open || toolbar_.colors_open != toolbar.colors_open ||
        toolbar_.sizes_open != toolbar.sizes_open || toolbar_.can_export != toolbar.can_export ||
        toolbar_.exporting != toolbar.exporting || toolbar_.export_ready != toolbar.export_ready ||
        ((toolbar_.tools_open || toolbar.tools_open) && toolbar_.tool != toolbar.tool) ||
        ((toolbar_.colors_open || toolbar.colors_open) && toolbar_.color != toolbar.color) ||
        ((toolbar_.sizes_open || toolbar.sizes_open) && toolbar_.size != toolbar.size);
    main_dirty_ = main_dirty_ || main_changed;
    if (battery_changed) {
      const auto old_rect = battery_overlay_rect(toolbar_);
      const auto new_rect = battery_overlay_rect(toolbar);
      battery_refresh_ = old_rect.value_or(new_rect.value_or(Rect{}));
      battery_dirty_ = old_rect.has_value() || new_rect.has_value();
    }
    if (toast_changed) {
      const auto old_rect = export_toast_rect(toolbar_);
      const auto new_rect = export_toast_rect(toolbar);
      toast_refresh_ = old_rect.value_or(new_rect.value_or(Rect{}));
      toast_dirty_ = old_rect.has_value() || new_rect.has_value();
    }
    if (palette_changed) {
      const int changed_top = std::min(palette_overlay_top(toolbar_), palette_overlay_top(toolbar));
      palette_refresh_top_ =
          palette_dirty_ ? std::min(palette_refresh_top_, changed_top) : changed_top;
      palette_dirty_ = true;
    }
    dialog_dirty_ = dialog_dirty_ || toolbar_.confirm_new != toolbar.confirm_new;
    toolbar_ = toolbar;
    toolbar_top_ = toolbar_overlay_top(toolbar);
    if (main_dirty_) {
      clear_overlay(0, kPhysicalMainOverlayTop, kCanvasWidth,
                    kCanvasHeight - kPhysicalMainOverlayTop);
    }
    if (battery_dirty_) {
      clear_overlay(battery_refresh_.x0, battery_refresh_.y0,
                    battery_refresh_.x1 - battery_refresh_.x0,
                    battery_refresh_.y1 - battery_refresh_.y0);
    }
    if (toast_dirty_) {
      clear_overlay(toast_refresh_.x0, toast_refresh_.y0, toast_refresh_.x1 - toast_refresh_.x0,
                    toast_refresh_.y1 - toast_refresh_.y0);
    }
    if (palette_dirty_) {
      clear_overlay(0, palette_refresh_top_, kCanvasWidth,
                    kPhysicalMainOverlayTop - palette_refresh_top_);
    }
    if (dialog_dirty_) {
      clear_overlay(kDialogOverlayX, kDialogOverlayTop, kDialogOverlayWidth, kDialogOverlayHeight);
    }
    draw_toolbar(std::span(overlay_, static_cast<std::size_t>(kCanvasWidth * kCanvasHeight)),
                 kCanvasWidth, kCanvasHeight, toolbar_);
  }

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels, int stride) {
    const int source_stride = stride == 0 ? width : stride;
    const bool in_bounds = x >= 0 && y >= 0 && x < kCanvasWidth && y < kCanvasHeight && width > 0 &&
                           height > 0 && width <= kCanvasWidth - x && height <= kCanvasHeight - y;
    const bool valid_window = ((x | y | width | height) & 1) == 0;
    if (!ready() || pixels == nullptr || !in_bounds || !valid_window || source_stride < width ||
        !overlays_enabled_) {
      transport_.push_rect(x, y, width, height, pixels, stride);
      return;
    }
    const auto battery_rect = battery_overlay_rect(toolbar_);
    const bool intersects_battery = battery_rect.has_value() && x < battery_rect->x1 &&
                                    x + width > battery_rect->x0 && y < battery_rect->y1 &&
                                    y + height > battery_rect->y0;
    const auto toast_rect = export_toast_rect(toolbar_);
    const bool intersects_toast = toast_rect.has_value() && x < toast_rect->x1 &&
                                  x + width > toast_rect->x0 && y < toast_rect->y1 &&
                                  y + height > toast_rect->y0;
    if (y + height <= toolbar_top_ && !intersects_battery && !intersects_toast) {
      transport_.push_rect(x, y, width, height, pixels, source_stride);
      return;
    }
    int rows_per_transfer = kCompositionPixels / width;
    rows_per_transfer -= rows_per_transfer % 2;
    if (rows_per_transfer <= 0) {
      transport_.push_rect(x, y, width, height, nullptr, source_stride);
      return;
    }
    for (int row0 = 0; row0 < height; row0 += rows_per_transfer) {
      const int rows = std::min(rows_per_transfer, height - row0);
      const std::int64_t composition_started = esp_timer_get_time();
      for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < width; ++column) {
          const int panel_x = x + column;
          const int panel_y = y + row0 + row;
          const std::size_t source =
              static_cast<std::size_t>((row0 + row) * source_stride + column);
          const std::size_t destination = static_cast<std::size_t>(row * width + column);
          const std::size_t canvas = static_cast<std::size_t>(panel_y * kCanvasWidth + panel_x);
          const Point point{static_cast<float>(panel_x) + 0.5F, static_cast<float>(panel_y) + 0.5F};
          composition_[destination] =
              toolbar_overlay_contains(point, toolbar_) ? overlay_[canvas] : pixels[source];
        }
      }
      overlay_prepare_us_ += esp_timer_get_time() - composition_started;
      transport_.push_rect(x, y + row0, width, rows, composition_, width);
    }
  }

  void push_canvas(std::span<const std::uint16_t> canvas, int top, int bottom) {
    constexpr int rows_per_transfer = (kCompositionPixels / kCanvasWidth) & ~1;
    for (int y = top; y < bottom; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, bottom - y);
      push_rect(0, y, kCanvasWidth, height,
                canvas.data() + static_cast<std::size_t>(y * kCanvasWidth), kCanvasWidth);
    }
    if (top == 0 && bottom == kCanvasHeight) {
      clear_dirty();
    }
  }

  void push_world(std::span<const std::uint16_t> world, ViewOrigin origin, int bottom) {
    constexpr int rows_per_transfer = (kCompositionPixels / kCanvasWidth) & ~1;
    for (int y = 0; y < bottom; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, bottom - y);
      const auto offset = static_cast<std::size_t>((origin.y + y) * WorldCanvas::kWidth + origin.x);
      push_rect(0, y, kCanvasWidth, height, world.data() + offset, WorldCanvas::kWidth);
    }
  }

  void refresh_toolbar(std::span<const std::uint16_t> canvas) {
    refresh_toolbar_source(canvas, kCanvasWidth, 0, 0);
  }
  void refresh_toolbar_world(std::span<const std::uint16_t> world, ViewOrigin origin) {
    refresh_toolbar_source(world, WorldCanvas::kWidth, origin.x, origin.y);
  }

 private:
  void refresh_toolbar_source(std::span<const std::uint16_t> source, int stride, int source_x,
                              int source_y) {
    const auto push = [&](Rect rect) {
      const auto offset =
          static_cast<std::size_t>((source_y + rect.y0) * stride + source_x + rect.x0);
      push_rect(rect.x0, rect.y0, rect.x1 - rect.x0, rect.y1 - rect.y0, source.data() + offset,
                stride);
    };
    if (battery_dirty_) {
      push(battery_refresh_);
    }
    if (toast_dirty_) {
      push(toast_refresh_);
    }
    if (dialog_dirty_) {
      push({kDialogOverlayX, kDialogOverlayTop, kDialogOverlayX + kDialogOverlayWidth,
            kDialogOverlayTop + kDialogOverlayHeight});
    }
    if (palette_dirty_) {
      push({0, palette_refresh_top_, kCanvasWidth, kPhysicalMainOverlayTop});
    }
    if (main_dirty_) {
      push({0, kPhysicalMainOverlayTop, kCanvasWidth, kCanvasHeight});
    }
    clear_dirty();
  }

  void clear_dirty() {
    main_dirty_ = false;
    battery_dirty_ = false;
    toast_dirty_ = false;
    palette_dirty_ = false;
    palette_refresh_top_ = kPhysicalMainOverlayTop;
    dialog_dirty_ = false;
  }

  void clear_overlay(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
      auto* start = overlay_ + static_cast<std::ptrdiff_t>((y + row) * kCanvasWidth + x);
      std::fill_n(start, width, kBackground);
    }
  }

  Co5300PanelTransport transport_;
  std::int64_t overlay_prepare_us_ = 0;
  std::uint16_t* overlay_ = nullptr;
  std::uint16_t* composition_ = nullptr;
  ToolbarState toolbar_{};
  int toolbar_top_ = toolbar_overlay_top(toolbar_);
  bool main_dirty_ = false;
  bool battery_dirty_ = false;
  Rect battery_refresh_{};
  bool toast_dirty_ = false;
  Rect toast_refresh_{};
  bool palette_dirty_ = false;
  int palette_refresh_top_ = kPhysicalMainOverlayTop;
  bool dialog_dirty_ = false;
  bool overlays_enabled_ = true;
};

PhysicalDisplay::PhysicalDisplay(bool enable_overlays)
    : impl_(std::make_unique<Impl>(enable_overlays)) {}
PhysicalDisplay::~PhysicalDisplay() = default;
bool PhysicalDisplay::ready() const { return impl_->ready(); }
void PhysicalDisplay::reset_timing() { impl_->reset_timing(); }
std::int64_t PhysicalDisplay::prepare_us() const { return impl_->prepare_us(); }
std::int64_t PhysicalDisplay::transfer_us() const { return impl_->transfer_us(); }
std::uint32_t PhysicalDisplay::push_count() const { return impl_->push_count(); }
std::uint32_t PhysicalDisplay::rejected_push_count() const { return impl_->rejected_push_count(); }
std::uint32_t PhysicalDisplay::submit_count() const { return impl_->submit_count(); }
std::uint32_t PhysicalDisplay::complete_count() const { return impl_->complete_count(); }
std::int64_t PhysicalDisplay::complete_time_us(std::uint32_t sequence) const {
  return impl_->complete_time_us(sequence);
}
void PhysicalDisplay::set_toolbar(const ToolbarState& toolbar) { impl_->set_toolbar(toolbar); }
void PhysicalDisplay::push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                                int stride) {
  impl_->push_rect(x, y, width, height, pixels, stride);
}
void PhysicalDisplay::push_canvas(std::span<const std::uint16_t> canvas, int top, int bottom) {
  impl_->push_canvas(canvas, top, bottom);
}
void PhysicalDisplay::push_world(std::span<const std::uint16_t> world, ViewOrigin origin,
                                 int bottom) {
  impl_->push_world(world, origin, bottom);
}
void PhysicalDisplay::refresh_toolbar(std::span<const std::uint16_t> canvas) {
  impl_->refresh_toolbar(canvas);
}
void PhysicalDisplay::refresh_toolbar_world(std::span<const std::uint16_t> world,
                                            ViewOrigin origin) {
  impl_->refresh_toolbar_world(world, origin);
}

std::uint32_t physical_display_submit_count(void* context) {
  return static_cast<PhysicalDisplay*>(context)->submit_count();
}
std::uint32_t physical_display_complete_count(void* context) {
  return static_cast<PhysicalDisplay*>(context)->complete_count();
}
std::int64_t physical_display_complete_time_us(void* context, std::uint32_t sequence) {
  return static_cast<PhysicalDisplay*>(context)->complete_time_us(sequence);
}

}  // namespace tinydraw::esp32
