#include "tinydraw/graphics/coverage_tile.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace tinydraw {
namespace {

constexpr int kSamplesPerAxis = 4;
constexpr int kSampleCount = kSamplesPerAxis * kSamplesPerAxis;
constexpr float kMaximumCoordinateMagnitude = 1'000'000.0F;

bool safe_coordinate(float value) {
  return std::isfinite(value) && std::abs(value) <= kMaximumCoordinateMagnitude;
}

std::uint8_t sample_coverage(int covered_samples) {
  return static_cast<std::uint8_t>(covered_samples * 255 / kSampleCount);
}

float sample_offset(int index) {
  return (static_cast<float>(index) + 0.5F) / static_cast<float>(kSamplesPerAxis);
}

int blend_channel(int destination, int source, int alpha) {
  return (source * alpha + destination * (255 - alpha) + 127) / 255;
}

void include_strip_x(float x, float& minimum_x, float& maximum_x, bool& found) {
  minimum_x = found ? std::min(minimum_x, x) : x;
  maximum_x = found ? std::max(maximum_x, x) : x;
  found = true;
}

}  // namespace

CoverageTile::CoverageTile(int origin_x, int origin_y, int width, int height) {
  reset(origin_x, origin_y, width, height);
}

void CoverageTile::reset(int origin_x, int origin_y, int width, int height) {
  assert(width > 0 && width <= kTileSize);
  assert(height > 0 && height <= kTileSize);
  origin_x_ = origin_x;
  origin_y_ = origin_y;
  width_ = width;
  height_ = height;
  clear();
}

void CoverageTile::clear() { coverage_.fill(0U); }

void CoverageTile::union_coverage(int x, int y, std::uint8_t coverage) {
  if (!contains(x, y)) {
    return;
  }
  auto& current = coverage_[index_of(x, y)];
  current = std::max(current, coverage);
}

std::uint8_t CoverageTile::coverage_at(int x, int y) const {
  return contains(x, y) ? coverage_[index_of(x, y)] : 0U;
}

void CoverageTile::rasterize_circle(Point center, float radius) {
  if (!safe_coordinate(center.x) || !safe_coordinate(center.y) || !safe_coordinate(radius) ||
      radius <= 0.0F) {
    return;
  }
  const float minimum_x = center.x - radius - 1.0F;
  const float maximum_x = center.x + radius + 1.0F;
  const float minimum_y = center.y - radius - 1.0F;
  const float maximum_y = center.y + radius + 1.0F;
  if (maximum_x < static_cast<float>(origin_x_) ||
      minimum_x > static_cast<float>(origin_x_ + width_ - 1) ||
      maximum_y < static_cast<float>(origin_y_) ||
      minimum_y > static_cast<float>(origin_y_ + height_ - 1)) {
    return;
  }
  const int first_x = static_cast<int>(std::floor(std::clamp(
      minimum_x, static_cast<float>(origin_x_), static_cast<float>(origin_x_ + width_ - 1))));
  const int last_x = static_cast<int>(std::ceil(std::clamp(
      maximum_x, static_cast<float>(origin_x_), static_cast<float>(origin_x_ + width_ - 1))));
  const int first_y = static_cast<int>(std::floor(std::clamp(
      minimum_y, static_cast<float>(origin_y_), static_cast<float>(origin_y_ + height_ - 1))));
  const int last_y = static_cast<int>(std::ceil(std::clamp(
      maximum_y, static_cast<float>(origin_y_), static_cast<float>(origin_y_ + height_ - 1))));
  const float radius_squared = radius * radius;

  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      const float sample_left = static_cast<float>(x) + sample_offset(0);
      const float sample_right = static_cast<float>(x) + sample_offset(kSamplesPerAxis - 1);
      const float sample_top = static_cast<float>(y) + sample_offset(0);
      const float sample_bottom = static_cast<float>(y) + sample_offset(kSamplesPerAxis - 1);
      const float nearest_x = std::clamp(center.x, sample_left, sample_right) - center.x;
      const float nearest_y = std::clamp(center.y, sample_top, sample_bottom) - center.y;
      if (nearest_x * nearest_x + nearest_y * nearest_y > radius_squared) {
        continue;
      }
      const float farthest_x =
          std::max(std::abs(sample_left - center.x), std::abs(sample_right - center.x));
      const float farthest_y =
          std::max(std::abs(sample_top - center.y), std::abs(sample_bottom - center.y));
      if (farthest_x * farthest_x + farthest_y * farthest_y <= radius_squared) {
        union_coverage(x, y, 255U);
        continue;
      }
      int covered = 0;
      for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
        for (int sample_x = 0; sample_x < kSamplesPerAxis; ++sample_x) {
          const float delta_x = static_cast<float>(x) + sample_offset(sample_x) - center.x;
          const float delta_y = static_cast<float>(y) + sample_offset(sample_y) - center.y;
          if (delta_x * delta_x + delta_y * delta_y <= radius_squared) {
            ++covered;
          }
        }
      }
      union_coverage(x, y, sample_coverage(covered));
    }
  }
}

