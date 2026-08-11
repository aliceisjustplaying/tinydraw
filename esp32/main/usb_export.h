#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "image_export_store.h"
#include "tinydraw/export/fat16_disk.h"

namespace tinydraw::esp32 {

// Exposes ImageExportStore as a synthesized, read-only FAT16 USB drive.
class UsbExport {
 public:
  explicit UsbExport(const ImageExportStore& store);

  [[nodiscard]] bool active() const { return active_; }
  void prepare_export();
  [[nodiscard]] bool finish_export(bool image_available);
  [[nodiscard]] bool read(std::uint32_t lba, std::uint32_t offset,
                          std::span<std::uint8_t> output) const;

 private:
  class ExportFile final : public ReadOnlyFile {
   public:
    explicit ExportFile(const ImageExportStore& store) : store_(store) {}
    [[nodiscard]] std::size_t size() const override { return store_.image_size(); }
    [[nodiscard]] bool read(std::size_t offset, std::span<std::uint8_t> output) const override {
      return store_.read(offset, output);
    }

   private:
    const ImageExportStore& store_;
  };

  [[nodiscard]] bool start();

  ExportFile file_;
  Fat16ExportDisk disk_;
  bool active_ = false;
};

}  // namespace tinydraw::esp32
