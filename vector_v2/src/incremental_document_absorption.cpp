#include <algorithm>

#include "incremental_document_internal.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/operation_builder.h"

namespace tinydraw::vector_v2 {

using incremental_document_internal::in_priority_view;
using incremental_document_internal::in_recent_view;
using incremental_document_internal::valid_in_place_workspace;
using incremental_document_internal::valid_priority_view;

class PendingOperationAbsorption::Context {
 public:
  static PendingAbsorptionSliceResult execute(const PendingAbsorptionRequest& request) {
    if (request.limit.raster_work_px == 0U) {
      return {.status = PendingAbsorptionStatus::kError};
    }
    if (request.state.active()) {
      if (!resume_matches(request)) {
        return {.status = PendingAbsorptionStatus::kError};
      }
    } else if (const auto initialized = initialize(request); initialized.has_value()) {
      return *initialized;
    }
    return Context(request.canvas, request.state, request.limit).run();
  }

  Context(MaterializedCanvas& canvas, PendingOperationAbsorption& state, CooperativeWorkLimit limit)
      : canvas_(canvas),
        state_(state),
        limit_(limit),
        operation_{.tool = state.operation_.tool,
                   .color = state.operation_.color,
                   .samples = state.operation_.samples},
        painted_color_(operation_.tool == OperationTool::kEraser ? 0xFFFFU : operation_.color) {}

  PendingAbsorptionSliceResult run() {
    while (true) {
      ++output_.checkpoints;
      if (limit_.yield_requested()) {
        output_.status = PendingAbsorptionStatus::kInProgress;
        return output_;
      }
      const std::int64_t unit_started_us = stamp();
      if (run_phase(unit_started_us)) {
        return output_;
      }
    }
  }

 private:
  template <typename T>
  static bool same_span(std::span<T> left, std::span<T> right) {
    return left.data() == right.data() && left.size() == right.size();
  }

  static bool resume_matches(const PendingAbsorptionRequest& request) {
    const PendingOperationAbsorption& state = request.state;
    return state.log_ == &request.log && state.canvas_ == &request.canvas &&
           same_span(state.workspace_.overview_scratch, request.workspace.overview_scratch) &&
           same_span(state.workspace_.affected_keys, request.workspace.affected_keys) &&
           same_span(state.workspace_.tile_mask, request.workspace.tile_mask) &&
           same_span(state.workspace_.operation_chord_plans,
                     request.workspace.operation_chord_plans) &&
           state.priority_view_ == request.priority_view &&
           state.retention_.now_us == request.retention.now_us &&
           state.retention_.budget_us == request.retention.budget_us;
  }

  static std::optional<PendingAbsorptionSliceResult> initialize(
      const PendingAbsorptionRequest& request) {
    const std::int64_t prepare_started_us =
        request.retention.now_us != nullptr ? request.retention.now_us() : 0;
    if (!initial_request_valid(request)) {
      return PendingAbsorptionSliceResult{.status = PendingAbsorptionStatus::kError};
    }
    const auto range = request.log.replay_range(
        request.log.epoch(), request.canvas.current_revision(), request.log.current_revision());
    if (!range.has_value()) {
      return PendingAbsorptionSliceResult{.status = PendingAbsorptionStatus::kError};
    }
    if (range->operation_count == 0U) {
      return PendingAbsorptionSliceResult{.status = PendingAbsorptionStatus::kIdle};
    }
    const auto stored = request.log.operation(range->first_operation);
    if (!stored.has_value()) {
      return PendingAbsorptionSliceResult{.status = PendingAbsorptionStatus::kError};
    }
    PendingOperationAbsorption& state = request.state;
    state.log_ = &request.log;
    state.canvas_ = &request.canvas;
    state.operation_ = *stored;
    state.workspace_ = request.workspace;
    state.priority_view_ = request.priority_view;
    state.retention_ = request.retention;
    state.overview_bounds_ = overview_bounds_for_world(stored->world_bounds);
    const int width = state.overview_bounds_.x1 - state.overview_bounds_.x0;
    const int height = state.overview_bounds_.y1 - state.overview_bounds_.y0;
    const std::size_t pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0 || request.workspace.overview_scratch.size() < pixel_count) {
      reset_state(state);
      return PendingAbsorptionSliceResult{.status = PendingAbsorptionStatus::kError};
    }
    state.overview_publication_ = {.bounds = state.overview_bounds_,
                                   .pixels = request.workspace.overview_scratch.first(pixel_count)};
    state.phases_.prepare_us =
        (state.retention_.now_us != nullptr ? state.retention_.now_us() : 0) - prepare_started_us;
    state.phase_ = Phase::kCopyOverview;
    return std::nullopt;
  }

