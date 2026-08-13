#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/ui/toolbar.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {

// Runs the historical Gate 1 workload and leaves the realistic seed-7 document
// loaded for manual glass testing. This harness is excluded from the normal
// Vector V2 application image.
[[nodiscard]] bool run_vector_v2_gate_harness(
    VectorV2Presenter& presenter, vector_v2::TileProducer& producer, vector_v2::OperationLog& log,
    vector_v2::MaterializedCanvas& canvas, const ToolbarState& toolbar,
    const vector_v2::IncrementalDocumentWorkspace& workspace,
    std::span<const std::uint16_t> blank_snapshot,
    std::span<vector_v2::CompactOperationSample> conversion_storage,
    std::span<std::uint16_t> packed_tile_pixels);

}  // namespace tinydraw::esp32
