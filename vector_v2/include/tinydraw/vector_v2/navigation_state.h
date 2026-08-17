#ifndef TINYDRAW_VECTOR_V2_NAVIGATION_STATE_H
#define TINYDRAW_VECTOR_V2_NAVIGATION_STATE_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace tinydraw::vector_v2 {

struct NavigationPoint {
  int x = 0;
  int y = 0;
  bool operator==(const NavigationPoint&) const = default;
};

struct NavigationExtent {
  bool top = false;
  bool left = false;
  bool right = false;
  bool bottom = false;
  bool operator==(const NavigationExtent&) const = default;
};

// Complete user-navigation state. Persistence stores this value rather than
// only the active origin so zoom-cycle return positions survive restart.
struct NavigationSnapshot {
  ZoomLevel zoom = ZoomLevel::k25Percent;
  NavigationPoint origin{};
  NavigationPoint focus_quarter_world{kWorldWidth * 2, kWorldHeight * 2};
  std::array<NavigationPoint, 4> remembered_origins{};
  std::array<NavigationPoint, 4> remembered_focuses{};
  std::array<bool, 4> remembered_valid{};
  bool operator==(const NavigationSnapshot&) const = default;
};

class NavigationState {
 public:
  NavigationState();

  [[nodiscard]] ZoomLevel zoom() const;
  [[nodiscard]] NavigationPoint origin() const;
  [[nodiscard]] NavigationPoint focus_quarter_world() const;
  [[nodiscard]] ViewRequest view() const;
  [[nodiscard]] NavigationExtent extent() const;
  [[nodiscard]] NavigationSnapshot snapshot() const;
  // Validates every persisted coordinate before replacing live navigation.
  // Failure leaves this object unchanged.
  [[nodiscard]] bool restore(const NavigationSnapshot& snapshot);

  // Changes zoom around panel_focus. A compatible remembered view restores
  // exactly; otherwise the retained world focus is centered there. Entering
  // 25% retains the tiled focus; the initial 25% view uses the world center.
  [[nodiscard]] bool set_zoom(ZoomLevel target_zoom, NavigationPoint panel_focus);
  // Moves the active camera and updates its world focus. This is also the seam
  // used by deterministic hardware views and future minimap navigation.
  [[nodiscard]] bool set_origin(int x, int y, NavigationPoint panel_focus);
  // Pure level-bounds clamping for a viewport origin at a zoom. Public so
  // idle-repair planning shares the single clamping truth.
  [[nodiscard]] static NavigationPoint clamp_origin(ZoomLevel zoom, int x, int y);

 private:
  [[nodiscard]] static bool valid_panel_focus(NavigationPoint point);
  [[nodiscard]] static NavigationPoint focus_for_view(ZoomLevel zoom, NavigationPoint origin,
                                                      NavigationPoint panel_focus);
  [[nodiscard]] NavigationPoint centered_origin(ZoomLevel target_zoom,
                                                NavigationPoint panel_focus) const;

  ZoomLevel zoom_ = ZoomLevel::k25Percent;
  NavigationPoint origin_{};
  NavigationPoint focus_quarter_world_{kWorldWidth * 2, kWorldHeight * 2};
  std::array<NavigationPoint, 4> remembered_origins_{};
  std::array<NavigationPoint, 4> remembered_focuses_{};
  std::array<bool, 4> remembered_valid_{};
};

[[nodiscard]] ZoomLevel next_zoom(ZoomLevel zoom);
[[nodiscard]] ZoomLevel previous_zoom(ZoomLevel zoom);

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_NAVIGATION_STATE_H
