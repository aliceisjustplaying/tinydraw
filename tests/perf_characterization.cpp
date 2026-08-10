#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "tinydraw/geometry.h"
#include "tinydraw/graphics/stroke_raster.h"
#include "tinydraw/graphics/tile_undo_history.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"

namespace {

constexpr std::size_t kSampleCount = 1'000U;
constexpr std::size_t kPixelCount =
    static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kInk = 0x001FU;

class NullDisplay final : public tinydraw::DisplayBackend {
 public:
  void push_rect(int, int, int, int, const std::uint16_t*, int = 0) override { ++pushes; }

  std::uint32_t pushes = 0U;
};

struct Totals {
  std::uint64_t tiles = 0U;
  std::uint64_t visits = 0U;
  std::uint64_t display_bytes = 0U;
  std::uint64_t committed_read = 0U;
  std::uint64_t committed_written = 0U;
  std::uint64_t coverage_read = 0U;
  std::uint64_t coverage_written = 0U;
  std::uint32_t maximum_update_tiles = 0U;
  std::uint32_t maximum_update_visits = 0U;
  std::uint32_t maximum_update_psram_read = 0U;
  std::uint32_t maximum_update_psram_written = 0U;
  tinydraw::StrokeRasterStats finish;
};

void accumulate(Totals& total, const tinydraw::StrokeRasterStats& frame, bool final = false) {
  total.tiles += frame.tiles_updated;
  total.visits += frame.primitive_tile_visits;
  total.display_bytes += frame.display_bytes;
  total.committed_read += frame.committed_bytes_read;
  total.committed_written += frame.committed_bytes_written;
  total.coverage_read += frame.coverage_bytes_read;
  total.coverage_written += frame.coverage_bytes_written;
  if (final) {
    total.finish = frame;
    return;
  }
  total.maximum_update_tiles = std::max(total.maximum_update_tiles, frame.tiles_updated);
  total.maximum_update_visits = std::max(total.maximum_update_visits, frame.primitive_tile_visits);
  total.maximum_update_psram_read = std::max(
      total.maximum_update_psram_read, frame.committed_bytes_read + frame.coverage_bytes_read);
  total.maximum_update_psram_written =
      std::max(total.maximum_update_psram_written,
               frame.committed_bytes_written + frame.coverage_bytes_written);
}

tinydraw::TouchPoint sample(std::size_t index) {
  const float step = static_cast<float>(index);
  const float phase = std::fmod(step * 2.5F, 640.0F);
  const float x = phase <= 320.0F ? 24.0F + phase : 344.0F - (phase - 320.0F);
  return {
      .x = x,
      .y = 220.0F + 140.0F * std::sin(step * 0.05F),
      .timestamp_us = static_cast<std::uint32_t>(index * 8'000U),
  };
}

}  // namespace

int main() {
  std::vector<std::uint16_t> committed(kPixelCount, kBackground);
  std::vector<std::uint8_t> coverage(kPixelCount, 0U);
  NullDisplay display;
  tinydraw::StrokeRaster raster(committed, coverage, display);
  std::vector<std::uint16_t> undo_storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(undo_storage);
  tinydraw::InkConfig config;
  config.size = 20.0F;
  tinydraw::InkStream ink(config);
  tinydraw::CurvedRibbonStream ribbon;
  Totals total;

  accumulate(total, raster.update(ribbon.append(ink.begin(sample(0U))), kInk));
  for (std::size_t index = 1U; index + 1U < kSampleCount; ++index) {
    accumulate(total, raster.update(ribbon.append(ink.update(sample(index))), kInk));
  }
  accumulate(total,
             raster.finish(ribbon.finish(ink.finish(sample(kSampleCount - 1U))), kInk, &history),
             true);
  const auto raster_pushes = display.pushes;
  const auto undo = history.undo(committed, {}, &display);

  const bool accounting_valid = total.display_bytes == total.committed_read &&
                                raster_pushes == total.tiles &&
                                undo.history_bytes_read == undo.canvas_bytes_written &&
                                undo.history_bytes_read == undo.display_bytes &&
                                display.pushes == raster_pushes + undo.tiles_restored &&
                                std::all_of(committed.begin(), committed.end(),
                                            [](auto pixel) { return pixel == kBackground; });
  const auto finish_psram_read =
      total.finish.committed_bytes_read + total.finish.coverage_bytes_read;
  const auto finish_psram_written = total.finish.committed_bytes_written +
                                    total.finish.coverage_bytes_written +
                                    total.finish.history_bytes_written;
  const bool bounded =
      total.maximum_update_tiles <= 20U && total.maximum_update_visits <= 100U &&
      total.maximum_update_psram_read <= 48U * 1024U &&
      total.maximum_update_psram_written <= 16U * 1024U &&
      total.coverage_read <= 9U * 1024U * 1024U &&
      total.finish.tiles_updated <=
          static_cast<std::uint32_t>(tinydraw::TileUndoHistory::kTileCount) &&
      finish_psram_read <= 512U * 1024U && finish_psram_written <= 1024U * 1024U &&
      undo.tiles_restored <= static_cast<std::uint32_t>(tinydraw::TileUndoHistory::kTileCount) &&
      undo.history_bytes_read <= 512U * 1024U;

  std::printf(
      "TINYDRAW_PERF samples=%u tiles=%llu visits=%llu display=%llu "
      "committed_read=%llu committed_write=%llu coverage_read=%llu coverage_write=%llu "
      "max_update_tiles=%u max_update_visits=%u max_update_psram_read=%u "
      "max_update_psram_write=%u finish_tiles=%u finish_psram_read=%u "
      "finish_psram_write=%u history_capture_write=%u undo_tiles=%u undo_history_read=%u "
      "undo_canvas_write=%u undo_display=%u\n",
      static_cast<unsigned>(kSampleCount), static_cast<unsigned long long>(total.tiles),
      static_cast<unsigned long long>(total.visits),
      static_cast<unsigned long long>(total.display_bytes),
      static_cast<unsigned long long>(total.committed_read),
      static_cast<unsigned long long>(total.committed_written),
      static_cast<unsigned long long>(total.coverage_read),
      static_cast<unsigned long long>(total.coverage_written), total.maximum_update_tiles,
      total.maximum_update_visits, total.maximum_update_psram_read,
      total.maximum_update_psram_written, total.finish.tiles_updated, finish_psram_read,
      finish_psram_written, total.finish.history_bytes_written, undo.tiles_restored,
      undo.history_bytes_read, undo.canvas_bytes_written, undo.display_bytes);
  return accounting_valid && bounded ? 0 : 1;
}
