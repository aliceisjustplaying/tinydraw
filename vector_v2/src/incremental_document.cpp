#include "tinydraw/vector_v2/incremental_document.h"

#include <algorithm>
#include <array>
#include <limits>

#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {

void PendingOperationAbsorption::cancel() {
  overview_stage_.cancel();
  *this = PendingOperationAbsorption{};
}
namespace {

bool prepare_overview(const MaterializedCanvas& canvas, const OperationAppend& operation,
                      PixelRect world_bounds, std::span<std::uint16_t> scratch,
                      OverviewRevisionPublication& publication) {
  const PixelRect bounds = overview_bounds_for_world(world_bounds);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (scratch.size() < pixel_count) {
    return false;
  }
  const auto pixels = scratch.first(pixel_count);
  for (int row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::ptrdiff_t>(bounds.y0 + row) * kOverviewWidth +
                               static_cast<std::ptrdiff_t>(bounds.x0);
    const auto destination_offset =
        static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(width);
    const auto source = canvas.overview_pixels().begin() + source_offset;
    std::copy_n(source, width, pixels.begin() + destination_offset);
  }
  if (!apply_incremental_operation(operation, {.zoom = ZoomLevel::k25Percent,
                                               .level_bounds = bounds,
                                               .pixels = pixels,
                                               .stride = width})) {
    return false;
  }
  publication = {.bounds = bounds, .pixels = pixels};
  return true;
}

bool intersects(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

bool valid_priority_view(const std::optional<ViewRequest>& view) {
  if (!view.has_value()) {
    return true;
  }
  const PixelRect bounds = view->level_pixels;
  return view->zoom != ZoomLevel::k25Percent && bounds.x0 >= 0 && bounds.y0 >= 0 &&
         bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kWorldWidth * zoom_percent(view->zoom) / 100 &&
         bounds.y1 <= kWorldHeight * zoom_percent(view->zoom) / 100;
}

bool in_priority_view(TileKey key, const std::optional<ViewRequest>& view) {
  return view.has_value() && key.zoom == view->zoom &&
         intersects(tile_pixel_bounds(key), view->level_pixels);
}

}  // namespace

namespace {

// Paints exactly the operation's curved covered pixels into one tile.
bool paint_operation_into_tile(const OperationAppend& operation, const InPlaceTileEdit& edit,
                               std::span<std::uint8_t> tile_mask) {
  const RasterSurface surface{
      .zoom = edit.key.zoom,
      .level_bounds = edit.bounds,
      .pixels = edit.pixels.first(static_cast<std::size_t>(edit.bounds.y1 - edit.bounds.y0 - 1) *
                                      kTileWidth +
                                  static_cast<std::size_t>(edit.bounds.x1 - edit.bounds.x0)),
      .stride = kTileWidth,
  };
  const std::size_t mask_bytes = (surface.pixels.size() + 7U) / 8U;
  std::fill_n(tile_mask.begin(), mask_bytes, std::uint8_t{0});
  return apply_masked_incremental_operation(operation, surface, tile_mask);
}

bool valid_in_place_workspace(const OperationLog& log, const MaterializedCanvas& canvas,
                              const InPlaceAppendWorkspace& workspace) {
  const std::array<std::span<const std::byte>, 4> workspaces{
      std::as_bytes(workspace.overview_scratch), std::as_bytes(workspace.affected_keys),
      std::as_bytes(workspace.tile_mask), workspace.operation_chord_plans};
  bool workspace_invalid = workspace.tile_mask.size() < kInPlaceTileMaskBytes;
  for (std::size_t left = 0; left < workspaces.size(); ++left) {
    workspace_invalid = workspace_invalid || !canvas.accepts_external_workspace(workspaces[left]) ||
                        log.workspace_overlaps_storage(workspaces[left]);
    for (std::size_t right = left + 1U; right < workspaces.size(); ++right) {
      workspace_invalid =
          workspace_invalid || storage_overlaps(workspaces[left], workspaces[right]);
    }
  }
  return !workspace_invalid;
}

// Mutation phase of the in-place append: no step may abandon the commit. A
// tile that cannot be updated is simply not retained and becomes correct
// overview fallback. Retained keys are swap-partitioned into the prefix of
// the enumeration so no key is lost or visited twice; uniform conversions run
// first so their slot eviction can never pick a raw tile edited earlier in
// this same commit. Returns the retained-prefix length.
//
// Mutation is bounded to the active zoom. Painting every intersecting
// resident raw tile at every zoom made warm-cache interactive chunk commits
// reach 130 ms at 25% (700-960 tiles per stroke); only tiles at the priority
// view's zoom are painted in place, and affected tiles at other zooms are
// dropped by the commit and re-produced lazily on their next visit. Matching
// same-color uniforms are still retained at every zoom because retention is
// free. Without a priority view (drawing over the 25% overview) no raw tile
// is painted at all.
struct InPlaceRetainScope {
  const OperationAppend& operation;
  std::uint16_t painted_color;
  const std::optional<ViewRequest>& priority_view;
  const InPlaceRetentionBudget& budget;
  std::int64_t deadline_us;
  // Déjà-vu fix (owner 2026-08-16): idle absorption retains resident raw
  // tiles at every zoom — the synchronous-cost argument that justified
  // dropping cross-zoom tiles died with the committed overlay. The
  // input-path fallback keeps the priority-only scope.
  bool retain_all_zooms = false;
  [[nodiscard]] bool over_budget() const {
    return budget.now_us != nullptr && budget.now_us() >= deadline_us;
  }
};

// True when the tile lies inside the remembered viewport at its zoom — the
// views a zoom return lands on, which is exactly the déjà-vu revisit
// population.
bool in_recent_view(const MaterializedCanvas& canvas, TileKey key) {
  for (const ViewFootprint& view : canvas.recent_views()) {
    if (!view.valid || view.zoom != key.zoom) {
      continue;
    }
    const PixelRect bounds = tile_pixel_bounds(key);
    if (bounds.x0 < view.level_pixels.x1 && view.level_pixels.x0 < bounds.x1 &&
        bounds.y0 < view.level_pixels.y1 && view.level_pixels.y0 < bounds.y1) {
      return true;
    }
  }
  return false;
}

// Uniform pass: same-color uniforms retain for free at every zoom; other
// affected uniforms materialize as raw and paint inside the priority view
// (budget-exempt: dropping one blurs pixels the user is looking at), and —
// during all-zoom absorption — inside remembered views at other zooms
// (budget-bounded; a skip falls back to the old drop-and-repair behavior).
std::size_t retain_uniform_tiles(MaterializedCanvas& canvas, const InPlaceRetainScope& scope,
                                 std::span<TileKey> affected, std::span<std::uint8_t> tile_mask,
                                 InPlaceRetainDrops& drops) {
  std::size_t retained = 0;
  for (std::size_t index = 0; index < affected.size(); ++index) {
    const TileKey key = affected[index];
    const auto color = canvas.uniform_color(key);
    if (!color.has_value()) {
      continue;
    }
    bool keep = false;
    if (*color == scope.painted_color) {
      // Painting this color over an identical uniform is a no-op; retain it.
      keep = true;
    } else if (in_priority_view(key, scope.priority_view)) {
      const auto edit = canvas.materialize_uniform_as_raw(key);
      if (edit.has_value() && paint_operation_into_tile(scope.operation, *edit, tile_mask)) {
        keep = true;
      } else if (edit.has_value()) {
        canvas.invalidate_identity(key);
        ++drops.visible_uniform_paint_fail;
      } else {
        ++drops.visible_uniform_no_slot;
      }
    } else if (scope.retain_all_zooms && in_recent_view(canvas, key) && !scope.over_budget()) {
      // Déjà-vu fix: a zoom return lands on this tile; materializing it now
      // (idle time) beats re-rendering it in front of the user later.
      const auto edit = canvas.materialize_uniform_as_raw(key);
      if (edit.has_value() && paint_operation_into_tile(scope.operation, *edit, tile_mask)) {
        keep = true;
      } else {
        if (edit.has_value()) {
          canvas.invalidate_identity(key);
        }
        ++drops.offscreen_skipped;
      }
    } else if (scope.priority_view.has_value() && key.zoom == scope.priority_view->zoom) {
      ++drops.offscreen_skipped;
    }
    if (keep) {
      std::swap(affected[index], affected[retained]);
      ++retained;
    }
  }
  return retained;
}

// Visible raw phase (§8.4 RetainVisibleRawTiles): resident raw tiles inside
// the priority view always paint in place — the viewport bounds them and
// dropping one is a visible blur, rejected on glass. A failed paint
// invalidates the identity.
std::size_t retain_visible_raw_tiles(MaterializedCanvas& canvas, const InPlaceRetainScope& scope,
                                     std::span<TileKey> affected, std::span<std::uint8_t> tile_mask,
                                     std::size_t retained, InPlaceRetainDrops& drops) {
  for (std::size_t index = retained; index < affected.size(); ++index) {
    const TileKey key = affected[index];
    if (!scope.priority_view.has_value() || key.zoom != scope.priority_view->zoom ||
        !in_priority_view(key, scope.priority_view)) {
      continue;
    }
    const auto edit = canvas.edit_resident_tile(key);
    if (!edit.has_value()) {
      ++drops.visible_raw_edit_fail;
      continue;
    }
    if (paint_operation_into_tile(scope.operation, *edit, tile_mask)) {
      std::swap(affected[index], affected[retained]);
      ++retained;
    } else {
      canvas.invalidate_identity(key);
      ++drops.visible_raw_paint_fail;
    }
  }
  return retained;
}

// Offscreen raw phase (§8.4 RetainOffscreenWithinBudget): active-zoom tiles
// outside the priority view paint within the remaining budget and otherwise
// drop for idle repair to rebuild. Cross-zoom tiles are not painted at all;
// their drops are counted by cross_zoom_invalidated at commit.
std::size_t retain_offscreen_raw_tiles(MaterializedCanvas& canvas, const InPlaceRetainScope& scope,
                                       std::span<TileKey> affected,
                                       std::span<std::uint8_t> tile_mask, std::size_t retained,
                                       InPlaceRetainDrops& drops) {
  for (std::size_t index = retained; index < affected.size(); ++index) {
    const TileKey key = affected[index];
    const bool active_zoom =
        scope.priority_view.has_value() && key.zoom == scope.priority_view->zoom;
    if (active_zoom && in_priority_view(key, scope.priority_view)) {
      continue;  // Already handled by the visible pass.
    }
    if (!active_zoom && !scope.retain_all_zooms) {
      continue;  // Cross-zoom drops are counted by cross_zoom_invalidated.
    }
    if (scope.over_budget()) {
      ++drops.offscreen_skipped;
      continue;
    }
    const auto edit = canvas.edit_resident_tile(key);
    if (!edit.has_value()) {
      ++drops.offscreen_skipped;
      continue;
    }
    if (paint_operation_into_tile(scope.operation, *edit, tile_mask)) {
      std::swap(affected[index], affected[retained]);
      ++retained;
    } else {
      canvas.invalidate_identity(key);
      ++drops.offscreen_skipped;
    }
  }
  return retained;
}

// Runs the retain phases in §8.4 order: visible uniforms, visible raw
// tiles, then budget-bounded offscreen raw tiles. Visible-first ordering is
// the resumability seam — a future drain slice pauses between phases — and
// means offscreen retention sees the deadline after visible work, which
// only changes outcomes when the budget engages (off_skip receipts).
std::size_t retain_affected_tiles(MaterializedCanvas& canvas, const InPlaceRetainScope& scope,
                                  std::span<TileKey> affected, std::span<std::uint8_t> tile_mask,
                                  InPlaceAppendPhases& phases, InPlaceRetainDrops& drops) {
  const auto stamp = [&scope]() {
    return scope.budget.now_us != nullptr ? scope.budget.now_us() : 0;
  };
  const std::int64_t uniform_started_us = stamp();
  std::size_t retained = retain_uniform_tiles(canvas, scope, affected, tile_mask, drops);
  const std::int64_t raw_started_us = stamp();
  retained = retain_visible_raw_tiles(canvas, scope, affected, tile_mask, retained, drops);
  const std::int64_t offscreen_started_us = stamp();
  retained = retain_offscreen_raw_tiles(canvas, scope, affected, tile_mask, retained, drops);
  phases.uniform_retain_us = raw_started_us - uniform_started_us;
  phases.raw_retain_us = offscreen_started_us - raw_started_us;
  phases.offscreen_retain_us = stamp() - offscreen_started_us;
  return retained;
}

// Saturating deadline for the offscreen retention pass. Zero time source
// means no deadline (host determinism).
std::int64_t retention_deadline_us(const InPlaceRetentionBudget& budget) {
  if (budget.now_us == nullptr) {
    return 0;
  }
  const std::int64_t now = budget.now_us();
  return budget.budget_us > std::numeric_limits<std::int64_t>::max() - now
             ? std::numeric_limits<std::int64_t>::max()
             : now + budget.budget_us;
}

// The §8.4 phase runner for one operation whose authority is already
// decided: PrepareOverviewRows → EnumerateAffected → RetainVisibleUniforms
// → RetainVisibleRawTiles → RetainOffscreenWithinBudget →
// CommitRevisionMetadata. Advances the canvas from identity.revision - 1 to
// identity.revision; never touches the operation log. Fail-safe: on nullopt
// the canvas keeps its old revision (tiles painted before a failed metadata
// commit are invalidated to correct overview fallback) and the call may be
// retried.
std::optional<IncrementalAppendResult> run_in_place_phases(
    MaterializedCanvas& canvas, const OperationIdentity& identity, const OperationAppend& operation,
    PixelRect world_bounds, const InPlaceAppendWorkspace& workspace,
    const std::optional<ViewRequest>& priority_view, const InPlaceRetentionBudget& budget,
    std::int64_t prepare_started_us, std::int64_t deadline_us, bool retain_all_zooms) {
  const auto stamp = [&budget]() { return budget.now_us != nullptr ? budget.now_us() : 0; };
  InPlaceAppendPhases phases{};
  const std::int64_t overview_started_us = stamp();
  phases.prepare_us = overview_started_us - prepare_started_us;
  OverviewRevisionPublication overview_publication{};
  const bool overview_ready = prepare_overview(canvas, operation, world_bounds,
                                               workspace.overview_scratch, overview_publication);
  const std::int64_t enumerate_started_us = stamp();
  phases.overview_us = enumerate_started_us - overview_started_us;
  const auto resident_count = canvas.materialized_tiles_intersecting(
      world_bounds, workspace.affected_keys, priority_view, false);
  if (!overview_ready || !resident_count.has_value() ||
      !canvas.can_edit_in_place_revision(identity.revision, overview_publication, world_bounds)) {
    return std::nullopt;
  }
  phases.enumerate_us = stamp() - enumerate_started_us;

  std::size_t affected_count = *resident_count;
  if (retain_all_zooms) {
    // Déjà-vu fix: also enumerate revisit-bound uniforms at other zooms so
    // the retain pass can materialize them during idle absorption. On
    // overflow the primary enumeration stands and those uniforms drop to
    // the old lazy-repair behavior.
    const auto extended = canvas.append_recent_view_uniform_keys(
        world_bounds, priority_view.has_value() ? std::optional{priority_view->zoom} : std::nullopt,
        workspace.affected_keys, affected_count);
    if (extended.has_value()) {
      affected_count = *extended;
    }
  }
  const auto affected = workspace.affected_keys.first(affected_count);
  const std::uint16_t painted_color =
      operation.tool == OperationTool::kEraser ? 0xFFFFU : operation.color;
  const InPlaceRetainScope scope{operation, painted_color, priority_view,
                                 budget,    deadline_us,   retain_all_zooms};
  InPlaceRetainDrops drops{};
  const std::size_t retained =
      retain_affected_tiles(canvas, scope, affected, workspace.tile_mask, phases, drops);
  std::size_t visible_fallback = 0;
  for (std::size_t index = retained; index < affected.size(); ++index) {
    visible_fallback += in_priority_view(affected[index], priority_view) ? 1U : 0U;
  }
  std::size_t cross_zoom_invalidated = 0;
  const std::int64_t commit_started_us = stamp();
  const MaterializedCanvas::InPlaceCommitScope commit_scope{
      .preserved_uniform_color = painted_color,
      .priority_zoom =
          priority_view.has_value() ? std::optional{priority_view->zoom} : std::nullopt,
      .cross_zoom_invalidated = &cross_zoom_invalidated,
  };
  if (!canvas.commit_in_place_revision(identity.revision, overview_publication, world_bounds,
                                       affected.first(retained), commit_scope)) {
    for (const TileKey key : affected.first(retained)) {
      canvas.invalidate_identity(key);
    }
    return std::nullopt;
  }
  phases.commit_us = stamp() - commit_started_us;
  return IncrementalAppendResult{.identity = identity,
                                 .affected_world_bounds = world_bounds,
                                 .affected_resident_tiles = affected_count,
                                 .published_tiles = retained,
                                 .fallback_tiles = affected_count - retained,
                                 .visible_fallback_tiles = visible_fallback,
                                 .cross_zoom_invalidated = cross_zoom_invalidated,
                                 .phases = phases,
                                 .drops = drops};
}

}  // namespace

std::optional<IncrementalAppendResult> append_authority_only(OperationLog& log,
                                                             const OperationAppend& append_request,
                                                             InPlaceRetentionBudget budget) {
  const auto stamp = [&budget]() { return budget.now_us != nullptr ? budget.now_us() : 0; };
  const std::int64_t prepare_started_us = stamp();
  const auto identity = log.append(append_request);
  if (!identity.has_value()) {
    return std::nullopt;
  }
  const auto operation = log.operation(identity->operation_index);
  if (!operation.has_value()) {
    return std::nullopt;
  }
  InPlaceAppendPhases phases{};
  phases.prepare_us = stamp() - prepare_started_us;
  return IncrementalAppendResult{
      .identity = *identity, .affected_world_bounds = operation->world_bounds, .phases = phases};
}

std::optional<IncrementalAppendResult> append_authority_only(OperationLog& log,
                                                             const BuiltOperation& operation,
                                                             InPlaceRetentionBudget budget) {
  const auto stamp = [&budget]() { return budget.now_us != nullptr ? budget.now_us() : 0; };
  const std::int64_t started_us = stamp();
  const auto identity = log.append(operation);
  if (!identity.has_value()) {
    return std::nullopt;
  }
  InPlaceAppendPhases phases{};
  phases.prepare_us = stamp() - started_us;
  return IncrementalAppendResult{
      .identity = *identity, .affected_world_bounds = operation.world_bounds(), .phases = phases};
}

std::optional<HistoryChange> move_history_incrementally(OperationLog& log,
                                                        MaterializedCanvas& canvas,
                                                        HistoryDirection direction,
                                                        std::span<std::uint16_t> overview_scratch) {
  if (!log.ready() || !canvas.ready() || log.current_revision() != canvas.current_revision() ||
      !canvas.accepts_external_workspace(std::as_bytes(overview_scratch)) ||
      log.workspace_overlaps_storage(std::as_bytes(overview_scratch))) {
    return std::nullopt;
  }
  auto prepared = direction == HistoryDirection::kUndo ? log.prepare_undo() : log.prepare_redo();
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const HistoryChange change = prepared->change();
  const PixelRect overview_bounds = overview_bounds_for_world(change.affected_world_bounds);
  const int width = overview_bounds.x1 - overview_bounds.x0;
  const int height = overview_bounds.y1 - overview_bounds.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (width <= 0 || height <= 0 || overview_scratch.size() < pixel_count) {
    prepared->cancel();
    return std::nullopt;
  }
  auto pixels = overview_scratch.first(pixel_count);
  std::fill(pixels.begin(), pixels.end(), 0xFFFFU);
  const RasterSurface surface{
      .zoom = ZoomLevel::k25Percent,
      .level_bounds = overview_bounds,
      .pixels = pixels,
      .stride = width,
  };
  for (std::size_t index = 0; index < change.active_operation_count; ++index) {
    const auto stored = prepared->target_operation(index);
    if (!stored.has_value()) {
      prepared->cancel();
      return std::nullopt;
    }
    if (!intersects(operation_level_bounds(stored->world_bounds, ZoomLevel::k25Percent),
                    overview_bounds)) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples}, surface)) {
      prepared->cancel();
      return std::nullopt;
    }
  }
  if (!canvas.commit_incremental_revision(change.generation,
                                          {.bounds = overview_bounds, .pixels = pixels},
                                          change.affected_world_bounds, {})) {
    prepared->cancel();
    return std::nullopt;
  }
  prepared->publish();
  return change;
}

