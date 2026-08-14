#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/graphics/stroke_raster.h"
#include "tinydraw/graphics/tile_undo_history.h"
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/platform/display_backend.h"
#include "tinydraw/ui/toolbar.h"

namespace tinydraw {

struct RasterCoreStorage {
  std::span<std::uint16_t> committed;
  std::span<std::uint16_t> visible;
  std::span<std::uint8_t> active_coverage;
  std::span<std::uint16_t> undo;
  std::span<std::uint16_t> world;
};

// Deterministic, platform-neutral Raster V1 input/UI reducer. Platform shells
// supply storage, a display adapter, normalized touch reports, and timestamps.
// Persistence, export, power, RTC, networking, and demo services deliberately
// remain outside this module.
class RasterCore {
 public:
  static constexpr std::size_t kPixelCount = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);

  RasterCore(RasterCoreStorage storage, DisplayBackend& display);

  [[nodiscard]] bool ready() const { return ready_; }
  [[nodiscard]] std::span<std::uint16_t> framebuffer() { return storage_.visible; }
  [[nodiscard]] std::span<const std::uint16_t> framebuffer() const { return storage_.visible; }
  [[nodiscard]] const ToolbarState& toolbar() const { return toolbar_; }
  [[nodiscard]] ViewOrigin origin() const { return world_.origin(); }

  // Apply one normalized controller report. Callers must use their injected
  // deterministic clock; this module never reads a wall clock itself.
  void touch(bool touching, Point point, std::uint64_t timestamp_us);

  // Platform shells call this on every loop tick, whether or not input arrived.
  // Raster V1 has no time-driven behavior yet, but keeping this contract here
  // prevents shells from making input activity the application's clock.
  void tick(std::uint64_t now_us);

 private:
  class ToolbarDisplay final : public DisplayBackend {
   public:
    ToolbarDisplay(std::span<std::uint16_t> framebuffer, ToolbarState& toolbar,
                   DisplayBackend& downstream)
        : framebuffer_(framebuffer), toolbar_(toolbar), downstream_(downstream) {}

    void push_rect(int x, int y, int width, int height, const std::uint16_t* rgb565,
                   int stride = 0) override;

   private:
    std::span<std::uint16_t> framebuffer_;
    ToolbarState& toolbar_;
    DisplayBackend& downstream_;
  };

  void close_popups();
  void reset_stroke();
  void refresh_toolbar();
  void push_full();
  void select_size(PenSize size);
  void apply_toolbar_action(Point point);
  void pan_to(Point point);
  void undo();
  void new_drawing();

  RasterCoreStorage storage_;
  DisplayBackend& downstream_;
  ToolbarState toolbar_{};
  ToolbarDisplay toolbar_display_;
  std::optional<TileUndoHistory> undo_history_;
  WorldCanvas world_;
  std::optional<StrokeRaster> raster_;
  InkStream ink_;
  CurvedRibbonStream ribbon_;
  InkPoint last_ink_{};
  Point last_touch_{};
  Point toolbar_sum_{};
  std::uint32_t toolbar_samples_ = 0U;
  Point pan_start_touch_{};
  ViewOrigin pan_start_origin_{};
  std::uint16_t stroke_color_ = 0U;
  bool pressed_ = false;
  bool toolbar_pressed_ = false;
  bool panning_ = false;
  bool ready_ = false;
};

}  // namespace tinydraw
