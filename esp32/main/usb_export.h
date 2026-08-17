#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/export/fat16_disk.h"
#include "tinydraw/export/usb_export_session.h"

namespace tinydraw::esp32 {

// Exposes one or two read-only files as a synthesized FAT16 USB drive.
class UsbExport {
 public:
  explicit UsbExport(const ReadOnlyFile& file, Fat83Name name = kDrawingPngName);
  UsbExport(const ReadOnlyFile& first_file, Fat83Name first_name, const ReadOnlyFile& second_file,
            Fat83Name second_name);

  [[nodiscard]] bool active() const { return session_.active(); }
  [[nodiscard]] bool media_present() const { return session_.media_present(); }
  [[nodiscard]] bool host_ejected() const { return session_.host_ejected(); }
  void set_modified_time(FatDateTime time) { disk_.set_modified_time(time); }
  void prepare_export();
  [[nodiscard]] bool finish_export(bool image_available);
  [[nodiscard]] bool stop();
  [[nodiscard]] bool read(std::uint32_t lba, std::uint32_t offset,
                          std::span<std::uint8_t> output) const;

  // TinyUSB callback boundary. Host eject and USB unmount remove the medium
  // until stop() ends this session; later host probes cannot re-present it.
  void note_host_ejected() { session_.note_host_ejected(); }

 private:
  [[nodiscard]] bool start();

  Fat16ExportDisk disk_;
  UsbExportSession session_;
};

}  // namespace tinydraw::esp32