std::size_t pending_operation_count(const OperationLog& log, const MaterializedCanvas& canvas) {
  if (!log.ready() || !canvas.ready()) {
    return 0;
  }
  const auto range =
      log.replay_range(log.epoch(), canvas.current_revision(), log.current_revision());
  return range.has_value() ? range->operation_count : 0;
}

bool overlay_pending_operations(const OperationLog& log, const MaterializedCanvas& canvas,
                                const RasterSurface& surface) {
  if (!log.ready() || !canvas.ready() || surface.pixels.empty() ||
      surface.stride < surface.level_bounds.x1 - surface.level_bounds.x0) {
    return false;
  }
  const auto range =
      log.replay_range(log.epoch(), canvas.current_revision(), log.current_revision());
  if (!range.has_value()) {
    return false;
  }
  for (std::size_t offset = 0; offset < range->operation_count; ++offset) {
    const auto stored = log.operation(range->first_operation + offset);
    if (!stored.has_value()) {
      return false;
    }
    const PixelRect bounds = operation_level_bounds(stored->world_bounds, surface.zoom);
    const bool intersects =
        bounds.x0 < surface.level_bounds.x1 && surface.level_bounds.x0 < bounds.x1 &&
        bounds.y0 < surface.level_bounds.y1 && surface.level_bounds.y0 < bounds.y1;
    if (!intersects) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples}, surface)) {
      return false;
    }
  }
  return true;
}