  static bool initial_request_valid(const PendingAbsorptionRequest& request) {
    const bool chord_storage_ready =
        request.workspace.operation_chord_plans.size() >= kOperationChordStorageBytes &&
        reinterpret_cast<std::uintptr_t>(request.workspace.operation_chord_plans.data()) %
                kPreparedOperationChordAlign ==
            0U;
    return request.canvas.ready() && request.log.ready() && chord_storage_ready &&
           valid_in_place_workspace(request.log, request.canvas, request.workspace) &&
           request.canvas.overview_pixels().size() == kOverviewPixels &&
           valid_priority_view(request.priority_view);
  }

  static void reset_state(PendingOperationAbsorption& state) {
    state.overview_stage_.cancel();
    state = PendingOperationAbsorption{};
  }

  void reset() { reset_state(state_); }

  bool fail() {
    reset();
    output_.status = PendingAbsorptionStatus::kError;
    return true;
  }

  [[nodiscard]] std::int64_t stamp() const {
    return state_.retention_.now_us != nullptr ? state_.retention_.now_us() : 0;
  }

  [[nodiscard]] bool over_retention_budget() const {
    if (state_.retention_.now_us == nullptr) {
      return false;
    }
    const InPlaceAppendPhases& phases = state_.phases_;
    std::int64_t remaining_us = state_.retention_.budget_us;
    for (const std::int64_t phase_us :
         {phases.prepare_us, phases.overview_us, phases.enumerate_us, phases.uniform_retain_us,
          phases.raw_retain_us, phases.offscreen_retain_us}) {
      if (remaining_us <= 0 || phase_us >= remaining_us) {
        return true;
      }
      remaining_us -= std::max<std::int64_t>(0, phase_us);
    }
    return false;
  }

  bool begin_raster(const RasterSurface& surface, bool masked) {
    state_.next_endpoint_ = operation_.samples.size() - 1U;
    state_.batch_ready_ = false;
    if (!masked) {
      return true;
    }
    const std::size_t mask_bytes = (surface.pixels.size() + 7U) / 8U;
    if (state_.workspace_.tile_mask.size() < mask_bytes) {
      return false;
    }
    std::fill_n(state_.workspace_.tile_mask.begin(), mask_bytes, std::uint8_t{0});
    return true;
  }

