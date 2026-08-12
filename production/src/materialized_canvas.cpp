#include "tinydraw/production/materialized_canvas.h"

#include <algorithm>
#include <limits>
#include <tuple>

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

template <typename Left, typename Right>
bool spans_overlap(std::span<Left> left, std::span<Right> right) {
  const auto* left_begin = reinterpret_cast<const std::byte*>(left.data());
  const auto* left_end = left_begin + left.size_bytes();
  const auto* right_begin = reinterpret_cast<const std::byte*>(right.data());
  const auto* right_end = right_begin + right.size_bytes();
  return left_begin < right_end && right_begin < left_end;
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

PinnedSource::PinnedSource(MaterializedCanvas& owner, const SourceSelection& source)
    : owner_(&owner), source_(source) {}

PinnedSource::~PinnedSource() { reset(); }

PinnedSource::PinnedSource(PinnedSource&& other) noexcept
    : owner_(other.owner_), source_(other.source_) {
  other.owner_ = nullptr;
}

PinnedSource& PinnedSource::operator=(PinnedSource&& other) noexcept {
  if (this != &other) {
    reset();
    owner_ = other.owner_;
    source_ = other.source_;
    other.owner_ = nullptr;
  }
  return *this;
}

const SourceSelection& PinnedSource::source() const { return source_; }

bool PinnedSource::valid() const { return owner_ != nullptr && owner_->validate(*this); }

void PinnedSource::reset() {
  if (owner_ != nullptr) {
    static_cast<void>(owner_->release_pin(source_));
    owner_ = nullptr;
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

bool MaterializedCanvas::ready() const {
  return overview_pixels_.size() == kOverviewPixels &&
         tile_pixels_.size() == slots_.size() * kTilePixels;
}

DocumentRevision MaterializedCanvas::current_revision() const { return current_revision_; }

std::size_t MaterializedCanvas::slot_capacity() const { return slots_.size(); }

std::span<const std::uint16_t> MaterializedCanvas::overview_pixels() const {
  return overview_pixels_;
}

bool MaterializedCanvas::publish_overview(DocumentRevision revision,
                                          std::span<const std::uint16_t> pixels) {
  const bool revision_is_publishable = overview_valid_ ? revision.value > current_revision_.value
                                                       : revision.value >= current_revision_.value;
  const bool source_is_publishable =
      !overview_valid_ || !spans_overlap(pixels, std::span(overview_pixels_));
  const bool tile_is_pinned = std::any_of(slots_.begin(), slots_.end(),
                                          [](const auto& slot) { return slot.pin_token_ != 0U; });
  if (!ready() || !revision_is_publishable || !source_is_publishable || overview_pin_token_ != 0U ||
      tile_is_pinned || pixels.size() != kOverviewPixels) {
    return false;
  }
  std::copy(pixels.begin(), pixels.end(), overview_pixels_.begin());
  for (auto& slot : slots_) {
    if (slot.occupied_) {
      slot.occupied_ = false;
      slot.generation_ = {next_generation_++};
    }
  }
  current_revision_ = revision;
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

std::optional<std::size_t> MaterializedCanvas::choose_slot() const {
  const auto available = std::find_if(slots_.begin(), slots_.end(), [](const auto& slot) {
    return !slot.occupied_ && slot.pin_token_ == 0U;
  });
  if (available != slots_.end()) {
    return static_cast<std::size_t>(available - slots_.begin());
  }
  const auto oldest =
      std::min_element(slots_.begin(), slots_.end(), [](const auto& left, const auto& right) {
        return std::tuple(left.pin_token_ != 0U, left.last_use_) <
               std::tuple(right.pin_token_ != 0U, right.last_use_);
      });
  if (oldest == slots_.end() || oldest->pin_token_ != 0U) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(oldest - slots_.begin());
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
  const bool source_overlaps_pool = spans_overlap(pixels, std::span(tile_pixels_));
  if (!ready() || slots_.empty() || !valid_tile_key(key) || revision != current_revision_ ||
      quality == MaterializationQuality::kOverviewFallback || source_overlaps_pool ||
      pixels.size() != expected_pixels) {
    return std::nullopt;
  }
  const auto existing = find_tile(key);
  if (existing.has_value() &&
      (slots_[*existing].pin_token_ != 0U ||
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
  slot.generation_ = {next_generation_++};
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
      .slot_index = std::nullopt,
      .source_stride = kOverviewWidth,
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
  if (source->kind == SourceKind::kOverview) {
    source->pin_token = next_pin_token_++;
    overview_pin_token_ = source->pin_token;
  } else {
    if (!source->slot_index.has_value()) {
      return std::nullopt;
    }
    MaterializedSlotStorage& slot = slots_[source->slot_index.value()];
    source->pin_token = next_pin_token_++;
    slot.pin_token_ = source->pin_token;
    touch(slot);
  }
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
    return source.pin_token != 0U && source.pin_token == overview_pin_token_ &&
           !source.slot_index.has_value() && overview_valid_ &&
           source.identity.revision == overview_revision_ &&
           source.identity.generation == overview_generation_;
  }
  if (!source.slot_index.has_value() || *source.slot_index >= slots_.size()) {
    return false;
  }
  const MaterializedSlotStorage& slot = slots_[*source.slot_index];
  return source.pin_token != 0U && source.pin_token == slot.pin_token_ && slot.occupied_ &&
         slot.key_ == source.requested_tile && slot.revision_ == source.identity.revision &&
         slot.generation_ == source.identity.generation;
}

bool MaterializedCanvas::release_pin(const SourceSelection& source) {
  if (source.kind == SourceKind::kOverview) {
    if (source.pin_token == 0U || source.pin_token != overview_pin_token_ ||
        source.identity.revision != overview_revision_ ||
        source.identity.generation != overview_generation_) {
      return false;
    }
    overview_pin_token_ = 0U;
    return true;
  }
  if (!source.slot_index.has_value() || *source.slot_index >= slots_.size()) {
    return false;
  }
  MaterializedSlotStorage& slot = slots_[*source.slot_index];
  if (source.pin_token == 0U || source.pin_token != slot.pin_token_ ||
      slot.key_ != source.requested_tile || slot.revision_ != source.identity.revision ||
      slot.generation_ != source.identity.generation) {
    return false;
  }
  slot.pin_token_ = 0U;
  return true;
}

bool MaterializedCanvas::mark_used(TileKey key) {
  const auto index = find_tile(key);
  if (!index.has_value() || slots_[*index].revision_ != current_revision_) {
    return false;
  }
  touch(slots_[*index]);
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
      if (!index.has_value() || slots_[*index].revision_ != current_revision_) {
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
      .fallback_pixels = destination.size(),
      .fallback_tiles = 1,
  };
}

void MaterializedCanvas::compose_tile(TileKey key, CompositionContext& context) {
  const PixelRect tile_bounds = tile_pixel_bounds(key);
  const PixelRect view = context.request.level_pixels;
  const int x0 = std::max(view.x0, tile_bounds.x0);
  const int x1 = std::min(view.x1, tile_bounds.x1);
  const int y0 = std::max(view.y0, tile_bounds.y0);
  const int y1 = std::min(view.y1, tile_bounds.y1);
  const auto tile_index = find_tile(key);
  const bool tile_current =
      tile_index.has_value() && slots_[*tile_index].revision_ == current_revision_;
  const bool overview_current = overview_valid_ && overview_revision_ == current_revision_;
  if (!tile_current && !overview_current) {
    return;
  }
  if (tile_current) {
    touch(slots_[*tile_index]);
    context.stats.exact_tiles +=
        slots_[*tile_index].quality_ == MaterializationQuality::kExact ? 1U : 0U;
    context.stats.settled_tiles +=
        slots_[*tile_index].quality_ == MaterializationQuality::kSettled ? 1U : 0U;
  } else {
    ++context.stats.fallback_tiles;
  }

  const int percent = zoom_percent(context.request.zoom);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      std::uint16_t pixel = 0;
      if (tile_current) {
        const std::size_t offset = *tile_index * kTilePixels +
                                   static_cast<std::size_t>(y % kTileHeight) * kTileWidth +
                                   static_cast<std::size_t>(x % kTileWidth);
        pixel = tile_pixels_[offset];
        ++context.stats.tile_pixels;
      } else {
        const std::size_t offset = static_cast<std::size_t>(y * 25 / percent) * kOverviewWidth +
                                   static_cast<std::size_t>(x * 25 / percent);
        pixel = overview_pixels_[offset];
        ++context.stats.fallback_pixels;
      }
      const std::size_t destination_offset =
          static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
          static_cast<std::size_t>(x - view.x0);
      context.destination[destination_offset] = pixel;
    }
  }
}

std::optional<ViewCompositionStats> MaterializedCanvas::compose_view(
    const ViewRequest& request, std::span<std::uint16_t> destination) {
  if (!valid_view(request, destination.size()) || !has_complete_source(request)) {
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

}  // namespace tinydraw::production
