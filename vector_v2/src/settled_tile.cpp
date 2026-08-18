#include "tinydraw/vector_v2/settled_tile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

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
  destination.saturated_skip_pixels += source.saturated_skip_pixels;
  destination.composite_pixels += source.composite_pixels;
  destination.fold_pixels += source.fold_pixels;
  destination.saturated_early = destination.saturated_early || source.saturated_early;
}

// Conservative exterior-capsule row span for the settled chord raster,
// mirroring conservative_tapered_row_span in incremental_rasterizer.cpp
// (same half-plane construction, rounding margin, and whole-pixel guard;
// see that function's derivation comments). coverage_alpha(d^2, r) is
// exactly zero iff d >= r + 0.5, so the span is computed for the chord
// with both radii inflated by 0.5: every pixel outside the returned
// interval provably evaluates to alpha 0 and is skipped without work.
// The per-pixel evaluator remains the sole coverage authority inside.
struct SettleRowSpan {
  int first = 0;
  int last = -1;  // inclusive; empty when last < first

  [[nodiscard]] bool empty() const { return last < first; }
};

SettleRowSpan settle_conservative_row_span(const SettledChordSpanTable& table, int x0, int x1,
                                           float pixel_y) {
  constexpr float kRoundingMargin = 0.01F;
  const SettleRowSpan empty{.first = 0, .last = -1};
  float interval_first = 0.0F;
  float interval_last = 1.0F;
  if (table.delta_low == 0.0F) {
    if (table.origin_low > pixel_y + kRoundingMargin) {
      return empty;
    }
  } else {
    const float crossing = (pixel_y + kRoundingMargin - table.origin_low) * table.inverse_low;
    if (table.delta_low > 0.0F) {
      interval_last = std::min(interval_last, crossing);
    } else {
      interval_first = std::max(interval_first, crossing);
    }
  }
  if (table.delta_high == 0.0F) {
    if (table.origin_high < pixel_y - kRoundingMargin) {
      return empty;
    }
  } else {
    const float crossing = (pixel_y - kRoundingMargin - table.origin_high) * table.inverse_high;
    if (table.delta_high > 0.0F) {
      interval_first = std::max(interval_first, crossing);
    } else {
      interval_last = std::min(interval_last, crossing);
    }
  }
  if (interval_last < interval_first) {
    return empty;
  }
  interval_first = std::clamp(interval_first, 0.0F, 1.0F);
  interval_last = std::clamp(interval_last, 0.0F, 1.0F);
  if (interval_last < interval_first) {
    return empty;
  }
  const float minimum_x = std::min(table.left_origin + interval_first * table.left_delta,
                                   table.left_origin + interval_last * table.left_delta);
  const float maximum_x = std::max(table.right_origin + interval_first * table.right_delta,
                                   table.right_origin + interval_last * table.right_delta);
  const int first = std::max(x0, static_cast<int>(std::floor(minimum_x - kRoundingMargin)) - 1);
  const int last = std::min(x1 - 1, static_cast<int>(std::ceil(maximum_x + kRoundingMargin)));
  return {.first = first, .last = last};
}

std::uint8_t coverage_alpha(float distance_squared, float radius) {
  const float exterior = radius + 0.5F;
  if (distance_squared >= exterior * exterior) {
    return 0U;
  }
  const float interior = radius - 0.5F;
  if (interior > 0.0F && distance_squared <= interior * interior) {
    return 255U;
  }
  const float distance = std::sqrt(distance_squared);
  const float alpha = std::clamp(0.5F + (radius - distance), 0.0F, 1.0F);
  return alpha > 0.0F ? static_cast<std::uint8_t>(std::lround(alpha * 255.0F)) : 0U;
}

}  // namespace

void SettledRenderCursor::cancel() { *this = SettledRenderCursor{}; }

bool SettledRenderCursor::active() const { return phase_ != Phase::kIdle; }

const SettledTileStats& SettledRenderCursor::stats() const { return stats_; }

struct SettledRenderCursor::WorkBudget {
  const SettledRenderRequest& request;
  std::size_t work = 0;
  std::optional<SettledRenderSlice> result;

  [[nodiscard]] std::size_t room() const {
    return request.max_work_px - std::min(request.max_work_px, work);
  }

