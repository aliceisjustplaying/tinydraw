#include <algorithm>
#include <array>

#include "tinydraw/vector_v2/materialized_canvas.h"

#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
#include "tinydraw/vector_v2/rerender_ledger.h"
#endif
#include "materialized_canvas_internal.h"

namespace tinydraw::vector_v2 {
using namespace materialized_canvas_detail;

namespace {
constexpr std::array kMetadataZooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent,
                                    ZoomLevel::k200Percent, ZoomLevel::k400Percent};
constexpr std::size_t kMarkRetained = kTiledZoomCount + 1U;
constexpr std::size_t kClearRetained = kTiledZoomCount + 2U;

struct MetadataTileRange {
  int first_column = 0;
  int first_row = 0;
  std::size_t columns = 0;
  std::size_t count = 0;
};

MetadataTileRange metadata_tile_range(ZoomLevel zoom, PixelRect affected_world_bounds) {
  const int percent = zoom_percent(zoom);
  const TileGrid grid = tile_grid(zoom);
  const int first_column =
      std::clamp(affected_world_bounds.x0 * percent / 100 / kTileWidth - 1, 0, grid.columns - 1);
  const int last_column =
      std::clamp((ceil_div(affected_world_bounds.x1 * percent, 100) - 1) / kTileWidth + 1, 0,
                 grid.columns - 1);
  const int first_row =
      std::clamp(affected_world_bounds.y0 * percent / 100 / kTileHeight - 1, 0, grid.rows - 1);
  const int last_row = std::clamp(
      (ceil_div(affected_world_bounds.y1 * percent, 100) - 1) / kTileHeight + 1, 0, grid.rows - 1);
  const std::size_t columns = static_cast<std::size_t>(last_column - first_column) + 1U;
  const std::size_t rows = static_cast<std::size_t>(last_row - first_row) + 1U;
  return {.first_column = first_column,
          .first_row = first_row,
          .columns = columns,
          .count = columns * rows};
}
}  // namespace

struct MaterializedCanvas::MetadataWork {
  explicit MetadataWork(const InPlaceMetadataRequest& input)
      : request(input),
        output{.status = OverviewStageStatus::kInProgress, .phase = input.stage.metadata_phase_},
        retained(input.stage.retained_keys_, input.stage.retained_count_) {}

  const InPlaceMetadataRequest& request;
  InPlaceMetadataSlice output{};
  std::span<const TileKey> retained{};
  bool mutated = false;
};

bool MaterializedCanvas::prepare_metadata_stage(const InPlaceMetadataRequest& request,
                                                const RerenderLedger* ledger) {
  InPlaceOverviewStage& stage = request.stage;
  if (request.max_work_items == 0U ||
      !valid_incremental_revision(request.revision, request.overview_publication,
                                  request.affected_world_bounds, {}) ||
      !stage.complete() || stage.canvas_ != this || stage.revision_ != request.revision ||
      stage.bounds_ != request.overview_publication.bounds ||
      stage.affected_world_bounds_ != request.affected_world_bounds ||
      stage.source_ != request.overview_publication.pixels.data() ||
      stage.source_size_ != request.overview_publication.pixels.size() ||
      stage.expected_canvas_epoch_ != composition_epoch_ ||
      (stage.raw_staging_started_ &&
       (!staged_in_place_active_ || staged_in_place_revision_ != request.revision))) {
    return false;
  }
  if (!stage.metadata_started_) {
    stage.retained_keys_ = request.retained_keys.data();
    stage.retained_count_ = request.retained_keys.size();
    stage.preserved_uniform_color_ = request.scope.preserved_uniform_color;
    stage.priority_zoom_ = request.scope.priority_zoom;
    stage.rerender_ledger_ = ledger;
    stage.metadata_started_ = true;
    return true;
  }
  return stage.retained_keys_ == request.retained_keys.data() &&
         stage.retained_count_ == request.retained_keys.size() &&
         stage.preserved_uniform_color_ == request.scope.preserved_uniform_color &&
         stage.priority_zoom_ == request.scope.priority_zoom && stage.rerender_ledger_ == ledger;
}

