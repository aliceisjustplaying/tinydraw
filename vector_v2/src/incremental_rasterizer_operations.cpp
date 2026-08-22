#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "incremental_rasterizer_internal.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/raster_census.h"
#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {

using raster_internal::advance_curve_sample_window;
using raster_internal::conservative_row_span;
using raster_internal::copy_segment;
using raster_internal::covers_pixel;
using raster_internal::curved_unit;
using raster_internal::CurveSampleWindow;
using raster_internal::CurveUnit;
using raster_internal::first_covered_at_or_after;
using raster_internal::initialize_curve_sample_window;
using raster_internal::kBackground;
using raster_internal::last_covered_at_or_before;
using raster_internal::make_pixel_coverage_row;
using raster_internal::make_row_seed;
using raster_internal::mask_unset_window;
using raster_internal::MaskedRowTarget;
using raster_internal::paint_masked_const_row;
using raster_internal::paint_masked_curve_unit_warm;
using raster_internal::paint_masked_segment;
using raster_internal::paint_masked_tapered_row;
using raster_internal::paint_segment;
using raster_internal::pixel_center;
using raster_internal::PixelCoverageRow;
using raster_internal::retreat_curve_sample_window;
using raster_internal::RowSeed;
using raster_internal::Sample;
using raster_internal::scaled_sample;
using raster_internal::ScanSpan;
using raster_internal::Segment;
using raster_internal::segment_bounds;
using raster_internal::valid_surface;

