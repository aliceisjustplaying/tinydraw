#ifndef TINYDRAW_VECTOR_V2_MEMORY_LAYOUT_H
#define TINYDRAW_VECTOR_V2_MEMORY_LAYOUT_H

#include <cstddef>
#include <cstdint>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

// This plan pins the canvas-side fixed regions so drift is a deliberate
// edit, not an accident. It is NOT a full model of AppStorage (the app
// allocates additional scratch and snapshot buffers); the battery's
// export-reserve gate is the authority on real peak memory.
// Revision publication renders the complete next overview outside the live
// overview before committing it. This region is distinct from kOverviewBytes.
inline constexpr std::size_t kOverviewPublicationBytes = kOverviewBytes;
// Retain five worst-case arbitrary-alignment viewport footprints: one at
// each tiled zoom plus one disjoint destination (5 x 56 = 280), leaving the
// remainder as LRU slots. Raised 320 -> 384 at 264b60e, then 384 -> 448 for
// the 2026-08-15 idle-repair round, then 448 -> 604 on 2026-08-18 by owner
// decision after the export-memory receipt
// (benchmark-results/export-memory-math-2026-08-18) showed the historical
// 1.5 MiB contiguous export reserve was uninherited V1-era insurance: the
// measured V2 export peak is 291,484 B and document-size-independent. The
// same-night 604-slot physical battery measured cold walls within layout
// dice (worst +1.6%, binding 400% unchanged) and left 870,792 B free -
// covering the 704,512 B measured full-capacity autosave staging line with
// slack; export flushes autosave before its own allocation, so the two
// peaks are sequential. The extra 156 slots fund Undo/Redo preserved
// pre-image versions (evicted first under pressure) plus retention.
// TINYDRAW_VECTOR_V2_TILE_SLOTS remains the measured cache-size A/B knob.
// The 512-slot rejection recorded here previously was an artifact of the
// fictional reserve check, not of cache pressure.
#ifndef TINYDRAW_VECTOR_V2_TILE_SLOTS
#define TINYDRAW_VECTOR_V2_TILE_SLOTS 604
#endif
inline constexpr std::size_t kTileSlotCount = TINYDRAW_VECTOR_V2_TILE_SLOTS;
// Five worst-case arbitrary-alignment viewport footprints (5 x 56 = 280) is
// the minimum retained working set; below that the producer can evict the
// active viewport's own tiles mid-fill.
static_assert(kTileSlotCount >= 280);
inline constexpr std::size_t kOperationCapacity = 4'000;
inline constexpr std::size_t kOperationSampleCapacity = 80'000;
inline constexpr std::size_t kRendererTaskCount = 2;
inline constexpr std::size_t kRendererCoverageBytes = 128U * 128U;
inline constexpr std::size_t kRendererDestinationBytes = 128U * 128U * sizeof(std::uint16_t);
inline constexpr std::size_t kRendererGeometryBytes = 64U * 1024U;
inline constexpr std::size_t kOverlayBytes = 368U * 76U * sizeof(std::uint16_t);
inline constexpr std::size_t kStagingBytes = 2U * 368U * 32U * sizeof(std::uint16_t);
// Honest concurrent-envelope reserve, replacing the fictional 1.5 MiB
// contiguous target: the measured full-capacity autosave staging peak plus
// the measured export workspace peak with margin. The battery asserts both
// can be held simultaneously even though the product sequences them.
inline constexpr std::size_t kAutosaveStagingReserveBytes = 704'512U;
inline constexpr std::size_t kExportWorkspaceReserveBytes = 320U * 1024U;

inline constexpr std::size_t kTilePoolBytes = kTileSlotCount * kTileBytes;
inline constexpr std::size_t kTileSlotStorageBytes =
    kTileSlotCount * sizeof(MaterializedSlotStorage);
// Keep the ESP32 allocation at its measured pre-packing footprint so records
// and every later PSRAM allocation retain their established dcache-set phase.
inline constexpr std::size_t kTileSlotAllocationStrideBytes = 32U;
static_assert(sizeof(MaterializedSlotStorage) <= kTileSlotAllocationStrideBytes);
inline constexpr std::size_t kTileSlotAllocationBytes =
    kTileSlotCount * kTileSlotAllocationStrideBytes;
inline constexpr std::size_t kTileSlotAllocationPaddingBytes =
    kTileSlotAllocationBytes - kTileSlotStorageBytes;
inline constexpr std::size_t kTileEvictionLinkBytes = kTileSlotCount * 2U * sizeof(std::uint16_t);
static_assert(kTileEvictionLinkBytes <= kTileSlotAllocationPaddingBytes);
inline constexpr std::size_t kTileMetadataBytes =
    kTileSlotAllocationBytes + kMaterializedTileIdentityCount * sizeof(MaterializedUniformStorage) +
    kOccupancyBytes;
inline constexpr std::size_t kOperationRecordBytes = kOperationCapacity * sizeof(OperationRecord);
inline constexpr std::size_t kOperationSampleBytes =
    kOperationSampleCapacity * sizeof(CompactOperationSample);
inline constexpr std::size_t kOperationStorageBytes = kOperationRecordBytes + kOperationSampleBytes;
inline constexpr std::size_t kRendererWorkspaceBytes =
    kRendererTaskCount * (kRendererCoverageBytes + kRendererDestinationBytes) +
    kRendererGeometryBytes;
inline constexpr std::size_t kDisplayWorkspaceBytes = kOverlayBytes + kStagingBytes;
inline constexpr std::size_t kExternalPlanBytes =
    kOverviewBytes + kOverviewPublicationBytes + kTilePoolBytes + kTileMetadataBytes +
    kOperationStorageBytes + kRendererWorkspaceBytes + kDisplayWorkspaceBytes;

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_MEMORY_LAYOUT_H
