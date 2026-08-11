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
  if (!valid_ || !valid_viewport(viewport)) {
    return false;
  }
  for (int row = 0; row < kCanvasHeight; ++row) {
    const auto source = viewport.begin() + static_cast<std::ptrdiff_t>(row * kCanvasWidth);
    const auto destination =
        storage_.begin() + static_cast<std::ptrdiff_t>((origin_.y + row) * kWidth + origin_.x);
    std::copy_n(source, kCanvasWidth, destination);
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
