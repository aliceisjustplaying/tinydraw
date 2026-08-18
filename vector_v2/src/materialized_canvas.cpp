#include "tinydraw/vector_v2/materialized_canvas.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
#include "tinydraw/vector_v2/rerender_ledger.h"
#endif
#include "materialized_canvas_internal.h"
#include "tinydraw/vector_v2/storage_overlap.h"
#include "tinydraw/vector_v2/tile_uniform.h"

namespace tinydraw::vector_v2 {
using namespace materialized_canvas_detail;

void ViewCompositionCursor::cancel() {
  canvas_ = nullptr;
  request_ = {};
  destination_ = nullptr;
  destination_size_ = 0;
  revision_ = {};
  canvas_epoch_ = 0;
  stats_ = {};
  next_y_ = 0;
  active_ = false;
}

void InPlaceOverviewStage::cancel() {
  if (canvas_ != nullptr) {
    canvas_->cancel_in_place_stage(*this);
  }
  *this = {};
}
namespace {

constexpr int clamp_end(int start, int extent, int limit) {
  return std::min(start + extent, limit);
}

bool valid_overview_bounds(PixelRect bounds) {
  return bounds.x0 >= 0 && bounds.y0 >= 0 && bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kOverviewWidth && bounds.y1 <= kOverviewHeight;
}

}  // namespace

int zoom_percent(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return 25;
    case ZoomLevel::k50Percent:
      return 50;
    case ZoomLevel::k100Percent:
      return 100;
    case ZoomLevel::k200Percent:
      return 200;
    case ZoomLevel::k400Percent:
      return 400;
  }
  return 0;
}

PixelRect overview_bounds_for_world(PixelRect world_bounds) {
  if (!valid_world_bounds(world_bounds)) {
    return {};
  }
  return {
      .x0 = world_bounds.x0 / 4,
      .y0 = world_bounds.y0 / 4,
      .x1 = ceil_div(world_bounds.x1, 4),
      .y1 = ceil_div(world_bounds.y1, 4),
  };
}

TileGrid tile_grid(ZoomLevel zoom) {
  return {
      .columns = ceil_div(scaled_extent(kWorldWidth, zoom), kTileWidth),
      .rows = ceil_div(scaled_extent(kWorldHeight, zoom), kTileHeight),
  };
}

