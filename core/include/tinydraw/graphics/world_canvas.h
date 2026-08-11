#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"

namespace tinydraw {

struct ViewOrigin {
  int x = 0;
  int y = 0;
  friend bool operator==(const ViewOrigin&, const ViewOrigin&) = default;
};

// A fixed 2x2 drawing world around one screen-sized raster viewport. Full-world
// copies stay on the pan path; normal stroke updates continue using StrokeRaster.
class WorldCanvas {
 public:
  static constexpr int kWidth = kCanvasWidth * 2;
  static constexpr int kHeight = kCanvasHeight * 2;
  static constexpr std::size_t kRequiredPixels = static_cast<std::size_t>(kWidth * kHeight);
  static constexpr std::size_t kViewportPixels =
      static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);

  explicit WorldCanvas(std::span<std::uint16_t> storage);

  [[nodiscard]] bool valid() const { return valid_; }
  [[nodiscard]] ViewOrigin origin() const { return origin_; }
  [[nodiscard]] std::span<const std::uint16_t> pixels() const;

  [[nodiscard]] bool capture(std::span<const std::uint16_t> viewport);
  [[nodiscard]] bool capture_rect(std::span<const std::uint16_t> viewport, Rect rect);
  [[nodiscard]] bool replace(std::span<const std::uint16_t> pixels, ViewOrigin origin,
                             std::span<std::uint16_t> committed,
                             std::span<std::uint16_t> visible = {});
  [[nodiscard]] bool move_to(ViewOrigin origin);
  [[nodiscard]] bool show(ViewOrigin origin, std::span<std::uint16_t> committed,
                          std::span<std::uint16_t> visible = {});
  [[nodiscard]] bool clear(std::span<std::uint16_t> committed,
                           std::span<std::uint16_t> visible = {});

 private:
  [[nodiscard]] static ViewOrigin clamp_origin(ViewOrigin origin);
  void copy_to_viewport(std::span<std::uint16_t> viewport) const;

  std::span<std::uint16_t> storage_;
  ViewOrigin origin_{kCanvasWidth / 2, kCanvasHeight / 2};
  bool valid_ = false;
};

}  // namespace tinydraw
