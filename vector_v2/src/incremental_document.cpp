#include "tinydraw/vector_v2/incremental_document.h"

#include <algorithm>
#include <array>
#include <limits>

#include "incremental_document_internal.h"
#include "tinydraw/vector_v2/operation_builder.h"

namespace tinydraw::vector_v2 {

using incremental_document_internal::in_priority_view;
using incremental_document_internal::in_recent_view;
using incremental_document_internal::valid_in_place_workspace;
using incremental_document_internal::valid_priority_view;

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
// Uniform pass: same-color uniforms retain for free at every zoom; other
// affected uniforms materialize as raw and paint inside the priority view
// (budget-exempt: dropping one blurs pixels the user is looking at), and —
// during all-zoom absorption — inside remembered views at other zooms
// (budget-bounded; a skip falls back to the old drop-and-repair behavior).
enum class UniformRetentionDecision : std::uint8_t { kNotUniform, kDrop, kKeep };

UniformRetentionDecision retain_uniform_tile(MaterializedCanvas& canvas,
                                             const InPlaceRetainScope& scope, TileKey key,
                                             std::span<std::uint8_t> tile_mask,
                                             InPlaceRetainDrops& drops) {
  const auto color = canvas.uniform_color(key);
  if (!color.has_value()) {
    return UniformRetentionDecision::kNotUniform;
  }
  if (*color == scope.painted_color) {
    return UniformRetentionDecision::kKeep;
  }
  const bool visible = in_priority_view(key, scope.priority_view);
  const bool retain_recent =
      !visible && scope.retain_all_zooms && in_recent_view(canvas, key) && !scope.over_budget();
  if (!visible && !retain_recent) {
    if (scope.priority_view.has_value() && key.zoom == scope.priority_view->zoom) {
      ++drops.offscreen_skipped;
    }
    return UniformRetentionDecision::kDrop;
  }
  const auto edit = canvas.materialize_uniform_as_raw(key);
  if (edit.has_value() && paint_operation_into_tile(scope.operation, *edit, tile_mask)) {
    return UniformRetentionDecision::kKeep;
  }
  if (edit.has_value()) {
    canvas.invalidate_identity(key);
  }
  if (visible) {
    edit.has_value() ? ++drops.visible_uniform_paint_fail : ++drops.visible_uniform_no_slot;
  } else {
    ++drops.offscreen_skipped;
  }
  return UniformRetentionDecision::kDrop;
}

std::size_t retain_uniform_tiles(MaterializedCanvas& canvas, const InPlaceRetainScope& scope,
                                 std::span<TileKey> affected, std::span<std::uint8_t> tile_mask,
                                 InPlaceRetainDrops& drops) {
  std::size_t retained = 0;
  for (std::size_t index = 0; index < affected.size(); ++index) {
    const UniformRetentionDecision decision =
        retain_uniform_tile(canvas, scope, affected[index], tile_mask, drops);
    if (decision == UniformRetentionDecision::kNotUniform) {
      continue;
    }
    if (decision == UniformRetentionDecision::kKeep) {
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
struct PreparedInPlaceRun {
  InPlaceAppendPhases phases{};
  OverviewRevisionPublication overview_publication{};
  std::size_t affected_count = 0;
};

class InPlaceRunContext {
 public:
  MaterializedCanvas& canvas;
  const OperationIdentity& identity;
  const OperationAppend& operation;
  PixelRect world_bounds;
  const InPlaceAppendWorkspace& workspace;
  const std::optional<ViewRequest>& priority_view;
  const InPlaceRetentionBudget& budget;
  std::int64_t prepare_started_us;
  std::int64_t deadline_us;
  bool retain_all_zooms;

  [[nodiscard]] std::optional<IncrementalAppendResult> run() const {
    auto prepared = prepare();
    if (!prepared.has_value()) {
      return std::nullopt;
    }
    const auto affected = workspace.affected_keys.first(prepared->affected_count);
    const std::uint16_t painted_color =
        operation.tool == OperationTool::kEraser ? 0xFFFFU : operation.color;
    const InPlaceRetainScope scope{operation, painted_color, priority_view,
                                   budget,    deadline_us,   retain_all_zooms};
    InPlaceRetainDrops drops{};
    const std::size_t retained = retain_affected_tiles(canvas, scope, affected, workspace.tile_mask,
                                                       prepared->phases, drops);
    const std::size_t visible_fallback = count_visible_fallback(affected, retained);
    std::size_t cross_zoom_invalidated = 0;
    const std::int64_t commit_started_us = stamp();
    const MaterializedCanvas::InPlaceCommitScope commit_scope{
        .preserved_uniform_color = painted_color,
        .priority_zoom =
            priority_view.has_value() ? std::optional{priority_view->zoom} : std::nullopt,
        .cross_zoom_invalidated = &cross_zoom_invalidated,
    };
    if (!canvas.commit_in_place_revision(identity.revision, prepared->overview_publication,
                                         world_bounds, affected.first(retained), commit_scope)) {
      for (const TileKey key : affected.first(retained)) {
        canvas.invalidate_identity(key);
      }
      return std::nullopt;
    }
    prepared->phases.commit_us = stamp() - commit_started_us;
    return IncrementalAppendResult{.identity = identity,
                                   .affected_world_bounds = world_bounds,
                                   .affected_resident_tiles = prepared->affected_count,
                                   .published_tiles = retained,
                                   .fallback_tiles = prepared->affected_count - retained,
                                   .visible_fallback_tiles = visible_fallback,
                                   .cross_zoom_invalidated = cross_zoom_invalidated,
                                   .phases = prepared->phases,
                                   .drops = drops};
  }

 private:
  [[nodiscard]] std::int64_t stamp() const {
    return budget.now_us != nullptr ? budget.now_us() : 0;
  }

  [[nodiscard]] std::optional<PreparedInPlaceRun> prepare() const {
    PreparedInPlaceRun prepared{};
    const std::int64_t overview_started_us = stamp();
    prepared.phases.prepare_us = overview_started_us - prepare_started_us;
    const bool overview_ready = prepare_overview(
        canvas, operation, world_bounds, workspace.overview_scratch, prepared.overview_publication);
    const std::int64_t enumerate_started_us = stamp();
    prepared.phases.overview_us = enumerate_started_us - overview_started_us;
    const auto resident_count = canvas.materialized_tiles_intersecting(
        world_bounds, workspace.affected_keys, priority_view, false);
    if (!overview_ready || !resident_count.has_value() ||
        !canvas.can_edit_in_place_revision(identity.revision, prepared.overview_publication,
                                           world_bounds)) {
      return std::nullopt;
    }
    prepared.phases.enumerate_us = stamp() - enumerate_started_us;
    prepared.affected_count = *resident_count;
    extend_recent_uniforms(prepared.affected_count);
    return prepared;
  }

  void extend_recent_uniforms(std::size_t& affected_count) const {
    if (!retain_all_zooms) {
      return;
    }
    const auto extended = canvas.append_recent_view_uniform_keys(
        world_bounds, priority_view.has_value() ? std::optional{priority_view->zoom} : std::nullopt,
        workspace.affected_keys, affected_count);
    if (extended.has_value()) {
      affected_count = *extended;
    }
  }

  [[nodiscard]] std::size_t count_visible_fallback(std::span<const TileKey> affected,
                                                   std::size_t retained) const {
    std::size_t visible = 0;
    for (std::size_t index = retained; index < affected.size(); ++index) {
      visible += in_priority_view(affected[index], priority_view) ? 1U : 0U;
    }
    return visible;
  }
};

std::optional<IncrementalAppendResult> run_in_place_phases(InPlaceRunContext context) {
  return context.run();
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
  return run_in_place_phases({canvas, stored->identity, operation, stored->world_bounds, workspace,
                              priority_view, budget, prepare_started_us, deadline_us,
                              /*retain_all_zooms=*/true});
}

}  // namespace tinydraw::vector_v2