std::optional<std::size_t> tile_identity_index(TileKey key) {
  if (!valid_tile_key(key)) {
    return std::nullopt;
  }
  constexpr std::array offsets{std::size_t{0}, std::size_t{168}, std::size_t{812},
                               std::size_t{3'388}};
  const std::size_t zoom_index = static_cast<std::size_t>(key.zoom) - 1U;
  const TileGrid grid = tile_grid(key.zoom);
  return offsets[zoom_index] +
         static_cast<std::size_t>(key.row) * static_cast<std::size_t>(grid.columns) + key.column;
}

bool valid_tile_key(TileKey key) {
  if (key.zoom == ZoomLevel::k25Percent) {
    return false;
  }
  const TileGrid grid = tile_grid(key.zoom);
  return static_cast<int>(key.column) < grid.columns && static_cast<int>(key.row) < grid.rows;
}

std::optional<TileKey> tile_key_for_world(ZoomLevel zoom, WorldPoint point) {
  if (zoom == ZoomLevel::k25Percent || point.x < 0 || point.y < 0 || point.x >= kWorldWidth ||
      point.y >= kWorldHeight) {
    return std::nullopt;
  }
  const int percent = zoom_percent(zoom);
  return TileKey{
      .zoom = zoom,
      .column = static_cast<std::uint16_t>((point.x * percent / 100) / kTileWidth),
      .row = static_cast<std::uint16_t>((point.y * percent / 100) / kTileHeight),
  };
}

PixelRect tile_pixel_bounds(TileKey key) {
  if (!valid_tile_key(key)) {
    return {};
  }
  const int x0 = static_cast<int>(key.column) * kTileWidth;
  const int y0 = static_cast<int>(key.row) * kTileHeight;
  return {
      .x0 = x0,
      .y0 = y0,
      .x1 = clamp_end(x0, kTileWidth, scaled_extent(kWorldWidth, key.zoom)),
      .y1 = clamp_end(y0, kTileHeight, scaled_extent(kWorldHeight, key.zoom)),
  };
}

MaterializedCanvas::MaterializedCanvas(const MaterializedCanvasStorage& storage)
    : overview_pixels_(storage.overview_pixels),
      uniform_catalog_(storage.uniform_catalog),
      occupancy_bits_(storage.occupancy_bits),
      slots_(storage.slots),
      tile_pixels_(storage.tile_pixels),
      raw_slot_directory_(storage.raw_slot_directory),
      current_revision_(storage.initial_revision) {
  std::fill(uniform_catalog_.begin(), uniform_catalog_.end(), MaterializedUniformStorage{});
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0U);
  std::fill(slots_.begin(), slots_.end(), MaterializedSlotStorage{});
  std::fill(raw_slot_directory_.begin(), raw_slot_directory_.end(), kNoRawSlot);
}

bool MaterializedCanvas::ready() const {
  return overview_pixels_.size() == kOverviewPixels &&
         uniform_catalog_.size() == kMaterializedTileIdentityCount &&
         occupancy_bits_.size() == kOccupancyBytes &&
         raw_slot_directory_.size() == kMaterializedTileIdentityCount &&
         slots_.size() < static_cast<std::size_t>(kRetainedUniformSlot) &&
         tile_pixels_.size() == slots_.size() * kTilePixels;
}

DocumentRevision MaterializedCanvas::current_revision() const { return current_revision_; }

std::size_t MaterializedCanvas::slot_capacity() const { return slots_.size(); }

std::size_t MaterializedCanvas::resident_raw_tiles() const { return occupied_slots_; }

std::span<const std::uint16_t> MaterializedCanvas::overview_pixels() const {
  return overview_pixels_;
}

bool MaterializedCanvas::certainly_paper(TileKey key) const {
  if (!ready() || !valid_tile_key(key)) {
    return false;
  }
  const PixelRect world = tile_world_bounds(key);
  const int first_column = world.x0 / kOccupancyCellWorldSize;
  const int last_column = (world.x1 - 1) / kOccupancyCellWorldSize;
  const int first_row = world.y0 / kOccupancyCellWorldSize;
  const int last_row = (world.y1 - 1) / kOccupancyCellWorldSize;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const std::size_t bit =
          static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
      if ((occupancy_bits_[bit / 8U] & static_cast<std::uint8_t>(1U << (bit % 8U))) != 0U) {
        return false;
      }
    }
  }
  return true;
}

void MaterializedCanvas::clear_uniforms() {
  std::fill(uniform_catalog_.begin(), uniform_catalog_.end(), MaterializedUniformStorage{});
}

