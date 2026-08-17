#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "co5300_panel_transport.h"
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/platform/display_backend.h"
#include "tinydraw/ui/toolbar.h"

namespace tinydraw::esp32 {

inline constexpr int kPhysicalMainOverlayTop = kMainToolbarOverlayTop;

// Owns the CO5300 panel transport and the legacy toolbar compositor. The
// overlay allocation can be disabled for isolated production display probes.
class PhysicalDisplay final : public DisplayBackend {
 public:
  explicit PhysicalDisplay(bool enable_overlays = true);
  ~PhysicalDisplay() override;

  PhysicalDisplay(const PhysicalDisplay&) = delete;
  PhysicalDisplay& operator=(const PhysicalDisplay&) = delete;
  PhysicalDisplay(PhysicalDisplay&&) = delete;
  PhysicalDisplay& operator=(PhysicalDisplay&&) = delete;

  [[nodiscard]] bool ready() const;
  void reset_timing();
  [[nodiscard]] std::int64_t prepare_us() const;
  [[nodiscard]] std::int64_t transfer_us() const;
  [[nodiscard]] std::uint32_t push_count() const;
  [[nodiscard]] std::uint32_t rejected_push_count() const;
  [[nodiscard]] std::uint32_t submit_count() const;
  [[nodiscard]] std::uint32_t complete_count() const;
  [[nodiscard]] std::int64_t complete_time_us(std::uint32_t sequence) const;

  void set_toolbar(const ToolbarState& toolbar);
  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                 int stride = 0) override;
  void push_canvas(std::span<const std::uint16_t> canvas, int top = 0, int bottom = kCanvasHeight);
  void push_world(std::span<const std::uint16_t> world, ViewOrigin origin, int bottom);
  void refresh_toolbar(std::span<const std::uint16_t> canvas);
  void refresh_toolbar_world(std::span<const std::uint16_t> world, ViewOrigin origin);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::uint32_t physical_display_submit_count(void* context);
[[nodiscard]] std::uint32_t physical_display_complete_count(void* context);
[[nodiscard]] std::int64_t physical_display_complete_time_us(void* context, std::uint32_t sequence);

}  // namespace tinydraw::esp32
