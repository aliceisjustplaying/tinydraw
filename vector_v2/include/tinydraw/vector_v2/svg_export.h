#ifndef TINYDRAW_VECTOR_V2_SVG_EXPORT_H
#define TINYDRAW_VECTOR_V2_SVG_EXPORT_H

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

struct SvgExportOptions {
  PixelRect world_bounds{0, 0, kWorldWidth, kWorldHeight};
  std::uint16_t background = 0xFFFFU;
};

// Streams a standalone SVG from the painter-ordered operation authority.
// Every operation is one group containing the exact circles and convex paths
// emitted by CurvedRibbonStream, the same ribbon geometry path used by the
// renderer. The shapes' set union is therefore identical to the rendered
// ribbon without constructing a potentially self-intersecting outline or
// approximating variable width. Erasers use the requested opaque background
// color and retain their operation order.
//
// No document-sized output or geometry buffer is allocated. Formatting uses
// fixed stack storage and RibbonPrimitiveBatch's bounded inline storage.
// Callers must serialize mutation of log for the duration of this authority
// snapshot read. A sink failure or authority change makes the export fail.
[[nodiscard]] bool export_svg(const OperationLog& log, SvgByteSink& sink,
                              SvgExportOptions options = {});

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_SVG_EXPORT_H
