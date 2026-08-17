#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/export/fat16_disk.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/svg_export.h"

namespace tinydraw {
class PngRowSource;
}  // namespace tinydraw

namespace tinydraw::esp32 {

struct SvgExportStoreStats {
  bool success = false;
  std::size_t bytes = 0;
  std::size_t png_bytes = 0;
  std::size_t sink_calls = 0;
  std::size_t flash_pages = 0;
  std::uint32_t content_crc32 = 0;
};

// Transactionally streams one settled PNG and one exact authority SVG into
// the dedicated export partition. A shared metadata page is committed last,
// so readers see a complete same-snapshot pair or no pair. PNG and SVG bytes
// occupy dynamically packed, page-aligned regions; no document-sized output
// buffer is retained.
class SvgExportStore final : public ReadOnlyFile {
 public:
  class PngFile final : public ReadOnlyFile {
   public:
    [[nodiscard]] std::size_t size() const override;
    [[nodiscard]] bool read(std::size_t offset, std::span<std::uint8_t> output) const override;

   private:
    friend class SvgExportStore;
    explicit PngFile(const SvgExportStore& owner) : owner_(owner) {}
    const SvgExportStore& owner_;
  };

  SvgExportStore();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool has_file() const;
  [[nodiscard]] bool has_png() const;
  [[nodiscard]] std::size_t size() const override;
  [[nodiscard]] std::size_t png_size() const;
  [[nodiscard]] const ReadOnlyFile& png_file() const { return png_file_; }
  [[nodiscard]] bool read(std::size_t offset, std::span<std::uint8_t> output) const override;
  [[nodiscard]] bool read_png(std::size_t offset, std::span<std::uint8_t> output) const;
  [[nodiscard]] SvgExportStoreStats encode(const vector_v2::OperationLog& log,
                                           PngRowSource& png_source,
                                           vector_v2::SvgExportProgress progress = nullptr,
                                           void* progress_context = nullptr);

 private:
  [[nodiscard]] bool read_region(std::size_t region_offset, std::size_t region_size,
                                 std::size_t offset, std::span<std::uint8_t> output) const;

  const void* partition_ = nullptr;
  std::size_t file_offset_ = 0;
  std::size_t file_size_ = 0;
  std::size_t png_offset_ = 0;
  std::size_t png_size_ = 0;
  std::uint32_t generation_ = 0;
  std::uint32_t content_crc32_ = 0;
  PngFile png_file_;
};

}  // namespace tinydraw::esp32