void MaterializedCanvas::invalidate_zoom_uniforms(ZoomLevel zoom, PixelRect world_bounds,
                                                  const InPlaceCommitScope& scope) {
  const int percent = zoom_percent(zoom);
  const TileGrid grid = tile_grid(zoom);
  const int first_column =
      std::clamp(world_bounds.x0 * percent / 100 / kTileWidth - 1, 0, grid.columns - 1);
  const int last_column = std::clamp(
      (ceil_div(world_bounds.x1 * percent, 100) - 1) / kTileWidth + 1, 0, grid.columns - 1);
  const int first_row =
      std::clamp(world_bounds.y0 * percent / 100 / kTileHeight - 1, 0, grid.rows - 1);
  const int last_row = std::clamp((ceil_div(world_bounds.y1 * percent, 100) - 1) / kTileHeight + 1,
                                  0, grid.rows - 1);
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)};
      const auto index = tile_identity_index(key);
      if (!index.has_value() || !uniform_catalog_[*index].occupied_ ||
          !rectangles_intersect(tile_world_bounds(key), world_bounds)) {
        continue;
      }
      // Painting a color over an identical uniform is a no-op at any zoom,
      // so those uniforms survive without being enumerated.
      const bool retained_marked = raw_slot_directory_[*index] == kRetainedUniformSlot;
      const bool retained =
          retained_marked || (scope.preserved_uniform_color.has_value() &&
                              uniform_catalog_[*index].color_ == *scope.preserved_uniform_color);
      if (retained_marked) {
        raw_slot_directory_[*index] = kNoRawSlot;
      }
      if (!retained && scope.cross_zoom_invalidated != nullptr && zoom != scope.priority_zoom) {
        ++*scope.cross_zoom_invalidated;
      }
      uniform_catalog_[*index].occupied_ = retained;
    }
  }
}

void MaterializedCanvas::invalidate_uniforms(PixelRect world_bounds,
                                             const InPlaceCommitScope& scope) {
  if (!valid_world_bounds(world_bounds)) {
    return;
  }
  // Walk only a conservative tile window per zoom instead of all 13,692
  // identities.
  constexpr std::array zooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                             ZoomLevel::k400Percent};
  for (const ZoomLevel zoom : zooms) {
    invalidate_zoom_uniforms(zoom, world_bounds, scope);
  }
}

void MaterializedCanvas::mark_occupied(PixelRect world_bounds) {
  const int first_column = world_bounds.x0 / kOccupancyCellWorldSize;
  const int last_column = (world_bounds.x1 - 1) / kOccupancyCellWorldSize;
  const int first_row = world_bounds.y0 / kOccupancyCellWorldSize;
  const int last_row = (world_bounds.y1 - 1) / kOccupancyCellWorldSize;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const std::size_t bit =
          static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
      occupancy_bits_[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
    }
  }
}

bool MaterializedCanvas::publish_overview(DocumentRevision revision,
                                          std::span<const std::uint16_t> pixels) {
  const bool revision_is_publishable = overview_valid_ ? revision.value > current_revision_.value
                                                       : revision.value >= current_revision_.value;
  const bool exact_bootstrap_source = !overview_valid_ &&
                                      pixels.data() == overview_pixels_.data() &&
                                      pixels.size() == overview_pixels_.size();
  const bool source_is_publishable =
      exact_bootstrap_source || accepts_external_workspace(std::as_bytes(pixels));
  if (!ready() || !revision_is_publishable || !source_is_publishable ||
      pixels.size() != kOverviewPixels) {
    return false;
  }
  ++composition_epoch_;
  if (!exact_bootstrap_source) {
    std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].occupied_) {
      release_slot(index);
    }
  }
  drop_preserved_slots();
  clear_uniforms();
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0xFFU);
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_valid_ = true;
  return true;
}

bool MaterializedCanvas::restore_snapshot(DocumentRevision revision,
                                          std::span<const std::uint16_t> pixels) {
  const bool source_is_external = accepts_external_workspace(std::as_bytes(pixels));
  if (!ready() || pixels.size() != kOverviewPixels || !source_is_external) {
    return false;
  }
  ++composition_epoch_;
  std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  // A restore replaces the entire document authority; prior per-group render
  // history is meaningless for the new document, so the re-render ledger
  // starts a fresh session instead of misclassifying every next render.
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr) {
    rerender_ledger_->reset();
  }
#endif
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].occupied_) {
      release_slot(index);
    }
  }
  drop_preserved_slots();
  clear_uniforms();
  // A 25% snapshot cannot prove that a higher-zoom hairline is absent.
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0xFFU);
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_valid_ = true;
  return true;
}

