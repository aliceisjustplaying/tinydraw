#include <algorithm>
#include <cstring>

#include "tinydraw/vector_v2/materialized_canvas.h"

#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
#include "tinydraw/vector_v2/rerender_ledger.h"
#endif
#include "materialized_canvas_internal.h"
#include "tinydraw/vector_v2/storage_overlap.h"
#include "tinydraw/vector_v2/tile_uniform.h"

namespace tinydraw::vector_v2 {
using namespace materialized_canvas_detail;

namespace {

#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_TILE_PUBLICATION)
extern "C" void tinydraw_publish_tile_64x64_stride128_pie(const std::uint16_t* source,
                                                          std::uint16_t* destination);
#endif

[[gnu::always_inline]] inline void copy_publication_rows(const std::uint16_t* source,
                                                         std::size_t source_stride,
                                                         std::uint16_t* destination, int width,
                                                         int height) {
#if defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TINYDRAW_QEMU) && \
    !defined(TINYDRAW_DISABLE_PIE_TILE_PUBLICATION)
  // Production immediate tiles are 64x64 windows of the aligned 128x128
  // producer surface. Tile-pool slots are 8192-byte aligned relative to the
  // allocation base, so one phase check covers every source and destination
  // row before using address-rounding PIE vector loads and stores.
  if (width == kTileWidth && height == kTileHeight && source_stride == 128U &&
      ((reinterpret_cast<std::uintptr_t>(source) | reinterpret_cast<std::uintptr_t>(destination)) &
       0x0FU) == 0U) {
    tinydraw_publish_tile_64x64_stride128_pie(source, destination);
    return;
  }
#endif

  const std::size_t row_bytes = static_cast<std::size_t>(width) * sizeof(std::uint16_t);
  for (int row = 0; row < height; ++row) {
    std::memmove(destination + static_cast<std::size_t>(row) * kTileWidth,
                 source + static_cast<std::size_t>(row) * source_stride, row_bytes);
  }
}

}  // namespace

#if defined(TINYDRAW_VECTOR_V2_GATE_HARNESS)
extern "C" void tinydraw_gate_copy_publication_rows(const std::uint16_t* source,
                                                    std::size_t source_stride,
                                                    std::uint16_t* destination, int width,
                                                    int height) {
  copy_publication_rows(source, source_stride, destination, width, height);
}
#endif

void MaterializedCanvas::cancel_in_place_stage(InPlaceOverviewStage& stage) {
  if (!stage.raw_staging_started_ || !staged_in_place_active_ ||
      staged_in_place_revision_ != stage.revision_) {
    return;
  }
  for (const TileKey key : std::span(stage.retained_keys_, stage.retained_count_)) {
    clear_retained_marker(key);
  }
  for (MaterializedSlotStorage& slot : slots_) {
    if (slot.occupied_ && slot.revision_ == stage.revision_) {
      slot.revision_ = current_revision_;
    }
  }
  staged_in_place_active_ = false;
  ++composition_epoch_;
}

bool MaterializedCanvas::raw_slot_is_current(const MaterializedSlotStorage& slot) const {
  return slot.revision_ == current_revision_ ||
         (staged_in_place_active_ && slot.revision_ == staged_in_place_revision_);
}

void MaterializedCanvas::invalidate_identity(TileKey key) {
  ++composition_epoch_;
  if (const std::uint16_t slot_index = find_tile(key); slot_index != kNoRawSlot) {
    release_slot(slot_index);
  }
  if (const std::uint16_t uniform_index = find_uniform(key); uniform_index != kNoRawSlot) {
    uniform_catalog_[uniform_index].occupied_ = false;
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
  const std::uint16_t slot_index = find_tile(key);
  const std::uint16_t uniform_index = find_uniform(key);
  if (slot_index == kNoRawSlot && uniform_index == kNoRawSlot) {
    return false;
  }
  const PixelRect bounds = tile_pixel_bounds(key);
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (destination.size() != expected || !accepts_external_workspace(std::as_bytes(destination))) {
    return false;
  }
  if (uniform_index != kNoRawSlot) {
    std::fill(destination.begin(), destination.end(), uniform_catalog_[uniform_index].color_);
    return true;
  }
  const auto source =
      tile_pixels_.subspan(static_cast<std::size_t>(slot_index) * kTilePixels, kTilePixels);
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
      if (find_uniform(key) == kNoRawSlot) {
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

std::uint16_t MaterializedCanvas::find_tile(TileKey key) const {
  const std::uint16_t identity = tile_identity_or_no_slot(key);
  if (!storage_ready_ || identity == kNoRawSlot) {
    return kNoRawSlot;
  }
  const std::uint16_t index = raw_slot_directory_[identity];
  if (index == kNoRawSlot || static_cast<std::size_t>(index) >= slots_.size() ||
      !slots_[index].occupied_ || !(slots_[index].key_ == key)) {
    return kNoRawSlot;
  }
  return index;
}

std::uint16_t MaterializedCanvas::find_uniform(TileKey key) const {
  const std::uint16_t index = tile_identity_or_no_slot(key);
  if (!storage_ready_ || index == kNoRawSlot || !uniform_catalog_[index].occupied_) {
    return kNoRawSlot;
  }
  return index;
}

std::uint8_t MaterializedCanvas::eviction_protection_rank(TileKey key) const {
  if (key.zoom == ZoomLevel::k25Percent) {
    return 0U;
  }
  const auto index = static_cast<std::size_t>(key.zoom) - 1U;
  if (index >= recent_views_.size()) {
    return 0U;
  }
  const ViewFootprint& footprint = recent_views_[index];
  const int tile_x0 = static_cast<int>(key.column) * kTileWidth;
  const int tile_y0 = static_cast<int>(key.row) * kTileHeight;
  const PixelRect& bounds = footprint.level_pixels;
  if (!footprint.valid || tile_x0 >= bounds.x1 || tile_y0 >= bounds.y1 ||
      tile_x0 + kTileWidth <= bounds.x0 || tile_y0 + kTileHeight <= bounds.y0) {
    return 0U;
  }
  return key.zoom == active_view_zoom_ ? 2U : 1U;
}

std::uint8_t MaterializedCanvas::eviction_class(const MaterializedSlotStorage& slot) const {
  return slot.preserved_ ? kPreservedEvictionClass : eviction_protection_rank(slot.key_);
}

bool MaterializedCanvas::eviction_index_ready() const { return eviction_index_enabled_; }

std::uint16_t MaterializedCanvas::eviction_previous(std::size_t index) const {
  return eviction_links_[index * 2U];
}

std::uint16_t MaterializedCanvas::eviction_next(std::size_t index) const {
  return eviction_links_[index * 2U + 1U];
}

void MaterializedCanvas::set_eviction_previous(std::size_t index, std::uint16_t previous) {
  eviction_links_[index * 2U] = previous;
}

void MaterializedCanvas::set_eviction_next(std::size_t index, std::uint16_t next) {
  eviction_links_[index * 2U + 1U] = next;
}

bool MaterializedCanvas::eviction_slot_linked(std::size_t index) const {
  return eviction_lru_head_ == index || eviction_previous(index) != kNoEvictionSlot ||
         eviction_next(index) != kNoEvictionSlot;
}

void MaterializedCanvas::unlink_eviction_slot(std::size_t index) {
  if (!eviction_index_ready() || !eviction_slot_linked(index)) {
    return;
  }
  const std::uint16_t previous = eviction_previous(index);
  const std::uint16_t next = eviction_next(index);
  if (!eviction_candidates_dirty_) {
    const std::uint8_t removed_class = eviction_class(slots_[index]);
    if (oldest_eviction_by_class_[removed_class] == index) {
      std::uint16_t candidate = next;
      while (candidate != kNoEvictionSlot && eviction_class(slots_[candidate]) != removed_class) {
        candidate = eviction_next(candidate);
      }
      oldest_eviction_by_class_[removed_class] = candidate;
    }
  }
  if (previous == kNoEvictionSlot) {
    eviction_lru_head_ = next;
  } else {
    set_eviction_next(previous, next);
  }
  if (next == kNoEvictionSlot) {
    eviction_lru_tail_ = previous;
  } else {
    set_eviction_previous(next, previous);
  }
  set_eviction_previous(index, kNoEvictionSlot);
  set_eviction_next(index, kNoEvictionSlot);
}

void MaterializedCanvas::append_eviction_slot(std::size_t index) {
  if (!eviction_index_ready()) {
    return;
  }
  set_eviction_previous(index, eviction_lru_tail_);
  set_eviction_next(index, kNoEvictionSlot);
  if (eviction_lru_tail_ == kNoEvictionSlot) {
    eviction_lru_head_ = static_cast<std::uint16_t>(index);
  } else {
    set_eviction_next(eviction_lru_tail_, static_cast<std::uint16_t>(index));
  }
  eviction_lru_tail_ = static_cast<std::uint16_t>(index);
  if (!eviction_candidates_dirty_) {
    const std::uint8_t added_class = eviction_class(slots_[index]);
    if (oldest_eviction_by_class_[added_class] == kNoEvictionSlot) {
      oldest_eviction_by_class_[added_class] = static_cast<std::uint16_t>(index);
    }
  }
}

void MaterializedCanvas::rebuild_eviction_candidates() {
  oldest_eviction_by_class_.fill(kNoEvictionSlot);
  std::uint16_t index = eviction_lru_head_;
  while (index != kNoEvictionSlot) {
    const std::uint8_t candidate_class = eviction_class(slots_[index]);
    if (oldest_eviction_by_class_[candidate_class] == kNoEvictionSlot) {
      oldest_eviction_by_class_[candidate_class] = index;
    }
    index = eviction_next(index);
  }
  eviction_candidates_dirty_ = false;
}

void MaterializedCanvas::consume_free_slot(std::size_t index) {
  if (!eviction_index_ready() || lowest_free_slot_ != index) {
    return;
  }
  if (occupied_slots_ + preserved_slots_ == slots_.size()) {
    lowest_free_slot_ = kNoEvictionSlot;
    return;
  }
  lowest_free_slot_ = kNoEvictionSlot;
  for (std::size_t candidate = index + 1U; candidate < slots_.size(); ++candidate) {
    if (!slots_[candidate].occupied_ && !slots_[candidate].preserved_) {
      lowest_free_slot_ = static_cast<std::uint16_t>(candidate);
      break;
    }
  }
}

std::optional<std::size_t> MaterializedCanvas::choose_slot() {
  if (eviction_index_ready()) {
    if (occupied_slots_ + preserved_slots_ < slots_.size()) {
      if (lowest_free_slot_ == kNoEvictionSlot) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
          if (!slots_[index].occupied_ && !slots_[index].preserved_) {
            lowest_free_slot_ = static_cast<std::uint16_t>(index);
            break;
          }
        }
      }
      if (lowest_free_slot_ != kNoEvictionSlot) {
        return lowest_free_slot_;
      }
    }
    if (slots_.empty()) {
      return std::nullopt;
    }
    if (eviction_candidates_dirty_) {
      rebuild_eviction_candidates();
    }
    if (preserved_slots_ != 0U) {
      return oldest_eviction_by_class_[kPreservedEvictionClass];
    }
    for (std::size_t candidate_class = 0; candidate_class < kPreservedEvictionClass;
         ++candidate_class) {
      if (oldest_eviction_by_class_[candidate_class] != kNoEvictionSlot) {
        return oldest_eviction_by_class_[candidate_class];
      }
    }
    return std::nullopt;
  }

  // A full pool has no unoccupied slot by definition; skip the free scan.
  if (occupied_slots_ + preserved_slots_ < slots_.size()) {
    const auto available = std::find_if(slots_.begin(), slots_.end(), [](const auto& slot) {
      return !slot.occupied_ && !slot.preserved_;
    });
    if (available != slots_.end()) {
      return static_cast<std::size_t>(available - slots_.begin());
    }
  }
  if (preserved_slots_ != 0U) {
    // Preserved history pre-images are the expendable class: reuse the
    // stalest one before evicting any current tile.
    const MaterializedSlotStorage* oldest_preserved = nullptr;
    for (const MaterializedSlotStorage& slot : slots_) {
      if (slot.preserved_ &&
          (oldest_preserved == nullptr || slot.last_use_ < oldest_preserved->last_use_)) {
        oldest_preserved = &slot;
      }
    }
    if (oldest_preserved != nullptr) {
      return static_cast<std::size_t>(oldest_preserved - slots_.data());
    }
  }
  if (slots_.empty()) {
    return std::nullopt;
  }
  // std::min_element's comparator evaluates both ranks for every candidate.
  // Cache the current minimum's rank so a full scan classifies each slot once.
  const MaterializedSlotStorage* oldest = slots_.data();
  std::uint8_t oldest_rank = 3U;
  for (const MaterializedSlotStorage& candidate : slots_) {
    const std::uint8_t candidate_rank = eviction_protection_rank(candidate.key_);
    if (candidate_rank < oldest_rank ||
        (candidate_rank == oldest_rank && candidate.last_use_ < oldest->last_use_)) {
      oldest = &candidate;
      oldest_rank = candidate_rank;
    }
  }
  return static_cast<std::size_t>(oldest - slots_.data());
}

void MaterializedCanvas::touch(MaterializedSlotStorage& slot) {
  if (eviction_index_ready()) {
    const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
    unlink_eviction_slot(index);
    slot.last_use_ = ++use_clock_;
    append_eviction_slot(index);
    return;
  }
  slot.last_use_ = ++use_clock_;
}

void MaterializedCanvas::release_slot(std::size_t index) {
  MaterializedSlotStorage& slot = slots_[index];
  if (!slot.occupied_) {
    return;
  }
  unlink_eviction_slot(index);
  slot.occupied_ = false;
  --occupied_slots_;
  if (eviction_index_ready() &&
      (lowest_free_slot_ == kNoEvictionSlot || index < lowest_free_slot_)) {
    lowest_free_slot_ = static_cast<std::uint16_t>(index);
  }
  if (const auto identity = tile_identity_index(slot.key_);
      identity.has_value() && raw_slot_directory_[*identity] == static_cast<std::uint16_t>(index)) {
    raw_slot_directory_[*identity] = kNoRawSlot;
  }
}

void MaterializedCanvas::claim_slot(std::size_t index) {
  MaterializedSlotStorage& slot = slots_[index];
  if (slot.preserved_) {
    // Any ordinary claim of a preserved slot consumes the pre-image.
    unlink_eviction_slot(index);
    slot.preserved_ = false;
    --preserved_slots_;
  }
  if (!slot.occupied_) {
    slot.occupied_ = true;
    ++occupied_slots_;
    consume_free_slot(index);
    append_eviction_slot(index);
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
  const std::uint16_t existing = find_tile(key);
  if (existing != kNoRawSlot &&
      static_cast<int>(quality) < static_cast<int>(slots_[existing].quality_)) {
    return std::nullopt;
  }
  // Same-revision quality must not regress through the representation swap:
  // a raw publication below an existing uniform's quality is a downgrade.
  if (const std::uint16_t uniform = find_uniform(key);
      uniform != kNoRawSlot &&
      static_cast<int>(quality) < static_cast<int>(uniform_catalog_[uniform].quality_)) {
    return std::nullopt;
  }
  const auto selected =
      existing != kNoRawSlot ? std::optional<std::size_t>{existing} : choose_slot();
  if (!selected.has_value()) {
    return std::nullopt;
  }
  ++composition_epoch_;
  const std::size_t index = *selected;
  MaterializedSlotStorage& slot = slots_[index];
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr && slot.occupied_ && !(slot.key_ == key)) {
    rerender_ledger_->mark_evicted(slot.key_);
  }
#endif
  release_slot(index);
  auto destination = tile_pixels_.subspan(index * kTilePixels, kTilePixels);
  copy_publication_rows(pixels.data(), source_stride, destination.data(), width, height);
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
  ++composition_epoch_;
  if (const std::uint16_t raw = find_tile(key); raw != kNoRawSlot) {
    const MaterializedSlotStorage& slot = slots_[raw];
    if (static_cast<int>(quality) < static_cast<int>(slot.quality_)) {
      return std::nullopt;
    }
    release_slot(raw);
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
  const std::uint16_t tile_index = find_tile(key);
  if (tile_index != kNoRawSlot && raw_slot_is_current(slots_[tile_index])) {
    return SourceSelection{.kind = SourceKind::kTileSlot,
                           .revision = current_revision_,
                           .quality = slots_[tile_index].quality_};
  }
  if (const std::uint16_t uniform = find_uniform(key); uniform != kNoRawSlot) {
    return SourceSelection{.kind = SourceKind::kUniform,
                           .revision = current_revision_,
                           .quality = uniform_catalog_[uniform].quality_};
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
  // GCC 15 lowers the 21-byte aggregate assignment to a stack temporary and
  // a ROM memcpy on Xtensa. These six scalar stores execute on every pan and
  // zoom view registration and preserve the exact field representation.
  ViewFootprint& footprint = recent_views_[index];
  footprint.zoom = view.zoom;
  footprint.level_pixels.x0 = view.level_pixels.x0;
  footprint.level_pixels.y0 = view.level_pixels.y0;
  footprint.level_pixels.x1 = view.level_pixels.x1;
  footprint.level_pixels.y1 = view.level_pixels.y1;
  footprint.valid = true;
  active_view_zoom_ = view.zoom;
  if (eviction_index_ready()) {
    eviction_candidates_dirty_ = true;
  }
  return true;
}

bool MaterializedCanvas::discard_tiles() {
  if (!ready()) {
    return false;
  }
  ++composition_epoch_;
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (!slots_[index].occupied_) {
      continue;
    }
    release_slot(index);
  }
  drop_preserved_slots();
  clear_uniforms();
  return true;
}

void MaterializedCanvas::drop_preserved_slots() {
  if (preserved_slots_ == 0U) {
    return;
  }
  for (MaterializedSlotStorage& slot : slots_) {
    if (!slot.preserved_) {
      continue;
    }
    const std::size_t index = static_cast<std::size_t>(&slot - slots_.data());
    unlink_eviction_slot(index);
    slot.preserved_ = false;
    slot.preserved_prefix_ = 0U;
    if (eviction_index_ready() &&
        (lowest_free_slot_ == kNoEvictionSlot || index < lowest_free_slot_)) {
      lowest_free_slot_ = static_cast<std::uint16_t>(index);
    }
  }
  preserved_slots_ = 0U;
}

void MaterializedCanvas::preserve_slot(std::size_t index, std::uint16_t prefix) {
  release_slot(index);
  MaterializedSlotStorage& slot = slots_[index];
  slot.preserved_ = true;
  slot.preserved_prefix_ = prefix;
  ++preserved_slots_;
  consume_free_slot(index);
  touch(slot);
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

}  // namespace tinydraw::vector_v2