bool MaterializedCanvas::stage_retained_metadata(MetadataWork& work) {
  InPlaceOverviewStage& stage = work.request.stage;
  if (stage.metadata_zoom_ != kMarkRetained) {
    return true;
  }
  if (!stage.raw_staging_started_) {
    staged_in_place_revision_ = work.request.revision;
    staged_in_place_active_ = true;
    stage.raw_staging_started_ = true;
    work.mutated = true;
  }
  while (stage.metadata_offset_ < work.retained.size() &&
         work.output.work_items < work.request.max_work_items) {
    mark_retained_key(work.retained[stage.metadata_offset_], work.request.revision);
    ++stage.metadata_offset_;
    ++work.output.work_items;
    work.mutated = true;
  }
  if (stage.metadata_offset_ != work.retained.size()) {
    return false;
  }
  stage.metadata_zoom_ = 0U;
  stage.metadata_offset_ = 0U;
  return true;
}

void MaterializedCanvas::update_uniform_metadata(TileKey key, MetadataWork& work) {
  const auto index = tile_identity_index(key);
  if (!index.has_value() || !uniform_catalog_[*index].occupied_ ||
      !rectangles_intersect(tile_world_bounds(key), work.request.affected_world_bounds)) {
    return;
  }
  const bool retained_marked = raw_slot_directory_[*index] == kRetainedUniformSlot;
  const bool keep = retained_marked || (work.request.scope.preserved_uniform_color.has_value() &&
                                        uniform_catalog_[*index].color_ ==
                                            *work.request.scope.preserved_uniform_color);
  if (retained_marked) {
    raw_slot_directory_[*index] = kNoRawSlot;
    work.mutated = true;
  }
  if (!keep) {
    uniform_catalog_[*index].occupied_ = false;
    work.request.stage.cross_zoom_invalidated_ +=
        key.zoom != work.request.scope.priority_zoom ? 1U : 0U;
    work.mutated = true;
  }
}

void MaterializedCanvas::stage_uniform_zoom_metadata(MetadataWork& work) {
  InPlaceOverviewStage& stage = work.request.stage;
  const ZoomLevel zoom = kMetadataZooms[stage.metadata_zoom_];
  const MetadataTileRange range = metadata_tile_range(zoom, work.request.affected_world_bounds);
  while (stage.metadata_offset_ < range.count &&
         work.output.work_items < work.request.max_work_items) {
    const TileKey key{
        zoom,
        static_cast<std::uint16_t>(range.first_column +
                                   static_cast<int>(stage.metadata_offset_ % range.columns)),
        static_cast<std::uint16_t>(range.first_row +
                                   static_cast<int>(stage.metadata_offset_ / range.columns)),
    };
    update_uniform_metadata(key, work);
    ++stage.metadata_offset_;
    ++work.output.work_items;
  }
  if (stage.metadata_offset_ == range.count) {
    ++stage.metadata_zoom_;
    stage.metadata_offset_ = 0U;
  }
}

void MaterializedCanvas::stage_clear_retained_metadata(MetadataWork& work) {
  InPlaceOverviewStage& stage = work.request.stage;
  while (stage.metadata_offset_ != 0U && work.output.work_items < work.request.max_work_items) {
    --stage.metadata_offset_;
    clear_retained_marker(work.retained[stage.metadata_offset_]);
    ++work.output.work_items;
    work.mutated = true;
  }
  if (stage.metadata_offset_ == 0U) {
    stage.metadata_phase_ = InPlaceMetadataPhase::kRawSlots;
  }
}

void MaterializedCanvas::stage_uniform_metadata(MetadataWork& work) {
  InPlaceOverviewStage& stage = work.request.stage;
  if (!stage_retained_metadata(work)) {
    return;
  }
  while (stage.metadata_zoom_ < kMetadataZooms.size() &&
         work.output.work_items < work.request.max_work_items) {
    stage_uniform_zoom_metadata(work);
  }
  if (stage.metadata_zoom_ == kMetadataZooms.size()) {
    stage.metadata_zoom_ = kClearRetained;
    stage.metadata_offset_ = work.retained.size();
  }
  if (stage.metadata_zoom_ == kClearRetained) {
    stage_clear_retained_metadata(work);
  }
}