  [[nodiscard]] bool stop_before(std::size_t charge) const { return work != 0U && charge > room(); }

  void pause() {
    result = SettledRenderSlice{.status = SettledRenderStatus::kInProgress, .work_px = work};
  }

  void fail() {
    result = SettledRenderSlice{.status = SettledRenderStatus::kError, .work_px = work};
  }

  void complete() {
    result = SettledRenderSlice{.status = SettledRenderStatus::kComplete, .work_px = work};
  }

  void complete_no_ink() {
    result = SettledRenderSlice{
        .status = SettledRenderStatus::kComplete, .work_px = work, .no_ink = true};
  }
};

bool SettledRenderCursor::bind(const SettledRenderRequest& request) {
  const int width = request.window_bounds.x1 - request.window_bounds.x0;
  const int height = request.window_bounds.y1 - request.window_bounds.y0;
  const std::size_t pixel_count =
      width > 0 && height > 0 ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                              : 0U;
  const bool request_valid =
      request.log.ready() && width > 0 && height > 0 && width <= static_cast<int>(kTileWidth) &&
      height <= static_cast<int>(kTileHeight) && request.out_pixels.size() >= pixel_count &&
      request.workspace.operation_alpha.size() >= pixel_count &&
      request.workspace.accumulated_alpha.size() >= pixel_count &&
      request.workspace.red.size() >= pixel_count &&
      request.workspace.green.size() >= pixel_count &&
      request.workspace.blue.size() >= pixel_count && request.max_work_px != 0U;
  if (!request_valid) {
    cancel();
    return false;
  }

  if (active()) {
    const bool same_request = log_ == &request.log && authority_ == request.log.read_view() &&
                              zoom_ == request.zoom && window_bounds_ == request.window_bounds &&
                              same_workspace(workspace_, request.workspace) &&
                              same_span(out_pixels_, request.out_pixels);
    if (!same_request) {
      cancel();
      return false;
    }
    return true;
  }

  cancel();
  const int percent = zoom_percent(request.zoom);
  log_ = &request.log;
  authority_ = request.log.read_view();
  zoom_ = request.zoom;
  window_bounds_ = request.window_bounds;
  world_bounds_ = {request.window_bounds.x0 * 100 / percent - 1,
                   request.window_bounds.y0 * 100 / percent - 1,
                   (request.window_bounds.x1 * 100 + percent - 1) / percent + 1,
                   (request.window_bounds.y1 * 100 + percent - 1) / percent + 1};
  workspace_ = request.workspace;
  out_pixels_ = request.out_pixels;
  narrowing_disabled_ = request.disable_row_narrowing;
  width_ = width;
  height_ = height;
  pixel_count_ = pixel_count;
  operation_min_x_.fill(static_cast<std::uint8_t>(width));
  operation_max_x_.fill(0U);
  operation_min_y_ = static_cast<std::uint8_t>(height);
  // Query first: an empty window then skips plane initialization and the
  // final fold entirely through the white fast path.
  phase_ = Phase::kQueryCandidates;
  return true;
}

void SettledRenderCursor::advance_initialize(WorkBudget& budget) {
  const std::size_t count = std::min(budget.room(), pixel_count_ - initialize_at_);
  std::memset(workspace_.accumulated_alpha.data() + initialize_at_, 0, count);
  std::memset(workspace_.red.data() + initialize_at_, 0, count * sizeof(std::uint16_t));
  std::memset(workspace_.green.data() + initialize_at_, 0, count * sizeof(std::uint16_t));
  std::memset(workspace_.blue.data() + initialize_at_, 0, count * sizeof(std::uint16_t));
  std::memset(workspace_.operation_alpha.data() + initialize_at_, 0, count);
  initialize_at_ += count;
  stats_.initialize_pixels += count;
  budget.work += count;
  if (initialize_at_ != pixel_count_) {
    budget.pause();
    return;
  }
  phase_ = Phase::kScanOperation;
}

