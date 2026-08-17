#include "vector_v2_export.h"

#include <cstdint>
#include <span>

#include "esp_heap_caps.h"
#include "esp_timer.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::size_t kSvgFlashWorkspaceBytes = 4'096U;

}  // namespace

VectorV2Export::VectorV2Export() : usb_(store_, kDrawingSvgName) {}

bool VectorV2Export::ready() const { return store_.ready(); }

std::size_t VectorV2Export::file_size() const { return store_.size(); }

bool VectorV2Export::read_file(std::size_t offset, std::span<std::uint8_t> output) const {
  return store_.read(offset, output);
}

VectorV2ExportStats VectorV2Export::encode(const vector_v2::OperationLog& log,
                                           VectorV2ExportProgress progress,
                                           void* progress_context) {
  VectorV2ExportStats stats{
      .workspace_bytes = kSvgFlashWorkspaceBytes,
      .operation_count = log.ready() ? log.operation_count() : 0U,
  };
  if (!ready() || !log.ready()) {
    return stats;
  }

  const std::int64_t started_us = esp_timer_get_time();
  const SvgExportStoreStats encoded = store_.encode(log, progress, progress_context);
  stats.encoded = encoded.success;
  stats.bytes = encoded.bytes;
  stats.elapsed_us = esp_timer_get_time() - started_us;
  stats.sink_calls = encoded.sink_calls;
  stats.flash_pages = encoded.flash_pages;
  stats.content_crc32 = encoded.content_crc32;
  stats.free_psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  stats.free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return stats;
}

void VectorV2Export::set_modified_time(FatDateTime time) { usb_.set_modified_time(time); }

bool VectorV2Export::present_usb() { return usb_.finish_export(store_.has_file()); }

void VectorV2Export::prepare_reencode() { usb_.prepare_export(); }

}  // namespace tinydraw::esp32