  // Returns -1 on invalid replay state, 0 after one bounded quantum, and 1
  // once every chord batch for this surface is complete.
  int raster_quantum(const RasterSurface& surface, bool masked) {
    while (!state_.batch_ready_) {
      const auto prepared = prepare_operation_chord_batch(
          operation_.samples, state_.next_endpoint_, surface.zoom, surface.level_bounds,
          state_.workspace_.operation_chord_plans.first(kOperationChordStorageBytes));
      if (!prepared.has_value()) {
        return -1;
      }
      state_.chord_batch_ = *prepared;
      state_.next_endpoint_ = prepared->next_endpoint;
      if (prepared->chord_count == 0U) {
        if (state_.next_endpoint_ == 0U) {
          return 1;
        }
        continue;
      }
      state_.raster_cursor_ = {.next_row = prepared->clipped_bounds.y0};
      state_.batch_ready_ = true;
    }
    OperationSweepSlice slice{};
    const bool okay =
        masked
            ? apply_masked_operation_chord_rows(
                  {.tool = operation_.tool,
                   .color = operation_.color,
                   .chord_storage = state_.workspace_.operation_chord_plans,
                   .batch = state_.chord_batch_,
                   .first_row = state_.raster_cursor_.next_row,
                   .max_work_px = limit_.raster_work_px,
                   .surface = surface,
                   .finalized_pixels = state_.workspace_.tile_mask,
                   .summary = nullptr,
                   .slice = slice})
            : apply_operation_chord_slice({.tool = operation_.tool,
                                           .color = operation_.color,
                                           .chord_storage = state_.workspace_.operation_chord_plans,
                                           .batch = state_.chord_batch_,
                                           .max_work_px = limit_.raster_work_px,
                                           .surface = surface,
                                           .cursor = state_.raster_cursor_,
                                           .slice = slice});
    if (!okay) {
      return -1;
    }
    if (masked) {
      state_.raster_cursor_ = {.next_row = slice.next_row};
    }
    if (state_.raster_cursor_.next_row >= state_.chord_batch_.clipped_bounds.y1) {
      state_.batch_ready_ = false;
      return state_.next_endpoint_ == 0U ? 1 : 0;
    }
    return 0;
  }

  [[nodiscard]] RasterSurface tile_surface() const {
    const InPlaceTileEdit& edit = state_.tile_edit_;
    return {
        .zoom = edit.key.zoom,
        .level_bounds = edit.bounds,
        .pixels = edit.pixels.first(static_cast<std::size_t>(edit.bounds.y1 - edit.bounds.y0 - 1) *
                                        kTileWidth +
                                    static_cast<std::size_t>(edit.bounds.x1 - edit.bounds.x0)),
        .stride = kTileWidth,
    };
  }

  bool start_tile(const InPlaceTileEdit& edit, TileKey key) {
    state_.tile_edit_ = edit;
    state_.tile_key_ = key;
    state_.painting_tile_ = true;
    return begin_raster(tile_surface(), true);
  }

  bool run_phase(std::int64_t unit_started_us) {
    switch (state_.phase_) {
      case Phase::kIdle:
        output_.status = PendingAbsorptionStatus::kError;
        return true;
      case Phase::kCopyOverview:
        return copy_overview(unit_started_us);
      case Phase::kRasterOverview:
        return raster_overview(unit_started_us);
      case Phase::kEnumerate:
        return enumerate(unit_started_us);
      case Phase::kUniform:
        return retain_uniform(unit_started_us);
      case Phase::kVisibleRaw:
        return retain_visible_raw(unit_started_us);
      case Phase::kOffscreenRaw:
        return retain_offscreen_raw(unit_started_us);
      case Phase::kStageOverview:
        return stage_overview(unit_started_us);
      case Phase::kStageMetadata:
        return stage_metadata(unit_started_us);
      case Phase::kCommit:
        return commit(unit_started_us);
    }
    return fail();
  }

  bool copy_overview(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kCopyOverview;
    const int width = state_.overview_bounds_.x1 - state_.overview_bounds_.x0;
    const int height = state_.overview_bounds_.y1 - state_.overview_bounds_.y0;
    const auto source = canvas_.overview_pixels().begin() +
                        static_cast<std::ptrdiff_t>(state_.overview_bounds_.y0 +
                                                    static_cast<int>(state_.copy_row_)) *
                            kOverviewWidth +
                        state_.overview_bounds_.x0;
    const auto destination = state_.workspace_.overview_scratch.begin() +
                             static_cast<std::ptrdiff_t>(state_.copy_row_) * width;
    std::copy_n(source, width, destination);
    ++state_.copy_row_;
    if (state_.copy_row_ == static_cast<std::size_t>(height)) {
      const RasterSurface surface{.zoom = ZoomLevel::k25Percent,
                                  .level_bounds = state_.overview_bounds_,
                                  .pixels = state_.workspace_.overview_scratch.first(
                                      state_.overview_publication_.pixels.size()),
                                  .stride = width};
      if (!begin_raster(surface, false)) {
        return fail();
      }
      state_.phase_ = Phase::kRasterOverview;
    }
    state_.phases_.overview_us += stamp() - started_us;
    return false;
  }

