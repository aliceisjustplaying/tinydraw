#include "tinydraw/vector_v2/materialized_canvas.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

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

bool overview_region_is_paper(std::span<const std::uint16_t> pixels, PixelRect bounds) {
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const auto row = pixels.subspan(
        static_cast<std::size_t>(y) * kOverviewWidth + static_cast<std::size_t>(bounds.x0),
        static_cast<std::size_t>(bounds.x1 - bounds.x0));
    if (std::any_of(row.begin(), row.end(), [](std::uint16_t pixel) { return pixel != 0xFFFFU; })) {
      return false;
    }
  }
  return true;
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

PinnedSource::PinnedSource(MaterializedCanvas& owner, const SourceSelection& source)
    : owner_(&owner), source_(source) {}

PinnedSource::~PinnedSource() { reset(); }

PinnedSource::PinnedSource(PinnedSource&& other) noexcept
    : owner_(other.owner_), source_(other.source_) {
  other.owner_ = nullptr;
  other.source_ = {};
}

PinnedSource& PinnedSource::operator=(PinnedSource&& other) noexcept {
  if (this != &other) {
    reset();
    owner_ = other.owner_;
    source_ = other.source_;
    other.owner_ = nullptr;
    other.source_ = {};
  }
  return *this;
}

const SourceSelection& PinnedSource::source() const { return source_; }

bool PinnedSource::valid() const { return owner_ != nullptr && owner_->validate(*this); }

void PinnedSource::reset() {
  if (owner_ != nullptr) {
    static_cast<void>(owner_->release_pin(source_));
    owner_ = nullptr;
    source_ = {};
  }
}

MaterializedCanvas::MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                                       std::span<MaterializedSlotStorage> slots,
                                       std::span<std::uint16_t> tile_pixels,
                                       DocumentRevision initial_revision)
    : overview_pixels_(overview_pixels),
      slots_(slots),
      tile_pixels_(tile_pixels),
      current_revision_(initial_revision) {
  std::fill(slots_.begin(), slots_.end(), MaterializedSlotStorage{});
}

MaterializedCanvas::MaterializedCanvas(std::span<std::uint16_t> overview_pixels,
                                       std::span<MaterializedUniformStorage> uniform_catalog,
                                       std::span<std::uint8_t> occupancy_bits,
                                       std::span<MaterializedSlotStorage> slots,
                                       std::span<std::uint16_t> tile_pixels,
                                       DocumentRevision initial_revision)
    : overview_pixels_(overview_pixels),
      uniform_catalog_(uniform_catalog),
      occupancy_bits_(occupancy_bits),
      slots_(slots),
      tile_pixels_(tile_pixels),
      current_revision_(initial_revision) {
  std::fill(uniform_catalog_.begin(), uniform_catalog_.end(), MaterializedUniformStorage{});
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0U);
  std::fill(slots_.begin(), slots_.end(), MaterializedSlotStorage{});
}

bool MaterializedCanvas::ready() const {
  const bool optional_catalog_ready = (uniform_catalog_.empty() && occupancy_bits_.empty()) ||
                                      (uniform_catalog_.size() == kMaterializedTileIdentityCount &&
                                       occupancy_bits_.size() == kOccupancyBytes);
  return overview_pixels_.size() == kOverviewPixels && optional_catalog_ready &&
         tile_pixels_.size() == slots_.size() * kTilePixels;
}

DocumentRevision MaterializedCanvas::current_revision() const { return current_revision_; }

std::size_t MaterializedCanvas::slot_capacity() const { return slots_.size(); }

std::size_t MaterializedCanvas::uniform_capacity() const { return uniform_catalog_.size(); }

std::uint64_t MaterializedCanvas::composition_epoch() const { return composition_epoch_; }

std::span<const std::uint16_t> MaterializedCanvas::overview_pixels() const {
  return overview_pixels_;
}