void SettledRenderCursor::advance_candidate_query(WorkBudget& budget) {
  if (budget.stop_before(1U)) {
    budget.pause();
    return;
  }
  OperationSpatialQueryStats spatial_stats{};
  const auto candidates = log_->query_spatial(world_bounds_, 0U, authority_.active_operation_count,
                                              workspace_.candidate_indices, &spatial_stats);
  ++stats_.candidate_queries;
  stats_.operations_in_authority += authority_.active_operation_count;
  if (candidates.has_value()) {
    stats_.index_candidates += spatial_stats.index_candidates;
    stats_.deduplicated_candidates += spatial_stats.deduplicated_candidates;
  }
  use_candidates_ = candidates.has_value() &&
                    prefer_spatial_candidates(*candidates, authority_.active_operation_count);
  replay_count_ = use_candidates_ ? *candidates : authority_.active_operation_count;
  ++budget.work;
  if (replay_count_ == 0U) {
    // The conservative index proves no operation touches this window: the
    // exact settled output is paper white. One atomic fill replaces plane
    // initialization, scanning, and the final fold.
    std::fill(out_pixels_.begin(), out_pixels_.begin() + static_cast<std::ptrdiff_t>(pixel_count_),
              std::uint16_t{0xFFFFU});
    stats_.fold_pixels += pixel_count_;
    budget.work += pixel_count_;
    phase_ = Phase::kIdle;
    budget.complete_no_ink();
    return;
  }
  phase_ = Phase::kInitialize;
}

void SettledRenderCursor::advance_operation_scan(WorkBudget& budget) {
  if (replay_index_ == replay_count_) {
    phase_ = Phase::kFinalFold;
    return;
  }
  if (budget.stop_before(1U)) {
    budget.pause();
    return;
  }
  operation_index_ = use_candidates_ ? workspace_.candidate_indices[replay_index_]
                                     : authority_.active_operation_count - 1U - replay_index_;
  const auto stored = log_->operation(operation_index_);
  if (!stored.has_value()) {
    cancel();
    budget.fail();
    return;
  }
  ++stats_.operations_scanned;
  ++budget.work;
  if (!rects_intersect(stored->world_bounds, world_bounds_)) {
    ++replay_index_;
    return;
  }
  ++stats_.operations_intersecting;
  operation_tool_ = stored->tool;
  operation_color_ = stored->color;
  operation_samples_ = stored->samples;
  operation_touched_ = false;
  clear_row_ = operation_min_y_;
  endpoint_ = 1U;
  phase_ = Phase::kClearOperation;
}

void SettledRenderCursor::advance_operation_clear(WorkBudget& budget) {
  while (clear_row_ < operation_max_y_) {
    const std::size_t row = clear_row_;
    const std::size_t begin = operation_min_x_[row];
    const std::size_t end = operation_max_x_[row];
    const std::size_t count = end > begin ? end - begin : 0U;
    if (budget.stop_before(count)) {
      budget.pause();
      return;
    }
    if (count != 0U) {
      std::memset(
          workspace_.operation_alpha.data() + row * static_cast<std::size_t>(width_) + begin, 0,
          count);
    }
    operation_min_x_[row] = static_cast<std::uint8_t>(width_);
    operation_max_x_[row] = 0U;
    ++clear_row_;
    stats_.operation_clear_pixels += count;
    budget.work += count;
  }
  operation_min_y_ = static_cast<std::uint8_t>(height_);
  operation_max_y_ = 0U;
  phase_ = Phase::kPrepareEndpoint;
}

void SettledRenderCursor::advance_endpoint_preparation(WorkBudget& budget) {
  if (endpoint_ == operation_samples_.size()) {
    if (!operation_touched_) {
      ++replay_index_;
      phase_ = Phase::kScanOperation;
      return;
    }
    ++stats_.operations_rendered;
    composite_row_ = operation_min_y_;
    composite_x_ = operation_min_x_[composite_row_];
    phase_ = Phase::kCompositeOperation;
    return;
  }
  if (budget.stop_before(1U)) {
    budget.pause();
    return;
  }
  const auto unit = prepare_incremental_curve_unit(operation_samples_, endpoint_, zoom_);
  ++budget.work;
  if (!unit.has_value()) {
    ++endpoint_;
    return;
  }
  prepared_unit_ = *unit;
  ++stats_.curve_units_prepared;
  step_ = 0U;
  chord_next_y_ = 0;
  chord_y1_ = 0;
  phase_ = Phase::kRasterChord;
}