void MaterializedCanvas::stage_raw_slot_metadata(MetadataWork& work) {
  InPlaceOverviewStage& stage = work.request.stage;
  while (stage.raw_slot_ < slots_.size() && work.output.work_items < work.request.max_work_items) {
    MaterializedSlotStorage& slot = slots_[stage.raw_slot_];
    if (slot.occupied_) {
      const bool affected =
          rectangles_intersect(tile_world_bounds(slot.key_), work.request.affected_world_bounds);
      const bool keep = !affected || slot.revision_ == work.request.revision;
      if (keep) {
        slot.revision_ = work.request.revision;
      } else {
        stage.cross_zoom_invalidated_ +=
            slot.key_.zoom != work.request.scope.priority_zoom ? 1U : 0U;
        release_slot(stage.raw_slot_);
      }
      work.mutated = true;
    }
    ++stage.raw_slot_;
    ++work.output.work_items;
  }
  if (stage.raw_slot_ == slots_.size()) {
    stage.metadata_phase_ = InPlaceMetadataPhase::kRerenderDamage;
  }
}

void MaterializedCanvas::stage_rerender_metadata(MetadataWork& work) {
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  if (rerender_ledger_ != nullptr) {
    InPlaceOverviewStage& stage = work.request.stage;
    std::size_t marked = 0;
    const bool complete = rerender_ledger_->mark_world_damage_slice(
        work.request.affected_world_bounds, work.request.max_work_items, stage.rerender_plane_,
        stage.rerender_offset_, marked);
    work.output.work_items = marked;
    work.mutated = marked != 0U;
    if (!complete) {
      return;
    }
  }
#endif
  work.request.stage.metadata_phase_ = InPlaceMetadataPhase::kOccupancy;
}

void MaterializedCanvas::stage_occupancy_metadata(MetadataWork& work) {
  InPlaceOverviewStage& stage = work.request.stage;
  const PixelRect bounds = work.request.affected_world_bounds;
  const int first_column = bounds.x0 / kOccupancyCellWorldSize;
  const int last_column = (bounds.x1 - 1) / kOccupancyCellWorldSize;
  const int first_row = bounds.y0 / kOccupancyCellWorldSize;
  const int last_row = (bounds.y1 - 1) / kOccupancyCellWorldSize;
  const std::size_t columns = static_cast<std::size_t>(last_column - first_column) + 1U;
  const std::size_t rows = static_cast<std::size_t>(last_row - first_row) + 1U;
  const std::size_t count = columns * rows;
  while (stage.occupancy_offset_ < count && work.output.work_items < work.request.max_work_items) {
    const int row = first_row + static_cast<int>(stage.occupancy_offset_ / columns);
    const int column = first_column + static_cast<int>(stage.occupancy_offset_ % columns);
    const std::size_t bit =
        static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
    occupancy_bits_[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
    ++stage.occupancy_offset_;
    ++work.output.work_items;
  }
  work.mutated = work.output.work_items != 0U;
  if (stage.occupancy_offset_ == count) {
    stage.metadata_phase_ = InPlaceMetadataPhase::kComplete;
    work.output.status = OverviewStageStatus::kComplete;
  }
}

InPlaceMetadataSlice MaterializedCanvas::stage_in_place_metadata(
    const InPlaceMetadataRequest& request) {
#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS
  const RerenderLedger* ledger = rerender_ledger_;
#else
  const RerenderLedger* ledger = nullptr;
#endif
  if (!prepare_metadata_stage(request, ledger)) {
    return {};
  }

  MetadataWork work{request};
  switch (request.stage.metadata_phase_) {
    case InPlaceMetadataPhase::kUniforms:
      stage_uniform_metadata(work);
      break;
    case InPlaceMetadataPhase::kRawSlots:
      stage_raw_slot_metadata(work);
      break;
    case InPlaceMetadataPhase::kRerenderDamage:
      stage_rerender_metadata(work);
      break;
    case InPlaceMetadataPhase::kOccupancy:
      stage_occupancy_metadata(work);
      break;
    case InPlaceMetadataPhase::kComplete:
      work.output.status = OverviewStageStatus::kComplete;
      break;
  }
  if (work.mutated) {
    ++composition_epoch_;
  }
  request.stage.expected_canvas_epoch_ = composition_epoch_;
  return work.output;
}

}  // namespace tinydraw::vector_v2
