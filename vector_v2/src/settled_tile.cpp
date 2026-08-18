#include "tinydraw/vector_v2/settled_tile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace tinydraw::vector_v2 {
namespace {

bool prefer_spatial_candidates(std::size_t candidate_count, std::size_t authority_count) {
  return candidate_count <= authority_count - authority_count / 4U;
}

bool rects_intersect(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

struct Channels {
  std::uint16_t red = 0;
  std::uint16_t green = 0;
  std::uint16_t blue = 0;
};

Channels expand_565(std::uint16_t rgb565) {
  // Bit replication, matching the frozen blend model.
  const auto r5 = static_cast<std::uint16_t>((rgb565 >> 11U) & 0x1FU);
  const auto g6 = static_cast<std::uint16_t>((rgb565 >> 5U) & 0x3FU);
  const auto b5 = static_cast<std::uint16_t>(rgb565 & 0x1FU);
  return {static_cast<std::uint16_t>((r5 << 3U) | (r5 >> 2U)),
          static_cast<std::uint16_t>((g6 << 2U) | (g6 >> 4U)),
          static_cast<std::uint16_t>((b5 << 3U) | (b5 >> 2U))};
}

template <typename T>
bool same_span(std::span<T> left, std::span<T> right) {
  return left.data() == right.data() && left.size() == right.size();
}

bool same_workspace(const SettledTileWorkspace& left, const SettledTileWorkspace& right) {
  return same_span(left.operation_alpha, right.operation_alpha) &&
         same_span(left.accumulated_alpha, right.accumulated_alpha) &&
         same_span(left.red, right.red) && same_span(left.green, right.green) &&
         same_span(left.blue, right.blue) &&
         same_span(left.candidate_indices, right.candidate_indices);
}

void add_stats(SettledTileStats& destination, const SettledTileStats& source) {
  destination.operations_scanned += source.operations_scanned;
  destination.operations_in_authority += source.operations_in_authority;
  destination.index_candidates += source.index_candidates;
  destination.deduplicated_candidates += source.deduplicated_candidates;
  destination.operations_intersecting += source.operations_intersecting;
  destination.operations_rendered += source.operations_rendered;
  destination.candidate_queries += source.candidate_queries;
  destination.initialize_pixels += source.initialize_pixels;
  destination.operation_clear_pixels += source.operation_clear_pixels;
  destination.curve_units_prepared += source.curve_units_prepared;
  destination.raster_pixels += source.raster_pixels;
  destination.composite_pixels += source.composite_pixels;
  destination.fold_pixels += source.fold_pixels;
  destination.saturated_early = destination.saturated_early || source.saturated_early;
}

}  // namespace

void SettledRenderCursor::cancel() { *this = SettledRenderCursor{}; }

bool SettledRenderCursor::active() const { return phase_ != Phase::kIdle; }

const SettledTileStats& SettledRenderCursor::stats() const { return stats_; }

SettledRenderSlice render_settled_window_slice(const OperationLog& log, ZoomLevel zoom,
                                               PixelRect window_bounds,
                                               const SettledTileWorkspace& workspace,
                                               std::span<std::uint16_t> out_pixels,
                                               SettledRenderCursor& cursor,
                                               std::size_t max_work_px) {
  const int width = window_bounds.x1 - window_bounds.x0;
  const int height = window_bounds.y1 - window_bounds.y0;
  const std::size_t pixel_count =
      width > 0 && height > 0 ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                              : 0U;
  const bool request_valid =
      log.ready() && width > 0 && height > 0 && width <= static_cast<int>(kTileWidth) &&
      height <= static_cast<int>(kTileHeight) && out_pixels.size() >= pixel_count &&
      workspace.operation_alpha.size() >= pixel_count &&
      workspace.accumulated_alpha.size() >= pixel_count && workspace.red.size() >= pixel_count &&
      workspace.green.size() >= pixel_count && workspace.blue.size() >= pixel_count &&
      max_work_px != 0U;
  if (!request_valid) {
    cursor.cancel();
    return {.status = SettledRenderStatus::kError};
  }

  if (cursor.active()) {
    const bool same_request = cursor.log_ == &log && cursor.authority_ == log.read_view() &&
                              cursor.zoom_ == zoom && cursor.window_bounds_ == window_bounds &&
                              same_workspace(cursor.workspace_, workspace) &&
                              same_span(cursor.out_pixels_, out_pixels);
    if (!same_request) {
      cursor.cancel();
      return {.status = SettledRenderStatus::kError};
    }
  } else {
    cursor.cancel();
    const int percent = zoom_percent(zoom);
    cursor.log_ = &log;
    cursor.authority_ = log.read_view();
    cursor.zoom_ = zoom;
    cursor.window_bounds_ = window_bounds;
    cursor.world_bounds_ = {window_bounds.x0 * 100 / percent - 1,
                            window_bounds.y0 * 100 / percent - 1,
                            (window_bounds.x1 * 100 + percent - 1) / percent + 1,
                            (window_bounds.y1 * 100 + percent - 1) / percent + 1};
    cursor.workspace_ = workspace;
    cursor.out_pixels_ = out_pixels;
    cursor.stats_ = {};
    cursor.width_ = width;
    cursor.height_ = height;
    cursor.pixel_count_ = pixel_count;
    cursor.operation_min_x_.fill(static_cast<std::uint8_t>(width));
    cursor.operation_max_x_.fill(0U);
    cursor.operation_min_y_ = static_cast<std::uint8_t>(height);
    cursor.operation_max_y_ = 0U;
    cursor.phase_ = SettledRenderCursor::Phase::kInitialize;
  }

  std::size_t work = 0;
  const auto room = [&]() { return max_work_px - std::min(max_work_px, work); };
  const auto stop_before = [&](std::size_t charge) { return work != 0U && charge > room(); };
  while (true) {
    switch (cursor.phase_) {
      case SettledRenderCursor::Phase::kIdle:
        return {.status = SettledRenderStatus::kError, .work_px = work};

      case SettledRenderCursor::Phase::kInitialize: {
        const std::size_t count = std::min(room(), cursor.pixel_count_ - cursor.initialize_at_);
        std::memset(workspace.accumulated_alpha.data() + cursor.initialize_at_, 0, count);
        std::memset(workspace.red.data() + cursor.initialize_at_, 0, count * sizeof(std::uint16_t));
        std::memset(workspace.green.data() + cursor.initialize_at_, 0,
                    count * sizeof(std::uint16_t));
        std::memset(workspace.blue.data() + cursor.initialize_at_, 0,
                    count * sizeof(std::uint16_t));
        std::memset(workspace.operation_alpha.data() + cursor.initialize_at_, 0, count);
        cursor.initialize_at_ += count;
        cursor.stats_.initialize_pixels += count;
        work += count;
        if (cursor.initialize_at_ != cursor.pixel_count_) {
          return {.status = SettledRenderStatus::kInProgress, .work_px = work};
        }
        cursor.phase_ = SettledRenderCursor::Phase::kQueryCandidates;
        break;
      }

      case SettledRenderCursor::Phase::kQueryCandidates: {
        if (stop_before(1U)) {
          return {.status = SettledRenderStatus::kInProgress, .work_px = work};
        }
        OperationSpatialQueryStats spatial_stats{};
        const auto candidates =
            log.query_spatial(cursor.world_bounds_, 0U, cursor.authority_.active_operation_count,
                              workspace.candidate_indices, &spatial_stats);
        ++cursor.stats_.candidate_queries;
        cursor.stats_.operations_in_authority += cursor.authority_.active_operation_count;
        if (candidates.has_value()) {
          cursor.stats_.index_candidates += spatial_stats.index_candidates;
          cursor.stats_.deduplicated_candidates += spatial_stats.deduplicated_candidates;
        }
        cursor.use_candidates_ =
            candidates.has_value() &&
            prefer_spatial_candidates(*candidates, cursor.authority_.active_operation_count);
        cursor.replay_count_ =
            cursor.use_candidates_ ? *candidates : cursor.authority_.active_operation_count;
        cursor.phase_ = SettledRenderCursor::Phase::kScanOperation;
        ++work;
        break;
      }

      case SettledRenderCursor::Phase::kScanOperation: {
        if (cursor.replay_index_ == cursor.replay_count_) {
          cursor.phase_ = SettledRenderCursor::Phase::kFinalFold;
          break;
        }
        if (stop_before(1U)) {
          return {.status = SettledRenderStatus::kInProgress, .work_px = work};
        }
        cursor.operation_index_ =
            cursor.use_candidates_
                ? workspace.candidate_indices[cursor.replay_index_]
                : cursor.authority_.active_operation_count - 1U - cursor.replay_index_;
        const auto stored = log.operation(cursor.operation_index_);
        if (!stored.has_value()) {
          cursor.cancel();
          return {.status = SettledRenderStatus::kError, .work_px = work};
        }
        ++cursor.stats_.operations_scanned;
        ++work;
        if (!rects_intersect(stored->world_bounds, cursor.world_bounds_)) {
          ++cursor.replay_index_;
          break;
        }
        ++cursor.stats_.operations_intersecting;
        cursor.operation_tool_ = stored->tool;
        cursor.operation_color_ = stored->color;
        cursor.operation_samples_ = stored->samples;
        cursor.operation_touched_ = false;
        cursor.clear_row_ = cursor.operation_min_y_;
        cursor.endpoint_ = 1U;
        cursor.phase_ = SettledRenderCursor::Phase::kClearOperation;
        break;
      }

      case SettledRenderCursor::Phase::kClearOperation: {
        while (cursor.clear_row_ < cursor.operation_max_y_) {
          const std::size_t row = cursor.clear_row_;
          const std::size_t begin = cursor.operation_min_x_[row];
          const std::size_t end = cursor.operation_max_x_[row];
          const std::size_t count = end > begin ? end - begin : 0U;
          if (stop_before(count)) {
            return {.status = SettledRenderStatus::kInProgress, .work_px = work};
          }
          if (count != 0U) {
            std::memset(workspace.operation_alpha.data() +
                            row * static_cast<std::size_t>(cursor.width_) + begin,
                        0, count);
          }
          cursor.operation_min_x_[row] = static_cast<std::uint8_t>(cursor.width_);
          cursor.operation_max_x_[row] = 0U;
          ++cursor.clear_row_;
          cursor.stats_.operation_clear_pixels += count;
          work += count;
        }
        cursor.operation_min_y_ = static_cast<std::uint8_t>(cursor.height_);
        cursor.operation_max_y_ = 0U;
        cursor.phase_ = SettledRenderCursor::Phase::kPrepareEndpoint;
        break;
      }

      case SettledRenderCursor::Phase::kPrepareEndpoint: {
        if (cursor.endpoint_ == cursor.operation_samples_.size()) {
          if (!cursor.operation_touched_) {
            ++cursor.replay_index_;
            cursor.phase_ = SettledRenderCursor::Phase::kScanOperation;
          } else {
            ++cursor.stats_.operations_rendered;
            cursor.composite_row_ = cursor.operation_min_y_;
            cursor.composite_x_ = cursor.operation_min_x_[cursor.composite_row_];
            cursor.phase_ = SettledRenderCursor::Phase::kCompositeOperation;
          }
          break;
        }
        if (stop_before(1U)) {
          return {.status = SettledRenderStatus::kInProgress, .work_px = work};
        }
        const auto unit = prepare_incremental_curve_unit(cursor.operation_samples_,
                                                         cursor.endpoint_, cursor.zoom_);
        ++work;
        if (!unit.has_value()) {
          ++cursor.endpoint_;
          break;
        }
        cursor.prepared_unit_ = *unit;
        ++cursor.stats_.curve_units_prepared;
        cursor.step_ = 0U;
        cursor.chord_next_y_ = 0;
        cursor.chord_y1_ = 0;
        cursor.phase_ = SettledRenderCursor::Phase::kRasterChord;
        break;
      }

      case SettledRenderCursor::Phase::kRasterChord: {
        if (cursor.step_ == cursor.prepared_unit_.step_count) {
          ++cursor.endpoint_;
          cursor.phase_ = SettledRenderCursor::Phase::kPrepareEndpoint;
          break;
        }
        if (cursor.chord_next_y_ >= cursor.chord_y1_) {
          const auto& chord = cursor.prepared_unit_.steps[cursor.step_];
          const float ax = chord.first_x - static_cast<float>(cursor.window_bounds_.x0);
          const float ay = chord.first_y - static_cast<float>(cursor.window_bounds_.y0);
          const float bx = chord.second_x - static_cast<float>(cursor.window_bounds_.x0);
          const float by = chord.second_y - static_cast<float>(cursor.window_bounds_.y0);
          const float radius_max = std::max(chord.first_radius, chord.second_radius);
          cursor.chord_x0_ =
              std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - radius_max - 1.5F)));
          cursor.chord_next_y_ =
              std::max(0, static_cast<int>(std::floor(std::min(ay, by) - radius_max - 1.5F)));
          cursor.chord_x1_ = std::min(
              cursor.width_, static_cast<int>(std::ceil(std::max(ax, bx) + radius_max + 1.5F)));
          cursor.chord_y1_ = std::min(
              cursor.height_, static_cast<int>(std::ceil(std::max(ay, by) + radius_max + 1.5F)));
          cursor.chord_ax_ = ax;
          cursor.chord_ay_ = ay;
          cursor.chord_delta_x_ = bx - ax;
          cursor.chord_delta_y_ = by - ay;
          const float length_squared = cursor.chord_delta_x_ * cursor.chord_delta_x_ +
                                       cursor.chord_delta_y_ * cursor.chord_delta_y_;
          cursor.chord_inverse_length_squared_ =
              length_squared > 0.0F ? 1.0F / length_squared : 0.0F;
          cursor.chord_first_radius_ = chord.first_radius;
          cursor.chord_radius_delta_ = chord.second_radius - chord.first_radius;
          if (cursor.chord_x0_ >= cursor.chord_x1_ || cursor.chord_next_y_ >= cursor.chord_y1_) {
            ++cursor.step_;
            cursor.chord_next_y_ = 0;
            cursor.chord_y1_ = 0;
            break;
          }
        }

        const std::size_t row_work = static_cast<std::size_t>(cursor.chord_x1_ - cursor.chord_x0_);
        if (stop_before(row_work)) {
          return {.status = SettledRenderStatus::kInProgress, .work_px = work};
        }
        const int y = cursor.chord_next_y_;
        const float sample_y = static_cast<float>(y) + 0.5F;
        std::uint8_t* row = workspace.operation_alpha.data() +
                            static_cast<std::size_t>(y) * static_cast<std::size_t>(cursor.width_);
        for (int x = cursor.chord_x0_; x < cursor.chord_x1_; ++x) {
          const float sample_x = static_cast<float>(x) + 0.5F;
          const float ap_x = sample_x - cursor.chord_ax_;
          const float ap_y = sample_y - cursor.chord_ay_;
          const float t = std::clamp((ap_x * cursor.chord_delta_x_ + ap_y * cursor.chord_delta_y_) *
                                         cursor.chord_inverse_length_squared_,
                                     0.0F, 1.0F);
          const float dx = ap_x - t * cursor.chord_delta_x_;
          const float dy = ap_y - t * cursor.chord_delta_y_;
          const float distance_squared = dx * dx + dy * dy;
          const float radius = cursor.chord_first_radius_ + cursor.chord_radius_delta_ * t;
          const float interior = radius - 0.5F;
          const float exterior = radius + 0.5F;
          if (distance_squared >= exterior * exterior) {
            continue;
          }
          std::uint8_t alpha_255 = 255U;
          if (interior <= 0.0F || distance_squared > interior * interior) {
            const float distance = std::sqrt(distance_squared);
            const float alpha = std::clamp(0.5F + (radius - distance), 0.0F, 1.0F);
            if (alpha <= 0.0F) {
              continue;
            }
            alpha_255 = static_cast<std::uint8_t>(alpha * 255.0F + 0.5F);
          }
          if (alpha_255 > row[x]) {
            row[x] = alpha_255;
            cursor.operation_touched_ = true;
            const std::size_t row_index = static_cast<std::size_t>(y);
            cursor.operation_min_x_[row_index] =
                std::min(cursor.operation_min_x_[row_index], static_cast<std::uint8_t>(x));
            cursor.operation_max_x_[row_index] =
                std::max(cursor.operation_max_x_[row_index], static_cast<std::uint8_t>(x + 1));
            cursor.operation_min_y_ =
                std::min(cursor.operation_min_y_, static_cast<std::uint8_t>(y));
            cursor.operation_max_y_ =
                std::max(cursor.operation_max_y_, static_cast<std::uint8_t>(y + 1));
          }
        }
        work += row_work;
        cursor.stats_.raster_pixels += row_work;
        ++cursor.chord_next_y_;
        if (cursor.chord_next_y_ == cursor.chord_y1_) {
          ++cursor.step_;
          cursor.chord_next_y_ = 0;
          cursor.chord_y1_ = 0;
        }
        break;
      }