void SettledRenderCursor::prepare_chord(const PreparedCurveStep& chord) {
  const float ax = chord.first_x - static_cast<float>(window_bounds_.x0);
  const float ay = chord.first_y - static_cast<float>(window_bounds_.y0);
  const float bx = chord.second_x - static_cast<float>(window_bounds_.x0);
  const float by = chord.second_y - static_cast<float>(window_bounds_.y0);
  const float radius_max = std::max(chord.first_radius, chord.second_radius);
  chord_x0_ = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - radius_max - 1.5F)));
  chord_next_y_ = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - radius_max - 1.5F)));
  chord_x1_ = std::min(width_, static_cast<int>(std::ceil(std::max(ax, bx) + radius_max + 1.5F)));
  chord_y1_ = std::min(height_, static_cast<int>(std::ceil(std::max(ay, by) + radius_max + 1.5F)));
  chord_ax_ = ax;
  chord_ay_ = ay;
  chord_delta_x_ = bx - ax;
  chord_delta_y_ = by - ay;
  const float length_squared = chord_delta_x_ * chord_delta_x_ + chord_delta_y_ * chord_delta_y_;
  chord_inverse_length_squared_ = length_squared > 0.0F ? 1.0F / length_squared : 0.0F;
  chord_first_radius_ = chord.first_radius;
  chord_radius_delta_ = chord.second_radius - chord.first_radius;
  // Narrow only chords with enough expected exterior per row for the
  // per-row span solve (~a handful of pixel evaluations) to pay for
  // itself. A row's savings is roughly the bbox width minus the capsule
  // diameter (the |dx| of a slanted chord): fat short chords have wide
  // bboxes but almost no exterior, and their exterior is often already
  // saturated-skip cheap. Both paths are exact, so this is purely a
  // cost-model gate (measured on the five-corpus host A/B: a plain width
  // gate left dense 200/400% regressing 7–10%; this discriminator keeps
  // the long-chord wins without them).
  constexpr int kNarrowingMinimumExteriorWidth = 16;
  chord_narrowed_ =
      !narrowing_disabled_ && (chord_x1_ - chord_x0_) - static_cast<int>(2.0F * radius_max) - 3 >=
                                  kNarrowingMinimumExteriorWidth;
  if (!chord_narrowed_) {
    return;
  }
  // Exterior-capsule span table: radii + 0.5 (the taper delta is
  // unchanged by a uniform inflation).
  const float exterior_first = chord.first_radius + 0.5F;
  span_table_.origin_low = ay - exterior_first;
  span_table_.delta_low = chord_delta_y_ - chord_radius_delta_;
  span_table_.inverse_low = span_table_.delta_low != 0.0F ? 1.0F / span_table_.delta_low : 0.0F;
  span_table_.origin_high = ay + exterior_first;
  span_table_.delta_high = chord_delta_y_ + chord_radius_delta_;
  span_table_.inverse_high = span_table_.delta_high != 0.0F ? 1.0F / span_table_.delta_high : 0.0F;
  span_table_.left_origin = ax - exterior_first;
  span_table_.left_delta = chord_delta_x_ - chord_radius_delta_;
  span_table_.right_origin = ax + exterior_first;
  span_table_.right_delta = chord_delta_x_ + chord_radius_delta_;
}

