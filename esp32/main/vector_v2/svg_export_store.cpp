#include "svg_export_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinydraw/export/png_encoder.h"

namespace tinydraw::esp32 {
namespace {

constexpr char kPartitionLabel[] = "export";
constexpr auto kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x41);
constexpr std::uint32_t kMetadataMagic = 0x3258'5054U;  // TPX2
constexpr std::uint32_t kMetadataVersion = 2U;
constexpr std::size_t kPageBytes = 4'096U;
constexpr std::size_t kPngOffset = kPageBytes;
constexpr std::size_t kYieldEveryOperations = 8U;
constexpr std::uint32_t kPngWidth = vector_v2::kWorldWidth;
constexpr std::uint32_t kPngHeight = vector_v2::kWorldHeight;

constexpr std::array<std::uint32_t, 256> make_crc32_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t value = 0; value < table.size(); ++value) {
    std::uint32_t entry = value;
    for (int bit = 0; bit < 8; ++bit) {
      entry = (entry >> 1U) ^ (0xEDB88320U & (0U - (entry & 1U)));
    }
    table[value] = entry;
  }
  return table;
}

constexpr auto kCrc32Table = make_crc32_table();

struct ExportMetadata {
  std::uint32_t magic = kMetadataMagic;
  std::uint32_t version = kMetadataVersion;
  std::uint32_t png_width = kPngWidth;
  std::uint32_t png_height = kPngHeight;
  std::uint32_t png_offset = 0;
  std::uint32_t png_size = 0;
  std::uint32_t svg_offset = 0;
  std::uint32_t svg_size = 0;
  std::uint32_t generation = 0;
  std::uint32_t content_crc32 = 0;
  std::uint64_t authority_epoch = 0;
  std::uint32_t authority_revision = 0;
  std::uint32_t operation_count = 0;
  std::uint32_t checksum = 0;
};

std::uint32_t metadata_checksum(const ExportMetadata& metadata) {
  return metadata.magic ^ metadata.version ^ metadata.png_width ^ metadata.png_height ^
         metadata.png_offset ^ metadata.png_size ^ metadata.svg_offset ^ metadata.svg_size ^
         metadata.generation ^ metadata.content_crc32 ^
         static_cast<std::uint32_t>(metadata.authority_epoch) ^
         static_cast<std::uint32_t>(metadata.authority_epoch >> 32U) ^ metadata.authority_revision ^
         metadata.operation_count ^ 0xA17E'5EEDU;
}

bool valid_metadata(const ExportMetadata& metadata, std::size_t partition_size) {
  return metadata.magic == kMetadataMagic && metadata.version == kMetadataVersion &&
         metadata.png_width == kPngWidth && metadata.png_height == kPngHeight &&
         metadata.png_offset == kPngOffset && metadata.png_size > 0U && metadata.svg_size > 0U &&
         metadata.svg_offset % kPageBytes == 0U && metadata.png_size <= partition_size &&
         metadata.png_offset <= partition_size - metadata.png_size &&
         metadata.png_offset + metadata.png_size <= metadata.svg_offset &&
         metadata.svg_size <= partition_size &&
         metadata.svg_offset <= partition_size - metadata.svg_size &&
         metadata.checksum == metadata_checksum(metadata);
}

const esp_partition_t* as_partition(const void* partition) {
  return static_cast<const esp_partition_t*>(partition);
}

class PartitionPngOutput final : public PngOutput {
 public:
  PartitionPngOutput(const esp_partition_t* partition, std::size_t physical_start,
                     std::span<std::uint8_t> first_page)
      : partition_(partition),
        physical_start_(physical_start),
        first_page_(first_page),
        erased_through_(physical_start + kPageBytes) {}

  bool write(std::size_t offset, std::span<const std::uint8_t> bytes) override {
    if (!valid_range(offset, bytes.size())) {
      return false;
    }
    if (offset < first_page_.size()) {
      const std::size_t count = std::min(bytes.size(), first_page_.size() - offset);
      std::copy_n(bytes.begin(), count, first_page_.begin() + static_cast<std::ptrdiff_t>(offset));
      offset += count;
      bytes = bytes.subspan(count);
    }
    if (bytes.empty()) {
      return true;
    }
    const std::size_t physical_offset = physical_start_ + offset;
    if (!erase_through(physical_offset + bytes.size())) {
      return false;
    }
    return esp_partition_write(partition_, physical_offset, bytes.data(), bytes.size()) == ESP_OK;
  }

