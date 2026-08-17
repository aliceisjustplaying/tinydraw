#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/export/fat16_disk.h"

namespace tinydraw {
class PngRowSource;
}  // namespace tinydraw

namespace tinydraw::esp32 {

struct ImageExportStats {
  std::size_t bytes = 0;
  std::int64_t elapsed_us = 0;
  std::size_t free_psram = 0;
  bool success = false;
};

// Stores one complete full-world PNG in a dedicated flash partition. Encoding
// uses fixed workspace plus one scanline and never duplicates the drawing
// world. Dimensions default to the Raster V1 world; callers may provide a
// different fixed raster size.
class ImageExportStore : public ReadOnlyFile {
 public:
  ImageExportStore();
  ImageExportStore(int width, int height);

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool has_image() const;
  [[nodiscard]] std::size_t image_size() const;
  [[nodiscard]] std::size_t size() const override { return image_size(); }
  [[nodiscard]] std::uint32_t generation() const;
  [[nodiscard]] bool read(std::size_t offset, std::span<std::uint8_t> output) const override;
  [[nodiscard]] ImageExportStats encode(std::span<const std::uint16_t> world);
  // Row-streamed sibling for worlds without one flat pixel buffer.
  [[nodiscard]] ImageExportStats encode_rows(PngRowSource& source);

 private:
  [[nodiscard]] ImageExportStats encode_with(std::span<const std::uint16_t> world,
                                             PngRowSource* source);

  const void* partition_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  std::size_t image_size_ = 0;
  std::uint32_t generation_ = 0;
};

}  // namespace tinydraw::esp32