bool MaterializedCanvas::restore_snapshot(DocumentRevision revision,
                                          std::span<const std::uint16_t> pixels,
                                          std::span<const std::uint8_t> tiled_may_ink) {
  const auto pixel_bytes = std::as_bytes(pixels);
  const auto map_bytes = std::as_bytes(tiled_may_ink);
  if (!ready() || pixels.size() != kOverviewPixels || tiled_may_ink.size() != kOccupancyBytes ||
      !accepts_external_workspace(pixel_bytes) || !accepts_external_workspace(map_bytes) ||
      storage_overlaps(pixel_bytes, map_bytes)) {
    return false;
  }
  ++composition_epoch_;
  std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  std::copy(tiled_may_ink.begin(), tiled_may_ink.end(), occupancy_bits_.begin());
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr) {
    rerender_ledger_->reset();
  }
#endif
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].occupied_) {
      release_slot(index);
    }
  }
  drop_preserved_slots();
  clear_uniforms();
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_valid_ = true;
  return true;
}

bool MaterializedCanvas::replace_tiled_may_ink(DocumentRevision revision,
                                               std::span<const std::uint8_t> tiled_may_ink) {
  if (!ready() || !overview_valid_ || revision != current_revision_ ||
      tiled_may_ink.size() != kOccupancyBytes ||
      !accepts_external_workspace(std::as_bytes(tiled_may_ink))) {
    return false;
  }
  std::copy(tiled_may_ink.begin(), tiled_may_ink.end(), occupancy_bits_.begin());
  return true;
}

bool MaterializedCanvas::reset_blank(DocumentRevision revision) {
  if (!ready()) {
    return false;
  }
  ++composition_epoch_;
  std::fill(overview_pixels_.begin(), overview_pixels_.end(), 0xFFFFU);
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr) {
    rerender_ledger_->reset();
  }
#endif
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].occupied_) {
      release_slot(index);
    }
  }
  drop_preserved_slots();
  clear_uniforms();
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0U);
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_valid_ = true;
  return true;
}

bool MaterializedCanvas::valid_incremental_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds,
    std::span<const TileRevisionPublication> tile_publications) const {
  const bool revision_advances_once =
      current_revision_.value != std::numeric_limits<std::uint32_t>::max() &&
      revision.value == current_revision_.value + 1U;
  const PixelRect required_overview_bounds = overview_bounds_for_world(affected_world_bounds);
  const int overview_width = overview_publication.bounds.x1 - overview_publication.bounds.x0;
  const int overview_height = overview_publication.bounds.y1 - overview_publication.bounds.y0;
  const std::size_t expected_overview_pixels =
      valid_overview_bounds(overview_publication.bounds)
          ? static_cast<std::size_t>(overview_width) * static_cast<std::size_t>(overview_height)
          : 0U;
  const bool source_is_external =
      accepts_external_workspace(std::as_bytes(overview_publication.pixels));
  if (!ready() || !overview_valid_ || !revision_advances_once ||
      !valid_world_bounds(affected_world_bounds) ||
      overview_publication.bounds != required_overview_bounds || expected_overview_pixels == 0U ||
      overview_publication.pixels.size() != expected_overview_pixels || !source_is_external) {
    return false;
  }

  std::size_t raw_publications = 0;
  for (std::size_t index = 0; index < tile_publications.size(); ++index) {
    const TileRevisionPublication& publication = tile_publications[index];
    const PixelRect bounds = tile_pixel_bounds(publication.key);
    const int width = bounds.x1 - bounds.x0;
    const int height = bounds.y1 - bounds.y0;
    const std::size_t expected_pixels =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const bool key_is_affected =
        rectangles_intersect(tile_world_bounds(publication.key), affected_world_bounds);
    const bool publication_is_unique =
        std::find_if(tile_publications.begin(),
                     tile_publications.begin() + static_cast<std::ptrdiff_t>(index),
                     [&publication](const auto& prior) { return prior.key == publication.key; }) ==
        tile_publications.begin() + static_cast<std::ptrdiff_t>(index);
    const bool resident =
        find_tile(publication.key).has_value() || find_uniform(publication.key).has_value();
    const auto classified_color =
        tile_uniform_color(publication.pixels, width, height, static_cast<std::size_t>(width));
    if (!key_is_affected || !publication_is_unique || !resident ||
        publication.quality == MaterializationQuality::kOverviewFallback ||
        publication.pixels.size() != expected_pixels ||
        !accepts_external_workspace(std::as_bytes(publication.pixels))) {
      return false;
    }
    raw_publications += !classified_color.has_value();
  }
  return raw_publications <= slots_.size();
}