      case SettledRenderCursor::Phase::kCompositeOperation: {
        const Channels color =
            expand_565(cursor.operation_tool_ == OperationTool::kEraser ? std::uint16_t{0xFFFFU}
                                                                        : cursor.operation_color_);
        while (cursor.composite_row_ < cursor.operation_max_y_) {
          const std::size_t row = cursor.composite_row_;
          const std::size_t end = cursor.operation_max_x_[row];
          if (cursor.composite_x_ >= end) {
            ++cursor.composite_row_;
            if (cursor.composite_row_ < cursor.operation_max_y_) {
              cursor.composite_x_ = cursor.operation_min_x_[cursor.composite_row_];
            }
            continue;
          }
          const std::size_t count = std::min(room(), end - cursor.composite_x_);
          if (count == 0U) {
            return {.status = SettledRenderStatus::kInProgress, .work_px = work};
          }
          const std::size_t first_at =
              row * static_cast<std::size_t>(cursor.width_) + cursor.composite_x_;
          for (std::size_t offset = 0; offset < count; ++offset) {
            const std::size_t at = first_at + offset;
            const std::uint8_t alpha = workspace.operation_alpha[at];
            if (alpha == 0U) {
              continue;
            }
            const std::uint8_t accumulated = workspace.accumulated_alpha[at];
            const auto contribution = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(alpha) * (255U - accumulated) + 127U) / 255U);
            if (contribution == 0U) {
              continue;
            }
            workspace.red[at] =
                static_cast<std::uint16_t>(workspace.red[at] + color.red * contribution / 255U);
            workspace.green[at] =
                static_cast<std::uint16_t>(workspace.green[at] + color.green * contribution / 255U);
            workspace.blue[at] =
                static_cast<std::uint16_t>(workspace.blue[at] + color.blue * contribution / 255U);
            const auto next_accumulated = static_cast<std::uint8_t>(
                std::min<std::uint32_t>(255U, accumulated + contribution));
            cursor.saturated_pixels_ += next_accumulated == 255U && accumulated != 255U ? 1U : 0U;
            workspace.accumulated_alpha[at] = next_accumulated;
          }
          cursor.composite_x_ += count;
          cursor.stats_.composite_pixels += count;
          work += count;
          if (work >= max_work_px) {
            return {.status = SettledRenderStatus::kInProgress, .work_px = work};
          }
        }
        if (cursor.saturated_pixels_ == cursor.pixel_count_) {
          cursor.stats_.saturated_early = true;
          cursor.phase_ = SettledRenderCursor::Phase::kFinalFold;
        } else {
          ++cursor.replay_index_;
          cursor.phase_ = SettledRenderCursor::Phase::kScanOperation;
        }
        break;
      }

      case SettledRenderCursor::Phase::kFinalFold: {
        const std::size_t count = std::min(room(), cursor.pixel_count_ - cursor.fold_at_);
        for (std::size_t offset = 0; offset < count; ++offset) {
          const std::size_t at = cursor.fold_at_ + offset;
          const std::uint32_t remaining = 255U - workspace.accumulated_alpha[at];
          const std::uint32_t r8 = std::min<std::uint32_t>(255U, workspace.red[at] + remaining);
          const std::uint32_t g8 = std::min<std::uint32_t>(255U, workspace.green[at] + remaining);
          const std::uint32_t b8 = std::min<std::uint32_t>(255U, workspace.blue[at] + remaining);
          out_pixels[at] =
              static_cast<std::uint16_t>(((r8 >> 3U) << 11U) | ((g8 >> 2U) << 5U) | (b8 >> 3U));
        }
        cursor.fold_at_ += count;
        cursor.stats_.fold_pixels += count;
        work += count;
        if (cursor.fold_at_ != cursor.pixel_count_) {
          return {.status = SettledRenderStatus::kInProgress, .work_px = work};
        }
        cursor.phase_ = SettledRenderCursor::Phase::kIdle;
        return {.status = SettledRenderStatus::kComplete, .work_px = work};
      }
    }

    if (work >= max_work_px) {
      return {.status = SettledRenderStatus::kInProgress, .work_px = work};
    }
  }
}

bool render_settled_window(const OperationLog& log, ZoomLevel zoom, PixelRect window_bounds,
                           const SettledTileWorkspace& workspace,
                           std::span<std::uint16_t> out_pixels, SettledTileStats* stats) {
  SettledRenderCursor cursor;
  while (true) {
    const SettledRenderSlice slice =
        render_settled_window_slice(log, zoom, window_bounds, workspace, out_pixels, cursor,
                                    std::numeric_limits<std::size_t>::max());
    if (slice.status == SettledRenderStatus::kError) {
      return false;
    }
    if (slice.status == SettledRenderStatus::kComplete) {
      if (stats != nullptr) {
        add_stats(*stats, cursor.stats());
      }
      return true;
    }
  }
}

bool render_settled_tile(const OperationLog& log, TileKey key,
                         const SettledTileWorkspace& workspace, std::span<std::uint16_t> out_pixels,
                         SettledTileStats* stats) {
  if (!valid_tile_key(key)) {
    return false;
  }
  return render_settled_window(log, key.zoom, tile_pixel_bounds(key), workspace, out_pixels, stats);
}

}  // namespace tinydraw::vector_v2