bool apply_incremental_operation(const OperationAppend& operation, const RasterSurface& surface) {
  if (!valid_surface(surface) || operation.samples.empty()) {
    return false;
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  if (operation.samples.size() <= 2U) {
    paint_segment(scaled_sample(operation.samples.front(), surface.zoom),
                  scaled_sample(operation.samples.back(), surface.zoom), color, surface);
    return true;
  }
  CurveSampleWindow window;
  CurveUnit unit;
  initialize_curve_sample_window(operation.samples, 2U, surface.zoom, window, unit);
  for (std::size_t endpoint = 2U;;) {
    for (std::size_t step = 0U; step < unit.count; ++step) {
      paint_segment(unit.segments[step].first, unit.segments[step].second, color, surface);
    }
    ++endpoint;
    if (endpoint == operation.samples.size()) {
      break;
    }
    advance_curve_sample_window(window, operation.samples[endpoint],
                                endpoint + 1U == operation.samples.size(), unit);
  }
  return true;
}

bool apply_masked_incremental_operation(const OperationAppend& operation,
                                        const RasterSurface& surface,
                                        std::span<std::uint8_t> finalized_pixels,
                                        MaskedRowSummary* summary) {
  const std::size_t required_mask_bytes = (surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(surface.pixels)),
      std::as_bytes(std::span(finalized_pixels)
                        .first(std::min(finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = surface.level_bounds.y1 - surface.level_bounds.y0;
  if (!valid_surface(surface) || operation.samples.empty() ||
      finalized_pixels.size() < required_mask_bytes || mask_aliases_pixels ||
      (summary != nullptr && !summary->ready(static_cast<std::size_t>(surface_rows)))) {
    return false;
  }
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? kBackground : operation.color;
  if (operation.samples.size() <= 2U) {
    paint_masked_segment(scaled_sample(operation.samples.front(), surface.zoom),
                         scaled_sample(operation.samples.back(), surface.zoom), color, surface,
                         finalized_pixels, summary);
    return true;
  }
  CurveSampleWindow window;
  CurveUnit unit;
  initialize_curve_sample_window(operation.samples, 2U, surface.zoom, window, unit);
  for (std::size_t endpoint = 2U;;) {
    paint_masked_curve_unit_warm(unit, color, surface, finalized_pixels, summary);
    ++endpoint;
    if (endpoint == operation.samples.size()) {
      break;
    }
    advance_curve_sample_window(window, operation.samples[endpoint],
                                endpoint + 1U == operation.samples.size(), unit);
  }
  return true;
}

namespace {

PreparedCurveStep pack_prepared_step(const Segment& segment) {
  return {
      .first_x = segment.first.x,
      .first_y = segment.first.y,
      .first_radius = segment.first.radius,
      .second_x = segment.second.x,
      .second_y = segment.second.y,
      .second_radius = segment.second.radius,
      .delta_x = segment.delta_x,
      .delta_y = segment.delta_y,
      .inverse_length_squared = segment.inverse_length_squared,
  };
}

}  // namespace

std::optional<PreparedCurveUnit> prepare_incremental_curve_unit(
    std::span<const CompactOperationSample> samples, std::size_t endpoint, ZoomLevel zoom) {
  CurveUnit unit;
  if (!curved_unit(samples, endpoint, zoom, unit)) {
    return std::nullopt;
  }
  PreparedCurveUnit prepared{.step_count = unit.count};
  for (std::size_t step = 0; step < unit.count; ++step) {
    prepared.steps[step] = pack_prepared_step(unit.segments[step]);
  }
  return prepared;
}

namespace {

// One prepared chord with its per-row sweep plan. Lives in caller-funded
// opaque storage so the internal geometry types stay private.
struct OperationChordPlan {
  Segment segment{};
  RowSeed seed{};
  PixelRect bounds{};
  bool constant = false;
  bool covers_surface = false;
};
static_assert(sizeof(OperationChordPlan) <= kPreparedOperationChordBytes);
static_assert(alignof(OperationChordPlan) <= kPreparedOperationChordAlign);

[[nodiscard]] bool chord_storage_usable(std::span<const std::byte> storage,
                                        std::size_t chord_count) {
  return chord_count <= kOperationChordCapacity && storage.size() >= kOperationChordStorageBytes &&
         reinterpret_cast<std::uintptr_t>(storage.data()) % alignof(OperationChordPlan) == 0U;
}

[[nodiscard]] OperationChordPlan* chord_plans(std::span<std::byte> storage) {
  return reinterpret_cast<OperationChordPlan*>(storage.data());
}

[[nodiscard]] const OperationChordPlan* chord_plans(std::span<const std::byte> storage) {
  return reinterpret_cast<const OperationChordPlan*>(storage.data());
}

// y0-ascending chord order, one byte per chord, stored after the plans.
[[nodiscard]] std::uint8_t* chord_order(std::span<std::byte> storage) {
  return reinterpret_cast<std::uint8_t*>(storage.data() +
                                         kOperationChordCapacity * kPreparedOperationChordBytes);
}

[[nodiscard]] const std::uint8_t* chord_order(std::span<const std::byte> storage) {
  return reinterpret_cast<const std::uint8_t*>(storage.data() + kOperationChordCapacity *
                                                                    kPreparedOperationChordBytes);
}

bool constant_capsule_covers_surface(const Segment& segment, PixelRect surface_bounds) {
  constexpr float kNumericGuard = 1.0F;
  if (segment.first.radius != segment.second.radius || segment.first.radius <= kNumericGuard ||
      surface_bounds.x1 <= surface_bounds.x0 || surface_bounds.y1 <= surface_bounds.y0) {
    return false;
  }
  Segment guarded;
  copy_segment(segment, guarded);
  guarded.first.radius -= kNumericGuard;
  guarded.second.radius -= kNumericGuard;
  // A constant-radius capsule is convex. If the four pixel-center corners
  // lie inside the capsule shrunk by a full level pixel, every pixel center
  // lies inside the authoritative capsule with ample float-rounding margin.
  const float first_x = static_cast<float>(surface_bounds.x0) + 0.5F;
  const float first_y = static_cast<float>(surface_bounds.y0) + 0.5F;
  const float last_x = static_cast<float>(surface_bounds.x1) - 0.5F;
  const float last_y = static_cast<float>(surface_bounds.y1) - 0.5F;
  return covers_pixel(guarded, first_x, first_y) && covers_pixel(guarded, last_x, first_y) &&
         covers_pixel(guarded, first_x, last_y) && covers_pixel(guarded, last_x, last_y);
}

struct ChordBatchBuilder {
  std::span<const CompactOperationSample> samples;
  ZoomLevel zoom;
  PixelRect surface_bounds;
  OperationChordPlan* plans;
  OperationChordBatch& batch;

  [[nodiscard]] bool append_endpoint(std::size_t endpoint) {
    CurveUnit unit;
    if (!curved_unit(samples, endpoint, zoom, unit)) {
      return false;
    }
    append(unit);
    return true;
  }

  void append(const CurveUnit& unit) {
    for (std::size_t step = 0; step < unit.count; ++step) {
      append(unit.segments[step]);
    }
  }

  void append(const Segment& segment) {
    const PixelRect bounds = segment_bounds(segment, surface_bounds);
    if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0) {
      return;
    }
    const bool spans_surface = bounds.x0 == surface_bounds.x0 && bounds.y0 == surface_bounds.y0 &&
                               bounds.x1 == surface_bounds.x1 && bounds.y1 == surface_bounds.y1;
    OperationChordPlan& plan = plans[batch.chord_count++];
    copy_segment(segment, plan.segment);
    make_row_seed(segment, plan.seed);
    plan.bounds = bounds;
    plan.constant = segment.first.radius == segment.second.radius;
    plan.covers_surface = spans_surface && constant_capsule_covers_surface(segment, surface_bounds);
    batch.raster_work += static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                         static_cast<std::size_t>(bounds.y1 - bounds.y0);
    batch.clipped_bounds.x0 = std::min(batch.clipped_bounds.x0, bounds.x0);
    batch.clipped_bounds.y0 = std::min(batch.clipped_bounds.y0, bounds.y0);
    batch.clipped_bounds.x1 = std::max(batch.clipped_bounds.x1, bounds.x1);
    batch.clipped_bounds.y1 = std::max(batch.clipped_bounds.y1, bounds.y1);
  }

  [[nodiscard]] bool prepare(std::size_t first_endpoint) {
    if (samples.size() <= 2U) {
      batch.next_endpoint = 0U;
      return append_endpoint(first_endpoint);
    }
    if (first_endpoint < 2U || first_endpoint >= samples.size()) {
      return false;
    }
    CurveSampleWindow window;
    CurveUnit unit;
    std::size_t endpoint = first_endpoint;
    initialize_curve_sample_window(samples, endpoint, zoom, window, unit);
    while (batch.chord_count + 3U <= kOperationChordCapacity) {
      append(unit);
      if (endpoint == 2U) {
        batch.next_endpoint = 0U;
        return true;
      }
      --endpoint;
      retreat_curve_sample_window(window, samples[endpoint - 2U], endpoint == 2U, unit);
    }
    batch.next_endpoint = endpoint;
    return true;
  }

  [[nodiscard]] static int chord_area(const OperationChordPlan& plan) {
    return (plan.bounds.x1 - plan.bounds.x0) * (plan.bounds.y1 - plan.bounds.y0);
  }

  [[nodiscard]] bool chord_before(std::uint8_t candidate, std::uint8_t prior) const {
    const int candidate_y = plans[candidate].bounds.y0;
    const int prior_y = plans[prior].bounds.y0;
    return candidate_y < prior_y ||
           (candidate_y == prior_y && chord_area(plans[candidate]) > chord_area(plans[prior]));
  }

  void insertion_sort(std::uint8_t* order) const {
    for (std::size_t index = 0; index < batch.chord_count; ++index) {
      const std::uint8_t chord = static_cast<std::uint8_t>(index);
      std::size_t position = index;
      while (position != 0U && chord_before(chord, order[position - 1U])) {
        order[position] = order[position - 1U];
        --position;
      }
      order[position] = chord;
    }
  }

  void prioritize_equal_row_chords(std::uint8_t* order) const {
    for (std::size_t index = 1; index < batch.chord_count; ++index) {
      const std::uint8_t chord = order[index];
      const int y = plans[chord].bounds.y0;
      const int area = chord_area(plans[chord]);
      std::size_t position = index;
      while (position != 0U) {
        const std::uint8_t prior = order[position - 1U];
        if (plans[prior].bounds.y0 != y || chord_area(plans[prior]) >= area) {
          break;
        }
        order[position] = prior;
        --position;
      }
      order[position] = chord;
    }
  }

  void sort(std::span<std::byte> storage) const {
    std::uint8_t* order = chord_order(storage);
    constexpr int kBucketRows = 128;
    constexpr std::size_t kInsertionSortThreshold = 12U;
    const int first_row = batch.clipped_bounds.y0;
    const int row_count = batch.clipped_bounds.y1 - first_row;
    if (batch.chord_count <= kInsertionSortThreshold || row_count <= 0 || row_count > kBucketRows) {
      insertion_sort(order);
      return;
    }

    // Clear only the live row range. A word backing array gives GCC aligned
    // native stores and keeps this free of libc/ROM string calls on Xtensa.
    std::array<std::uint32_t, kBucketRows / 4> bucket_words;
    const std::size_t live_words = (static_cast<std::size_t>(row_count) + 3U) / 4U;
    for (std::size_t word = 0; word < live_words; ++word) {
#if defined(__XTENSA__)
      // IDF disables the memset builtin, so a variable-size C++ clear becomes
      // a ROM call. One aligned store keeps the short live-range clear local.
      std::uint32_t* destination = bucket_words.data() + word;
      asm volatile("s32i %1, %0, 0" : : "r"(destination), "r"(0U) : "memory");
#else
      bucket_words[word] = 0U;
#endif
    }
    auto* next = reinterpret_cast<std::uint8_t*>(bucket_words.data());
    for (std::size_t index = 0; index < batch.chord_count; ++index) {
      ++next[static_cast<std::size_t>(plans[index].bounds.y0 - first_row)];
    }
    std::uint8_t position = 0;
    for (int row = 0; row < row_count; ++row) {
      const std::uint8_t count = next[static_cast<std::size_t>(row)];
      next[static_cast<std::size_t>(row)] = position;
      position = static_cast<std::uint8_t>(position + count);
    }
    for (std::size_t index = 0; index < batch.chord_count; ++index) {
      const std::size_t row = static_cast<std::size_t>(plans[index].bounds.y0 - first_row);
      order[next[row]++] = static_cast<std::uint8_t>(index);
    }
    // Equal-y0 ordering affects masked replay cost: broad chords first
    // finalize more of the row before narrower chords inspect it.
    prioritize_equal_row_chords(order);
  }
};

}  // namespace

std::optional<OperationChordBatch> prepare_operation_chord_batch(
    std::span<const CompactOperationSample> samples, std::size_t first_endpoint, ZoomLevel zoom,
    PixelRect surface_bounds, std::span<std::byte> chord_storage) {
  OperationChordBatch batch;
  if (!prepare_operation_chord_batch(samples, first_endpoint, zoom, surface_bounds, chord_storage,
                                     batch)) {
    return std::nullopt;
  }
  return batch;
}

bool prepare_operation_chord_batch(std::span<const CompactOperationSample> samples,
                                   std::size_t first_endpoint, ZoomLevel zoom,
                                   PixelRect surface_bounds, std::span<std::byte> chord_storage,
                                   OperationChordBatch& batch) {
  const std::size_t capacity = kOperationChordCapacity;
  if (samples.empty() || !chord_storage_usable(chord_storage, capacity)) {
    return false;
  }
  batch = {.clipped_bounds = {surface_bounds.x1, surface_bounds.y1, surface_bounds.x0,
                              surface_bounds.y0}};
  ChordBatchBuilder builder{
      .samples = samples,
      .zoom = zoom,
      .surface_bounds = surface_bounds,
      .plans = chord_plans(chord_storage),
      .batch = batch,
  };
  if (!builder.prepare(first_endpoint)) {
    return false;
  }
  builder.sort(chord_storage);
  return true;
}

namespace {

std::uint16_t applied_color(OperationTool tool, std::uint16_t color) {
  return tool == OperationTool::kEraser ? kBackground : color;
}

bool valid_masked_sweep(const MaskedOperationChordRowsRequest& sweep) {
  const std::size_t required_mask_bytes = (sweep.surface.pixels.size() + 7U) / 8U;
  const bool mask_aliases_pixels = storage_overlaps(
      std::as_bytes(std::span(sweep.surface.pixels)),
      std::as_bytes(std::span(sweep.finalized_pixels)
                        .first(std::min(sweep.finalized_pixels.size(), required_mask_bytes))));
  const int surface_rows = sweep.surface.level_bounds.y1 - sweep.surface.level_bounds.y0;
  const PixelRect bounds = sweep.batch.clipped_bounds;
  return valid_surface(sweep.surface) && sweep.batch.chord_count != 0U &&
         chord_storage_usable(sweep.chord_storage, sweep.batch.chord_count) &&
         sweep.finalized_pixels.size() >= required_mask_bytes && !mask_aliases_pixels &&
         sweep.max_work_px != 0U && sweep.first_row >= bounds.y0 && sweep.first_row < bounds.y1 &&
         bounds.x0 >= sweep.surface.level_bounds.x0 && bounds.y0 >= sweep.surface.level_bounds.y0 &&
         bounds.x1 <= sweep.surface.level_bounds.x1 && bounds.y1 <= sweep.surface.level_bounds.y1 &&
         (sweep.summary == nullptr || sweep.summary->ready(static_cast<std::size_t>(surface_rows)));
}

bool try_bulk_masked_fill(const MaskedOperationChordRowsRequest& sweep,
                          const OperationChordPlan* plans) {
  const int width = sweep.surface.level_bounds.x1 - sweep.surface.level_bounds.x0;
  const int height = sweep.surface.level_bounds.y1 - sweep.surface.level_bounds.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const std::size_t mask_bytes = (sweep.surface.pixels.size() + 7U) / 8U;
  const std::size_t work = (pixel_count + 1U) / 2U + mask_bytes + static_cast<std::size_t>(height);
  const bool contiguous = sweep.surface.stride == width &&
                          sweep.surface.pixels.size() == pixel_count && pixel_count % 8U == 0U;
  const bool covers_surface =
      std::any_of(plans, plans + sweep.batch.chord_count, [&](const OperationChordPlan& plan) {
        return plan.covers_surface && plan.bounds.x0 == sweep.surface.level_bounds.x0 &&
               plan.bounds.y0 == sweep.surface.level_bounds.y0 &&
               plan.bounds.x1 == sweep.surface.level_bounds.x1 &&
               plan.bounds.y1 == sweep.surface.level_bounds.y1;
      });
  const bool fresh =
      covers_surface && contiguous &&
      std::all_of(sweep.finalized_pixels.begin(),
                  sweep.finalized_pixels.begin() + static_cast<std::ptrdiff_t>(mask_bytes),
                  [](std::uint8_t byte) { return byte == 0U; });
  if (sweep.first_row != sweep.surface.level_bounds.y0 || !fresh || work > sweep.max_work_px) {
    return false;
  }

  TINYDRAW_V2_CENSUS_ADD(const_full_surface_fills, 1);
  TINYDRAW_V2_CENSUS_ADD(const_full_surface_pixels, pixel_count);
  std::fill(sweep.surface.pixels.begin(), sweep.surface.pixels.end(),
            applied_color(sweep.tool, sweep.color));
  std::fill_n(sweep.finalized_pixels.begin(), mask_bytes, 0xFFU);
  if (sweep.summary != nullptr) {
    for (int row = 0; row < height; ++row) {
      sweep.summary->note_finalized(row, width);
    }
  }
  sweep.slice.next_row = sweep.surface.level_bounds.y1;
  sweep.slice.rows_swept = height;
  sweep.slice.work_px = work;
  return true;
}

struct ActiveChords {
  std::array<std::uint8_t, kOperationChordCapacity> indices;
  std::size_t count = 0;
  std::size_t enter = 0;

  void initialize(const OperationChordPlan* plans, const std::uint8_t* order,
                  std::size_t chord_count, int row) {
    while (enter < chord_count && plans[order[enter]].bounds.y0 <= row) {
      if (plans[order[enter]].bounds.y1 > row) {
        indices[count++] = order[enter];
      }
      ++enter;
    }
  }

  void update(const OperationChordPlan* plans, const std::uint8_t* order, std::size_t chord_count,
              int row) {
    while (enter < chord_count && plans[order[enter]].bounds.y0 <= row) {
      indices[count++] = order[enter++];
    }
    for (std::size_t index = 0; index < count;) {
      if (plans[indices[index]].bounds.y1 <= row) {
        indices[index] = indices[--count];
      } else {
        ++index;
      }
    }
  }

  [[nodiscard]] std::span<const std::uint8_t> active() const {
    return std::span(indices).first(count);
  }
};

struct MaskedSweepRow {
  const MaskedOperationChordRowsRequest& sweep;
  int y;
  int surface_row;
  std::size_t row;
  int window_first;
  int window_last;
  float pixel_y;
};

PixelRect active_row_bounds(const OperationChordPlan* plans, std::span<const std::uint8_t> active,
                            PixelRect batch_bounds) {
  PixelRect bounds{batch_bounds.x1, 0, batch_bounds.x0, 0};
  for (const std::uint8_t index : active) {
    bounds.x0 = std::min(bounds.x0, plans[index].bounds.x0);
    bounds.x1 = std::max(bounds.x1, plans[index].bounds.x1);
  }
  return bounds;
}

std::optional<ScanSpan> relevant_chord_span(const OperationChordPlan& plan,
                                            std::size_t active_count, const MaskedSweepRow& row) {
  ScanSpan span{
      .first = std::max(row.window_first, plan.bounds.x0),
      .last = std::min(row.window_last, plan.bounds.x1 - 1),
  };
  if (span.empty()) {
    return std::nullopt;
  }
  if (active_count == 1U) {
    return span;
  }
  const std::size_t first =
      row.row + static_cast<std::size_t>(span.first - row.sweep.surface.level_bounds.x0);
  const std::size_t last =
      row.row + static_cast<std::size_t>(span.last - row.sweep.surface.level_bounds.x0);
  const auto window = mask_unset_window(row.sweep.finalized_pixels, first, last);
  row.sweep.slice.work_px += 4U;
  if (!window.has_value()) {
    return std::nullopt;
  }
  span.first += static_cast<int>(window->first - first);
  span.last -= static_cast<int>(last - window->last);
  return span;
}

int paint_masked_chord(const OperationChordPlan& plan, ScanSpan span, const MaskedSweepRow& row) {
  if (plan.constant) {
    return paint_masked_const_row(plan.segment, plan.seed, plan.bounds,
                                  {.y = row.y,
                                   .row = row.row,
                                   .window_first = span.first,
                                   .window_last = span.last,
                                   .color = applied_color(row.sweep.tool, row.sweep.color),
                                   .surface = row.sweep.surface,
                                   .finalized = row.sweep.finalized_pixels});
  }
  const ScanSpan conservative = conservative_row_span(plan.seed, plan.bounds, row.pixel_y);
  span.first = std::max(conservative.first, span.first);
  span.last = std::min(conservative.last, span.last);
  if (span.empty()) {
    TINYDRAW_V2_CENSUS_ADD(rows_empty_span, 1);
    return 0;
  }
  TINYDRAW_V2_CENSUS_ADD(rows_scanned, 1);
  TINYDRAW_V2_CENSUS_ADD(span_pixels, static_cast<std::uint64_t>(span.last - span.first + 1));
  return paint_masked_tapered_row(plan.segment, row.y, span,
                                  applied_color(row.sweep.tool, row.sweep.color), row.sweep.surface,
                                  row.sweep.finalized_pixels);
}

void paint_masked_sweep_row(const OperationChordPlan* plans, std::span<const std::uint8_t> active,
                            const MaskedOperationChordRowsRequest& sweep, int y) {
  const PixelRect x_bounds = active_row_bounds(plans, active, sweep.batch.clipped_bounds);
  const int surface_row = y - sweep.surface.level_bounds.y0;
  const std::size_t row =
      static_cast<std::size_t>(surface_row) * static_cast<std::size_t>(sweep.surface.stride);
  const std::size_t row_first =
      row + static_cast<std::size_t>(x_bounds.x0 - sweep.surface.level_bounds.x0);
  const std::size_t row_last =
      row + static_cast<std::size_t>(x_bounds.x1 - 1 - sweep.surface.level_bounds.x0);
  ++sweep.slice.rows_swept;
  const auto window = mask_unset_window(sweep.finalized_pixels, row_first, row_last);
  if (!window.has_value()) {
    TINYDRAW_V2_CENSUS_ADD(rows_prefinalized, 1);
    return;
  }
  sweep.slice.work_px += 4U;
  const MaskedSweepRow context{
      .sweep = sweep,
      .y = y,
      .surface_row = surface_row,
      .row = row,
      .window_first = x_bounds.x0 + static_cast<int>(window->first - row_first),
      .window_last = x_bounds.x0 + static_cast<int>(window->last - row_first),
      .pixel_y = static_cast<float>(y) + 0.5F,
  };
  for (const std::uint8_t index : active) {
    const auto span = relevant_chord_span(plans[index], active.size(), context);
    if (!span.has_value()) {
      continue;
    }
    sweep.slice.work_px += static_cast<std::size_t>(span->last - span->first + 1);
    const int finalized = paint_masked_chord(plans[index], *span, context);
    if (finalized != 0 && sweep.summary != nullptr) {
      sweep.summary->note_finalized(surface_row, finalized);
    }
  }
}

bool run_masked_sweep(const MaskedOperationChordRowsRequest& sweep) {
  if (!valid_masked_sweep(sweep)) {
    return false;
  }
  const OperationChordPlan* plans = chord_plans(sweep.chord_storage);
  if (try_bulk_masked_fill(sweep, plans)) {
    return true;
  }
  const std::uint8_t* order = chord_order(sweep.chord_storage);
  ActiveChords active;
  active.initialize(plans, order, sweep.batch.chord_count, sweep.first_row);
  int y = sweep.first_row;
  sweep.slice.rows_swept = 0;
  sweep.slice.work_px = 0;
  while (y < sweep.batch.clipped_bounds.y1 &&
         (sweep.slice.rows_swept == 0 || sweep.slice.work_px < sweep.max_work_px)) {
    active.update(plans, order, sweep.batch.chord_count, y);
    if (active.count == 0U) {
      if (active.enter >= sweep.batch.chord_count) {
        y = sweep.batch.clipped_bounds.y1;
        break;
      }
      y = plans[order[active.enter]].bounds.y0;
      continue;
    }
    paint_masked_sweep_row(plans, active.active(), sweep, y);
    ++y;
  }
  sweep.slice.next_row = y;
  return true;
}

}  // namespace

bool apply_masked_operation_chord_rows(const MaskedOperationChordRowsRequest& call) {
  return run_masked_sweep(call);
}

namespace {

bool valid_unmasked_sweep(const OperationChordSliceRequest& sweep) {
  const PixelRect bounds = sweep.batch.clipped_bounds;
  return valid_surface(sweep.surface) && sweep.batch.chord_count != 0U &&
         chord_storage_usable(sweep.chord_storage, sweep.batch.chord_count) &&
         sweep.max_work_px != 0U && sweep.cursor.next_row >= bounds.y0 &&
         sweep.cursor.next_row < bounds.y1 && bounds.x0 >= sweep.surface.level_bounds.x0 &&
         bounds.y0 >= sweep.surface.level_bounds.y0 && bounds.x1 <= sweep.surface.level_bounds.x1 &&
         bounds.y1 <= sweep.surface.level_bounds.y1;
}

struct RowChords {
  std::array<std::uint8_t, kOperationChordCapacity> active;
  std::size_t count = 0;
  std::size_t entered = 0;
};

RowChords chords_for_row(const OperationChordPlan* plans, const std::uint8_t* order,
                         std::size_t chord_count, int y) {
  RowChords row;
  while (row.entered < chord_count && plans[order[row.entered]].bounds.y0 <= y) {
    if (plans[order[row.entered]].bounds.y1 > y) {
      row.active[row.count++] = order[row.entered];
    }
    ++row.entered;
  }
  return row;
}

void paint_unmasked_tapered_chord(const OperationChordPlan& plan, ScanSpan span, float pixel_y,
                                  std::size_t row, std::uint16_t color,
                                  const RasterSurface& surface) {
  const PixelCoverageRow coverage = make_pixel_coverage_row(plan.segment, pixel_y);
  for (int x = span.first; x <= span.last; ++x) {
    if (covers_pixel(coverage, pixel_center(x))) {
      surface.pixels[row + static_cast<std::size_t>(x - surface.level_bounds.x0)] = color;
    }
  }
}

std::size_t paint_unmasked_chord(const OperationChordPlan& plan, int y, std::size_t row,
                                 std::uint16_t color, const RasterSurface& surface) {
  const float pixel_y = static_cast<float>(y) + 0.5F;
  const ScanSpan span = conservative_row_span(plan.seed, plan.bounds, pixel_y);
  if (span.empty()) {
    return 0U;
  }
  if (!plan.constant) {
    paint_unmasked_tapered_chord(plan, span, pixel_y, row, color, surface);
    return static_cast<std::size_t>(span.last - span.first) + 1U;
  }
  const int first = first_covered_at_or_after(plan.segment, span.first, span.last, pixel_y);
  if (first <= span.last) {
    const int last = last_covered_at_or_before(plan.segment, first, span.last, pixel_y);
    const auto begin = surface.pixels.begin() +
                       static_cast<std::ptrdiff_t>(
                           row + static_cast<std::size_t>(first - surface.level_bounds.x0));
    std::fill_n(begin, static_cast<std::size_t>(last - first) + 1U, color);
  }
  return static_cast<std::size_t>(span.last - span.first) + 1U;
}

bool run_unmasked_sweep(const OperationChordSliceRequest& sweep) {
  if (!valid_unmasked_sweep(sweep)) {
    return false;
  }
  const PixelRect bounds = sweep.batch.clipped_bounds;
  const OperationChordPlan* plans = chord_plans(sweep.chord_storage);
  const std::uint8_t* order = chord_order(sweep.chord_storage);
  sweep.slice.rows_swept = 0;
  sweep.slice.work_px = 0;
  while (sweep.cursor.next_row < bounds.y1) {
    const int y = sweep.cursor.next_row;
    const RowChords row_chords = chords_for_row(plans, order, sweep.batch.chord_count, y);
    if (row_chords.count == 0U) {
      if (row_chords.entered >= sweep.batch.chord_count) {
        sweep.cursor.next_row = bounds.y1;
        sweep.cursor.next_chord = 0U;
        break;
      }
      sweep.cursor.next_row = plans[order[row_chords.entered]].bounds.y0;
      sweep.cursor.next_chord = 0U;
      continue;
    }
    if (sweep.cursor.next_chord >= row_chords.count) {
      return false;
    }
    const std::size_t row = static_cast<std::size_t>(y - sweep.surface.level_bounds.y0) *
                            static_cast<std::size_t>(sweep.surface.stride);
    for (std::size_t index = sweep.cursor.next_chord; index < row_chords.count; ++index) {
      sweep.slice.work_px +=
          paint_unmasked_chord(plans[row_chords.active[index]], y, row,
                               applied_color(sweep.tool, sweep.color), sweep.surface);
      sweep.cursor.next_chord = index + 1U;
      if (sweep.slice.work_px >= sweep.max_work_px && sweep.cursor.next_chord < row_chords.count) {
        sweep.slice.next_row = sweep.cursor.next_row;
        return true;
      }
    }
    ++sweep.slice.rows_swept;
    ++sweep.cursor.next_row;
    sweep.cursor.next_chord = 0U;
    break;
  }
  sweep.slice.next_row = sweep.cursor.next_row;
  return true;
}

}  // namespace

bool apply_operation_chord_slice(const OperationChordSliceRequest& call) {
  return run_unmasked_sweep(call);
}

}  // namespace tinydraw::vector_v2