void MaterializedCanvas::write_tile(std::size_t slot_index,
                                    const TileRevisionPublication& publication,
                                    DocumentRevision revision) {
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr && slots_[slot_index].occupied_ &&
      !(slots_[slot_index].key_ == publication.key)) {
    rerender_ledger_->mark_evicted(slots_[slot_index].key_);
  }
#endif
  const PixelRect bounds = tile_pixel_bounds(publication.key);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  auto destination = tile_pixels_.subspan(slot_index * kTilePixels, kTilePixels);
  for (int row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    const auto destination_offset =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(kTileWidth);
    std::copy_n(publication.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), width,
                destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  release_slot(slot_index);
  MaterializedSlotStorage& slot = slots_[slot_index];
  slot.key_ = publication.key;
  slot.revision_ = revision;
  slot.quality_ = publication.quality;
  claim_slot(slot_index);
  touch(slot);
}

void MaterializedCanvas::apply_overview_publication(
    const OverviewRevisionPublication& overview_publication) {
  const int overview_width = overview_publication.bounds.x1 - overview_publication.bounds.x0;
  const int overview_height = overview_publication.bounds.y1 - overview_publication.bounds.y0;
  for (int row = 0; row < overview_height; ++row) {
    const auto source_offset =
        static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(overview_width);
    const auto destination_offset =
        static_cast<std::ptrdiff_t>(overview_publication.bounds.y0 + row) * kOverviewWidth +
        static_cast<std::ptrdiff_t>(overview_publication.bounds.x0);
    const auto source = overview_publication.pixels.begin() + source_offset;
    auto destination = overview_pixels_.begin() + destination_offset;
    std::copy_n(source, overview_width, destination);
  }
}

void MaterializedCanvas::finish_revision(DocumentRevision revision,
                                         PixelRect affected_world_bounds) {
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr) {
    rerender_ledger_->mark_world_damage(affected_world_bounds);
  }
#endif
  // Damage can add ink but cannot prove its absence. Erasers deliberately
  // remain conservative until the next authority-derived bootstrap.
  mark_occupied(affected_world_bounds);
  current_revision_ = revision;
  overview_revision_ = revision;
}

bool MaterializedCanvas::commit_history_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::uint64_t authority_timeline,
    std::uint16_t departing_prefix, std::uint16_t arriving_prefix) {
  if (!valid_incremental_revision(revision, overview_publication, affected_world_bounds, {})) {
    return false;
  }
  ++composition_epoch_;
  last_history_commit_stats_ = {};
  if (preserved_epoch_ != authority_timeline) {
    // Branch replacement created new authority: every preserved pre-image
    // belongs to a dead timeline.
    drop_preserved_slots();
    preserved_epoch_ = authority_timeline;
  }
  apply_overview_publication(overview_publication);
  invalidate_uniforms(affected_world_bounds);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    MaterializedSlotStorage& slot = slots_[index];
    if (!slot.occupied_) {
      continue;
    }
    const bool affected = rectangles_intersect(tile_world_bounds(slot.key_), affected_world_bounds);
    if (affected) {
      // Retag instead of discarding: this content is the exact pre-image of
      // the departing state and becomes the runway for the inverse move.
      release_slot(index);
      slot.preserved_ = true;
      slot.preserved_prefix_ = departing_prefix;
      ++preserved_slots_;
      touch(slot);
      ++last_history_commit_stats_.preserved;
    } else {
      slot.revision_ = revision;
    }
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    MaterializedSlotStorage& slot = slots_[index];
    if (!slot.preserved_ || slot.preserved_prefix_ != arriving_prefix) {
      continue;
    }
    if (find_tile(slot.key_).has_value()) {
      // A current resident already covers this identity exactly.
      continue;
    }
    slot.preserved_ = false;
    --preserved_slots_;
    slot.revision_ = revision;
    claim_slot(index);
    touch(slot);
    ++last_history_commit_stats_.swapped_in;
  }
  // Every retagged tile left current residency; swap-ins are the arriving
  // identities restored without rebuild.
  last_history_commit_stats_.invalidated = last_history_commit_stats_.preserved;
  finish_revision(revision, affected_world_bounds);
  return true;
}

