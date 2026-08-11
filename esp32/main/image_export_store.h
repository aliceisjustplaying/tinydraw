#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace tinydraw::esp32 {

struct ImageExportStats {
  std::size_t bytes = 0;
  std::int64_t elapsed_us = 0;
  std::size_t free_psram = 0;
  bool success = false;
};

// Stores one complete full-world PNG in a dedicated flash partition. Encoding
// uses fixed workspace plus one scanline and never duplicates the drawing world.
class ImageExportStore {
 public:
  ImageExportStore();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool has_image() const;
  [[nodiscard]] std::size_t image_size() const;
  [[nodiscard]] std::uint32_t generation() const;
  [[nodiscard]] bool read(std::size_t offset, std::span<std::uint8_t> output) const;
  [[nodiscard]] ImageExportStats encode(std::span<const std::uint16_t> world);

 private:
  const void* partition_ = nullptr;
  std::size_t image_size_ = 0;
  std::uint32_t generation_ = 0;
};

}  // namespace tinydraw::esp32