std::optional<IncrementalAppendResult> absorb_pending_operation(
    const OperationLog& log, MaterializedCanvas& canvas, const InPlaceAppendWorkspace& workspace,
    std::optional<ViewRequest> priority_view, InPlaceRetentionBudget budget) {
  const auto stamp = [&budget]() { return budget.now_us != nullptr ? budget.now_us() : 0; };
  const std::int64_t prepare_started_us = stamp();
  const std::int64_t deadline_us = retention_deadline_us(budget);
  if (!canvas.ready() || !log.ready() || !valid_in_place_workspace(log, canvas, workspace) ||
      canvas.overview_pixels().size() != kOverviewPixels || !valid_priority_view(priority_view)) {
    return std::nullopt;
  }
  const auto range =
      log.replay_range(log.epoch(), canvas.current_revision(), log.current_revision());
  if (!range.has_value() || range->operation_count == 0U) {
    return std::nullopt;
  }
  const auto stored = log.operation(range->first_operation);
  if (!stored.has_value()) {
    return std::nullopt;
  }
  const OperationAppend operation{
      .tool = stored->tool, .color = stored->color, .samples = stored->samples};
  // Idle absorption pays for cross-zoom retention (déjà-vu fix): resident
  // raw tiles at every zoom stay exact instead of dropping to overview.
  return run_in_place_phases(canvas, stored->identity, operation, stored->world_bounds, workspace,
                             priority_view, budget, prepare_started_us, deadline_us,
                             /*retain_all_zooms=*/true);
}