void SettledRenderCursor::raster_chord_row(int span_first, int span_last) {
  const int y = chord_next_y_;
  const float sample_y = static_cast<float>(y) + 0.5F;
  std::uint8_t* row = workspace_.operation_alpha.data() +
                      static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
  const std::uint8_t* accumulated_row =
      workspace_.accumulated_alpha.data() +
      static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
  std::size_t computed = 0;
  std::size_t skipped = 0;
  for (int x = span_first; x <= span_last; ++x) {
    // Newest-first compositing: a saturated destination pixel can receive
    // no further contribution, so its coverage math is skipped exactly
    // (operation_alpha stays 0 and composite contributes 0 either way).
    // Re-opened 2026-08-18 under the deterministic same-corpus settle gate;
    // the earlier rejection compared unlike corpora.
    if (accumulated_row[x] == 255U) {
      ++skipped;
      continue;
    }
    ++computed;
    const float sample_x = static_cast<float>(x) + 0.5F;
    const float ap_x = sample_x - chord_ax_;
    const float ap_y = sample_y - chord_ay_;
    const float t =
        std::clamp((ap_x * chord_delta_x_ + ap_y * chord_delta_y_) * chord_inverse_length_squared_,
                   0.0F, 1.0F);
    const float dx = ap_x - t * chord_delta_x_;
    const float dy = ap_y - t * chord_delta_y_;
    const std::uint8_t alpha =
        coverage_alpha(dx * dx + dy * dy, chord_first_radius_ + chord_radius_delta_ * t);
    if (alpha <= row[x]) {
      continue;
    }
    row[x] = alpha;
    operation_touched_ = true;
    const std::size_t row_index = static_cast<std::size_t>(y);
    operation_min_x_[row_index] =
        std::min(operation_min_x_[row_index], static_cast<std::uint8_t>(x));
    operation_max_x_[row_index] =
        std::max(operation_max_x_[row_index], static_cast<std::uint8_t>(x + 1));
    operation_min_y_ = std::min(operation_min_y_, static_cast<std::uint8_t>(y));
    operation_max_y_ = std::max(operation_max_y_, static_cast<std::uint8_t>(y + 1));
  }
  stats_.raster_pixels += computed;
  stats_.saturated_skip_pixels += skipped;
}

void SettledRenderCursor::advance_chord_raster(WorkBudget& budget) {
  if (step_ == prepared_unit_.step_count) {
    ++endpoint_;
    phase_ = Phase::kPrepareEndpoint;
    return;
  }
  if (chord_next_y_ >= chord_y1_) {
    prepare_chord(prepared_unit_.steps[step_]);
    if (chord_x0_ >= chord_x1_ || chord_next_y_ >= chord_y1_) {
      ++step_;
      chord_next_y_ = 0;
      chord_y1_ = 0;
      return;
    }
  }

  // Edge-span narrowing (final-round AA lever 2, stage 1): the row only
  // traverses the conservative exterior-capsule interval; pixels outside
  // provably evaluate to alpha 0. The charge equals the traversed width
  // (real work, unlike the rejected 2026-08-18 work-charge recalibration,
  // which repriced unchanged traversal); empty rows charge one pixel for
  // the span computation.
  const SettleRowSpan span =
      chord_narrowed_ ? settle_conservative_row_span(span_table_, chord_x0_, chord_x1_,
                                                     static_cast<float>(chord_next_y_) + 0.5F)
                      : SettleRowSpan{.first = chord_x0_, .last = chord_x1_ - 1};
  const std::size_t row_work =
      span.empty() ? 1U : static_cast<std::size_t>(span.last - span.first + 1);
  if (budget.stop_before(row_work)) {
    budget.pause();
    return;
  }
  if (!span.empty()) {
    raster_chord_row(span.first, span.last);
  }
  budget.work += row_work;
  ++chord_next_y_;
  if (chord_next_y_ == chord_y1_) {
    ++step_;
    chord_next_y_ = 0;
    chord_y1_ = 0;
  }
}

void SettledRenderCursor::composite_pixels(std::size_t first_at, std::size_t count,
                                           std::uint16_t red, std::uint16_t green,
                                           std::uint16_t blue) {
  for (std::size_t offset = 0; offset < count; ++offset) {
    const std::size_t at = first_at + offset;
    const std::uint8_t alpha = workspace_.operation_alpha[at];
    if (alpha == 0U) {
      continue;
    }
    const std::uint8_t accumulated = workspace_.accumulated_alpha[at];
    const auto contribution = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(alpha) * (255U - accumulated) + 127U) / 255U);
    if (contribution == 0U) {
      continue;
    }
    workspace_.red[at] = static_cast<std::uint16_t>(workspace_.red[at] + red * contribution / 255U);
    workspace_.green[at] =
        static_cast<std::uint16_t>(workspace_.green[at] + green * contribution / 255U);
    workspace_.blue[at] =
        static_cast<std::uint16_t>(workspace_.blue[at] + blue * contribution / 255U);
    const auto next_accumulated =
        static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, accumulated + contribution));
    if (next_accumulated == 255U && accumulated != 255U) {
      ++saturated_pixels_;
    }
    workspace_.accumulated_alpha[at] = next_accumulated;
  }
}