  bool read(std::size_t offset, std::span<std::uint8_t> bytes) override {
    if (!valid_range(offset, bytes.size())) {
      return false;
    }
    if (offset < first_page_.size()) {
      const std::size_t count = std::min(bytes.size(), first_page_.size() - offset);
      std::copy_n(first_page_.begin() + static_cast<std::ptrdiff_t>(offset), count, bytes.begin());
      offset += count;
      bytes = bytes.subspan(count);
    }
    return bytes.empty() || esp_partition_read(partition_, physical_start_ + offset, bytes.data(),
                                               bytes.size()) == ESP_OK;
  }

 private:
  [[nodiscard]] bool valid_range(std::size_t offset, std::size_t size) const {
    const std::size_t capacity = partition_->size - physical_start_;
    return offset <= capacity && size <= capacity - offset;
  }

  bool erase_through(std::size_t physical_end) {
    const std::size_t aligned_end = (physical_end + kPageBytes - 1U) & ~(kPageBytes - 1U);
    if (aligned_end <= erased_through_) {
      return true;
    }
    const bool erased = esp_partition_erase_range(partition_, erased_through_,
                                                  aligned_end - erased_through_) == ESP_OK;
    if (erased) {
      erased_through_ = aligned_end;
    }
    return erased;
  }

  const esp_partition_t* partition_;
  std::size_t physical_start_ = 0;
  std::span<std::uint8_t> first_page_;
  std::size_t erased_through_ = 0;
};

struct ProgressAdapter {
  vector_v2::SvgExportProgress progress = nullptr;
  void* context = nullptr;
};

void service_export_progress(std::size_t completed, std::size_t total, void* raw_adapter) {
  auto& adapter = *static_cast<ProgressAdapter*>(raw_adapter);
  if (adapter.progress != nullptr) {
    adapter.progress(completed, total, adapter.context);
  }
  // The gate and product can enter export after a long uninterrupted drawing
  // commit. Service CPU0's idle task at the boundary, then periodically while
  // geometry streams; waiting for the eighth operation can be too late when
  // the watchdog budget was already mostly consumed by the caller.
  if (completed == 0U || completed % kYieldEveryOperations == 0U || completed == total) {
    vTaskDelay(1);
  }
}

class PartitionSvgSink final : public vector_v2::SvgByteSink {
 public:
  PartitionSvgSink(const esp_partition_t* partition, std::size_t physical_start,
                   std::span<std::uint8_t> page)
      : partition_(partition), physical_start_(physical_start), page_(page) {
    std::fill(page_.begin(), page_.end(), 0xFFU);
  }

  bool append(std::string_view bytes) override {
    ++calls_;
    if (!valid_ || bytes.size() > capacity() - written_) {
      valid_ = false;
      return false;
    }
    for (const unsigned char byte : bytes) {
      crc_ = (crc_ >> 8U) ^ kCrc32Table[(crc_ ^ byte) & 0xFFU];
    }
    while (!bytes.empty()) {
      const std::size_t count = std::min(bytes.size(), page_.size() - buffered_);
      std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.data()), count,
                  page_.begin() + static_cast<std::ptrdiff_t>(buffered_));
      buffered_ += count;
      written_ += count;
      bytes.remove_prefix(count);
      if (buffered_ == page_.size() && !flush_page()) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool finish() { return valid_ && (buffered_ == 0U || flush_page()); }
  [[nodiscard]] std::size_t bytes() const { return written_; }
  [[nodiscard]] std::size_t calls() const { return calls_; }
  [[nodiscard]] std::size_t pages() const { return pages_; }
  [[nodiscard]] std::uint32_t crc32() const { return ~crc_; }

 private:
  [[nodiscard]] std::size_t capacity() const { return partition_->size - physical_start_; }