PendingAbsorptionSliceResult absorb_pending_operation_slice(
    const OperationLog& log, MaterializedCanvas& canvas, const InPlaceAppendWorkspace& workspace,
    PendingOperationAbsorption& state, std::optional<ViewRequest> priority_view,
    CooperativeWorkLimit limit, InPlaceRetentionBudget retention) {
  PendingAbsorptionSliceResult output{};
  const auto reset = [&state]() {
    state.overview_stage_.cancel();
    state = PendingOperationAbsorption{};
  };
  const auto stamp = [&state]() {
    return state.retention_.now_us != nullptr ? state.retention_.now_us() : 0;
  };
  const auto checkpoint = [&]() {
    ++output.checkpoints;
    return limit.yield_requested();
  };
  const auto same_span = [](auto left, auto right) {
    return left.data() == right.data() && left.size() == right.size();
  };
  if (limit.raster_work_px == 0U) {
    output.status = PendingAbsorptionStatus::kError;
    return output;
  }
  if (state.active()) {
    if (state.log_ != &log || state.canvas_ != &canvas ||
        !same_span(state.workspace_.overview_scratch, workspace.overview_scratch) ||
        !same_span(state.workspace_.affected_keys, workspace.affected_keys) ||
        !same_span(state.workspace_.tile_mask, workspace.tile_mask) ||
        !same_span(state.workspace_.operation_chord_plans, workspace.operation_chord_plans) ||
        state.priority_view_ != priority_view || state.retention_.now_us != retention.now_us ||
        state.retention_.budget_us != retention.budget_us) {
      // Keep the continuation live: the owner can resume with its original
      // transaction after rejecting an accidental mismatched call.
      output.status = PendingAbsorptionStatus::kError;
      return output;
    }
  } else {
    const std::int64_t prepare_started_us = retention.now_us != nullptr ? retention.now_us() : 0;
    const bool chord_storage_ready =
        workspace.operation_chord_plans.size() >= kOperationChordStorageBytes &&
        reinterpret_cast<std::uintptr_t>(workspace.operation_chord_plans.data()) %
                kPreparedOperationChordAlign ==
            0U;
    if (!canvas.ready() || !log.ready() || !valid_in_place_workspace(log, canvas, workspace) ||
        !chord_storage_ready || canvas.overview_pixels().size() != kOverviewPixels ||
        !valid_priority_view(priority_view)) {
      output.status = PendingAbsorptionStatus::kError;
      return output;
    }
    const auto range =
        log.replay_range(log.epoch(), canvas.current_revision(), log.current_revision());
    if (!range.has_value()) {
      output.status = PendingAbsorptionStatus::kError;
      return output;
    }
    if (range->operation_count == 0U) {
      output.status = PendingAbsorptionStatus::kIdle;
      return output;
    }
    const auto stored = log.operation(range->first_operation);
    if (!stored.has_value()) {
      output.status = PendingAbsorptionStatus::kError;
      return output;
    }
    state.log_ = &log;
    state.canvas_ = &canvas;
    state.operation_ = *stored;
    state.workspace_ = workspace;
    state.priority_view_ = priority_view;
    state.retention_ = retention;
    state.overview_bounds_ = overview_bounds_for_world(stored->world_bounds);
    const int width = state.overview_bounds_.x1 - state.overview_bounds_.x0;
    const int height = state.overview_bounds_.y1 - state.overview_bounds_.y0;
    const std::size_t pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0 || workspace.overview_scratch.size() < pixel_count) {
      reset();
      output.status = PendingAbsorptionStatus::kError;
      return output;
    }
    state.overview_publication_ = {.bounds = state.overview_bounds_,
                                   .pixels = workspace.overview_scratch.first(pixel_count)};
    state.phases_.prepare_us = stamp() - prepare_started_us;
    state.phase_ = PendingOperationAbsorption::Phase::kCopyOverview;
  }

  const OperationAppend operation{.tool = state.operation_.tool,
                                  .color = state.operation_.color,
                                  .samples = state.operation_.samples};
  const auto begin_raster = [&](const RasterSurface& surface, bool masked) {
    state.next_endpoint_ = operation.samples.size() - 1U;
    state.batch_ready_ = false;
    if (masked) {
      const std::size_t mask_bytes = (surface.pixels.size() + 7U) / 8U;
      if (state.workspace_.tile_mask.size() < mask_bytes) {
        return false;
      }
      std::fill_n(state.workspace_.tile_mask.begin(), mask_bytes, std::uint8_t{0});
    }
    return true;
  };
  // Returns -1 on invalid replay state, 0 after one bounded quantum, and 1
  // once every chord batch for this surface is complete.
  const auto raster_quantum = [&](const RasterSurface& surface, bool masked) {
    while (!state.batch_ready_) {
      const auto prepared = prepare_operation_chord_batch(
          operation.samples, state.next_endpoint_, surface.zoom, surface.level_bounds,
          state.workspace_.operation_chord_plans.first(kOperationChordStorageBytes));
      if (!prepared.has_value()) {
        return -1;
      }
      state.chord_batch_ = *prepared;
      state.next_endpoint_ = prepared->next_endpoint;
      if (prepared->chord_count == 0U) {
        if (state.next_endpoint_ == 0U) {
          return 1;
        }
        continue;
      }
      state.raster_cursor_ = {.next_row = prepared->clipped_bounds.y0};
      state.batch_ready_ = true;
    }
    OperationSweepSlice slice{};
    const bool okay =
        masked
            ? apply_masked_operation_chord_rows(
                  operation.tool, operation.color, state.workspace_.operation_chord_plans,
                  state.chord_batch_, state.raster_cursor_.next_row, limit.raster_work_px, surface,
                  state.workspace_.tile_mask, nullptr, slice)
            : apply_operation_chord_slice(
                  operation.tool, operation.color, state.workspace_.operation_chord_plans,
                  state.chord_batch_, limit.raster_work_px, surface, state.raster_cursor_, slice);
    if (!okay) {
      return -1;
    }
    if (masked) {
      state.raster_cursor_ = {.next_row = slice.next_row};
    }
    if (state.raster_cursor_.next_row >= state.chord_batch_.clipped_bounds.y1) {
      state.batch_ready_ = false;
      return state.next_endpoint_ == 0U ? 1 : 0;
    }
    return 0;
  };
  const auto over_retention_budget = [&]() {
    if (state.retention_.now_us == nullptr) {
      return false;
    }
    const InPlaceAppendPhases& phases = state.phases_;
    std::int64_t remaining_us = state.retention_.budget_us;
    for (const std::int64_t phase_us :
         {phases.prepare_us, phases.overview_us, phases.enumerate_us, phases.uniform_retain_us,
          phases.raw_retain_us, phases.offscreen_retain_us}) {
      if (remaining_us <= 0 || phase_us >= remaining_us) {
        return true;
      }
      remaining_us -= std::max<std::int64_t>(0, phase_us);
    }
    return false;
  };
  const auto start_tile = [&](const InPlaceTileEdit& edit, TileKey key) {
    state.tile_edit_ = edit;
    state.tile_key_ = key;
    state.painting_tile_ = true;
    const RasterSurface surface{
        .zoom = edit.key.zoom,
        .level_bounds = edit.bounds,
        .pixels = edit.pixels.first(static_cast<std::size_t>(edit.bounds.y1 - edit.bounds.y0 - 1) *
                                        kTileWidth +
                                    static_cast<std::size_t>(edit.bounds.x1 - edit.bounds.x0)),
        .stride = kTileWidth,
    };
    return begin_raster(surface, true);
  };
  const auto tile_surface = [&]() {
    const InPlaceTileEdit& edit = state.tile_edit_;
    return RasterSurface{
        .zoom = edit.key.zoom,
        .level_bounds = edit.bounds,
        .pixels = edit.pixels.first(static_cast<std::size_t>(edit.bounds.y1 - edit.bounds.y0 - 1) *
                                        kTileWidth +
                                    static_cast<std::size_t>(edit.bounds.x1 - edit.bounds.x0)),
        .stride = kTileWidth,
    };
  };
  const std::uint16_t painted_color =
      operation.tool == OperationTool::kEraser ? 0xFFFFU : operation.color;

  while (true) {
    if (checkpoint()) {
      output.status = PendingAbsorptionStatus::kInProgress;
      return output;
    }
    const std::int64_t unit_started_us = stamp();
    switch (state.phase_) {
      case PendingOperationAbsorption::Phase::kIdle:
        output.status = PendingAbsorptionStatus::kError;
        return output;
      case PendingOperationAbsorption::Phase::kCopyOverview: {
        output.work_unit = PendingAbsorptionWorkUnit::kCopyOverview;
        const int width = state.overview_bounds_.x1 - state.overview_bounds_.x0;
        const int height = state.overview_bounds_.y1 - state.overview_bounds_.y0;
        const auto source = canvas.overview_pixels().begin() +
                            static_cast<std::ptrdiff_t>(state.overview_bounds_.y0 +
                                                        static_cast<int>(state.copy_row_)) *
                                kOverviewWidth +
                            state.overview_bounds_.x0;
        const auto destination = state.workspace_.overview_scratch.begin() +
                                 static_cast<std::ptrdiff_t>(state.copy_row_) * width;
        std::copy_n(source, width, destination);
        ++state.copy_row_;
        if (state.copy_row_ == static_cast<std::size_t>(height)) {
          const RasterSurface surface{.zoom = ZoomLevel::k25Percent,
                                      .level_bounds = state.overview_bounds_,
                                      .pixels = state.workspace_.overview_scratch.first(
                                          state.overview_publication_.pixels.size()),
                                      .stride = width};
          if (!begin_raster(surface, false)) {
            reset();
            output.status = PendingAbsorptionStatus::kError;
            return output;
          }
          state.phase_ = PendingOperationAbsorption::Phase::kRasterOverview;
        }
        state.phases_.overview_us += stamp() - unit_started_us;
        break;
      }
      case PendingOperationAbsorption::Phase::kRasterOverview: {
        output.work_unit = PendingAbsorptionWorkUnit::kRasterOverview;
        const RasterSurface surface{
            .zoom = ZoomLevel::k25Percent,
            .level_bounds = state.overview_bounds_,
            .pixels =
                state.workspace_.overview_scratch.first(state.overview_publication_.pixels.size()),
            .stride = state.overview_bounds_.x1 - state.overview_bounds_.x0};
        const int replayed = raster_quantum(surface, false);
        state.phases_.overview_us += stamp() - unit_started_us;
        if (replayed < 0) {
          reset();
          output.status = PendingAbsorptionStatus::kError;
          return output;
        }
        if (replayed > 0) {
          state.phase_ = PendingOperationAbsorption::Phase::kEnumerate;
        }
        break;
      }
      case PendingOperationAbsorption::Phase::kEnumerate: {
        output.work_unit = PendingAbsorptionWorkUnit::kEnumerate;
        const auto resident_count = canvas.materialized_tiles_intersecting(
            state.operation_.world_bounds, state.workspace_.affected_keys, state.priority_view_,
            false);
        if (!resident_count.has_value() ||
            !canvas.can_edit_in_place_revision(state.operation_.identity.revision,
                                               state.overview_publication_,
                                               state.operation_.world_bounds)) {
          reset();
          output.status = PendingAbsorptionStatus::kError;
          return output;
        }
        state.affected_count_ = *resident_count;
        const auto extended = canvas.append_recent_view_uniform_keys(
            state.operation_.world_bounds,
            state.priority_view_.has_value() ? std::optional{state.priority_view_->zoom}
                                             : std::nullopt,
            state.workspace_.affected_keys, state.affected_count_);
        if (extended.has_value()) {
          state.affected_count_ = *extended;
        }
        state.scan_index_ = 0U;
        state.phases_.enumerate_us += stamp() - unit_started_us;
        state.phase_ = PendingOperationAbsorption::Phase::kUniform;
        break;
      }
      case PendingOperationAbsorption::Phase::kUniform: {
        output.work_unit = PendingAbsorptionWorkUnit::kUniform;
        auto affected = state.workspace_.affected_keys.first(state.affected_count_);
        if (state.painting_tile_) {
          const int replayed = raster_quantum(tile_surface(), true);
          if (replayed < 0) {
            canvas.invalidate_identity(state.tile_key_);
            if (in_priority_view(state.tile_key_, state.priority_view_)) {
              ++state.drops_.visible_uniform_paint_fail;
            } else {
              ++state.drops_.offscreen_skipped;
            }
            state.painting_tile_ = false;
            ++state.scan_index_;
          } else if (replayed > 0) {
            std::swap(affected[state.scan_index_], affected[state.retained_count_++]);
            state.painting_tile_ = false;
            ++state.scan_index_;
          }
        } else if (state.scan_index_ == state.affected_count_) {
          state.scan_index_ = state.retained_count_;
          state.phase_ = PendingOperationAbsorption::Phase::kVisibleRaw;
        } else {
          const TileKey key = affected[state.scan_index_];
          const auto color = canvas.uniform_color(key);
          if (!color.has_value()) {
            ++state.scan_index_;
          } else if (*color == painted_color) {
            std::swap(affected[state.scan_index_++], affected[state.retained_count_++]);
          } else {
            const bool visible = in_priority_view(key, state.priority_view_);
            const bool recent = in_recent_view(canvas, key);
            if (visible || (recent && !over_retention_budget())) {
              const auto edit = canvas.materialize_uniform_as_raw(key);
              if (!edit.has_value()) {
                if (visible) {
                  ++state.drops_.visible_uniform_no_slot;
                } else {
                  ++state.drops_.offscreen_skipped;
                }
                ++state.scan_index_;
              } else if (!start_tile(*edit, key)) {
                canvas.invalidate_identity(key);
                ++state.scan_index_;
              }
            } else {
              if (state.priority_view_.has_value() && key.zoom == state.priority_view_->zoom) {
                ++state.drops_.offscreen_skipped;
              }
              ++state.scan_index_;
            }
          }
        }
        state.phases_.uniform_retain_us += stamp() - unit_started_us;
        break;
      }
      case PendingOperationAbsorption::Phase::kVisibleRaw: {
        output.work_unit = PendingAbsorptionWorkUnit::kVisibleRaw;
        auto affected = state.workspace_.affected_keys.first(state.affected_count_);
        if (state.painting_tile_) {
          const int replayed = raster_quantum(tile_surface(), true);
          if (replayed < 0) {
            canvas.invalidate_identity(state.tile_key_);
            ++state.drops_.visible_raw_paint_fail;
            state.painting_tile_ = false;
            ++state.scan_index_;
          } else if (replayed > 0) {
            std::swap(affected[state.scan_index_], affected[state.retained_count_++]);
            state.painting_tile_ = false;
            ++state.scan_index_;
          }
        } else if (state.scan_index_ == state.affected_count_) {
          state.scan_index_ = state.retained_count_;
          state.phase_ = PendingOperationAbsorption::Phase::kOffscreenRaw;
        } else {
          const TileKey key = affected[state.scan_index_];
          if (!in_priority_view(key, state.priority_view_)) {
            ++state.scan_index_;
          } else {
            const auto edit = canvas.edit_resident_tile(key);
            if (!edit.has_value()) {
              ++state.drops_.visible_raw_edit_fail;
              ++state.scan_index_;
            } else if (!start_tile(*edit, key)) {
              canvas.invalidate_identity(key);
              ++state.drops_.visible_raw_paint_fail;
              ++state.scan_index_;
            }
          }
        }
        state.phases_.raw_retain_us += stamp() - unit_started_us;
        break;
      }
      case PendingOperationAbsorption::Phase::kOffscreenRaw: {
        output.work_unit = PendingAbsorptionWorkUnit::kOffscreenRaw;
        auto affected = state.workspace_.affected_keys.first(state.affected_count_);
        if (state.painting_tile_) {
          const int replayed = raster_quantum(tile_surface(), true);
          if (replayed < 0) {
            canvas.invalidate_identity(state.tile_key_);
            ++state.drops_.offscreen_skipped;
            state.painting_tile_ = false;
            ++state.scan_index_;
          } else if (replayed > 0) {
            std::swap(affected[state.scan_index_], affected[state.retained_count_++]);
            state.painting_tile_ = false;
            ++state.scan_index_;
          }
        } else if (state.scan_index_ == state.affected_count_) {
          state.phase_ = PendingOperationAbsorption::Phase::kStageOverview;
        } else {
          const TileKey key = affected[state.scan_index_];
          if (in_priority_view(key, state.priority_view_)) {
            ++state.scan_index_;
          } else if (over_retention_budget()) {
            ++state.drops_.offscreen_skipped;
            ++state.scan_index_;
          } else {
            const auto edit = canvas.edit_resident_tile(key);
            if (!edit.has_value()) {
              ++state.drops_.offscreen_skipped;
              ++state.scan_index_;
            } else if (!start_tile(*edit, key)) {
              canvas.invalidate_identity(key);
              ++state.drops_.offscreen_skipped;
              ++state.scan_index_;
            }
          }
        }
        state.phases_.offscreen_retain_us += stamp() - unit_started_us;
        break;
      }
      case PendingOperationAbsorption::Phase::kStageOverview: {
        output.work_unit = PendingAbsorptionWorkUnit::kStageOverview;
        const OverviewStageStatus staged = canvas.stage_in_place_overview_rows(
            state.operation_.identity.revision, state.overview_publication_,
            state.operation_.world_bounds, 1U, state.overview_stage_);
        state.phases_.overview_us += stamp() - unit_started_us;
        if (staged == OverviewStageStatus::kError) {
          reset();
          output.status = PendingAbsorptionStatus::kError;
          return output;
        }
        if (staged == OverviewStageStatus::kComplete) {
          state.phase_ = PendingOperationAbsorption::Phase::kStageMetadata;
        }
        break;
      }
      case PendingOperationAbsorption::Phase::kStageMetadata: {
        auto affected = state.workspace_.affected_keys.first(state.affected_count_);
        const MaterializedCanvas::InPlaceCommitScope scope{
            .preserved_uniform_color = painted_color,
            .priority_zoom = state.priority_view_.has_value()
                                 ? std::optional{state.priority_view_->zoom}
                                 : std::nullopt,
            .cross_zoom_invalidated = nullptr,
        };
        const InPlaceMetadataSlice staged = canvas.stage_in_place_metadata(
            state.operation_.identity.revision, state.overview_publication_,
            state.operation_.world_bounds, affected.first(state.retained_count_),
            limit.raster_work_px, state.overview_stage_, scope);
        switch (staged.phase) {
          case InPlaceMetadataPhase::kUniforms:
            output.work_unit = PendingAbsorptionWorkUnit::kStageUniforms;
            break;
          case InPlaceMetadataPhase::kRawSlots:
            output.work_unit = PendingAbsorptionWorkUnit::kStageRawSlots;
            break;
          case InPlaceMetadataPhase::kRerenderDamage:
            output.work_unit = PendingAbsorptionWorkUnit::kStageRerenderDamage;
            break;
          case InPlaceMetadataPhase::kOccupancy:
            output.work_unit = PendingAbsorptionWorkUnit::kStageOccupancy;
            break;
          case InPlaceMetadataPhase::kComplete:
            output.work_unit = PendingAbsorptionWorkUnit::kCommit;
            break;
        }
        state.phases_.commit_us += stamp() - unit_started_us;
        if (staged.status == OverviewStageStatus::kError) {
          reset();
          output.status = PendingAbsorptionStatus::kError;
          return output;
        }
        if (staged.status == OverviewStageStatus::kComplete) {
          state.phase_ = PendingOperationAbsorption::Phase::kCommit;
        }
        break;
      }
      case PendingOperationAbsorption::Phase::kCommit: {
        output.work_unit = PendingAbsorptionWorkUnit::kCommit;
        auto affected = state.workspace_.affected_keys.first(state.affected_count_);
        std::size_t visible_fallback = 0;
        for (std::size_t index = state.retained_count_; index < affected.size(); ++index) {
          visible_fallback += in_priority_view(affected[index], state.priority_view_) ? 1U : 0U;
        }
        std::size_t cross_zoom_invalidated = 0;
        const MaterializedCanvas::InPlaceCommitScope commit_scope{
            .preserved_uniform_color = painted_color,
            .priority_zoom = state.priority_view_.has_value()
                                 ? std::optional{state.priority_view_->zoom}
                                 : std::nullopt,
            .cross_zoom_invalidated = &cross_zoom_invalidated,
        };
        if (!canvas.commit_staged_in_place_revision(
                state.operation_.identity.revision, state.overview_publication_,
                state.operation_.world_bounds, affected.first(state.retained_count_),
                state.overview_stage_, commit_scope)) {
          for (const TileKey key : affected.first(state.retained_count_)) {
            canvas.invalidate_identity(key);
          }
          reset();
          output.status = PendingAbsorptionStatus::kError;
          return output;
        }
        state.phases_.commit_us += stamp() - unit_started_us;
        output.result = {.identity = state.operation_.identity,
                         .affected_world_bounds = state.operation_.world_bounds,
                         .affected_resident_tiles = state.affected_count_,
                         .published_tiles = state.retained_count_,
                         .fallback_tiles = state.affected_count_ - state.retained_count_,
                         .visible_fallback_tiles = visible_fallback,
                         .cross_zoom_invalidated = cross_zoom_invalidated,
                         .phases = state.phases_,
                         .drops = state.drops_};
        reset();
        output.status = PendingAbsorptionStatus::kComplete;
        return output;
      }
    }
  }
}