  bool raster_overview(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kRasterOverview;
    const RasterSurface surface{.zoom = ZoomLevel::k25Percent,
                                .level_bounds = state_.overview_bounds_,
                                .pixels = state_.workspace_.overview_scratch.first(
                                    state_.overview_publication_.pixels.size()),
                                .stride = state_.overview_bounds_.x1 - state_.overview_bounds_.x0};
    const int replayed = raster_quantum(surface, false);
    state_.phases_.overview_us += stamp() - started_us;
    if (replayed < 0) {
      return fail();
    }
    if (replayed > 0) {
      state_.phase_ = Phase::kEnumerate;
    }
    return false;
  }

  bool enumerate(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kEnumerate;
    const auto resident_count = canvas_.materialized_tiles_intersecting(
        state_.operation_.world_bounds, state_.workspace_.affected_keys, state_.priority_view_,
        false);
    if (!resident_count.has_value() ||
        !canvas_.can_edit_in_place_revision(state_.operation_.identity.revision,
                                            state_.overview_publication_,
                                            state_.operation_.world_bounds)) {
      return fail();
    }
    state_.affected_count_ = *resident_count;
    const auto extended = canvas_.append_recent_view_uniform_keys(
        state_.operation_.world_bounds,
        state_.priority_view_.has_value() ? std::optional{state_.priority_view_->zoom}
                                          : std::nullopt,
        state_.workspace_.affected_keys, state_.affected_count_);
    if (extended.has_value()) {
      state_.affected_count_ = *extended;
    }
    state_.scan_index_ = 0U;
    state_.phases_.enumerate_us += stamp() - started_us;
    state_.phase_ = Phase::kUniform;
    return false;
  }

  void finish_tile(std::span<TileKey> affected) {
    std::swap(affected[state_.scan_index_], affected[state_.retained_count_++]);
    state_.painting_tile_ = false;
    ++state_.scan_index_;
  }

  bool retain_uniform(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kUniform;
    auto affected = state_.workspace_.affected_keys.first(state_.affected_count_);
    if (state_.painting_tile_) {
      const int replayed = raster_quantum(tile_surface(), true);
      if (replayed < 0) {
        canvas_.invalidate_identity(state_.tile_key_);
        in_priority_view(state_.tile_key_, state_.priority_view_)
            ? ++state_.drops_.visible_uniform_paint_fail
            : ++state_.drops_.offscreen_skipped;
        state_.painting_tile_ = false;
        ++state_.scan_index_;
      } else if (replayed > 0) {
        finish_tile(affected);
      }
    } else if (state_.scan_index_ == state_.affected_count_) {
      state_.scan_index_ = state_.retained_count_;
      state_.phase_ = Phase::kVisibleRaw;
    } else {
      consider_uniform(affected);
    }
    state_.phases_.uniform_retain_us += stamp() - started_us;
    return false;
  }

  void consider_uniform(std::span<TileKey> affected) {
    const TileKey key = affected[state_.scan_index_];
    const auto color = canvas_.uniform_color(key);
    if (!color.has_value()) {
      ++state_.scan_index_;
      return;
    }
    if (*color == painted_color_) {
      std::swap(affected[state_.scan_index_++], affected[state_.retained_count_++]);
      return;
    }
    const bool visible = in_priority_view(key, state_.priority_view_);
    const bool recent = in_recent_view(canvas_, key);
    if (!visible && (!recent || over_retention_budget())) {
      if (state_.priority_view_.has_value() && key.zoom == state_.priority_view_->zoom) {
        ++state_.drops_.offscreen_skipped;
      }
      ++state_.scan_index_;
      return;
    }
    const auto edit = canvas_.materialize_uniform_as_raw(key);
    if (!edit.has_value()) {
      visible ? ++state_.drops_.visible_uniform_no_slot : ++state_.drops_.offscreen_skipped;
      ++state_.scan_index_;
    } else if (!start_tile(*edit, key)) {
      canvas_.invalidate_identity(key);
      ++state_.scan_index_;
    }
  }

