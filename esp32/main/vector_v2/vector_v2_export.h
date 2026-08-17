#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "svg_export_store.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "usb_export.h"

namespace tinydraw::esp32 {

using VectorV2ExportProgress = vector_v2::SvgExportProgress;

struct VectorV2ExportStats {
  bool encoded = false;
  std::size_t bytes = 0;
  std::int64_t elapsed_us = 0;
  std::size_t workspace_bytes = 0;
  std::size_t operation_count = 0;
  std::size_t sink_calls = 0;
  std::size_t flash_pages = 0;
  std::uint32_t content_crc32 = 0;
  std::size_t free_psram_after = 0;
  std::size_t free_internal_after = 0;
};

// Exports the complete Vector V2 authority as one SVG in the dedicated flash
// partition and presents DRAWING.SVG on the proven read-only USB transport.
// Every operation is one filled variable-width path built from the renderer's
// exact ribbon geometry; tile caches and presentation state are never read.
// Encoding batches output through one transient 4 KiB flash-page workspace.
class VectorV2Export {
 public:
  VectorV2Export();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::size_t file_size() const;
  [[nodiscard]] bool read_file(std::size_t offset, std::span<std::uint8_t> output) const;
  // Streams authority paths transactionally into flash. Never touches USB, so
  // automated runs keep their serial console.
  [[nodiscard]] VectorV2ExportStats encode(const vector_v2::OperationLog& log,
                                           VectorV2ExportProgress progress = nullptr,
                                           void* progress_context = nullptr);
  // Starts or refreshes the USB mass-storage presentation. Activating USB
  // repurposes the shared USB port and ends any serial console session until
  // the next reset, matching Raster V1 export semantics.
  void set_modified_time(FatDateTime time);
  [[nodiscard]] bool present_usb();
  // Disconnects an active USB disk before re-encoding so hosts re-read it.
  void prepare_reencode();

 private:
  SvgExportStore store_;
  UsbExport usb_;
};

}  // namespace tinydraw::esp32
