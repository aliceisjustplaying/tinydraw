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
  std::fill_n(coverage_.begin(), static_cast<std::size_t>(width_ * height_), 0U);
  dirty_left_ = 0;
  dirty_top_ = 0;
  dirty_right_ = -1;
  dirty_bottom_ = -1;
}

void CoverageTile::clear() {
  for (int y = dirty_top_; y <= dirty_bottom_; ++y) {
    std::fill_n(row(y) + dirty_left_, static_cast<std::size_t>(dirty_right_ - dirty_left_ + 1), 0U);
  }
  dirty_left_ = 0;
  dirty_top_ = 0;
  dirty_right_ = -1;
  dirty_bottom_ = -1;
}

std::uint8_t* CoverageTile::row(int y) {
  assert(y >= 0 && y < height_);
  return coverage_.data() + static_cast<std::ptrdiff_t>(y * width_);
}

const std::uint8_t* CoverageTile::row(int y) const {
  assert(y >= 0 && y < height_);
  return coverage_.data() + static_cast<std::ptrdiff_t>(y * width_);
}

void CoverageTile::mark_dirty(int left, int top, int right, int bottom) {
  left = std::max(left, 0);
  top = std::max(top, 0);
  right = std::min(right, width_ - 1);
  bottom = std::min(bottom, height_ - 1);
  if (left > right || top > bottom) {
    return;
  }
  if (dirty_right_ < dirty_left_) {
    dirty_left_ = left;
    dirty_top_ = top;
    dirty_right_ = right;
    dirty_bottom_ = bottom;
  } else {
    dirty_left_ = std::min(dirty_left_, left);
    dirty_top_ = std::min(dirty_top_, top);
    dirty_right_ = std::max(dirty_right_, right);
    dirty_bottom_ = std::max(dirty_bottom_, bottom);
  }
}

void CoverageTile::union_coverage(int x, int y, std::uint8_t coverage) {
  if (coverage == 0U || !contains(x, y)) {
    return;
  }
  const int local_x = x - origin_x_;
  const int local_y = y - origin_y_;
  if (dirty_right_ < dirty_left_) {
    dirty_left_ = local_x;
    dirty_right_ = local_x;
    dirty_top_ = local_y;
    dirty_bottom_ = local_y;
  } else {
    dirty_left_ = std::min(dirty_left_, local_x);
    dirty_right_ = std::max(dirty_right_, local_x);
    dirty_top_ = std::min(dirty_top_, local_y);
    dirty_bottom_ = std::max(dirty_bottom_, local_y);
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
    const float sample_top = static_cast<float>(y) + sample_offset(0);
    const float sample_bottom = static_cast<float>(y) + sample_offset(kSamplesPerAxis - 1);
    std::array<float, kSamplesPerAxis> delta_y_squared;
    for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
      const float delta_y = static_cast<float>(y) + sample_offset(sample_y) - center.y;
      delta_y_squared[static_cast<std::size_t>(sample_y)] = delta_y * delta_y;
    }
    for (int x = first_x; x <= last_x; ++x) {
      const float sample_left = static_cast<float>(x) + sample_offset(0);
      const float sample_right = static_cast<float>(x) + sample_offset(kSamplesPerAxis - 1);
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
      std::array<float, kSamplesPerAxis> delta_x_squared;
      for (int sample_x = 0; sample_x < kSamplesPerAxis; ++sample_x) {
        const float delta_x = static_cast<float>(x) + sample_offset(sample_x) - center.x;
        delta_x_squared[static_cast<std::size_t>(sample_x)] = delta_x * delta_x;
      }
      int covered = 0;
      for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
        for (int sample_x = 0; sample_x < kSamplesPerAxis; ++sample_x) {
          if (delta_x_squared[static_cast<std::size_t>(sample_x)] +
                  delta_y_squared[static_cast<std::size_t>(sample_y)] <=
              radius_squared) {
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

  // The S3 FPU has no divide instruction, so hoist one reciprocal per edge
  // out of the scanline loop instead of dividing on every sample row.
  struct ScanEdge {
    Point start;
    Point end;
    float minimum_y;
    float maximum_y;
    float inverse_delta_y;
  };
  std::array<ScanEdge, 4> edges;
  std::size_t edge_count = 0;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    const Point start = polygon[index];
    const Point end = polygon[(index + 1U) % polygon.size()];
    if (start.y == end.y) {
      continue;
    }
    edges[edge_count++] = {
        .start = start,
        .end = end,
        .minimum_y = std::min(start.y, end.y),
        .maximum_y = std::max(start.y, end.y),
        .inverse_delta_y = 1.0F / (end.y - start.y),
    };
  }

  for (int y = first_y; y <= last_y; ++y) {
    std::array<std::uint8_t, kTileSize> covered{};
    for (int sample_y = 0; sample_y < kSamplesPerAxis; ++sample_y) {
      const float scan_y = static_cast<float>(y) + sample_offset(sample_y);
      float scan_minimum_x = 0.0F;
      float scan_maximum_x = 0.0F;
      bool found = false;
      for (std::size_t index = 0; index < edge_count; ++index) {
        const ScanEdge& edge = edges[index];
        if (scan_y < edge.minimum_y || scan_y > edge.maximum_y) {
          continue;
        }
        const float amount = (scan_y - edge.start.y) * edge.inverse_delta_y;
        include_strip_x(edge.start.x + amount * (edge.end.x - edge.start.x), scan_minimum_x,
                        scan_maximum_x, found);
      }
      if (!found) {
        continue;
      }
      const int scan_first_x = std::max(first_x, static_cast<int>(std::floor(scan_minimum_x)) - 1);
      const int scan_last_x = std::min(last_x, static_cast<int>(std::ceil(scan_maximum_x)) + 1);
      for (int x = scan_first_x; x <= scan_last_x; ++x) {
        // Sample columns ascend, so testing the outermost two columns decides
        // fully-inside and fully-outside pixels without per-column tests.
        const float first_sample = static_cast<float>(x) + sample_offset(0);
        const float last_sample = static_cast<float>(x) + sample_offset(kSamplesPerAxis - 1);
        if (first_sample >= scan_minimum_x && last_sample <= scan_maximum_x) {
          covered[static_cast<std::size_t>(x - origin_x_)] += kSamplesPerAxis;
          continue;
        }
        if (last_sample < scan_minimum_x || first_sample > scan_maximum_x) {
          continue;
        }
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
  return static_cast<std::size_t>(local_y * width_ + local_x);
}

void composite_rgb565(const CoverageTile& coverage, std::uint16_t source,
                      std::span<std::uint16_t> destination) {
  assert(destination.size() >= static_cast<std::size_t>(coverage.width() * coverage.height()));

  const int source_red = (source >> 11U) & 0x1FU;
  const int source_green = (source >> 5U) & 0x3FU;
  const int source_blue = source & 0x1FU;
  for (int y = coverage.dirty_top(); y <= coverage.dirty_bottom(); ++y) {
    const std::uint8_t* coverage_row = coverage.row(y);
    for (int x = coverage.dirty_left(); x <= coverage.dirty_right(); ++x) {
      const int alpha = coverage_row[x];
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