  bool retain_visible_raw(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kVisibleRaw;
    auto affected = state_.workspace_.affected_keys.first(state_.affected_count_);
    if (state_.painting_tile_) {
      const int replayed = raster_quantum(tile_surface(), true);
      if (replayed < 0) {
        canvas_.invalidate_identity(state_.tile_key_);
        ++state_.drops_.visible_raw_paint_fail;
        state_.painting_tile_ = false;
        ++state_.scan_index_;
      } else if (replayed > 0) {
        finish_tile(affected);
      }
    } else if (state_.scan_index_ == state_.affected_count_) {
      state_.scan_index_ = state_.retained_count_;
      state_.phase_ = Phase::kOffscreenRaw;
    } else {
      const TileKey key = affected[state_.scan_index_];
      if (!in_priority_view(key, state_.priority_view_)) {
        ++state_.scan_index_;
      } else {
        const auto edit = canvas_.edit_resident_tile(key);
        if (!edit.has_value()) {
          ++state_.drops_.visible_raw_edit_fail;
          ++state_.scan_index_;
        } else if (!start_tile(*edit, key)) {
          canvas_.invalidate_identity(key);
          ++state_.drops_.visible_raw_paint_fail;
          ++state_.scan_index_;
        }
      }
    }
    state_.phases_.raw_retain_us += stamp() - started_us;
    return false;
  }

  bool retain_offscreen_raw(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kOffscreenRaw;
    auto affected = state_.workspace_.affected_keys.first(state_.affected_count_);
    if (state_.painting_tile_) {
      const int replayed = raster_quantum(tile_surface(), true);
      if (replayed < 0) {
        canvas_.invalidate_identity(state_.tile_key_);
        ++state_.drops_.offscreen_skipped;
        state_.painting_tile_ = false;
        ++state_.scan_index_;
      } else if (replayed > 0) {
        finish_tile(affected);
      }
    } else if (state_.scan_index_ == state_.affected_count_) {
      state_.phase_ = Phase::kStageOverview;
    } else {
      const TileKey key = affected[state_.scan_index_];
      if (in_priority_view(key, state_.priority_view_)) {
        ++state_.scan_index_;
      } else if (over_retention_budget()) {
        ++state_.drops_.offscreen_skipped;
        ++state_.scan_index_;
      } else {
        const auto edit = canvas_.edit_resident_tile(key);
        if (!edit.has_value()) {
          ++state_.drops_.offscreen_skipped;
          ++state_.scan_index_;
        } else if (!start_tile(*edit, key)) {
          canvas_.invalidate_identity(key);
          ++state_.drops_.offscreen_skipped;
          ++state_.scan_index_;
        }
      }
    }
    state_.phases_.offscreen_retain_us += stamp() - started_us;
    return false;
  }

  bool stage_overview(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kStageOverview;
    const OverviewStageStatus staged = canvas_.stage_in_place_overview_rows(
        state_.operation_.identity.revision, state_.overview_publication_,
        state_.operation_.world_bounds, 1U, state_.overview_stage_);
    state_.phases_.overview_us += stamp() - started_us;
    if (staged == OverviewStageStatus::kError) {
      return fail();
    }
    if (staged == OverviewStageStatus::kComplete) {
      state_.phase_ = Phase::kStageMetadata;
    }
    return false;
  }

  void set_metadata_work_unit(InPlaceMetadataPhase phase) {
    switch (phase) {
      case InPlaceMetadataPhase::kUniforms:
        output_.work_unit = PendingAbsorptionWorkUnit::kStageUniforms;
        break;
      case InPlaceMetadataPhase::kRawSlots:
        output_.work_unit = PendingAbsorptionWorkUnit::kStageRawSlots;
        break;
      case InPlaceMetadataPhase::kRerenderDamage:
        output_.work_unit = PendingAbsorptionWorkUnit::kStageRerenderDamage;
        break;
      case InPlaceMetadataPhase::kOccupancy:
        output_.work_unit = PendingAbsorptionWorkUnit::kStageOccupancy;
        break;
      case InPlaceMetadataPhase::kComplete:
        output_.work_unit = PendingAbsorptionWorkUnit::kCommit;
        break;
    }
  }

