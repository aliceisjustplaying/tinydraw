#include "tinydraw/vector_v2/rerender_ledger.h"

#include <algorithm>
#include <array>
#include <limits>

namespace tinydraw::vector_v2 {
namespace {

constexpr std::uint8_t kRenderedFlag = 0x1U;
constexpr std::uint8_t kDamagedFlag = 0x2U;
constexpr std::uint8_t kEvictedFlag = 0x4U;

constexpr int kGroupLevelPixels = 128;

struct ZoomPlane {
  ZoomLevel zoom;
  int percent;
};

constexpr std::array<ZoomPlane, 5> kPlanes{{
    {ZoomLevel::k25Percent, 25},
    {ZoomLevel::k50Percent, 50},
    {ZoomLevel::k100Percent, 100},
    {ZoomLevel::k200Percent, 200},
    {ZoomLevel::k400Percent, 400},
}};

struct PlaneGeometry {
  std::size_t offset = 0;
  int group_columns = 0;
  int group_rows = 0;
  int percent = 0;
};

PlaneGeometry plane_geometry(ZoomLevel zoom) {
  std::size_t offset = 0;
  for (const ZoomPlane& plane : kPlanes) {
    const int width = (kWorldWidth * plane.percent + 99) / 100;
    const int height = (kWorldHeight * plane.percent + 99) / 100;
    const int columns = (width + kGroupLevelPixels - 1) / kGroupLevelPixels;
    const int rows = (height + kGroupLevelPixels - 1) / kGroupLevelPixels;
    if (plane.zoom == zoom) {
      return {
          .offset = offset, .group_columns = columns, .group_rows = rows, .percent = plane.percent};
    }
    offset += static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows);
  }
  return {};
}

}  // namespace

RerenderLedger::RerenderLedger(std::span<RerenderLedgerEntry> entries) : entries_(entries) {
  reset();
}

bool RerenderLedger::ready() const { return entries_.size() >= kRerenderLedgerEntryCount; }

RerenderLedgerEntry* RerenderLedger::entry_at(ZoomLevel zoom, int group_column, int group_row) {
  if (!ready()) {
    return nullptr;
  }
  const PlaneGeometry plane = plane_geometry(zoom);
  if (group_column < 0 || group_column >= plane.group_columns || group_row < 0 ||
      group_row >= plane.group_rows) {
    return nullptr;
  }
  return &entries_[plane.offset +
                   static_cast<std::size_t>(group_row) *
                       static_cast<std::size_t>(plane.group_columns) +
                   static_cast<std::size_t>(group_column)];
}

void RerenderLedger::mark_world_damage(PixelRect world_bounds) {
  if (!ready() || world_bounds.x1 <= world_bounds.x0 || world_bounds.y1 <= world_bounds.y0) {
    return;
  }
  for (const ZoomPlane& plane : kPlanes) {
    // Conservative outward rounding: a partially covered group is damaged.
    const int x0 = world_bounds.x0 * plane.percent / 100;
    const int y0 = world_bounds.y0 * plane.percent / 100;
    const int x1 = (world_bounds.x1 * plane.percent + 99) / 100;
    const int y1 = (world_bounds.y1 * plane.percent + 99) / 100;
    const PlaneGeometry geometry = plane_geometry(plane.zoom);
    const int first_column = std::clamp(x0 / kGroupLevelPixels, 0, geometry.group_columns - 1);
    const int last_column = std::clamp((x1 - 1) / kGroupLevelPixels, 0, geometry.group_columns - 1);
    const int first_row = std::clamp(y0 / kGroupLevelPixels, 0, geometry.group_rows - 1);
    const int last_row = std::clamp((y1 - 1) / kGroupLevelPixels, 0, geometry.group_rows - 1);
    for (int row = first_row; row <= last_row; ++row) {
      for (int column = first_column; column <= last_column; ++column) {
        if (RerenderLedgerEntry* entry = entry_at(plane.zoom, column, row)) {
          entry->flags |= kDamagedFlag;
        }
      }
    }
  }
}

void RerenderLedger::mark_evicted(TileKey key) {
  if (RerenderLedgerEntry* entry = entry_at(key.zoom, key.column / 2, key.row / 2)) {
    entry->flags |= kEvictedFlag;
  }
}

RerenderCause RerenderLedger::record_group_render(ZoomLevel zoom, std::uint16_t origin_tile_column,
                                                  std::uint16_t origin_tile_row,
                                                  DocumentRevision revision) {
  RerenderLedgerEntry* entry = entry_at(zoom, origin_tile_column / 2, origin_tile_row / 2);
  if (entry == nullptr) {
    return RerenderCause::kColdMiss;
  }
  RerenderCause cause = RerenderCause::kUnexplained;
  if ((entry->flags & kRenderedFlag) == 0U) {
    cause = RerenderCause::kColdMiss;
    ++totals_.cold_miss;
    ++totals_.unique_groups;
  } else if ((entry->flags & kDamagedFlag) != 0U) {
    cause = RerenderCause::kExpectedDamage;
    ++totals_.expected_damage;
  } else if ((entry->flags & kEvictedFlag) != 0U) {
    cause = RerenderCause::kEviction;
    ++totals_.eviction;
  } else if (entry->revision != revision.value) {
    cause = RerenderCause::kStaleRevision;
    ++totals_.stale_revision;
  } else {
    ++totals_.unexplained;
  }
  ++totals_.renders;
  entry->revision = revision.value;
  entry->flags = kRenderedFlag;
  entry->renders = entry->renders == std::numeric_limits<std::uint16_t>::max()
                       ? entry->renders
                       : entry->renders + 1U;
  entry->last_cause = static_cast<std::uint8_t>(cause);
  return cause;
}

void RerenderLedger::reset() {
  std::fill(entries_.begin(), entries_.end(), RerenderLedgerEntry{});
  totals_ = {};
}

RerenderLedgerTotals RerenderLedger::totals() const { return totals_; }

}  // namespace tinydraw::vector_v2
