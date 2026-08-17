#include "vector_v2_export.h"

#include <algorithm>
#include <cstddef>
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

constexpr std::size_t kSvgFlashWorkspaceBytes = 4'096U;
constexpr std::size_t kExportBandPixels =
    static_cast<std::size_t>(vector_v2::kWorldWidth) * vector_v2::kTileHeight;
constexpr std::size_t kExportRenderWorkspaceBytes =
    kExportBandPixels * sizeof(std::uint16_t) + vector_v2::kTileBytes +
    vector_v2::kTilePixels * 2U * sizeof(std::uint8_t) +
    vector_v2::kTilePixels * 3U * sizeof(std::uint16_t);
constexpr std::size_t kPngWindowCount =
    ((vector_v2::kWorldWidth + vector_v2::kTileWidth - 1U) / vector_v2::kTileWidth) *
    ((vector_v2::kWorldHeight + vector_v2::kTileHeight - 1U) / vector_v2::kTileHeight);

struct CombinedExportProgress {
  VectorV2ExportProgress progress = nullptr;
  void* context = nullptr;
  std::size_t png_completed = 0;
  std::size_t svg_operations = 0;
};

void service_png_render(void* raw_progress) {
  auto& progress = *static_cast<CombinedExportProgress*>(raw_progress);
  progress.png_completed = std::min(progress.png_completed + 1U, kPngWindowCount);
  if (progress.progress != nullptr) {
    progress.progress(progress.png_completed, kPngWindowCount + progress.svg_operations,
                      progress.context);
  }
  vTaskDelay(1);
}

void present_svg_progress(std::size_t completed, std::size_t total, void* raw_progress) {
  auto& progress = *static_cast<CombinedExportProgress*>(raw_progress);
  if (progress.progress != nullptr) {
    progress.progress(kPngWindowCount + completed, kPngWindowCount + total, progress.context);
  }
}

class SettledBandRowSource final : public PngRowSource {
 public:
  SettledBandRowSource(const vector_v2::OperationLog& log, std::span<std::uint16_t> band,
                       std::span<std::uint16_t> window, vector_v2::SettledTileWorkspace workspace,
                       CombinedExportProgress& progress)
      : renderer_(log, band, window, workspace, service_png_render, &progress) {}

  [[nodiscard]] bool ready() const { return renderer_.ready(); }

  bool row(int y, std::span<std::uint16_t> destination) override {
    if ((y & 0xFF) == 0) {
      std::printf("TINYDRAW_V2_EXPORT_PROGRESS format=png row=%d of=%d\n", y,
                  vector_v2::kWorldHeight);
      std::fflush(stdout);
    }
    return renderer_.render_row(y, destination);
  }

 private:
  vector_v2::SettledWorldBandRenderer renderer_;
};

}  // namespace

VectorV2Export::VectorV2Export()
    : usb_(store_, kDrawingSvgName, store_.png_file(), kDrawingPngName) {}

bool VectorV2Export::ready() const { return store_.ready(); }

std::size_t VectorV2Export::file_size() const { return store_.size(); }

std::size_t VectorV2Export::png_size() const { return store_.png_size(); }

bool VectorV2Export::read_file(std::size_t offset, std::span<std::uint8_t> output) const {
  return store_.read(offset, output);
}

bool VectorV2Export::read_png(std::size_t offset, std::span<std::uint8_t> output) const {
  return store_.read_png(offset, output);
}

VectorV2ExportStats VectorV2Export::encode(const vector_v2::OperationLog& log,
                                           VectorV2ExportProgress progress,
                                           void* progress_context) {
  const std::size_t png_workspace_bytes =
      png_encoder_workspace_bytes() + png_encoder_row_bytes(vector_v2::kWorldWidth) +
      static_cast<std::size_t>(vector_v2::kWorldWidth) * sizeof(std::uint16_t) +
      kSvgFlashWorkspaceBytes;
  VectorV2ExportStats stats{
      .workspace_bytes = kSvgFlashWorkspaceBytes,
      .png_workspace_bytes = png_workspace_bytes,
      .render_workspace_bytes = kExportRenderWorkspaceBytes,
      .peak_workspace_bytes = kExportRenderWorkspaceBytes + png_workspace_bytes,
      .operation_count = log.ready() ? log.operation_count() : 0U,
  };
  if (!ready() || !log.ready()) {
    return stats;
  }

  const std::int64_t started_us = esp_timer_get_time();
  auto* memory = static_cast<std::uint8_t*>(
      heap_caps_malloc(kExportRenderWorkspaceBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (memory != nullptr) {
    std::size_t offset = 0U;
    auto band = std::span(reinterpret_cast<std::uint16_t*>(memory + offset), kExportBandPixels);
    offset += band.size_bytes();
    auto window =
        std::span(reinterpret_cast<std::uint16_t*>(memory + offset), vector_v2::kTilePixels);
    offset += window.size_bytes();
    auto operation_alpha = std::span(memory + offset, vector_v2::kTilePixels);
    offset += operation_alpha.size_bytes();
    auto accumulated_alpha = std::span(memory + offset, vector_v2::kTilePixels);
    offset += accumulated_alpha.size_bytes();
    auto red = std::span(reinterpret_cast<std::uint16_t*>(memory + offset), vector_v2::kTilePixels);
    offset += red.size_bytes();
    auto green =
        std::span(reinterpret_cast<std::uint16_t*>(memory + offset), vector_v2::kTilePixels);
    offset += green.size_bytes();
    auto blue =
        std::span(reinterpret_cast<std::uint16_t*>(memory + offset), vector_v2::kTilePixels);

    CombinedExportProgress combined_progress{
        .progress = progress,
        .context = progress_context,
        .svg_operations = stats.operation_count,
    };
    SettledBandRowSource source(log, band, window,
                                {.operation_alpha = operation_alpha,
                                 .accumulated_alpha = accumulated_alpha,
                                 .red = red,
                                 .green = green,
                                 .blue = blue},
                                combined_progress);
    if (source.ready()) {
      const SvgExportStoreStats encoded =
          store_.encode(log, source, present_svg_progress, &combined_progress);
      stats.encoded = encoded.success;
      stats.bytes = encoded.bytes;
      stats.png_bytes = encoded.png_bytes;
      stats.sink_calls = encoded.sink_calls;
      stats.flash_pages = encoded.flash_pages;
      stats.content_crc32 = encoded.content_crc32;
    }
  }
  heap_caps_free(memory);
  stats.elapsed_us = esp_timer_get_time() - started_us;
  stats.free_psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  stats.free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return stats;
}

void VectorV2Export::set_modified_time(FatDateTime time) { usb_.set_modified_time(time); }

bool VectorV2Export::present_usb() {
  return usb_.finish_export(store_.has_file() && store_.has_png());
}

bool VectorV2Export::usb_host_ejected() const { return usb_.host_ejected(); }

bool VectorV2Export::stop_usb() { return usb_.stop(); }

bool VectorV2Export::prepare_reencode() { return usb_.prepare_export(); }

}  // namespace tinydraw::esp32
