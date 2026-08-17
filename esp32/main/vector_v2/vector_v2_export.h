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
  std::size_t png_bytes = 0;
  std::int64_t elapsed_us = 0;
  std::size_t workspace_bytes = 0;
  std::size_t png_workspace_bytes = 0;
  std::size_t render_workspace_bytes = 0;
  std::size_t peak_workspace_bytes = 0;
  std::size_t operation_count = 0;
  std::size_t sink_calls = 0;
  std::size_t flash_pages = 0;
  std::uint32_t content_crc32 = 0;
  std::size_t free_psram_after = 0;
  std::size_t free_internal_after = 0;
};

// Exports one authority snapshot as an editable path-based DRAWING.SVG and a
// settled-AA DRAWING.PNG in the dedicated flash partition, then presents both
// through the proven read-only USB transport. SVG geometry and bytes retain
// the existing exact ribbon path behavior. PNG rows stream from the production
// settled renderer through bounded band/tile/encoder workspaces; neither file
// depends on tile-cache residency or presentation state.
class VectorV2Export {
 public:
  VectorV2Export();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::size_t file_size() const;
  [[nodiscard]] std::size_t png_size() const;
  [[nodiscard]] bool read_file(std::size_t offset, std::span<std::uint8_t> output) const;
  [[nodiscard]] bool read_png(std::size_t offset, std::span<std::uint8_t> output) const;
  // Streams the same authority snapshot into both formats and commits their
  // shared manifest last. Never touches USB, so automated runs keep serial.
  [[nodiscard]] VectorV2ExportStats encode(const vector_v2::OperationLog& log,
                                           VectorV2ExportProgress progress = nullptr,
                                           void* progress_context = nullptr);
  // Starts the USB mass-storage presentation. Activating USB repurposes the
  // shared port until stop_usb() returns the device to the drawing app.
  void set_modified_time(FatDateTime time);
  [[nodiscard]] bool present_usb();
  [[nodiscard]] bool usb_host_ejected() const;
  [[nodiscard]] bool stop_usb();
  // Fully stops an active USB session before re-encoding. A fresh session is
  // started only after the new export has committed.
  [[nodiscard]] bool prepare_reencode();

 private:
  SvgExportStore store_;
  UsbExport usb_;
};

}  // namespace tinydraw::esp32