bool MaterializedCanvas::commit_incremental_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::span<const TileRevisionPublication> tile_publications) {
  if (!valid_incremental_revision(revision, overview_publication, affected_world_bounds,
                                  tile_publications)) {
    return false;
  }
  ++composition_epoch_;

  apply_overview_publication(overview_publication);
  invalidate_uniforms(affected_world_bounds);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    MaterializedSlotStorage& slot = slots_[index];
    if (!slot.occupied_) {
      continue;
    }
    const bool affected = rectangles_intersect(tile_world_bounds(slot.key_), affected_world_bounds);
    const bool published = std::any_of(
        tile_publications.begin(), tile_publications.end(),
        [&slot](const TileRevisionPublication& candidate) { return candidate.key == slot.key_; });
    if (affected && !published) {
      release_slot(index);
    } else if (!affected) {
      slot.revision_ = revision;
    }
  }
  for (const TileRevisionPublication& publication : tile_publications) {
    const PixelRect bounds = tile_pixel_bounds(publication.key);
    const auto classified_color =
        tile_uniform_color(publication.pixels, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0,
                           static_cast<std::size_t>(bounds.x1 - bounds.x0));
    const auto uniform_index = tile_identity_index(publication.key);
    if (classified_color.has_value() && uniform_index.has_value()) {
      if (const auto slot_index = find_tile(publication.key); slot_index.has_value()) {
        release_slot(*slot_index);
      }
      MaterializedUniformStorage& uniform = uniform_catalog_[*uniform_index];
      uniform.color_ = *classified_color;
      uniform.quality_ = publication.quality;
      uniform.occupied_ = true;
      continue;
    }
    const auto existing = find_tile(publication.key);
    const auto selected = existing.has_value() ? existing : choose_slot();
    if (!selected.has_value()) {
      return false;
    }
    write_tile(*selected, publication, revision);
  }
  finish_revision(revision, affected_world_bounds);
  return true;
}

bool MaterializedCanvas::can_edit_in_place_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds) const {
  return valid_incremental_revision(revision, overview_publication, affected_world_bounds, {});
}

std::optional<InPlaceTileEdit> MaterializedCanvas::edit_resident_tile(TileKey key) {
  const auto slot_index = find_tile(key);
  if (!slot_index.has_value()) {
    return std::nullopt;
  }
  MaterializedSlotStorage& slot = slots_[*slot_index];
  if (slot.revision_ != current_revision_) {
    return std::nullopt;
  }
  ++composition_epoch_;
  // Touch keeps the edited slot off the LRU floor so a same-commit uniform
  // conversion cannot evict it. Fresh hard-edged ink also demotes a settled
  // tile so the idle settle pass revisits it.
  slot.quality_ = MaterializationQuality::kImmediate;
  touch(slot);
  return InPlaceTileEdit{
      .key = key,
      .bounds = tile_pixel_bounds(key),
      .pixels = tile_pixels_.subspan(*slot_index * kTilePixels, kTilePixels),
  };
}

