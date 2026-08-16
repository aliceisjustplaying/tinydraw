#ifndef TINYDRAW_VECTOR_V2_SVG_EXPORT_H
#define TINYDRAW_VECTOR_V2_SVG_EXPORT_H

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

class SvgByteSink {
 public:
  virtual ~SvgByteSink() = default;

  // Appends one short fragment. Implementations may write it directly to
  // caller-owned storage, flash, or a file and must not retain the view.
  [[nodiscard]] virtual bool append(std::string_view bytes) = 0;
};

using SvgExportProgress = void (*)(std::size_t completed_operations, std::size_t total_operations,
                                   void* context);

struct SvgExportOptions {
  PixelRect world_bounds{0, 0, kWorldWidth, kWorldHeight};
  std::uint16_t background = 0xFFFFU;
  SvgExportProgress progress = nullptr;
  void* progress_context = nullptr;
};

// Streams a standalone SVG from the painter-ordered operation authority.
// Every operation becomes one filled SVG path containing consistently wound
// subpaths for the exact circles and convex shapes emitted by
// CurvedRibbonStream, the same variable-width ribbon geometry used by the
// renderer. Nonzero filling makes their union identical to the rendered
// stroke without a centerline/stroke-width approximation. Erasers use the
// requested opaque background color and retain their operation order.
//
// No document-sized output or geometry buffer is allocated. Formatting and
// sink batching use fixed stack storage plus RibbonPrimitiveBatch's bounded
// inline storage.
// Callers must serialize mutation of log for the duration of this authority
// snapshot read. A sink failure or authority change makes the export fail.
[[nodiscard]] bool export_svg(const OperationLog& log, SvgByteSink& sink,
                              SvgExportOptions options = {});

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_SVG_EXPORT_H
