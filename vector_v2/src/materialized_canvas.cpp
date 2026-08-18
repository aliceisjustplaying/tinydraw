#include "tinydraw/vector_v2/materialized_canvas.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
#include "tinydraw/vector_v2/rerender_ledger.h"
#endif
#include "tinydraw/vector_v2/storage_overlap.h"
#include "tinydraw/vector_v2/tile_payload_analysis.h"

namespace tinydraw::vector_v2 {
namespace {

constexpr int ceil_div(int numerator, int denominator) {
  return (numerator + denominator - 1) / denominator;
}

int scaled_extent(int world_extent, ZoomLevel zoom) {
  return world_extent * zoom_percent(zoom) / 100;
}

constexpr int clamp_end(int start, int extent, int limit) {
  return std::min(start + extent, limit);
}

bool rectangles_intersect(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

PixelRect tile_world_bounds(TileKey key) {
  const PixelRect level = tile_pixel_bounds(key);
  const int percent = zoom_percent(key.zoom);
  return {
      .x0 = level.x0 * 100 / percent,
      .y0 = level.y0 * 100 / percent,
      .x1 = ceil_div(level.x1 * 100, percent),
      .y1 = ceil_div(level.y1 * 100, percent),
  };
}

bool valid_world_bounds(PixelRect bounds) {
  return bounds.x0 >= 0 && bounds.y0 >= 0 && bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
         bounds.x1 <= kWorldWidth && bounds.y1 <= kWorldHeight;
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

MaterializedCanvas::MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                                       std::span<MaterializedUniformStorage> uniform_catalog,
                                       std::span<std::uint8_t> occupancy_bits,
                                       std::span<MaterializedSlotStorage> slots,
                                       std::span<std::uint16_t> tile_pixels,
                                       DocumentRevision initial_revision,
                                       std::span<std::uint16_t> raw_slot_directory)
    : overview_pixels_(overview_pixels),
      uniform_catalog_(uniform_catalog),
      occupancy_bits_(occupancy_bits),
      slots_(slots),
      tile_pixels_(tile_pixels),
      raw_slot_directory_(raw_slot_directory),
      current_revision_(initial_revision) {
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
         slots_.size() < static_cast<std::size_t>(kNoRawSlot) &&
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
                                                  std::span<const TileKey> retained_keys,
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
      const bool retained =
          (scope.preserved_uniform_color.has_value() &&
           uniform_catalog_[*index].color_ == *scope.preserved_uniform_color) ||
          std::find(retained_keys.begin(), retained_keys.end(), key) != retained_keys.end();
      if (!retained && scope.cross_zoom_invalidated != nullptr && zoom != scope.priority_zoom) {
        ++*scope.cross_zoom_invalidated;
      }
      uniform_catalog_[*index].occupied_ = retained;
    }
  }
}

void MaterializedCanvas::invalidate_uniforms(PixelRect world_bounds,
                                             std::span<const TileKey> retained_keys,
                                             const InPlaceCommitScope& scope) {
  if (!valid_world_bounds(world_bounds)) {
    return;
  }
  // Walk only a conservative tile window per zoom instead of all 13,692
  // identities.
  constexpr std::array zooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                             ZoomLevel::k400Percent};
  for (const ZoomLevel zoom : zooms) {
    invalidate_zoom_uniforms(zoom, world_bounds, retained_keys, scope);
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
  if (!exact_bootstrap_source) {
    std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].occupied_) {
      release_slot(index);
    }
  }
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
  clear_uniforms();
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_valid_ = true;
  return true;
}

