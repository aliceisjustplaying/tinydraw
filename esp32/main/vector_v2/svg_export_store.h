#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/export/fat16_disk.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/svg_export.h"

namespace tinydraw::esp32 {

struct SvgExportStoreStats {
  bool success = false;
  std::size_t bytes = 0;
  std::size_t sink_calls = 0;
  std::size_t flash_pages = 0;
  std::uint32_t content_crc32 = 0;
};

// Transactionally streams one exact authority SVG into the dedicated export
// partition. The metadata page is committed last, so an interrupted encode is
// never exposed as a complete file. One 4 KiB workspace batches geometry into
// flash-page writes; storage cost scales with the SVG, not the drawing world.
class SvgExportStore final : public ReadOnlyFile {
 public:
  SvgExportStore();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool has_file() const;
  [[nodiscard]] std::size_t size() const override;
  [[nodiscard]] bool read(std::size_t offset, std::span<std::uint8_t> output) const override;
  [[nodiscard]] SvgExportStoreStats encode(const vector_v2::OperationLog& log,
                                           vector_v2::SvgExportProgress progress = nullptr,
                                           void* progress_context = nullptr);

 private:
  const void* partition_ = nullptr;
  std::size_t file_size_ = 0;
  std::uint32_t generation_ = 0;
  std::uint32_t content_crc32_ = 0;
};

}  // namespace tinydraw::esp32