void CoverageTile::rasterize_convex(std::span<const Point> polygon) {
  if (polygon.size() < 3U) {
    return;
  }
  float minimum_x = polygon.front().x;
  float maximum_x = minimum_x;
  float minimum_y = polygon.front().y;
  float maximum_y = minimum_y;
  float twice_area = 0.0F;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    const Point point = polygon[index];
    if (!safe_coordinate(point.x) || !safe_coordinate(point.y)) {
      return;
    }
    minimum_x = std::min(minimum_x, point.x);
    maximum_x = std::max(maximum_x, point.x);
    minimum_y = std::min(minimum_y, point.y);
    maximum_y = std::max(maximum_y, point.y);
    const Point next = polygon[(index + 1U) % polygon.size()];
    twice_area += point.x * next.y - next.x * point.y;
  }
  if (twice_area == 0.0F) {
    return;
  }
  if (maximum_x < static_cast<float>(origin_x_) ||
      minimum_x > static_cast<float>(origin_x_ + width_ - 1) ||
      maximum_y < static_cast<float>(origin_y_) ||
      minimum_y > static_cast<float>(origin_y_ + height_ - 1)) {
    return;
  }
  const int first_x = static_cast<int>(std::floor(std::clamp(
      minimum_x, static_cast<float>(origin_x_), static_cast<float>(origin_x_ + width_ - 1))));
  const int last_x = static_cast<int>(std::ceil(std::clamp(
      maximum_x, static_cast<float>(origin_x_), static_cast<float>(origin_x_ + width_ - 1))));
  const int first_y = static_cast<int>(std::floor(std::clamp(
      minimum_y, static_cast<float>(origin_y_), static_cast<float>(origin_y_ + height_ - 1))));
  const int last_y = static_cast<int>(std::ceil(std::clamp(
      maximum_y, static_cast<float>(origin_y_), static_cast<float>(origin_y_ + height_ - 1))));

  for (int y = first_y; y <= last_y; ++y) {
    std::array<std::uint8_t, kTileSize> covered{};
    for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
      const float scan_y = static_cast<float>(y) + sample_offset(sample_y);
      float scan_minimum_x = 0.0F;
      float scan_maximum_x = 0.0F;
      bool found = false;
      for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point start = polygon[index];
        const Point end = polygon[(index + 1U) % polygon.size()];
        const float minimum_edge_y = std::min(start.y, end.y);
        const float maximum_edge_y = std::max(start.y, end.y);
        if (start.y == end.y || scan_y < minimum_edge_y || scan_y > maximum_edge_y) {
          continue;
        }
        const float amount = (scan_y - start.y) / (end.y - start.y);
        include_strip_x(start.x + amount * (end.x - start.x), scan_minimum_x, scan_maximum_x,
                        found);
      }
      if (!found) {
        continue;
      }
      const int scan_first_x = std::max(first_x, static_cast<int>(std::floor(scan_minimum_x)) - 1);
      const int scan_last_x = std::min(last_x, static_cast<int>(std::ceil(scan_maximum_x)) + 1);
      for (int x = scan_first_x; x <= scan_last_x; ++x) {
        for (int sample_x = 0; sample_x < kSamplesPerAxis; ++sample_x) {
          const float scan_x = static_cast<float>(x) + sample_offset(sample_x);
          if (scan_x >= scan_minimum_x && scan_x <= scan_maximum_x) {
            ++covered[static_cast<std::size_t>(x - origin_x_)];
          }
        }
      }
    }
    for (int x = first_x; x <= last_x; ++x) {
      union_coverage(x, y, sample_coverage(covered[static_cast<std::size_t>(x - origin_x_)]));
    }
  }
}

bool CoverageTile::contains(int x, int y) const {
  return x >= origin_x_ && x < origin_x_ + width_ && y >= origin_y_ && y < origin_y_ + height_;
}

std::size_t CoverageTile::index_of(int x, int y) const {
  const int local_x = x - origin_x_;
  const int local_y = y - origin_y_;
  return static_cast<std::size_t>(local_y * kTileSize + local_x);
}

void composite_rgb565(const CoverageTile& coverage, std::uint16_t source,
                      std::span<std::uint16_t> destination) {
  assert(destination.size() >= static_cast<std::size_t>(coverage.width() * coverage.height()));

  const int source_red = (source >> 11U) & 0x1FU;
  const int source_green = (source >> 5U) & 0x3FU;
  const int source_blue = source & 0x1FU;
  for (int y = 0; y < coverage.height(); ++y) {
    for (int x = 0; x < coverage.width(); ++x) {
      const int alpha = coverage.coverage_at(coverage.origin_x() + x, coverage.origin_y() + y);
      if (alpha == 0) {
        continue;
      }
      const std::size_t index = static_cast<std::size_t>(y * coverage.width() + x);
      const std::uint16_t current = destination[index];
      const int red = blend_channel((current >> 11U) & 0x1FU, source_red, alpha);
      const int green = blend_channel((current >> 5U) & 0x3FU, source_green, alpha);
      const int blue = blend_channel(current & 0x1FU, source_blue, alpha);
      destination[index] = static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
    }
  }
}

}  // namespace tinydraw
