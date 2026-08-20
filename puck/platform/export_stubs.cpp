// Export, for the wasm build: absent, and reported as absent.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// The product's export writes DRAWING.SVG and DRAWING.PNG into a dedicated
// 10.125 MiB flash partition and then presents both as a read-only USB
// mass-storage device (TinyUSB, a synthesized FAT16 image). A browser tab has
// neither a flash partition nor a USB gadget port, so the two ways the device
// hands those bytes to a human do not exist here.
//
// This implements the SAME product headers (vector_v2_export.h,
// svg_export_store.h, usb_export.h are used unmodified) with bodies that say
// so: SvgExportStore::ready() is false because there is no partition, and
// VectorV2Export::ready() is false because of that. The app reads that at
// startup into chrome.can_export, and the chrome draws the control
// unavailable - the same path a board whose export partition is missing
// already takes. Nothing here reports a success that produced no file.
//
// What is NOT the blocker, and is worth being precise about because it decides
// how much work a real browser export would be: the encoders are portable.
// vector_v2/src/svg_export.cpp streams renderer-derived ribbon geometry with
// no platform dependency at all, and the settled PNG path is core/ plus a
// vendored encoder. A browser export would keep both and replace only the
// destination (a Blob and a download instead of a partition and a USB
// endpoint). That is the shape of the remaining work, not a rewrite.

#include "vector_v2_export.h"

#include "svg_export_store.h"
#include "tinydraw/export/fat16_disk.h"
#include "usb_export.h"

namespace tinydraw::esp32 {

// ---- SvgExportStore --------------------------------------------------------

std::size_t SvgExportStore::PngFile::size() const { return owner_.png_size(); }

bool SvgExportStore::PngFile::read(std::size_t offset, std::span<std::uint8_t> output) const {
  return owner_.read_png(offset, output);
}

SvgExportStore::SvgExportStore() : png_file_(*this) {}

bool SvgExportStore::ready() const { return false; }  // no export partition exists
bool SvgExportStore::has_file() const { return false; }
bool SvgExportStore::has_png() const { return false; }
std::size_t SvgExportStore::size() const { return 0; }
std::size_t SvgExportStore::png_size() const { return 0; }
bool SvgExportStore::read(std::size_t, std::span<std::uint8_t>) const { return false; }
bool SvgExportStore::read_png(std::size_t, std::span<std::uint8_t>) const { return false; }
bool SvgExportStore::read_region(std::size_t, std::size_t, std::size_t,
                                 std::span<std::uint8_t>) const {
  return false;
}

SvgExportStoreStats SvgExportStore::encode(const vector_v2::OperationLog&, PngRowSource&,
                                           vector_v2::SvgExportProgress, void*) {
  return {};  // success == false
}

// ---- UsbExport -------------------------------------------------------------

UsbExport::UsbExport(const ReadOnlyFile& file, Fat83Name name) : disk_(file, name) {}

UsbExport::UsbExport(const ReadOnlyFile& first_file, Fat83Name first_name,
                     const ReadOnlyFile& second_file, Fat83Name second_name)
    : disk_(first_file, first_name, second_file, second_name) {}

bool UsbExport::prepare_export() { return false; }
bool UsbExport::finish_export(bool) { return false; }
bool UsbExport::stop() { return false; }
bool UsbExport::start() { return false; }

bool UsbExport::read(std::uint32_t lba, std::uint32_t offset,
                     std::span<std::uint8_t> output) const {
  // The FAT16 image itself is portable core/ code and would answer correctly;
  // there is simply no host asking, because there is no USB endpoint.
  return disk_.read(lba, offset, output);
}

// ---- VectorV2Export --------------------------------------------------------

VectorV2Export::VectorV2Export()
    : store_(), usb_(store_.png_file(), kDrawingPngName, store_, kDrawingSvgName) {}

bool VectorV2Export::ready() const { return store_.ready(); }

VectorV2ExportStats VectorV2Export::encode(const vector_v2::OperationLog&, VectorV2ExportProgress,
                                           void*) {
  return {};  // encoded == false
}

void VectorV2Export::set_modified_time(FatDateTime time) { usb_.set_modified_time(time); }

bool VectorV2Export::present_usb() { return false; }
bool VectorV2Export::usb_host_ejected() const { return usb_.host_ejected(); }
bool VectorV2Export::stop_usb() { return false; }
bool VectorV2Export::prepare_reencode() { return false; }

}  // namespace tinydraw::esp32
