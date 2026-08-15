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
// each tiled zoom plus one disjoint destination (5 x 56 = 280), leaving the
// remainder as LRU slots. Raised 320 -> 384 at 264b60e, then 384 -> 448 for
// the 2026-08-15 idle-repair round: dense documents (hairlines defeat
// uniform coverage) exceed any affordable pool at 100%, and the extra 64
// slots (512 KiB) widen the warm neighborhood from ~9 to ~10.6 viewports
// while the repair saturation guard prevents churn past capacity. The
// 1.5 MiB contiguous export reserve stays funded; remaining PSRAM slack is the Undo/Redo insurance,
// which rides the operation log and needs little. TINYDRAW_VECTOR_V2_TILE_SLOTS exists for measured
// cache-size A/B builds; the product default is 448. 512 was
// tried first and rejected: the battery's export-reserve gate could no
// longer hold the 1.5 MiB contiguous block at peak harness state.
#ifndef TINYDRAW_VECTOR_V2_TILE_SLOTS
#define TINYDRAW_VECTOR_V2_TILE_SLOTS 448
#endif
inline constexpr std::size_t kTileSlotCount = TINYDRAW_VECTOR_V2_TILE_SLOTS;
// Five worst-case arbitrary-alignment viewport footprints (5 x 56 = 280) is
// the minimum retained working set; below that the producer can evict the
// active viewport's own tiles mid-fill.
static_assert(kTileSlotCount >= 280);
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
