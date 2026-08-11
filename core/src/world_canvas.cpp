#include "tinydraw/graphics/world_canvas.h"

#include <algorithm>

namespace tinydraw {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;

bool valid_viewport(std::span<const std::uint16_t> viewport) {
  return viewport.size() >= WorldCanvas::kViewportPixels;
}

bool valid_viewport(std::span<std::uint16_t> viewport) {
  return viewport.size() >= WorldCanvas::kViewportPixels;
}

}  // namespace

WorldCanvas::WorldCanvas(std::span<std::uint16_t> storage) : storage_(storage) {
  valid_ = storage_.size() >= kRequiredPixels;
  if (valid_) {
    std::fill_n(storage_.begin(), kRequiredPixels, kBackground);
  }
}

std::span<const std::uint16_t> WorldCanvas::pixels() const {
  return valid_ ? std::span<const std::uint16_t>(storage_.data(), kRequiredPixels)
                : std::span<const std::uint16_t>{};
}

bool WorldCanvas::capture(std::span<const std::uint16_t> viewport) {
  return capture_rect(viewport, {0, 0, kCanvasWidth, kCanvasHeight});
}

bool WorldCanvas::capture_rect(std::span<const std::uint16_t> viewport, Rect rect) {
  if (!valid_ || !valid_viewport(viewport)) {
    return false;
  }
  rect.x0 = std::clamp(rect.x0, 0, kCanvasWidth);
  rect.y0 = std::clamp(rect.y0, 0, kCanvasHeight);
  rect.x1 = std::clamp(rect.x1, rect.x0, kCanvasWidth);
  rect.y1 = std::clamp(rect.y1, rect.y0, kCanvasHeight);
  const int width = rect.x1 - rect.x0;
  for (int row = rect.y0; row < rect.y1; ++row) {
    const auto source =
        viewport.begin() + static_cast<std::ptrdiff_t>(row * kCanvasWidth + rect.x0);
    const auto destination =
        storage_.begin() +
        static_cast<std::ptrdiff_t>((origin_.y + row) * kWidth + origin_.x + rect.x0);
    std::copy_n(source, width, destination);
  }
  return true;
}

bool WorldCanvas::replace(std::span<const std::uint16_t> pixels, ViewOrigin origin,
                          std::span<std::uint16_t> committed, std::span<std::uint16_t> visible) {
  if (!valid_ || pixels.size() < kRequiredPixels || !valid_viewport(committed) ||
      (!visible.empty() && !valid_viewport(visible))) {
    return false;
  }
  std::copy_n(pixels.begin(), kRequiredPixels, storage_.begin());
  origin_ = clamp_origin(origin);
  copy_to_viewport(committed);
  if (!visible.empty()) {
    copy_to_viewport(visible);
  }
  return true;
}

bool WorldCanvas::move_to(ViewOrigin origin) {
  if (!valid_) {
    return false;
  }
  const ViewOrigin next = clamp_origin(origin);
  const bool changed = next != origin_;
  origin_ = next;
  return changed;
}

bool WorldCanvas::show(ViewOrigin origin, std::span<std::uint16_t> committed,
                       std::span<std::uint16_t> visible) {
  if (!valid_ || !valid_viewport(committed) || (!visible.empty() && !valid_viewport(visible))) {
    return false;
  }
  const bool changed = move_to(origin);
  copy_to_viewport(committed);
  if (!visible.empty()) {
    copy_to_viewport(visible);
  }
  return changed;
}

bool WorldCanvas::clear(std::span<std::uint16_t> committed, std::span<std::uint16_t> visible) {
  if (!valid_ || !valid_viewport(committed) || (!visible.empty() && !valid_viewport(visible))) {
    return false;
  }
  std::fill_n(storage_.begin(), kRequiredPixels, kBackground);
  origin_ = {kCanvasWidth / 2, kCanvasHeight / 2};
  std::fill_n(committed.begin(), kViewportPixels, kBackground);
  if (!visible.empty()) {
    std::fill_n(visible.begin(), kViewportPixels, kBackground);
  }
  return true;
}

ViewOrigin WorldCanvas::clamp_origin(ViewOrigin origin) {
  return {
      .x = std::clamp(origin.x, 0, kWidth - kCanvasWidth),
      .y = std::clamp(origin.y, 0, kHeight - kCanvasHeight),
  };
}

void WorldCanvas::copy_to_viewport(std::span<std::uint16_t> viewport) const {
  for (int row = 0; row < kCanvasHeight; ++row) {
    const auto source =
        storage_.begin() + static_cast<std::ptrdiff_t>((origin_.y + row) * kWidth + origin_.x);
    const auto destination = viewport.begin() + static_cast<std::ptrdiff_t>(row * kCanvasWidth);
    std::copy_n(source, kCanvasWidth, destination);
  }
}

}  // namespace tinydraw
