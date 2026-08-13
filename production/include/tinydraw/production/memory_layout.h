#ifndef TINYDRAW_PRODUCTION_MEMORY_LAYOUT_H
#define TINYDRAW_PRODUCTION_MEMORY_LAYOUT_H

#include <cstddef>
#include <cstdint>

#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation.h"

namespace tinydraw::production {

// Revision publication renders the complete next overview outside the live
// overview before committing it. This region is distinct from kOverviewBytes.
inline constexpr std::size_t kOverviewPublicationBytes = kOverviewBytes;
inline constexpr std::size_t kTileSlotCount = 128;
inline constexpr std::size_t kOperationCapacity = 4'000;
inline constexpr std::size_t kOperationSampleCapacity = 80'000;
inline constexpr std::size_t kMaterializedZoomCount = 4;
inline constexpr std::size_t kLodSampleCapacity = 90'000;
inline constexpr std::size_t kRendererTaskCount = 2;
inline constexpr std::size_t kRendererCoverageBytes = 128U * 128U;
inline constexpr std::size_t kRendererDestinationBytes = 128U * 128U * sizeof(std::uint16_t);
inline constexpr std::size_t kRendererGeometryBytes = 64U * 1024U;
inline constexpr std::size_t kOverlayBytes = 368U * 76U * sizeof(std::uint16_t);
inline constexpr std::size_t kStagingBytes = 2U * 368U * 32U * sizeof(std::uint16_t);
inline constexpr std::size_t kTargetContiguousReserveBytes = 1536U * 1024U;

struct OperationRecord {
  std::uint32_t first_sample = 0;
  std::uint16_t sample_count = 0;
  std::uint16_t color = 0;
  std::uint16_t bounds_x0 = 0;
  std::uint16_t bounds_y0 = 0;
  std::uint16_t bounds_x1 = 0;
  std::uint16_t bounds_y1 = 0;
  std::uint8_t tool = 0;
  std::uint8_t flags = 0;
  std::uint16_t reserved = 0;
};

struct CompactLodSample {
  std::uint16_t x_quarter = 0;
  std::uint16_t y_quarter = 0;
  std::uint16_t radius_256 = 0;
};

struct LodSpan {
  std::uint32_t first_sample = 0;
  std::uint16_t sample_count = 0;
  std::uint16_t flags = 0;
};

static_assert(sizeof(OperationRecord) == 20);
static_assert(sizeof(CompactLodSample) == 6);
static_assert(sizeof(LodSpan) == 8);

inline constexpr std::size_t kTilePoolBytes = kTileSlotCount * kTileBytes;
inline constexpr std::size_t kTileMetadataBytes = kTileSlotCount * sizeof(MaterializedSlotStorage);
inline constexpr std::size_t kOperationRecordBytes = kOperationCapacity * sizeof(OperationRecord);
inline constexpr std::size_t kOperationSampleBytes =
    kOperationSampleCapacity * sizeof(CompactOperationSample);
inline constexpr std::size_t kOperationStorageBytes = kOperationRecordBytes + kOperationSampleBytes;
inline constexpr std::size_t kLodSampleBytes = kLodSampleCapacity * sizeof(CompactLodSample);
inline constexpr std::size_t kLodSpanBytes =
    kMaterializedZoomCount * kOperationCapacity * sizeof(LodSpan);
inline constexpr std::size_t kLodStorageBytes = kLodSampleBytes + kLodSpanBytes;
inline constexpr std::size_t kRendererWorkspaceBytes =
    kRendererTaskCount * (kRendererCoverageBytes + kRendererDestinationBytes) +
    kRendererGeometryBytes;
inline constexpr std::size_t kDisplayWorkspaceBytes = kOverlayBytes + kStagingBytes;
inline constexpr std::size_t kExternalPlanBytes =
    kOverviewBytes + kOverviewPublicationBytes + kTilePoolBytes + kTileMetadataBytes +
    kOperationStorageBytes + kLodStorageBytes + kRendererWorkspaceBytes + kDisplayWorkspaceBytes;

}  // namespace tinydraw::production

#endif  // TINYDRAW_PRODUCTION_MEMORY_LAYOUT_H