bool replay_active_overview(const OperationLog& log, std::span<std::uint16_t> output) {
  if (!log.ready() || output.size() != kOverviewPixels ||
      log.workspace_overlaps_storage(std::as_bytes(output))) {
    return false;
  }
  const AuthorityReadView view = log.read_view();
  std::fill(output.begin(), output.end(), 0xFFFFU);
  const RasterSurface surface{
      .zoom = ZoomLevel::k25Percent,
      .level_bounds = {0, 0, kOverviewWidth, kOverviewHeight},
      .pixels = output,
      .stride = kOverviewWidth,
  };
  for (std::size_t index = 0; index < view.active_operation_count; ++index) {
    const auto operation = log.operation(index);
    if (!operation.has_value() ||
        !apply_incremental_operation(
            {.tool = operation->tool, .color = operation->color, .samples = operation->samples},
            surface)) {
      return false;
    }
  }
  return true;
}

bool build_tiled_may_ink(const OperationLog& log, std::span<std::uint8_t> output) {
  if (!log.ready() || output.size() != kOccupancyBytes ||
      log.workspace_overlaps_storage(std::as_bytes(output))) {
    return false;
  }
  std::fill(output.begin(), output.end(), 0U);
  const AuthorityReadView view = log.read_view();
  for (std::size_t index = 0; index < view.active_operation_count; ++index) {
    const auto operation = log.operation(index);
    if (!operation.has_value()) {
      return false;
    }
    if (operation->tool == OperationTool::kEraser) {
      continue;
    }
    const PixelRect bounds = operation->world_bounds;
    const int first_column = bounds.x0 / kOccupancyCellWorldSize;
    const int last_column = (bounds.x1 - 1) / kOccupancyCellWorldSize;
    const int first_row = bounds.y0 / kOccupancyCellWorldSize;
    const int last_row = (bounds.y1 - 1) / kOccupancyCellWorldSize;
    for (int row = first_row; row <= last_row; ++row) {
      for (int column = first_column; column <= last_column; ++column) {
        const std::size_t bit =
            static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
        output[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
      }
    }
  }
  return true;
}

bool restore_document_snapshot(OperationLog& log, MaterializedCanvas& canvas,
                               DocumentRevision revision,
                               std::span<const std::uint16_t> overview_pixels) {
  if (!log.ready() || !canvas.ready() || !log.can_reset() ||
      overview_pixels.size() != kOverviewPixels ||
      !canvas.accepts_external_workspace(std::as_bytes(overview_pixels)) ||
      log.workspace_overlaps_storage(std::as_bytes(overview_pixels))) {
    return false;
  }
  // restore_snapshot cannot fail after the checks above under the serialized
  // ownership contract. Reset is called second so a failed canvas validation
  // cannot discard document authority.
  if (!canvas.restore_snapshot(revision, overview_pixels)) {
    return false;
  }
  return log.reset(revision);
}

bool reset_blank_document(OperationLog& log, MaterializedCanvas& canvas,
                          DocumentRevision revision) {
  if (!log.ready() || !canvas.ready() || !log.can_reset()) {
    return false;
  }
  // reset_blank cannot fail after the readiness check under the serialized
  // ownership contract. Reset is called second so validation cannot discard
  // document authority.
  if (!canvas.reset_blank(revision)) {
    return false;
  }
  return log.reset(revision);
}

}  // namespace tinydraw::vector_v2
