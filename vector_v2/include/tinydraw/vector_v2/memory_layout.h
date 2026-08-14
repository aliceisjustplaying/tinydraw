#ifndef TINYDRAW_VECTOR_V2_MEMORY_LAYOUT_H
#define TINYDRAW_VECTOR_V2_MEMORY_LAYOUT_H

#include <cstddef>
#include <cstdint>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation_lod_store.h"

namespace tinydraw::vector_v2 {

// Revision publication renders the complete next overview outside the live
// overview before committing it. This region is distinct from kOverviewBytes.
inline constexpr std::size_t kOverviewPublicationBytes = kOverviewBytes;
// Retain five worst-case arbitrary-alignment viewport footprints: one at
// each tiled zoom plus one disjoint destination (5 x 56 = 280), leaving 104
// additional LRU slots. Raised from 320 after the interactive path stopped
// funding its 56-tile staging workspace (449 KiB freed at 264b60e); the
// additional 64 slots cost 512 KiB and the live export reserve, stack, and
// fragmentation margins were re-proven on hardware with the new count.
inline constexpr std::size_t kTileSlotCount = 384;
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
inline constexpr std::size_t kTileMetadataBytes =
    kTileSlotCount * sizeof(MaterializedSlotStorage) +
    kMaterializedTileIdentityCount * sizeof(MaterializedUniformStorage) + kOccupancyBytes;
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

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_MEMORY_LAYOUT_H
