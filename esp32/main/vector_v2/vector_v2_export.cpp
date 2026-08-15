#include "vector_v2_export.h"

#include <cstdint>
#include <cstdio>
#include <span>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinydraw/export/png_encoder.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/world_export.h"

namespace tinydraw::esp32 {
namespace {

// One band of full-resolution world rows rendered per replay pass. 128 rows
// keep the transient buffer at 368 KiB while bounding replay passes to
// fourteen for the complete 1792-row world.
constexpr int kExportBandRows = 128;
constexpr std::size_t kExportBandPixels =
    static_cast<std::size_t>(vector_v2::kWorldWidth) * static_cast<std::size_t>(kExportBandRows);

class BandRowSource final : public PngRowSource {
 public:
  BandRowSource(const vector_v2::OperationLog& log, std::span<std::uint16_t> band,
                VectorV2ExportProgress progress, void* progress_context)
      : renderer_(log, band), progress_(progress), progress_context_(progress_context) {}

  [[nodiscard]] bool ready() const { return renderer_.ready(); }

  bool row(int y, std::span<std::uint16_t> destination) override {
    if (y % kExportBandRows == 0 && progress_ != nullptr) {
      progress_(y, vector_v2::kWorldHeight, progress_context_);
    }
    if ((y & 0xFFU) == 0U) {
      // Progress marker roughly every 256 rows so long encodes remain
      // observable over serial and hangs are attributable to a row.
      std::printf("TINYDRAW_V2_EXPORT_PROGRESS row=%d of=%d\n", y, vector_v2::kWorldHeight);
      std::fflush(stdout);
    }
    if ((y & 0x1FU) == 0U) {
      // The encode is a long CPU-bound loop on the main task; yield every 32
      // rows so the CPU0 idle task feeds the task watchdog.
      vTaskDelay(1);
    }
    return renderer_.render_row(y, destination);
  }

 private:
  vector_v2::WorldBandRenderer renderer_;
  VectorV2ExportProgress progress_ = nullptr;
  void* progress_context_ = nullptr;
};

}  // namespace

VectorV2Export::VectorV2Export()
    : store_(vector_v2::kWorldWidth, vector_v2::kWorldHeight), usb_(store_) {}

bool VectorV2Export::ready() const { return store_.ready(); }

std::size_t VectorV2Export::image_size() const { return store_.image_size(); }

bool VectorV2Export::read_image(std::size_t offset, std::span<std::uint8_t> output) const {
  return store_.read(offset, output);
}

VectorV2ExportStats VectorV2Export::encode(const vector_v2::OperationLog& log,
                                           VectorV2ExportProgress progress,
                                           void* progress_context) {
  VectorV2ExportStats stats{
      .workspace_bytes = png_encoder_workspace_bytes(),
      .band_bytes = kExportBandPixels * sizeof(std::uint16_t),
  };
  if (!ready() || !log.ready()) {
    return stats;
  }
  const std::int64_t started_us = esp_timer_get_time();
  auto* band = static_cast<std::uint16_t*>(heap_caps_malloc(
      kExportBandPixels * sizeof(std::uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (band == nullptr) {
    return stats;
  }
  {
    BandRowSource source(log, std::span(band, kExportBandPixels), progress, progress_context);
    if (source.ready()) {
      const ImageExportStats encoded = store_.encode_rows(source);
      stats.encoded = encoded.success;
      stats.bytes = encoded.bytes;
      if (stats.encoded && progress != nullptr) {
        progress(vector_v2::kWorldHeight, vector_v2::kWorldHeight, progress_context);
      }
    }
  }
  heap_caps_free(band);
  stats.elapsed_us = esp_timer_get_time() - started_us;
  stats.free_psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  stats.free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return stats;
}

bool VectorV2Export::present_usb() { return usb_.finish_export(store_.has_image()); }

void VectorV2Export::prepare_reencode() { usb_.prepare_export(); }

}  // namespace tinydraw::esp32