bool MaterializedCanvas::certainly_paper(TileKey key) const {
  if (occupancy_bits_.size() != kOccupancyBytes || !valid_tile_key(key)) {
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

void MaterializedCanvas::bump_composition_epoch() {
  ++composition_epoch_;
  if (composition_epoch_ == 0U) {
    composition_epoch_ = 1U;
  }
}

void MaterializedCanvas::clear_uniforms() {
  std::fill(uniform_catalog_.begin(), uniform_catalog_.end(), MaterializedUniformStorage{});
}

TileKey MaterializedCanvas::key_for_identity(std::size_t index) {
  constexpr std::array zooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                             ZoomLevel::k400Percent};
  constexpr std::array offsets{std::size_t{0}, std::size_t{168}, std::size_t{812},
                               std::size_t{3'388}};
  for (std::size_t zoom_index = zooms.size(); zoom_index-- > 0U;) {
    if (index < offsets[zoom_index]) {
      continue;
    }
    const TileGrid grid = tile_grid(zooms[zoom_index]);
    const std::size_t local = index - offsets[zoom_index];
    return {zooms[zoom_index],
            static_cast<std::uint16_t>(local % static_cast<std::size_t>(grid.columns)),
            static_cast<std::uint16_t>(local / static_cast<std::size_t>(grid.columns))};
  }
  return {};
}

bool MaterializedCanvas::uniform_intersects(std::size_t index, PixelRect world_bounds) {
  return index < kMaterializedTileIdentityCount &&
         rectangles_intersect(tile_world_bounds(key_for_identity(index)), world_bounds);
}

void MaterializedCanvas::invalidate_uniforms(PixelRect world_bounds) {
  for (std::size_t index = 0; index < uniform_catalog_.size(); ++index) {
    uniform_catalog_[index].occupied_ =
        uniform_catalog_[index].occupied_ && !uniform_intersects(index, world_bounds);
  }
}

void MaterializedCanvas::rebuild_occupancy_from_overview() {
  if (occupancy_bits_.size() != kOccupancyBytes) {
    return;
  }
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0U);
  for (int row = 0; row < kOccupancyRows; ++row) {
    for (int column = 0; column < kOccupancyColumns; ++column) {
      const int x0 = column * kOccupancyCellWorldSize / 4;
      const int y0 = row * kOccupancyCellWorldSize / 4;
      const int x1 = std::min(kOverviewWidth, (column + 1) * kOccupancyCellWorldSize / 4);
      const int y1 = std::min(kOverviewHeight, (row + 1) * kOccupancyCellWorldSize / 4);
      if (!overview_region_is_paper(overview_pixels_, {x0, y0, x1, y1})) {
        const std::size_t bit =
            static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
        occupancy_bits_[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
      }
    }
  }
}

void MaterializedCanvas::mark_occupied(PixelRect world_bounds) {
  if (occupancy_bits_.size() != kOccupancyBytes) {
    return;
  }
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
  const bool tile_is_pinned = std::any_of(slots_.begin(), slots_.end(),
                                          [](const auto& slot) { return slot.pin_count_ != 0U; });
  if (!ready() || !revision_is_publishable || !source_is_publishable || overview_pin_count_ != 0U ||
      uniform_pin_count_ != 0U || tile_is_pinned || pixels.size() != kOverviewPixels) {
    return false;
  }
  if (!exact_bootstrap_source) {
    std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  }
  for (auto& slot : slots_) {
    if (slot.occupied_) {
      slot.occupied_ = false;
      slot.generation_ = take_generation();
    }
  }
  clear_uniforms();
  std::fill(occupancy_bits_.begin(), occupancy_bits_.end(), 0xFFU);
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_generation_ = take_generation();
  overview_valid_ = true;
  bump_composition_epoch();
  return true;
}

bool MaterializedCanvas::restore_snapshot(DocumentRevision revision,
                                          std::span<const std::uint16_t> pixels) {
  const bool source_is_external = accepts_external_workspace(std::as_bytes(pixels));
  const bool any_tile_pinned = std::any_of(slots_.begin(), slots_.end(),
                                           [](const auto& slot) { return slot.pin_count_ != 0U; });
  if (!ready() || pixels.size() != kOverviewPixels || !source_is_external ||
      overview_pin_count_ != 0U || uniform_pin_count_ != 0U || any_tile_pinned) {
    return false;
  }
  std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  for (auto& slot : slots_) {
    if (slot.occupied_) {
      slot.occupied_ = false;
      slot.generation_ = take_generation();
    }
  }
  clear_uniforms();
  rebuild_occupancy_from_overview();
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_generation_ = take_generation();
  overview_valid_ = true;
  bump_composition_epoch();
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
  const bool any_tile_pinned = std::any_of(slots_.begin(), slots_.end(),
                                           [](const auto& slot) { return slot.pin_count_ != 0U; });
  if (!ready() || !overview_valid_ || !revision_advances_once ||
      !valid_world_bounds(affected_world_bounds) ||
      overview_publication.bounds != required_overview_bounds || expected_overview_pixels == 0U ||
      overview_publication.pixels.size() != expected_overview_pixels || !source_is_external ||
      overview_pin_count_ != 0U || uniform_pin_count_ != 0U || any_tile_pinned) {
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
    raw_publications += !analysis->uniform || uniform_catalog_.empty();
  }
  return raw_publications <= slots_.size();
}

void MaterializedCanvas::write_tile(std::size_t slot_index,
                                    const TileRevisionPublication& publication,
                                    DocumentRevision revision) {
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
  MaterializedSlotStorage& slot = slots_[slot_index];
  slot.key_ = publication.key;
  slot.revision_ = revision;
  slot.generation_ = take_generation();
  slot.quality_ = publication.quality;
  slot.occupied_ = true;
  touch(slot);
}

bool MaterializedCanvas::commit_incremental_revision(
    DocumentRevision revision, const OverviewRevisionPublication& overview_publication,
    PixelRect affected_world_bounds, std::span<const TileRevisionPublication> tile_publications) {
  if (!valid_incremental_revision(revision, overview_publication, affected_world_bounds,
                                  tile_publications)) {
    return false;
  }

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
  invalidate_uniforms(affected_world_bounds);
  for (MaterializedSlotStorage& slot : slots_) {
    if (!slot.occupied_) {
      continue;
    }
    const bool affected = rectangles_intersect(tile_world_bounds(slot.key_), affected_world_bounds);
    const bool published = std::any_of(
        tile_publications.begin(), tile_publications.end(),
        [&slot](const TileRevisionPublication& candidate) { return candidate.key == slot.key_; });
    if (affected && !published) {
      slot.occupied_ = false;
      slot.generation_ = take_generation();
    } else if (!affected) {
      slot.revision_ = revision;
    }
  }
  for (const TileRevisionPublication& publication : tile_publications) {
    const PixelRect bounds = tile_pixel_bounds(publication.key);
    const auto analysis =
        analyze_tile_payload(publication.pixels, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
    const auto uniform_index = tile_identity_index(publication.key);
    if (!uniform_catalog_.empty() && analysis.has_value() && analysis->uniform &&
        uniform_index.has_value()) {
      if (const auto slot_index = find_tile(publication.key); slot_index.has_value()) {
        slots_[*slot_index].occupied_ = false;
        slots_[*slot_index].generation_ = take_generation();
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
  mark_occupied(affected_world_bounds);
  current_revision_ = revision;
  overview_revision_ = revision;
  overview_generation_ = take_generation();
  bump_composition_epoch();
  return true;
}

bool MaterializedCanvas::accepts_external_workspace(std::span<const std::byte> workspace) const {
  return !storage_overlaps(workspace, std::as_bytes(std::span(overview_pixels_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(uniform_catalog_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(occupancy_bits_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(tile_pixels_))) &&
         !storage_overlaps(workspace, std::as_bytes(std::span(slots_)));
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

std::optional<std::size_t> MaterializedCanvas::find_tile(TileKey key) const {
  const auto found = std::find_if(slots_.begin(), slots_.end(), [key](const auto& slot) {
    return slot.occupied_ && slot.key_ == key;
  });
  if (found == slots_.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - slots_.begin());
}

std::optional<std::size_t> MaterializedCanvas::find_uniform(TileKey key) const {
  const auto index = tile_identity_index(key);
  if (!index.has_value() || *index >= uniform_catalog_.size() ||
      !uniform_catalog_[*index].occupied_) {
    return std::nullopt;
  }
  return index;
}

std::optional<std::size_t> MaterializedCanvas::choose_slot() const {
  const auto available = std::find_if(slots_.begin(), slots_.end(), [](const auto& slot) {
    return !slot.occupied_ && slot.pin_count_ == 0U;
  });
  if (available != slots_.end()) {
    return static_cast<std::size_t>(available - slots_.begin());
  }
  const auto oldest =
      std::min_element(slots_.begin(), slots_.end(), [](const auto& left, const auto& right) {
        return std::tuple(left.pin_count_ != 0U, left.last_use_) <
               std::tuple(right.pin_count_ != 0U, right.last_use_);
      });
  if (oldest == slots_.end() || oldest->pin_count_ != 0U) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(oldest - slots_.begin());
}

SlotGeneration MaterializedCanvas::take_generation() {
  const SlotGeneration generation{next_generation_++};
  if (next_generation_ == 0U) {
    next_generation_ = 1U;
  }
  return generation;
}

void MaterializedCanvas::touch(MaterializedSlotStorage& slot) { slot.last_use_ = ++use_clock_; }

std::optional<std::size_t> MaterializedCanvas::publish_tile(TileKey key, DocumentRevision revision,
                                                            MaterializationQuality quality,
                                                            std::span<const std::uint16_t> pixels) {
  const PixelRect bounds = tile_pixel_bounds(key);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t expected_pixels =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const bool source_overlaps_pool = !accepts_external_workspace(std::as_bytes(pixels));
  if (!ready() || slots_.empty() || !valid_tile_key(key) || revision != current_revision_ ||
      quality == MaterializationQuality::kOverviewFallback || source_overlaps_pool ||
      pixels.size() != expected_pixels) {
    return std::nullopt;
  }
  const auto existing = find_tile(key);
  if (existing.has_value() &&
      (slots_[*existing].pin_count_ != 0U ||
       static_cast<int>(quality) < static_cast<int>(slots_[*existing].quality_))) {
    return std::nullopt;
  }
  const auto selected = existing.has_value() ? existing : choose_slot();
  if (!selected.has_value()) {
    return std::nullopt;
  }
  const std::size_t index = *selected;
  MaterializedSlotStorage& slot = slots_[index];
  slot.occupied_ = false;
  slot.generation_ = take_generation();
  auto destination = tile_pixels_.subspan(index * kTilePixels, kTilePixels);
  for (int row = 0; row < height; ++row) {
    const auto source_offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    const auto destination_offset =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(kTileWidth);
    std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), width,
                destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  slot.key_ = key;
  slot.revision_ = revision;
  slot.quality_ = quality;
  slot.occupied_ = true;
  if (const auto uniform = tile_identity_index(key);
      uniform.has_value() && *uniform < uniform_catalog_.size()) {
    uniform_catalog_[*uniform].occupied_ = false;
  }
  touch(slot);
  bump_composition_epoch();
  return index;
}

std::optional<std::size_t> MaterializedCanvas::publish_uniform(TileKey key,
                                                               DocumentRevision revision,
                                                               MaterializationQuality quality,
                                                               std::uint16_t color) {
  const auto index = tile_identity_index(key);
  if (!ready() || !index.has_value() || *index >= uniform_catalog_.size() ||
      revision != current_revision_ || quality == MaterializationQuality::kOverviewFallback ||
      uniform_pin_count_ != 0U) {
    return std::nullopt;
  }
  if (const auto raw = find_tile(key); raw.has_value()) {
    MaterializedSlotStorage& slot = slots_[*raw];
    if (slot.pin_count_ != 0U || static_cast<int>(quality) < static_cast<int>(slot.quality_)) {
      return std::nullopt;
    }
    slot.occupied_ = false;
    slot.generation_ = take_generation();
  }
  MaterializedUniformStorage& uniform = uniform_catalog_[*index];
  if (uniform.occupied_ && static_cast<int>(quality) < static_cast<int>(uniform.quality_)) {
    return std::nullopt;
  }
  uniform.color_ = color;
  uniform.quality_ = quality;
  uniform.occupied_ = true;
  bump_composition_epoch();
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
      .slot_index = std::nullopt,
      .source_stride = kOverviewWidth,
  };
}

SourceSelection MaterializedCanvas::select_uniform(TileKey requested, std::size_t index) const {
  const MaterializedUniformStorage& uniform = uniform_catalog_[index];
  return {
      .kind = SourceKind::kUniform,
      .identity =
          {
              .revision = current_revision_,
              .generation = {},
              .quality = uniform.quality_,
              .provenance = MaterializationProvenance::kWorldTile,
          },
      .requested_tile = requested,
      .source_pixels = tile_pixel_bounds(requested),
      .destination_pixels = tile_pixel_bounds(requested),
      .slot_index = std::nullopt,
      .source_stride = 0,
      .uniform_color = uniform.color_,
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
      .source_stride = kTileWidth,
  };
}

std::optional<SourceSelection> MaterializedCanvas::lookup(TileKey key) const {
  if (!ready() || !valid_tile_key(key)) {
    return std::nullopt;
  }
  const std::optional<std::size_t> tile_index = find_tile(key);
  if (tile_index.has_value() && slots_[*tile_index].revision_ == current_revision_) {
    return select_tile(key, *tile_index);
  }
  if (const auto uniform = find_uniform(key); uniform.has_value()) {
    return select_uniform(key, *uniform);
  }
  if (overview_valid_ && overview_revision_ == current_revision_) {
    return select_overview(key);
  }
  return std::nullopt;
}

std::optional<PinnedSource> MaterializedCanvas::pin(TileKey key) {
  auto source = lookup(key);
  if (!source.has_value()) {
    return std::nullopt;
  }
  std::uint32_t* pin_count = nullptr;
  if (source->kind == SourceKind::kOverview) {
    pin_count = &overview_pin_count_;
  } else if (source->kind == SourceKind::kUniform) {
    pin_count = &uniform_pin_count_;
  } else {
    if (!source->slot_index.has_value()) {
      return std::nullopt;
    }
    MaterializedSlotStorage& slot = slots_[source->slot_index.value()];
    pin_count = &slot.pin_count_;
    touch(slot);
  }
  if (*pin_count == std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  source->pin_token = next_pin_token_++;
  if (next_pin_token_ == 0U) {
    next_pin_token_ = 1U;
  }
  ++*pin_count;
  return PinnedSource(*this, *source);
}

bool MaterializedCanvas::validate(const PinnedSource& source) const {
  return source.owner_ == this && validate_selection(source.source_);
}

bool MaterializedCanvas::validate_selection(const SourceSelection& source) const {
  if (source.identity.revision != current_revision_) {
    return false;
  }
  if (source.kind == SourceKind::kOverview) {
    return source.pin_token != 0U && overview_pin_count_ != 0U && !source.slot_index.has_value() &&
           overview_valid_ && source.identity.revision == overview_revision_ &&
           source.identity.generation == overview_generation_;
  }
  if (source.kind == SourceKind::kUniform) {
    const auto index = find_uniform(source.requested_tile);
    return source.pin_token != 0U && uniform_pin_count_ != 0U && index.has_value() &&
           !source.slot_index.has_value() &&
           uniform_catalog_[*index].quality_ == source.identity.quality &&
           uniform_catalog_[*index].color_ == source.uniform_color;
  }
  if (!source.slot_index.has_value() || *source.slot_index >= slots_.size()) {
    return false;
  }
  const MaterializedSlotStorage& slot = slots_[*source.slot_index];
  return source.pin_token != 0U && slot.pin_count_ != 0U && slot.occupied_ &&
         slot.key_ == source.requested_tile && slot.revision_ == source.identity.revision &&
         slot.generation_ == source.identity.generation;
}

bool MaterializedCanvas::release_pin(const SourceSelection& source) {
  if (source.kind == SourceKind::kOverview) {
    if (source.pin_token == 0U || overview_pin_count_ == 0U ||
        source.identity.revision != overview_revision_ ||
        source.identity.generation != overview_generation_) {
      return false;
    }
    --overview_pin_count_;
    return true;
  }
  if (source.kind == SourceKind::kUniform) {
    if (source.pin_token == 0U || uniform_pin_count_ == 0U || !validate_selection(source)) {
      return false;
    }
    --uniform_pin_count_;
    return true;
  }
  if (!source.slot_index.has_value() || *source.slot_index >= slots_.size()) {
    return false;
  }
  MaterializedSlotStorage& slot = slots_[*source.slot_index];
  if (source.pin_token == 0U || slot.pin_count_ == 0U || slot.key_ != source.requested_tile ||
      slot.revision_ != source.identity.revision ||
      slot.generation_ != source.identity.generation) {
    return false;
  }
  --slot.pin_count_;
  return true;
}

std::size_t MaterializedCanvas::pins_outstanding() const {
  return std::accumulate(slots_.begin(), slots_.end(),
                         static_cast<std::size_t>(overview_pin_count_) + uniform_pin_count_,
                         [](std::size_t count, const MaterializedSlotStorage& slot) {
                           return count + slot.pin_count_;
                         });
}

bool MaterializedCanvas::mark_used(TileKey key) {
  const auto index = find_tile(key);
  if (index.has_value() && slots_[*index].revision_ == current_revision_) {
    touch(slots_[*index]);
    return true;
  }
  return find_uniform(key).has_value();
}

bool MaterializedCanvas::discard_tiles() {
  if (!ready() || overview_pin_count_ != 0U || uniform_pin_count_ != 0U ||
      std::any_of(slots_.begin(), slots_.end(),
                  [](const auto& slot) { return slot.pin_count_ != 0U; })) {
    return false;
  }
  for (MaterializedSlotStorage& slot : slots_) {
    if (!slot.occupied_) {
      continue;
    }
    slot.occupied_ = false;
    slot.generation_ = take_generation();
  }
  clear_uniforms();
  bump_composition_epoch();
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
  stats.exact_tiles += quality == MaterializationQuality::kExact;
  stats.immediate_tiles += quality == MaterializationQuality::kImmediate;
  stats.settled_tiles += quality == MaterializationQuality::kSettled;
}

void MaterializedCanvas::compose_raw_pixels(std::size_t slot_index, PixelRect bounds,
                                            CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    for (int x = bounds.x0; x < bounds.x1; ++x) {
      const std::size_t source = slot_index * kTilePixels +
                                 static_cast<std::size_t>(y % kTileHeight) * kTileWidth +
                                 static_cast<std::size_t>(x % kTileWidth);
      const std::size_t destination =
          static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
          static_cast<std::size_t>(x - view.x0);
      context.destination[destination] = tile_pixels_[source];
      ++context.stats.tile_pixels;
    }
  }
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
  const int percent = zoom_percent(context.request.zoom);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    for (int x = bounds.x0; x < bounds.x1; ++x) {
      const std::size_t source = static_cast<std::size_t>(y * 25 / percent) * kOverviewWidth +
                                 static_cast<std::size_t>(x * 25 / percent);
      const std::size_t destination =
          static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
          static_cast<std::size_t>(x - view.x0);
      context.destination[destination] = overview_pixels_[source];
      ++context.stats.fallback_pixels;
    }
  }
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