  bool flush_page() {
    const std::size_t physical_offset = physical_start_ + pages_ * kPageBytes;
    const bool ok =
        physical_offset <= partition_->size - kPageBytes &&
        esp_partition_erase_range(partition_, physical_offset, kPageBytes) == ESP_OK &&
        esp_partition_write(partition_, physical_offset, page_.data(), page_.size()) == ESP_OK;
    if (!ok) {
      valid_ = false;
      return false;
    }
    ++pages_;
    buffered_ = 0U;
    std::fill(page_.begin(), page_.end(), 0xFFU);
    // Flash erase/write runs with cache-disabled critical sections. Give the
    // idle task one scheduling opportunity after every page rather than
    // accumulating multiple pages against the watchdog budget.
    vTaskDelay(1);
    return true;
  }

  const esp_partition_t* partition_;
  std::size_t physical_start_ = 0;
  std::span<std::uint8_t> page_;
  std::size_t buffered_ = 0;
  std::size_t written_ = 0;
  std::size_t calls_ = 0;
  std::size_t pages_ = 0;
  std::uint32_t crc_ = 0xFFFF'FFFFU;
  bool valid_ = true;
};

}  // namespace

SvgExportStore::SvgExportStore() : png_file_(*this) {
  const auto* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
  if (partition == nullptr || partition->size <= kPngOffset + kPageBytes) {
    return;
  }
  partition_ = partition;
  ExportMetadata metadata;
  if (esp_partition_read(partition, 0, &metadata, sizeof(metadata)) == ESP_OK &&
      valid_metadata(metadata, partition->size)) {
    png_offset_ = metadata.png_offset;
    png_size_ = metadata.png_size;
    file_offset_ = metadata.svg_offset;
    file_size_ = metadata.svg_size;
    generation_ = metadata.generation;
    content_crc32_ = metadata.content_crc32;
  }
}

std::size_t SvgExportStore::PngFile::size() const { return owner_.png_size(); }

bool SvgExportStore::PngFile::read(std::size_t offset, std::span<std::uint8_t> output) const {
  return owner_.read_png(offset, output);
}

bool SvgExportStore::ready() const { return partition_ != nullptr; }

bool SvgExportStore::has_file() const { return file_size_ > 0U; }

bool SvgExportStore::has_png() const { return png_size_ > 0U; }

std::size_t SvgExportStore::size() const { return file_size_; }

std::size_t SvgExportStore::png_size() const { return png_size_; }

bool SvgExportStore::read_region(std::size_t region_offset, std::size_t region_size,
                                 std::size_t offset, std::span<std::uint8_t> output) const {
  if (region_size == 0U || offset > region_size || output.size() > region_size - offset) {
    return false;
  }
  return output.empty() || esp_partition_read(as_partition(partition_), region_offset + offset,
                                              output.data(), output.size()) == ESP_OK;
}

bool SvgExportStore::read(std::size_t offset, std::span<std::uint8_t> output) const {
  return read_region(file_offset_, file_size_, offset, output);
}

bool SvgExportStore::read_png(std::size_t offset, std::span<std::uint8_t> output) const {
  return read_region(png_offset_, png_size_, offset, output);
}

