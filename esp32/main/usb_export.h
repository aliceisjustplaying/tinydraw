#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/export/fat16_disk.h"

namespace tinydraw::esp32 {

// Exposes one read-only file as a synthesized FAT16 USB drive.
class UsbExport {
 public:
  explicit UsbExport(const ReadOnlyFile& file, Fat83Name name = kDrawingPngName);

  [[nodiscard]] bool active() const { return active_; }
  void set_modified_time(FatDateTime time) { disk_.set_modified_time(time); }
  void prepare_export();
  [[nodiscard]] bool finish_export(bool image_available);
  [[nodiscard]] bool read(std::uint32_t lba, std::uint32_t offset,
                          std::span<std::uint8_t> output) const;

 private:
  [[nodiscard]] bool start();

  Fat16ExportDisk disk_;
  bool active_ = false;
};

}  // namespace tinydraw::esp32
