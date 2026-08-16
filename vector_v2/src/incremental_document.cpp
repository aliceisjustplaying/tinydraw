#include "tinydraw/vector_v2/incremental_document.h"

#include <algorithm>
#include <array>
#include <limits>

#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {
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

void prioritize_view(std::span<TileKey> keys, const std::optional<ViewRequest>& view) {
  std::size_t destination = 0;
  for (std::size_t candidate = 0; candidate < keys.size(); ++candidate) {
    if (in_priority_view(keys[candidate], view)) {
      std::swap(keys[destination], keys[candidate]);
      ++destination;
    }
  }
}

bool prepare_tile(const MaterializedCanvas& canvas, const OperationAppend& operation, TileKey key,
                  std::span<std::uint16_t> scratch, TileRevisionPublication& publication) {
  const PixelRect bounds = tile_pixel_bounds(key);
  const std::size_t pixel_count = static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                  static_cast<std::size_t>(bounds.y1 - bounds.y0);
  const auto pixels = scratch.first(pixel_count);
  if (!canvas.copy_resident_tile(key, pixels) ||
      !apply_incremental_operation(operation, {.zoom = key.zoom,
                                               .level_bounds = bounds,
                                               .pixels = pixels,
                                               .stride = bounds.x1 - bounds.x0})) {
    return false;
  }
  publication = {
      .key = key,
      .quality = MaterializationQuality::kImmediate,
      .pixels = pixels,
  };
  return true;
}

}  // namespace

std::optional<IncrementalAppendResult> append_incrementally(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const IncrementalDocumentWorkspace& workspace, IncrementalAppendOptions options) {
  const std::array<std::span<const std::byte>, 4> workspaces{
      std::as_bytes(workspace.overview_scratch), std::as_bytes(workspace.tile_scratch),
      std::as_bytes(workspace.publications), std::as_bytes(workspace.affected_keys)};
  bool workspaces_overlap = false;
  for (std::size_t left = 0; left < workspaces.size(); ++left) {
    for (std::size_t right = left + 1U; right < workspaces.size(); ++right) {
      workspaces_overlap =
          workspaces_overlap || storage_overlaps(workspaces[left], workspaces[right]);
    }
  }
  const bool workspace_aliases_owned_storage =
      std::any_of(workspaces.begin(), workspaces.end(), [&](const auto workspace_bytes) {
        return !canvas.accepts_external_workspace(workspace_bytes) ||
               log.workspace_overlaps_storage(workspace_bytes);
      });
  if (!canvas.ready() || !log.ready() || canvas.overview_pixels().size() != kOverviewPixels ||
      workspaces_overlap || workspace_aliases_owned_storage ||
      log.current_revision() != canvas.current_revision() ||
      !valid_priority_view(options.priority_view)) {
    return std::nullopt;
  }
  auto prepared = log.prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const StoredOperation& stored = prepared->operation();
  // publish() clears the prepared view; copy everything the result needs
  // before the commit succeeds.
  const OperationIdentity identity = stored.identity;
  const PixelRect world_bounds = stored.world_bounds;
  const OperationAppend operation{
      .tool = stored.tool, .color = stored.color, .samples = stored.samples};
  OverviewRevisionPublication overview_publication{};
  const bool overview_ready = prepare_overview(canvas, operation, world_bounds,
                                               workspace.overview_scratch, overview_publication);
  const auto resident_count = canvas.materialized_tiles_intersecting(
      world_bounds, workspace.affected_keys, options.priority_view,
      options.publication_scope == IncrementalPublicationScope::kPriorityView);
  if (!overview_ready || !resident_count.has_value()) {
    prepared->cancel();
    return std::nullopt;
  }
  const std::size_t publication_capacity =
      std::min(workspace.publications.size(), workspace.tile_scratch.size() / kTilePixels);
  const std::size_t publication_count = std::min(*resident_count, publication_capacity);
  if (options.priority_view.has_value() && publication_count < *resident_count) {
    prioritize_view(workspace.affected_keys.first(*resident_count), options.priority_view);
  }
  for (std::size_t index = 0; index < publication_count; ++index) {
    auto scratch = workspace.tile_scratch.subspan(index * kTilePixels, kTilePixels);
    if (!prepare_tile(canvas, operation, workspace.affected_keys[index], scratch,
                      workspace.publications[index])) {
      prepared->cancel();
      return std::nullopt;
    }
  }
  if (!canvas.commit_incremental_revision(identity.revision, overview_publication, world_bounds,
                                          workspace.publications.first(publication_count))) {
    prepared->cancel();
    return std::nullopt;
  }
  prepared->publish();
  return IncrementalAppendResult{.identity = identity,
                                 .affected_world_bounds = world_bounds,
                                 .affected_resident_tiles = *resident_count,
                                 .published_tiles = publication_count,
                                 .fallback_tiles = *resident_count - publication_count};
}

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
  const std::array<std::span<const std::byte>, 3> workspaces{
      std::as_bytes(workspace.overview_scratch), std::as_bytes(workspace.affected_keys),
      std::as_bytes(workspace.tile_mask)};
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

