#include "tinydraw/graphics/coverage_tile.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace tinydraw {
namespace {

constexpr int kSamplesPerAxis = 4;
constexpr int kSampleCount = kSamplesPerAxis * kSamplesPerAxis;

std::uint8_t sample_coverage(int covered_samples) {
  return static_cast<std::uint8_t>(covered_samples * 255 / kSampleCount);
}

bool point_in_convex(Point sample, std::span<const Point> polygon) {
  float sign = 0.0F;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    const Point start = polygon[index];
    const Point end = polygon[(index + 1U) % polygon.size()];
    const float cross =
        (end.x - start.x) * (sample.y - start.y) - (end.y - start.y) * (sample.x - start.x);
    if (cross == 0.0F) {
      continue;
    }
    if (sign == 0.0F) {
      sign = cross;
    } else if ((cross > 0.0F) != (sign > 0.0F)) {
      return false;
    }
  }
  return true;
}

int blend_channel(int destination, int source, int alpha) {
  return (source * alpha + destination * (255 - alpha) + 127) / 255;
}

}  // namespace

CoverageTile::CoverageTile(int origin_x, int origin_y, int width, int height)
    : origin_x_(origin_x), origin_y_(origin_y), width_(width), height_(height) {
  assert(width > 0 && width <= kTileSize);
  assert(height > 0 && height <= kTileSize);
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
  if (radius <= 0.0F) {
    return;
  }
  const int first_x = std::max(origin_x_, static_cast<int>(std::floor(center.x - radius - 1.0F)));
  const int last_x =
      std::min(origin_x_ + width_ - 1, static_cast<int>(std::ceil(center.x + radius + 1.0F)));
  const int first_y = std::max(origin_y_, static_cast<int>(std::floor(center.y - radius - 1.0F)));
  const int last_y =
      std::min(origin_y_ + height_ - 1, static_cast<int>(std::ceil(center.y + radius + 1.0F)));
  const float radius_squared = radius * radius;

  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      int covered = 0;
      for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
        for (int sample_x = 0; sample_x < kSamplesPerAxis; ++sample_x) {
          const float offset_x =
              (static_cast<float>(sample_x) + 0.5F) / static_cast<float>(kSamplesPerAxis);
          const float offset_y =
              (static_cast<float>(sample_y) + 0.5F) / static_cast<float>(kSamplesPerAxis);
          const float delta_x = static_cast<float>(x) + offset_x - center.x;
          const float delta_y = static_cast<float>(y) + offset_y - center.y;
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
  for (const Point point : polygon) {
    minimum_x = std::min(minimum_x, point.x);
    maximum_x = std::max(maximum_x, point.x);
    minimum_y = std::min(minimum_y, point.y);
    maximum_y = std::max(maximum_y, point.y);
  }
  const int first_x = std::max(origin_x_, static_cast<int>(std::floor(minimum_x)));
  const int last_x = std::min(origin_x_ + width_ - 1, static_cast<int>(std::ceil(maximum_x)));
  const int first_y = std::max(origin_y_, static_cast<int>(std::floor(minimum_y)));
  const int last_y = std::min(origin_y_ + height_ - 1, static_cast<int>(std::ceil(maximum_y)));

  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      int covered = 0;
      for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
        for (int sample_x = 0; sample_x < kSamplesPerAxis; ++sample_x) {
          const Point sample{
              .x = static_cast<float>(x) +
                   (static_cast<float>(sample_x) + 0.5F) / static_cast<float>(kSamplesPerAxis),
              .y = static_cast<float>(y) +
                   (static_cast<float>(sample_y) + 0.5F) / static_cast<float>(kSamplesPerAxis),
          };
          if (point_in_convex(sample, polygon)) {
            ++covered;
          }
        }
      }
      union_coverage(x, y, sample_coverage(covered));
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
