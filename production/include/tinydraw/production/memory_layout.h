#ifndef TINYDRAW_PRODUCTION_MEMORY_LAYOUT_H
#define TINYDRAW_PRODUCTION_MEMORY_LAYOUT_H

#include <cstddef>
#include <cstdint>

#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/operation_lod_store.h"

namespace tinydraw::production {

// Revision publication renders the complete next overview outside the live
// overview before committing it. This region is distinct from kOverviewBytes.
inline constexpr std::size_t kOverviewPublicationBytes = kOverviewBytes;
// Retain one arbitrary-alignment viewport at each tiled zoom (4 x 56),
// with a small margin for adjacent pan tiles.
inline constexpr std::size_t kTileSlotCount = 256;
inline constexpr std::size_t kOperationCapacity = 4'000;
inline constexpr std::size_t kOperationSampleCapacity = 80'000;
inline constexpr std::size_t kMaterializedZoomCount = kLodZoomCount;
inline constexpr std::size_t kLodSampleCapacity = 90'000;
inline constexpr std::size_t kRendererTaskCount = 2;
inline constexpr std::size_t kRendererCoverageBytes = 128U * 128U;
inline constexpr std::size_t kRendererDestinationBytes = 128U * 128U * sizeof(std::uint16_t);
inline constexpr std::size_t kRendererGeometryBytes = 64U * 1024U;
inline constexpr std::size_t kOverlayBytes = 368U * 76U * sizeof(std::uint16_t);
inline constexpr std::size_t kStagingBytes = 2U * 368U * 32U * sizeof(std::uint16_t);
inline constexpr std::size_t kTargetContiguousReserveBytes = 1536U * 1024U;

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