  bool stage_metadata(std::int64_t started_us) {
    auto affected = state_.workspace_.affected_keys.first(state_.affected_count_);
    const MaterializedCanvas::InPlaceCommitScope scope{
        .preserved_uniform_color = painted_color_,
        .priority_zoom = state_.priority_view_.has_value()
                             ? std::optional{state_.priority_view_->zoom}
                             : std::nullopt,
        .cross_zoom_invalidated = nullptr,
    };
    const InPlaceMetadataSlice staged =
        canvas_.stage_in_place_metadata({.revision = state_.operation_.identity.revision,
                                         .overview_publication = state_.overview_publication_,
                                         .affected_world_bounds = state_.operation_.world_bounds,
                                         .retained_keys = affected.first(state_.retained_count_),
                                         .max_work_items = limit_.raster_work_px,
                                         .stage = state_.overview_stage_,
                                         .scope = scope});
    set_metadata_work_unit(staged.phase);
    state_.phases_.commit_us += stamp() - started_us;
    if (staged.status == OverviewStageStatus::kError) {
      return fail();
    }
    if (staged.status == OverviewStageStatus::kComplete) {
      state_.phase_ = Phase::kCommit;
    }
    return false;
  }

  bool commit(std::int64_t started_us) {
    output_.work_unit = PendingAbsorptionWorkUnit::kCommit;
    auto affected = state_.workspace_.affected_keys.first(state_.affected_count_);
    std::size_t visible_fallback = 0;
    for (std::size_t index = state_.retained_count_; index < affected.size(); ++index) {
      visible_fallback += in_priority_view(affected[index], state_.priority_view_) ? 1U : 0U;
    }
    std::size_t cross_zoom_invalidated = 0;
    const MaterializedCanvas::InPlaceCommitScope commit_scope{
        .preserved_uniform_color = painted_color_,
        .priority_zoom = state_.priority_view_.has_value()
                             ? std::optional{state_.priority_view_->zoom}
                             : std::nullopt,
        .cross_zoom_invalidated = &cross_zoom_invalidated,
    };
    if (!canvas_.commit_staged_in_place_revision(
            state_.operation_.identity.revision, state_.overview_publication_,
            state_.operation_.world_bounds, affected.first(state_.retained_count_),
            state_.overview_stage_, commit_scope)) {
      for (const TileKey key : affected.first(state_.retained_count_)) {
        canvas_.invalidate_identity(key);
      }
      return fail();
    }
    state_.phases_.commit_us += stamp() - started_us;
    output_.result = {.identity = state_.operation_.identity,
                      .affected_world_bounds = state_.operation_.world_bounds,
                      .affected_resident_tiles = state_.affected_count_,
                      .published_tiles = state_.retained_count_,
                      .fallback_tiles = state_.affected_count_ - state_.retained_count_,
                      .visible_fallback_tiles = visible_fallback,
                      .cross_zoom_invalidated = cross_zoom_invalidated,
                      .phases = state_.phases_,
                      .drops = state_.drops_};
    reset();
    output_.status = PendingAbsorptionStatus::kComplete;
    return true;
  }

  MaterializedCanvas& canvas_;
  PendingOperationAbsorption& state_;
  CooperativeWorkLimit limit_;
  OperationAppend operation_;
  std::uint16_t painted_color_;
  PendingAbsorptionSliceResult output_{};
};

PendingAbsorptionSliceResult absorb_pending_operation_slice(
    const PendingAbsorptionRequest& request) {
  return PendingOperationAbsorption::Context::execute(request);
}

}  // namespace tinydraw::vector_v2