SvgExportStoreStats SvgExportStore::encode(const vector_v2::OperationLog& log,
                                           PngRowSource& png_source,
                                           vector_v2::SvgExportProgress progress,
                                           void* progress_context) {
  SvgExportStoreStats stats;
  if (!ready() || !log.ready()) {
    return stats;
  }
  const vector_v2::OperationLogEpoch epoch = log.epoch();
  const vector_v2::DocumentRevision revision = log.current_revision();
  const std::size_t operation_count = log.operation_count();
  const auto authority_matches = [&]() {
    return log.epoch() == epoch && log.current_revision() == revision &&
           log.operation_count() == operation_count;
  };

  auto* workspace =
      heap_caps_malloc(png_encoder_workspace_bytes(), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (workspace == nullptr) {
    workspace =
        heap_caps_malloc(png_encoder_workspace_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  auto* row = static_cast<std::uint8_t*>(heap_caps_malloc(
      png_encoder_row_bytes(static_cast<int>(kPngWidth)), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* row_pixels = static_cast<std::uint16_t*>(
      heap_caps_malloc(static_cast<std::size_t>(kPngWidth) * sizeof(std::uint16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* first_page =
      static_cast<std::uint8_t*>(heap_caps_malloc(kPageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (workspace == nullptr || row == nullptr || row_pixels == nullptr || first_page == nullptr) {
    heap_caps_free(first_page);
    heap_caps_free(row_pixels);
    heap_caps_free(row);
    heap_caps_free(workspace);
    return stats;
  }

  const auto* partition = as_partition(partition_);
  file_offset_ = 0U;
  file_size_ = 0U;
  png_offset_ = 0U;
  png_size_ = 0U;
  content_crc32_ = 0U;
  std::fill_n(first_page, kPageBytes, 0xFFU);
  const bool prefix_erased =
      esp_partition_erase_range(partition, 0, kPngOffset + kPageBytes) == ESP_OK;
  PartitionPngOutput png_output(partition, kPngOffset, std::span(first_page, kPageBytes));
  PngEncodeResult png_result;
  if (prefix_erased) {
    png_result = encode_png_rgb565_rows(
        png_source, static_cast<int>(kPngWidth), static_cast<int>(kPngHeight), png_output,
        workspace, png_encoder_workspace_bytes(),
        std::span(row, png_encoder_row_bytes(static_cast<int>(kPngWidth))),
        std::span(row_pixels, static_cast<std::size_t>(kPngWidth)));
  }
  const bool png_stored =
      png_result.success() && authority_matches() &&
      esp_partition_write(partition, kPngOffset, first_page, kPageBytes) == ESP_OK;
  stats.png_bytes = png_result.bytes_written;
  heap_caps_free(first_page);
  heap_caps_free(row_pixels);
  heap_caps_free(row);
  heap_caps_free(workspace);
  if (!png_stored) {
    return stats;
  }

  const std::size_t svg_offset =
      (kPngOffset + png_result.bytes_written + kPageBytes - 1U) & ~(kPageBytes - 1U);
  if (svg_offset > partition->size - kPageBytes ||
      svg_offset > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return stats;
  }
  auto* page = static_cast<std::uint8_t*>(
      heap_caps_malloc(kPageBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (page == nullptr) {
    page = static_cast<std::uint8_t*>(
        heap_caps_malloc(kPageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (page == nullptr) {
    return stats;
  }

  PartitionSvgSink sink(partition, svg_offset, std::span(page, kPageBytes));
  ProgressAdapter progress_adapter{.progress = progress, .context = progress_context};
  const bool exported =
      authority_matches() &&
      vector_v2::export_svg(
          log, sink, {.progress = service_export_progress, .progress_context = &progress_adapter});
  const bool flushed = exported && sink.finish();
  bool committed = false;
  if (flushed && authority_matches() &&
      png_result.bytes_written <= static_cast<std::size_t>(UINT32_MAX) &&
      sink.bytes() <= static_cast<std::size_t>(UINT32_MAX) &&
      operation_count <= static_cast<std::size_t>(UINT32_MAX)) {
    ExportMetadata metadata;
    metadata.png_offset = static_cast<std::uint32_t>(kPngOffset);
    metadata.png_size = static_cast<std::uint32_t>(png_result.bytes_written);
    metadata.svg_offset = static_cast<std::uint32_t>(svg_offset);
    metadata.svg_size = static_cast<std::uint32_t>(sink.bytes());
    metadata.generation = generation_ + 1U;
    metadata.content_crc32 = sink.crc32();
    metadata.authority_epoch = epoch.value;
    metadata.authority_revision = revision.value;
    metadata.operation_count = static_cast<std::uint32_t>(operation_count);
    metadata.checksum = metadata_checksum(metadata);
    committed = esp_partition_write(partition, 0, &metadata, sizeof(metadata)) == ESP_OK;
    if (committed && !authority_matches()) {
      static_cast<void>(esp_partition_erase_range(partition, 0, kPageBytes));
      committed = false;
    }
    if (committed) {
      png_offset_ = metadata.png_offset;
      png_size_ = metadata.png_size;
      file_offset_ = metadata.svg_offset;
      file_size_ = metadata.svg_size;
      generation_ = metadata.generation;
      content_crc32_ = metadata.content_crc32;
    }
  }

  stats.success = committed;
  stats.bytes = sink.bytes();
  stats.sink_calls = sink.calls();
  stats.flash_pages = sink.pages();
  stats.content_crc32 = sink.crc32();
  heap_caps_free(page);
  return stats;
}

}  // namespace tinydraw::esp32