std::optional<InPlaceTileEdit> MaterializedCanvas::materialize_uniform_as_raw(TileKey key) {
  const auto uniform_index = find_uniform(key);
  if (!uniform_index.has_value()) {
    return std::nullopt;
  }
  const auto selected = choose_slot();
  if (!selected.has_value()) {
    return std::nullopt;
  }
  ++composition_epoch_;
  MaterializedUniformStorage& uniform = uniform_catalog_[*uniform_index];
  MaterializedSlotStorage& slot = slots_[*selected];
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr && slot.occupied_ && !(slot.key_ == key)) {
    rerender_ledger_->mark_evicted(slot.key_);
  }
#endif
  release_slot(*selected);
  auto pixels = tile_pixels_.subspan(*selected * kTilePixels, kTilePixels);
  std::fill(pixels.begin(), pixels.end(), uniform.color_);
  slot.key_ = key;
  slot.revision_ = current_revision_;
  slot.quality_ = uniform.quality_;
  claim_slot(*selected);
  touch(slot);
  uniform.occupied_ = false;
  return InPlaceTileEdit{
      .key = key,
      .bounds = tile_pixel_bounds(key),
      .pixels = pixels,
  };
}

std::optional<std::uint16_t> MaterializedCanvas::uniform_color(TileKey key) const {
  const auto index = find_uniform(key);
  if (!index.has_value()) {
    return std::nullopt;
  }
  return uniform_catalog_[*index].color_;
}

bool MaterializedCanvas::commit_in_place_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::span<const TileKey> retained_keys,
    const InPlaceCommitScope& scope) {
  if (!valid_incremental_revision(revision, overview_publication, affected_world_bounds, {})) {
    return false;
  }
  ++composition_epoch_;
  apply_overview_publication(overview_publication);
  finish_in_place_revision(revision, affected_world_bounds, retained_keys, scope);
  return true;
}

void MaterializedCanvas::finish_in_place_revision(DocumentRevision revision,
                                                  PixelRect affected_world_bounds,
                                                  std::span<const TileKey> retained_keys,
                                                  const InPlaceCommitScope& scope) {
  for (const TileKey key : retained_keys) {
    mark_retained_key(key, revision);
  }
  invalidate_uniforms(affected_world_bounds, scope);
  for (const TileKey key : retained_keys) {
    clear_retained_marker(key);
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    MaterializedSlotStorage& slot = slots_[index];
    if (!slot.occupied_) {
      continue;
    }
    const bool affected = rectangles_intersect(tile_world_bounds(slot.key_), affected_world_bounds);
    const bool retained = !affected || slot.revision_ == revision;
    if (retained) {
      slot.revision_ = revision;
    } else {
      if (scope.cross_zoom_invalidated != nullptr && slot.key_.zoom != scope.priority_zoom) {
        ++*scope.cross_zoom_invalidated;
      }
      release_slot(index);
    }
  }
  finish_revision(revision, affected_world_bounds);
}

void MaterializedCanvas::mark_retained_key(TileKey key, DocumentRevision revision) {
  if (const auto raw = find_tile(key); raw.has_value()) {
    slots_[*raw].revision_ = revision;
    return;
  }
  if (const auto uniform = find_uniform(key); uniform.has_value()) {
    raw_slot_directory_[*uniform] = kRetainedUniformSlot;
  }
}

void MaterializedCanvas::clear_retained_marker(TileKey key) {
  if (const auto identity = tile_identity_index(key);
      identity.has_value() && raw_slot_directory_[*identity] == kRetainedUniformSlot) {
    raw_slot_directory_[*identity] = kNoRawSlot;
  }
}