void SettledRenderCursor::advance_operation_composite(WorkBudget& budget) {
  const Channels color = expand_565(
      operation_tool_ == OperationTool::kEraser ? std::uint16_t{0xFFFFU} : operation_color_);
  while (composite_row_ < operation_max_y_) {
    const std::size_t row = composite_row_;
    const std::size_t end = operation_max_x_[row];
    if (composite_x_ >= end) {
      ++composite_row_;
      if (composite_row_ < operation_max_y_) {
        composite_x_ = operation_min_x_[composite_row_];
      }
      continue;
    }
    const std::size_t count = std::min(budget.room(), end - composite_x_);
    if (count == 0U) {
      budget.pause();
      return;
    }
    const std::size_t first_at = row * static_cast<std::size_t>(width_) + composite_x_;
    composite_pixels(first_at, count, color.red, color.green, color.blue);
    composite_x_ += count;
    stats_.composite_pixels += count;
    budget.work += count;
    if (budget.work >= budget.request.max_work_px) {
      budget.pause();
      return;
    }
  }
  if (saturated_pixels_ == pixel_count_) {
    stats_.saturated_early = true;
    phase_ = Phase::kFinalFold;
    return;
  }
  ++replay_index_;
  phase_ = Phase::kScanOperation;
}

void SettledRenderCursor::advance_final_fold(WorkBudget& budget) {
  const std::size_t count = std::min(budget.room(), pixel_count_ - fold_at_);
  for (std::size_t offset = 0; offset < count; ++offset) {
    const std::size_t at = fold_at_ + offset;
    const std::uint32_t remaining = 255U - workspace_.accumulated_alpha[at];
    const std::uint32_t r8 = std::min<std::uint32_t>(255U, workspace_.red[at] + remaining);
    const std::uint32_t g8 = std::min<std::uint32_t>(255U, workspace_.green[at] + remaining);
    const std::uint32_t b8 = std::min<std::uint32_t>(255U, workspace_.blue[at] + remaining);
    out_pixels_[at] =
        static_cast<std::uint16_t>(((r8 >> 3U) << 11U) | ((g8 >> 2U) << 5U) | (b8 >> 3U));
  }
  fold_at_ += count;
  stats_.fold_pixels += count;
  budget.work += count;
  if (fold_at_ != pixel_count_) {
    budget.pause();
    return;
  }
  phase_ = Phase::kIdle;
  budget.complete();
}

SettledRenderSlice SettledRenderCursor::advance(WorkBudget& budget) {
  while (!budget.result.has_value()) {
    switch (phase_) {
      case Phase::kIdle:
        budget.fail();
        break;
      case Phase::kInitialize:
        advance_initialize(budget);
        break;
      case Phase::kQueryCandidates:
        advance_candidate_query(budget);
        break;
      case Phase::kScanOperation:
        advance_operation_scan(budget);
        break;
      case Phase::kClearOperation:
        advance_operation_clear(budget);
        break;
      case Phase::kPrepareEndpoint:
        advance_endpoint_preparation(budget);
        break;
      case Phase::kRasterChord:
        advance_chord_raster(budget);
        break;
      case Phase::kCompositeOperation:
        advance_operation_composite(budget);
        break;
      case Phase::kFinalFold:
        advance_final_fold(budget);
        break;
    }
    if (!budget.result.has_value() && budget.work >= budget.request.max_work_px) {
      budget.pause();
    }
  }
  return *budget.result;
}

SettledRenderSlice render_settled_window_slice(const SettledRenderRequest& request) {
  if (!request.cursor.bind(request)) {
    return {.status = SettledRenderStatus::kError};
  }
  SettledRenderCursor::WorkBudget budget{.request = request, .work = 0U, .result = std::nullopt};
  return request.cursor.advance(budget);
}

bool render_settled_window(const OperationLog& log, ZoomLevel zoom, PixelRect window_bounds,
                           const SettledTileWorkspace& workspace,
                           std::span<std::uint16_t> out_pixels, SettledTileStats* stats) {
  SettledRenderCursor cursor;
  while (true) {
    const SettledRenderSlice slice =
        render_settled_window_slice({.log = log,
                                     .zoom = zoom,
                                     .window_bounds = window_bounds,
                                     .workspace = workspace,
                                     .out_pixels = out_pixels,
                                     .cursor = cursor,
                                     .max_work_px = std::numeric_limits<std::size_t>::max()});
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
