#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "tinydraw/vector_v2/materialized_canvas.h"

namespace vector_v2 = tinydraw::vector_v2;

// Tests use the same catalog, occupancy, and raw-slot directory shape as the
// firmware. The metadata base is constructed before MaterializedCanvas.
class TestCanvasMetadata {
 protected:
  TestCanvasMetadata()
      : uniforms_(std::make_unique<std::array<vector_v2::MaterializedUniformStorage,
                                              vector_v2::kMaterializedTileIdentityCount>>()),
        directory_(std::make_unique<
                   std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>>()) {}

  std::unique_ptr<
      std::array<vector_v2::MaterializedUniformStorage, vector_v2::kMaterializedTileIdentityCount>>
      uniforms_;
  std::array<std::uint8_t, vector_v2::kOccupancyBytes> occupancy_{};
  std::unique_ptr<std::array<std::uint16_t, vector_v2::kMaterializedTileIdentityCount>> directory_;
};

class TestCanvas : private TestCanvasMetadata, public vector_v2::MaterializedCanvas {
 public:
  TestCanvas(std::span<std::uint16_t> overview, std::span<vector_v2::MaterializedSlotStorage> slots,
             std::span<std::uint16_t> tile_pixels, vector_v2::DocumentRevision revision = {})
      : vector_v2::MaterializedCanvas(overview, *uniforms_, occupancy_, slots, tile_pixels,
                                      revision, *directory_) {}

  TestCanvas(std::span<std::uint16_t> overview,
             std::span<vector_v2::MaterializedUniformStorage> uniforms,
             std::span<std::uint8_t> occupancy, std::span<vector_v2::MaterializedSlotStorage> slots,
             std::span<std::uint16_t> tile_pixels, vector_v2::DocumentRevision revision = {})
      : vector_v2::MaterializedCanvas(overview, uniforms, occupancy, slots, tile_pixels, revision,
                                      *directory_) {}

  TestCanvas(std::span<std::uint16_t> overview,
             std::span<vector_v2::MaterializedUniformStorage> uniforms,
             std::span<std::uint8_t> occupancy, std::span<vector_v2::MaterializedSlotStorage> slots,
             std::span<std::uint16_t> tile_pixels, vector_v2::DocumentRevision revision,
             std::span<std::uint16_t> directory)
      : vector_v2::MaterializedCanvas(overview, uniforms, occupancy, slots, tile_pixels, revision,
                                      directory) {}
};
