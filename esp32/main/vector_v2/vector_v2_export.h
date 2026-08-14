#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "image_export_store.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "usb_export.h"

namespace tinydraw::esp32 {

struct VectorV2ExportStats {
  bool encoded = false;
  std::size_t bytes = 0;
  std::int64_t elapsed_us = 0;
  std::size_t workspace_bytes = 0;
  std::size_t band_bytes = 0;
  std::size_t free_psram_after = 0;
  std::size_t free_internal_after = 0;
};

// Exports the complete Vector V2 world as one full-resolution PNG in the
// dedicated flash partition and presents it as a read-only USB drive. The
// store, FAT16 disk, and USB mass-storage transport are the proven Raster V1
// mechanisms reused through their narrow interfaces; only the pixel source
// differs: rows stream from exact vector-authority replay, independent of
// cache residency. Encoding allocates its workspace transiently and frees it
// before returning.
class VectorV2Export {
 public:
  VectorV2Export();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] std::size_t image_size() const;
  [[nodiscard]] bool read_image(std::size_t offset, std::span<std::uint8_t> output) const;
  // Renders authority in horizontal bands and writes the PNG. Never touches
  // USB, so automated runs keep their serial console.
  [[nodiscard]] VectorV2ExportStats encode(const vector_v2::OperationLog& log);
  // Starts or refreshes the USB mass-storage presentation. Activating USB
  // repurposes the shared USB port and ends any serial console session until
  // the next reset, matching Raster V1 export semantics.
  [[nodiscard]] bool present_usb();
  // Disconnects an active USB disk before re-encoding so hosts re-read it.
  void prepare_reencode();

 private:
  ImageExportStore store_;
  UsbExport usb_;
};

}  // namespace tinydraw::esp32