OverviewStageStatus MaterializedCanvas::stage_in_place_overview_rows(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::size_t max_rows, InPlaceOverviewStage& stage) {
  if (max_rows == 0U ||
      !valid_incremental_revision(revision, overview_publication, affected_world_bounds, {})) {
    return OverviewStageStatus::kError;
  }
  if (!stage.active()) {
    stage.canvas_ = this;
    stage.revision_ = revision;
    stage.bounds_ = overview_publication.bounds;
    stage.affected_world_bounds_ = affected_world_bounds;
    stage.source_ = overview_publication.pixels.data();
    stage.source_size_ = overview_publication.pixels.size();
    stage.expected_canvas_epoch_ = composition_epoch_;
    stage.next_row_ = overview_publication.bounds.y0;
  } else if (stage.canvas_ != this || stage.revision_ != revision ||
             stage.bounds_ != overview_publication.bounds ||
             stage.affected_world_bounds_ != affected_world_bounds ||
             stage.source_ != overview_publication.pixels.data() ||
             stage.source_size_ != overview_publication.pixels.size() ||
             stage.expected_canvas_epoch_ != composition_epoch_) {
    return OverviewStageStatus::kError;
  }
  if (stage.complete()) {
    return OverviewStageStatus::kComplete;
  }

  const int width = stage.bounds_.x1 - stage.bounds_.x0;
  const std::size_t remaining_rows = static_cast<std::size_t>(stage.bounds_.y1 - stage.next_row_);
  const int rows = static_cast<int>(std::min(remaining_rows, max_rows));
  for (int offset = 0; offset < rows; ++offset) {
    const int row = stage.next_row_ - stage.bounds_.y0 + offset;
    const auto source =
        overview_publication.pixels.begin() + static_cast<std::ptrdiff_t>(row) * width;
    auto destination = overview_pixels_.begin() +
                       static_cast<std::ptrdiff_t>(stage.next_row_ + offset) * kOverviewWidth +
                       stage.bounds_.x0;
    std::copy_n(source, width, destination);
  }
  stage.next_row_ += rows;
  ++composition_epoch_;
  stage.expected_canvas_epoch_ = composition_epoch_;
  return stage.complete() ? OverviewStageStatus::kComplete : OverviewStageStatus::kInProgress;
}

bool MaterializedCanvas::commit_staged_in_place_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::span<const TileKey> retained_keys,
    InPlaceOverviewStage& stage, const InPlaceCommitScope& scope) {
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  const RerenderLedger* ledger = rerender_ledger_;
#else
  const RerenderLedger* ledger = nullptr;
#endif
  if (!valid_incremental_revision(revision, overview_publication, affected_world_bounds, {}) ||
      !stage.complete() || stage.canvas_ != this || stage.revision_ != revision ||
      stage.bounds_ != overview_publication.bounds ||
      stage.affected_world_bounds_ != affected_world_bounds ||
      stage.source_ != overview_publication.pixels.data() ||
      stage.source_size_ != overview_publication.pixels.size() ||
      stage.expected_canvas_epoch_ != composition_epoch_ || !stage.metadata_started_ ||
      stage.retained_keys_ != retained_keys.data() ||
      stage.retained_count_ != retained_keys.size() ||
      stage.preserved_uniform_color_ != scope.preserved_uniform_color ||
      stage.priority_zoom_ != scope.priority_zoom || stage.rerender_ledger_ != ledger ||
      stage.metadata_phase_ != InPlaceMetadataPhase::kComplete || !stage.raw_staging_started_ ||
      !staged_in_place_active_ || staged_in_place_revision_ != revision) {
    return false;
  }
  ++composition_epoch_;
  current_revision_ = revision;
  overview_revision_ = revision;
  if (scope.cross_zoom_invalidated != nullptr) {
    *scope.cross_zoom_invalidated += stage.cross_zoom_invalidated_;
  }
  staged_in_place_active_ = false;
  stage.raw_staging_started_ = false;
  stage.cancel();
  return true;
}

}  // namespace tinydraw::vector_v2