std::optional<IncrementalAppendResult> append_incrementally_in_place(
    OperationLog& log, MaterializedCanvas& canvas, const OperationAppend& append_request,
    const InPlaceAppendWorkspace& workspace, std::optional<ViewRequest> priority_view,
    InPlaceRetentionBudget budget) {
  // The deadline bounds only the offscreen raw retention pass; overview
  // replay, visible tiles, and the metadata commit run to completion, so the
  // caller-visible poll gap is attributed by the phase timings rather than
  // bounded by this budget. Saturate instead of overflowing on adversarial
  // clocks or budgets.
  const auto stamp = [&budget]() { return budget.now_us != nullptr ? budget.now_us() : 0; };
  const std::int64_t prepare_started_us = stamp();
  const std::int64_t deadline_us = retention_deadline_us(budget);
  if (!canvas.ready() || !log.ready() || !valid_in_place_workspace(log, canvas, workspace) ||
      canvas.overview_pixels().size() != kOverviewPixels ||
      log.current_revision() != canvas.current_revision() || !valid_priority_view(priority_view)) {
    return std::nullopt;
  }
  auto prepared = log.prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const StoredOperation& stored = prepared->operation();
  // publish() clears the prepared view; copy everything the phases need
  // before deciding the log's fate.
  const OperationIdentity identity = stored.identity;
  const PixelRect world_bounds = stored.world_bounds;
  const OperationAppend operation{
      .tool = stored.tool, .color = stored.color, .samples = stored.samples};
  const auto result =
      run_in_place_phases(canvas, identity, operation, world_bounds, workspace, priority_view,
                          budget, prepare_started_us, deadline_us, /*retain_all_zooms=*/false);
  if (!result.has_value()) {
    prepared->cancel();
    return std::nullopt;
  }
  prepared->publish();
  return result;
}

std::optional<IncrementalAppendResult> append_authority_only(OperationLog& log,
                                                             const OperationAppend& append_request,
                                                             InPlaceRetentionBudget budget) {
  const auto stamp = [&budget]() { return budget.now_us != nullptr ? budget.now_us() : 0; };
  const std::int64_t prepare_started_us = stamp();
  if (!log.ready()) {
    return std::nullopt;
  }
  auto prepared = log.prepare(append_request);
  if (!prepared.has_value()) {
    return std::nullopt;
  }
  const OperationIdentity identity = prepared->operation().identity;
  const PixelRect world_bounds = prepared->operation().world_bounds;
  prepared->publish();
  InPlaceAppendPhases phases{};
  phases.prepare_us = stamp() - prepare_started_us;
  return IncrementalAppendResult{
      .identity = identity, .affected_world_bounds = world_bounds, .phases = phases};
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

}  // namespace tinydraw::vector_v2
