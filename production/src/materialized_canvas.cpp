#include "tinydraw/production/materialized_canvas.h"

#include <algorithm>
#include <limits>

namespace tinydraw::production {
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

TileGrid tile_grid(ZoomLevel zoom) {
  return {
      .columns = ceil_div(scaled_extent(kWorldWidth, zoom), kTileWidth),
      .rows = ceil_div(scaled_extent(kWorldHeight, zoom), kTileHeight),
  };
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

PixelRect overview_source_bounds(TileKey key) {
  const PixelRect destination = tile_pixel_bounds(key);
  if (destination.x1 <= destination.x0 || destination.y1 <= destination.y0) {
    return {};
  }
  const int percent = zoom_percent(key.zoom);
  return {
      .x0 = destination.x0 * 25 / percent,
      .y0 = destination.y0 * 25 / percent,
      .x1 = ceil_div(destination.x1 * 25, percent),
      .y1 = ceil_div(destination.y1 * 25, percent),
  };
}

MaterializedCanvas::MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                                       std::span<MaterializedSlotStorage> slots,
                                       DocumentRevision initial_revision)
    : overview_pixels_(overview_pixels), slots_(slots), current_revision_(initial_revision) {}

bool MaterializedCanvas::ready() const { return overview_pixels_.size() == kOverviewPixels; }

DocumentRevision MaterializedCanvas::current_revision() const { return current_revision_; }

std::size_t MaterializedCanvas::slot_capacity() const { return slots_.size(); }

std::span<const std::uint16_t> MaterializedCanvas::overview_pixels() const {
  return overview_pixels_;
}

bool MaterializedCanvas::advance_revision(DocumentRevision revision) {
  if (!ready() || revision.value <= current_revision_.value) {
    return false;
  }
  current_revision_ = revision;
  return true;
}

bool MaterializedCanvas::publish_overview(DocumentRevision revision) {
  if (!ready() || revision != current_revision_) {
    return false;
  }
  overview_revision_ = revision;
  overview_generation_ = {next_generation_++};
  overview_valid_ = true;
  return true;
}

std::optional<std::size_t> MaterializedCanvas::find_tile(TileKey key) const {
  const auto found = std::find_if(slots_.begin(), slots_.end(), [key](const auto& slot) {
    return slot.occupied_ && slot.key_ == key;
  });
  if (found == slots_.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - slots_.begin());
}

std::size_t MaterializedCanvas::choose_slot() const {
  const auto empty =
      std::find_if(slots_.begin(), slots_.end(), [](const auto& slot) { return !slot.occupied_; });
  if (empty != slots_.end()) {
    return static_cast<std::size_t>(empty - slots_.begin());
  }
  const auto oldest = std::min_element(
      slots_.begin(), slots_.end(),
      [](const auto& left, const auto& right) { return left.last_use_ < right.last_use_; });
  return static_cast<std::size_t>(oldest - slots_.begin());
}

void MaterializedCanvas::touch(MaterializedSlotStorage& slot) { slot.last_use_ = ++use_clock_; }

std::optional<std::size_t> MaterializedCanvas::publish_tile(TileKey key, DocumentRevision revision,
                                                            MaterializationQuality quality) {
  if (!ready() || slots_.empty() || !valid_tile_key(key) || revision != current_revision_ ||
      quality == MaterializationQuality::kOverviewFallback) {
    return std::nullopt;
  }
  const std::size_t index = find_tile(key).value_or(choose_slot());
  MaterializedSlotStorage& slot = slots_[index];
  slot.key_ = key;
  slot.revision_ = revision;
  slot.generation_ = {next_generation_++};
  slot.quality_ = quality;
  slot.occupied_ = true;
  touch(slot);
  return index;
}

SourceSelection MaterializedCanvas::select_overview(TileKey requested) const {
  return {
      .kind = SourceKind::kOverview,
      .identity =
          {
              .revision = overview_revision_,
              .generation = overview_generation_,
              .quality = MaterializationQuality::kOverviewFallback,
              .provenance = MaterializationProvenance::kCompleteOverview,
          },
      .requested_tile = requested,
      .source_pixels = overview_source_bounds(requested),
      .destination_pixels = tile_pixel_bounds(requested),
      .slot_index = 0,
  };
}

SourceSelection MaterializedCanvas::select_tile(TileKey requested, std::size_t slot_index) const {
  const MaterializedSlotStorage& slot = slots_[slot_index];
  const PixelRect bounds = tile_pixel_bounds(requested);
  return {
      .kind = SourceKind::kTileSlot,
      .identity =
          {
              .revision = slot.revision_,
              .generation = slot.generation_,
              .quality = slot.quality_,
              .provenance = MaterializationProvenance::kWorldTile,
          },
      .requested_tile = requested,
      .source_pixels = {0, 0, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0},
      .destination_pixels = bounds,
      .slot_index = slot_index,
  };
}

std::optional<SourceSelection> MaterializedCanvas::lookup(TileKey key) {
  if (!ready() || !valid_tile_key(key)) {
    return std::nullopt;
  }
  const std::optional<std::size_t> tile_index = find_tile(key);
  if (tile_index.has_value() && slots_[*tile_index].revision_ == current_revision_) {
    touch(slots_[*tile_index]);
    return select_tile(key, *tile_index);
  }
  if (overview_valid_ && overview_revision_ == current_revision_) {
    return select_overview(key);
  }
  return std::nullopt;
}

}  // namespace tinydraw::production
