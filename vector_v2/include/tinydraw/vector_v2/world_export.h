#ifndef TINYDRAW_VECTOR_V2_WORLD_EXPORT_H
#define TINYDRAW_VECTOR_V2_WORLD_EXPORT_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/settled_tile.h"

namespace tinydraw::vector_v2 {

using WorldExportProgress = void (*)(void* context);

// Full-world sibling that stitches the production settled-AA tile renderer
// into caller-owned horizontal bands. It retains only one band, one tile, and
// the settled tile workspace, so memory is fixed regardless of document size.
// The authority identity is pinned at construction and checked around every
// band; a concurrent mutation fails the stream instead of mixing revisions.
class SettledWorldBandRenderer {
 public:
  SettledWorldBandRenderer(const OperationLog& log, std::span<std::uint16_t> band_pixels,
                           std::span<std::uint16_t> window_pixels, SettledTileWorkspace workspace,
                           WorldExportProgress progress = nullptr,
                           void* progress_context = nullptr);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] int band_rows() const;
  [[nodiscard]] bool render_row(int y, std::span<std::uint16_t> destination);

 private:
  [[nodiscard]] bool authority_matches() const;
  [[nodiscard]] bool render_band(int first_row);

  const OperationLog& log_;
  std::span<std::uint16_t> band_pixels_;
  std::span<std::uint16_t> window_pixels_;
  SettledTileWorkspace workspace_;
  WorldExportProgress progress_ = nullptr;
  void* progress_context_ = nullptr;
  OperationLogEpoch epoch_{};
  DocumentRevision revision_{};
  std::size_t operation_count_ = 0;
  int band_rows_ = 0;
  int band_first_ = 0;
  int band_height_ = 0;
  bool band_valid_ = false;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_WORLD_EXPORT_H