bool MaterializedCanvas::reset_blank(DocumentRevision revision) {
  if (!ready()) {
    return false;
  }
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
    const auto analysis = analyze_tile_payload(publication.pixels, width, height);
    if (!key_is_affected || !publication_is_unique || !resident ||
        publication.quality == MaterializationQuality::kOverviewFallback ||
        publication.pixels.size() != expected_pixels || !analysis.has_value() ||
        !accepts_external_workspace(std::as_bytes(publication.pixels))) {
      return false;
    }
    raw_publications += !analysis->uniform;
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

bool MaterializedCanvas::commit_incremental_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::span<const TileRevisionPublication> tile_publications) {
  if (!valid_incremental_revision(revision, overview_publication, affected_world_bounds,
                                  tile_publications)) {
    return false;
  }

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
    const auto analysis =
        analyze_tile_payload(publication.pixels, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
    const auto uniform_index = tile_identity_index(publication.key);
    if (analysis.has_value() && analysis->uniform && uniform_index.has_value()) {
      if (const auto slot_index = find_tile(publication.key); slot_index.has_value()) {
        release_slot(*slot_index);
      }
      MaterializedUniformStorage& uniform = uniform_catalog_[*uniform_index];
      uniform.color_ = analysis->uniform_color;
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
  apply_overview_publication(overview_publication);
  invalidate_uniforms(affected_world_bounds, retained_keys, scope);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    MaterializedSlotStorage& slot = slots_[index];
    if (!slot.occupied_) {
      continue;
    }
    const bool affected = rectangles_intersect(tile_world_bounds(slot.key_), affected_world_bounds);
    const bool retained = !affected || std::find(retained_keys.begin(), retained_keys.end(),
                                                 slot.key_) != retained_keys.end();
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
  return true;
}

void MaterializedCanvas::invalidate_identity(TileKey key) {
  if (const auto slot_index = find_tile(key); slot_index.has_value()) {
    release_slot(*slot_index);
  }
  if (const auto uniform_index = find_uniform(key); uniform_index.has_value()) {
    uniform_catalog_[*uniform_index].occupied_ = false;
  }
}

bool MaterializedCanvas::accepts_external_workspace(std::span<const std::byte> workspace) const {
  return !storage_overlaps(workspace, std::as_bytes(std::span(overview_pixels_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(uniform_catalog_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(occupancy_bits_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(tile_pixels_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(slots_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(raw_slot_directory_)));
}

bool MaterializedCanvas::copy_resident_tile(TileKey key,
                                            std::span<std::uint16_t> destination) const {
  const auto slot_index = find_tile(key);
  const auto uniform_index = find_uniform(key);
  if (!slot_index.has_value() && !uniform_index.has_value()) {
    return false;
  }
  const PixelRect bounds = tile_pixel_bounds(key);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (destination.size() != expected || !accepts_external_workspace(std::as_bytes(destination))) {
    return false;
  }
  if (uniform_index.has_value()) {
    std::fill(destination.begin(), destination.end(), uniform_catalog_[*uniform_index].color_);
    return true;
  }
  const auto source = tile_pixels_.subspan(*slot_index * kTilePixels, kTilePixels);
  for (int row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::size_t>(row) * kTileWidth;
    const auto destination_offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(source_offset), width,
                destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  return true;
}

std::optional<std::size_t> MaterializedCanvas::append_visible_uniform_keys(
    PixelRect world_bounds, ViewRequest view, std::span<TileKey> output,
    std::size_t written) const {
  const int percent = zoom_percent(view.zoom);
  const PixelRect operation_bounds{
      .x0 = world_bounds.x0 * percent / 100,
      .y0 = world_bounds.y0 * percent / 100,
      .x1 =
          std::min(scaled_extent(kWorldWidth, view.zoom), ceil_div(world_bounds.x1 * percent, 100)),
      .y1 = std::min(scaled_extent(kWorldHeight, view.zoom),
                     ceil_div(world_bounds.y1 * percent, 100)),
  };
  const PixelRect affected{
      .x0 = std::max(operation_bounds.x0, view.level_pixels.x0),
      .y0 = std::max(operation_bounds.y0, view.level_pixels.y0),
      .x1 = std::min(operation_bounds.x1, view.level_pixels.x1),
      .y1 = std::min(operation_bounds.y1, view.level_pixels.y1),
  };
  if (affected.x0 >= affected.x1 || affected.y0 >= affected.y1) {
    return written;
  }
  const int first_column = affected.x0 / kTileWidth;
  const int last_column = (affected.x1 - 1) / kTileWidth;
  const int first_row = affected.y0 / kTileHeight;
  const int last_row = (affected.y1 - 1) / kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      if (!find_uniform(key).has_value()) {
        continue;
      }
      if (written == output.size()) {
        return std::nullopt;
      }
      output[written++] = key;
    }
  }
  return written;
}

std::optional<std::size_t> MaterializedCanvas::materialized_tiles_intersecting(
    PixelRect world_bounds, std::span<TileKey> output, std::optional<ViewRequest> priority_view,
    bool priority_view_only) const {
  if (!ready() || !valid_world_bounds(world_bounds) ||
      !accepts_external_workspace(std::as_bytes(output)) ||
      (priority_view_only && !priority_view.has_value())) {
    return std::nullopt;
  }
  std::size_t written = 0;
  for (const MaterializedSlotStorage& slot : slots_) {
    const bool in_priority =
        priority_view.has_value() && slot.key_.zoom == priority_view->zoom &&
        rectangles_intersect(tile_pixel_bounds(slot.key_), priority_view->level_pixels);
    if (!slot.occupied_ || !rectangles_intersect(tile_world_bounds(slot.key_), world_bounds) ||
        (priority_view_only && !in_priority)) {
      continue;
    }
    if (written == output.size()) {
      return std::nullopt;
    }
    output[written++] = slot.key_;
  }
  if (!priority_view.has_value()) {
    return written;
  }
  const PixelRect bounds = priority_view->level_pixels;
  const bool priority_is_valid = priority_view->zoom != ZoomLevel::k25Percent && bounds.x0 >= 0 &&
                                 bounds.y0 >= 0 && bounds.x0 < bounds.x1 && bounds.y0 < bounds.y1 &&
                                 bounds.x1 <= scaled_extent(kWorldWidth, priority_view->zoom) &&
                                 bounds.y1 <= scaled_extent(kWorldHeight, priority_view->zoom);
  return priority_is_valid
             ? append_visible_uniform_keys(world_bounds, *priority_view, output, written)
             : std::nullopt;
}

std::optional<std::size_t> MaterializedCanvas::append_recent_view_uniform_keys(
    PixelRect world_bounds, std::optional<ZoomLevel> exclude_zoom, std::span<TileKey> output,
    std::size_t written) const {
  if (!ready() || !valid_world_bounds(world_bounds) ||
      !accepts_external_workspace(std::as_bytes(output))) {
    return std::nullopt;
  }
  for (const ViewFootprint& view : recent_views_) {
    if (!view.valid || view.zoom == ZoomLevel::k25Percent ||
        (exclude_zoom.has_value() && view.zoom == *exclude_zoom)) {
      continue;
    }
    const auto appended = append_visible_uniform_keys(
        world_bounds, {.zoom = view.zoom, .level_pixels = view.level_pixels}, output, written);
    if (!appended.has_value()) {
      return std::nullopt;
    }
    written = *appended;
  }
  return written;
}

std::optional<std::size_t> MaterializedCanvas::find_tile(TileKey key) const {
  const auto identity = tile_identity_index(key);
  if (raw_slot_directory_.size() != kMaterializedTileIdentityCount || !identity.has_value()) {
    return std::nullopt;
  }
  const std::uint16_t index = raw_slot_directory_[*identity];
  if (index == kNoRawSlot || static_cast<std::size_t>(index) >= slots_.size() ||
      !slots_[index].occupied_ || !(slots_[index].key_ == key)) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(index);
}

std::optional<std::size_t> MaterializedCanvas::find_uniform(TileKey key) const {
  const auto index = tile_identity_index(key);
  if (uniform_catalog_.size() != kMaterializedTileIdentityCount || !index.has_value() ||
      !uniform_catalog_[*index].occupied_) {
    return std::nullopt;
  }
  return index;
}

std::uint8_t MaterializedCanvas::protection_rank(TileKey key) const {
  if (key.zoom == ZoomLevel::k25Percent) {
    return 0U;
  }
  const auto index = static_cast<std::size_t>(key.zoom) - 1U;
  if (index >= recent_views_.size()) {
    return 0U;
  }
  const ViewFootprint& footprint = recent_views_[index];
  if (!footprint.valid || !rectangles_intersect(tile_pixel_bounds(key), footprint.level_pixels)) {
    return 0U;
  }
  return key.zoom == active_view_zoom_ ? 2U : 1U;
}

std::optional<std::size_t> MaterializedCanvas::choose_slot() const {
  // A full pool has no unoccupied slot by definition; skip the free scan.
  if (occupied_slots_ < slots_.size()) {
    const auto available = std::find_if(slots_.begin(), slots_.end(),
                                        [](const auto& slot) { return !slot.occupied_; });
    if (available != slots_.end()) {
      return static_cast<std::size_t>(available - slots_.begin());
    }
  }
  const auto oldest =
      std::min_element(slots_.begin(), slots_.end(), [this](const auto& left, const auto& right) {
        return std::tuple(protection_rank(left.key_), left.last_use_) <
               std::tuple(protection_rank(right.key_), right.last_use_);
      });
  if (oldest == slots_.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(oldest - slots_.begin());
}

void MaterializedCanvas::touch(MaterializedSlotStorage& slot) { slot.last_use_ = ++use_clock_; }

void MaterializedCanvas::release_slot(std::size_t index) {
  MaterializedSlotStorage& slot = slots_[index];
  if (!slot.occupied_) {
    return;
  }
  slot.occupied_ = false;
  --occupied_slots_;
  if (const auto identity = tile_identity_index(slot.key_);
      identity.has_value() && raw_slot_directory_[*identity] == static_cast<std::uint16_t>(index)) {
    raw_slot_directory_[*identity] = kNoRawSlot;
  }
}

void MaterializedCanvas::claim_slot(std::size_t index) {
  MaterializedSlotStorage& slot = slots_[index];
  if (!slot.occupied_) {
    slot.occupied_ = true;
    ++occupied_slots_;
  }
  if (const auto identity = tile_identity_index(slot.key_); identity.has_value()) {
    raw_slot_directory_[*identity] = static_cast<std::uint16_t>(index);
  }
}

std::optional<std::size_t> MaterializedCanvas::publish_tile(TileKey key, DocumentRevision revision,
                                                            MaterializationQuality quality,
                                                            std::span<const std::uint16_t> pixels) {
  const PixelRect bounds = tile_pixel_bounds(key);
  return publish_tile(key, revision, quality, pixels,
                      static_cast<std::size_t>(bounds.x1 - bounds.x0));
}

std::optional<std::size_t> MaterializedCanvas::publish_tile(TileKey key, DocumentRevision revision,
                                                            MaterializationQuality quality,
                                                            std::span<const std::uint16_t> pixels,
                                                            std::size_t source_stride) {
  const PixelRect bounds = tile_pixel_bounds(key);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const bool source_overlaps_pool = !accepts_external_workspace(std::as_bytes(pixels));
  if (!ready() || slots_.empty() || !valid_tile_key(key) || revision != current_revision_ ||
      quality == MaterializationQuality::kOverviewFallback || source_overlaps_pool ||
      source_stride < static_cast<std::size_t>(width)) {
    return std::nullopt;
  }
  const std::size_t expected_pixels =
      static_cast<std::size_t>(height - 1) * source_stride + static_cast<std::size_t>(width);
  if (pixels.size() != expected_pixels) {
    return std::nullopt;
  }
  const auto existing = find_tile(key);
  if (existing.has_value() &&
      static_cast<int>(quality) < static_cast<int>(slots_[*existing].quality_)) {
    return std::nullopt;
  }
  // Same-revision quality must not regress through the representation swap:
  // a raw publication below an existing uniform's quality is a downgrade.
  if (const auto uniform = find_uniform(key);
      uniform.has_value() &&
      static_cast<int>(quality) < static_cast<int>(uniform_catalog_[*uniform].quality_)) {
    return std::nullopt;
  }
  const auto selected = existing.has_value() ? existing : choose_slot();
  if (!selected.has_value()) {
    return std::nullopt;
  }
  const std::size_t index = *selected;
  MaterializedSlotStorage& slot = slots_[index];
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr && slot.occupied_ && !(slot.key_ == key)) {
    rerender_ledger_->mark_evicted(slot.key_);
  }
#endif
  release_slot(index);
  auto destination = tile_pixels_.subspan(index * kTilePixels, kTilePixels);
  for (int row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::size_t>(row) * source_stride;
    const auto destination_offset =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(kTileWidth);
    std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), width,
                destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  slot.key_ = key;
  slot.revision_ = revision;
  slot.quality_ = quality;
  claim_slot(index);
  if (const auto uniform = tile_identity_index(key); uniform.has_value()) {
    uniform_catalog_[*uniform].occupied_ = false;
  }
  touch(slot);
  return index;
}

std::optional<std::size_t> MaterializedCanvas::publish_uniform(TileKey key,
                                                               DocumentRevision revision,
                                                               MaterializationQuality quality,
                                                               std::uint16_t color) {
  const auto index = tile_identity_index(key);
  if (!ready() || !index.has_value() || revision != current_revision_ ||
      quality == MaterializationQuality::kOverviewFallback) {
    return std::nullopt;
  }
  if (const auto raw = find_tile(key); raw.has_value()) {
    MaterializedSlotStorage& slot = slots_[*raw];
    if (static_cast<int>(quality) < static_cast<int>(slot.quality_)) {
      return std::nullopt;
    }
    release_slot(*raw);
  }
  MaterializedUniformStorage& uniform = uniform_catalog_[*index];
  if (uniform.occupied_ && static_cast<int>(quality) < static_cast<int>(uniform.quality_)) {
    return std::nullopt;
  }
  uniform.color_ = color;
  uniform.quality_ = quality;
  uniform.occupied_ = true;
  return index;
}

std::optional<SourceSelection> MaterializedCanvas::lookup(TileKey key) const {
  if (!ready() || !valid_tile_key(key)) {
    return std::nullopt;
  }
  const std::optional<std::size_t> tile_index = find_tile(key);
  if (tile_index.has_value() && slots_[*tile_index].revision_ == current_revision_) {
    return SourceSelection{.kind = SourceKind::kTileSlot,
                           .revision = slots_[*tile_index].revision_,
                           .quality = slots_[*tile_index].quality_};
  }
  if (const auto uniform = find_uniform(key); uniform.has_value()) {
    return SourceSelection{.kind = SourceKind::kUniform,
                           .revision = current_revision_,
                           .quality = uniform_catalog_[*uniform].quality_};
  }
  if (overview_valid_ && overview_revision_ == current_revision_) {
    return SourceSelection{.kind = SourceKind::kOverview,
                           .revision = overview_revision_,
                           .quality = MaterializationQuality::kOverviewFallback};
  }
  return std::nullopt;
}

bool MaterializedCanvas::remember_view(const ViewRequest& view) {
  const int width = view.level_pixels.x1 - view.level_pixels.x0;
  const int height = view.level_pixels.y1 - view.level_pixels.y0;
  const std::size_t pixel_count =
      static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
  if (!ready() || view.zoom == ZoomLevel::k25Percent || !valid_view(view, pixel_count)) {
    return false;
  }
  const auto index = static_cast<std::size_t>(view.zoom) - 1U;
  if (index >= recent_views_.size()) {
    return false;
  }
  recent_views_[index] = {.zoom = view.zoom, .level_pixels = view.level_pixels, .valid = true};
  active_view_zoom_ = view.zoom;
  return true;
}

bool MaterializedCanvas::discard_tiles() {
  if (!ready()) {
    return false;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (!slots_[index].occupied_) {
      continue;
    }
    release_slot(index);
  }
  clear_uniforms();
  return true;
}

bool MaterializedCanvas::valid_view(const ViewRequest& request,
                                    std::size_t destination_size) const {
  const PixelRect rect = request.level_pixels;
  const int width = rect.x1 - rect.x0;
  const int height = rect.y1 - rect.y0;
  const std::size_t expected_size =
      static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
  return ready() && width > 0 && height > 0 && rect.x0 >= 0 && rect.y0 >= 0 &&
         rect.x1 <= scaled_extent(kWorldWidth, request.zoom) &&
         rect.y1 <= scaled_extent(kWorldHeight, request.zoom) && destination_size == expected_size;
}

bool MaterializedCanvas::has_complete_source(const ViewRequest& request) const {
  if (overview_valid_ && overview_revision_ == current_revision_) {
    return true;
  }
  const PixelRect rect = request.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{request.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      const auto index = find_tile(key);
      const bool raw_current = index.has_value() && slots_[*index].revision_ == current_revision_;
      if (!raw_current && !find_uniform(key).has_value()) {
        return false;
      }
    }
  }
  return true;
}

std::optional<ViewCompositionStats> MaterializedCanvas::compose_overview_view(
    const ViewRequest& request, std::span<std::uint16_t> destination) const {
  if (!overview_valid_ || overview_revision_ != current_revision_) {
    return std::nullopt;
  }
  const PixelRect rect = request.level_pixels;
  const int width = rect.x1 - rect.x0;
  for (int y = rect.y0; y < rect.y1; ++y) {
    const auto source_offset =
        static_cast<std::size_t>(y) * kOverviewWidth + static_cast<std::size_t>(rect.x0);
    const auto destination_offset =
        static_cast<std::size_t>(y - rect.y0) * static_cast<std::size_t>(width);
    std::copy_n(overview_pixels_.begin() + static_cast<std::ptrdiff_t>(source_offset), width,
                destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  return ViewCompositionStats{
      .revision = current_revision_,
      .overview_pixels = destination.size(),
  };
}

void MaterializedCanvas::include_quality(MaterializationQuality quality,
                                         ViewCompositionStats& stats) {
  stats.immediate_tiles += quality == MaterializationQuality::kImmediate;
  stats.settled_tiles += quality == MaterializationQuality::kSettled;
}

void MaterializedCanvas::compose_raw_pixels(std::size_t slot_index, PixelRect bounds,
                                            CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  const std::size_t tile_base = slot_index * kTilePixels;
  const int local_x = bounds.x0 % kTileWidth;
  const int width = bounds.x1 - bounds.x0;
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const std::size_t source = tile_base + static_cast<std::size_t>(y % kTileHeight) * kTileWidth +
                               static_cast<std::size_t>(local_x);
    const std::size_t destination =
        static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
        static_cast<std::size_t>(bounds.x0 - view.x0);
    std::copy_n(tile_pixels_.begin() + static_cast<std::ptrdiff_t>(source), width,
                context.destination.begin() + static_cast<std::ptrdiff_t>(destination));
  }
  context.stats.tile_pixels +=
      static_cast<std::size_t>(width) * static_cast<std::size_t>(bounds.y1 - bounds.y0);
}

void MaterializedCanvas::compose_uniform_pixels(std::uint16_t color, PixelRect bounds,
                                                CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const auto offset =
        static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
        static_cast<std::size_t>(bounds.x0 - view.x0);
    std::fill_n(context.destination.begin() + static_cast<std::ptrdiff_t>(offset),
                bounds.x1 - bounds.x0, color);
  }
  context.stats.uniform_pixels += static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                  static_cast<std::size_t>(bounds.y1 - bounds.y0);
}

void MaterializedCanvas::compose_fallback_pixels(PixelRect bounds, CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  const unsigned overview_shift = static_cast<unsigned>(context.request.zoom);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const std::size_t source_row = static_cast<std::size_t>(y >> overview_shift) * kOverviewWidth;
    for (int x = bounds.x0; x < bounds.x1;) {
      const int source_x = x >> overview_shift;
      const int run_end = std::min(bounds.x1, static_cast<int>((source_x + 1) << overview_shift));
      const int run_width = run_end - x;
      const std::size_t destination =
          static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
          static_cast<std::size_t>(x - view.x0);
      std::fill_n(context.destination.begin() + static_cast<std::ptrdiff_t>(destination), run_width,
                  overview_pixels_[source_row + static_cast<std::size_t>(source_x)]);
      x = run_end;
    }
  }
  context.stats.fallback_pixels += static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                   static_cast<std::size_t>(bounds.y1 - bounds.y0);
}

void MaterializedCanvas::compose_tile(TileKey key, CompositionContext& context) {
  const PixelRect tile = tile_pixel_bounds(key);
  const PixelRect view = context.request.level_pixels;
  const PixelRect bounds{.x0 = std::max(view.x0, tile.x0),
                         .y0 = std::max(view.y0, tile.y0),
                         .x1 = std::min(view.x1, tile.x1),
                         .y1 = std::min(view.y1, tile.y1)};
  const auto raw = find_tile(key);
  const bool raw_current = raw.has_value() && slots_[*raw].revision_ == current_revision_;
  if (raw_current) {
    touch(slots_[*raw]);
    include_quality(slots_[*raw].quality_, context.stats);
    compose_raw_pixels(*raw, bounds, context);
    return;
  }
  const auto uniform = find_uniform(key);
  if (uniform.has_value()) {
    include_quality(uniform_catalog_[*uniform].quality_, context.stats);
    compose_uniform_pixels(uniform_catalog_[*uniform].color_, bounds, context);
    return;
  }
  ++context.stats.fallback_tiles;
  compose_fallback_pixels(bounds, context);
}

std::optional<ViewCompositionStats> MaterializedCanvas::compose_view(
    const ViewRequest& request, std::span<std::uint16_t> destination) {
  const bool destination_aliases_source =
      storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(overview_pixels_))) ||
      storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(tile_pixels_))) ||
      storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(slots_)));
  if (!valid_view(request, destination.size()) || destination_aliases_source ||
      !has_complete_source(request)) {
    return std::nullopt;
  }
  if (request.zoom == ZoomLevel::k25Percent) {
    return compose_overview_view(request, destination);
  }
  CompositionContext context{
      .request = request,
      .destination = destination,
      .stats = {.revision = current_revision_},
      .view_width = request.level_pixels.x1 - request.level_pixels.x0,
  };
  const PixelRect rect = request.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      compose_tile(
          {request.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)},
          context);
    }
  }
  return context.stats;
}

}  // namespace tinydraw::vector_v2
