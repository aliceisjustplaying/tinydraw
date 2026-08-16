#include "svg_export_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

constexpr char kPartitionLabel[] = "export";
constexpr auto kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x41);
constexpr std::uint32_t kMetadataMagic = 0x3247'5653U;  // SVG2
constexpr std::uint32_t kMetadataVersion = 1U;
constexpr std::size_t kPageBytes = 4'096U;
constexpr std::size_t kSvgOffset = kPageBytes;
constexpr std::size_t kYieldEveryOperations = 8U;

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
  std::uint32_t file_size = 0;
  std::uint32_t generation = 0;
  std::uint32_t content_crc32 = 0;
  std::uint32_t checksum = 0;
};

std::uint32_t metadata_checksum(const ExportMetadata& metadata) {
  return metadata.magic ^ metadata.version ^ metadata.file_size ^ metadata.generation ^
         metadata.content_crc32 ^ 0xA17E'5EEDU;
}

bool valid_metadata(const ExportMetadata& metadata, std::size_t partition_size) {
  return metadata.magic == kMetadataMagic && metadata.version == kMetadataVersion &&
         metadata.file_size > 0U && metadata.file_size <= partition_size - kSvgOffset &&
         metadata.checksum == metadata_checksum(metadata);
}

const esp_partition_t* as_partition(const void* partition) {
  return static_cast<const esp_partition_t*>(partition);
}

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
  PartitionSvgSink(const esp_partition_t* partition, std::span<std::uint8_t> page)
      : partition_(partition), page_(page) {
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
  [[nodiscard]] std::size_t capacity() const { return partition_->size - kSvgOffset; }

  bool flush_page() {
    const std::size_t physical_offset = kSvgOffset + pages_ * kPageBytes;
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
  std::span<std::uint8_t> page_;
  std::size_t buffered_ = 0;
  std::size_t written_ = 0;
  std::size_t calls_ = 0;
  std::size_t pages_ = 0;
  std::uint32_t crc_ = 0xFFFF'FFFFU;
  bool valid_ = true;
};

}  // namespace

SvgExportStore::SvgExportStore() {
  const auto* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
  if (partition == nullptr || partition->size <= kSvgOffset) {
    return;
  }
  partition_ = partition;
  ExportMetadata metadata;
  if (esp_partition_read(partition, 0, &metadata, sizeof(metadata)) == ESP_OK &&
      valid_metadata(metadata, partition->size)) {
    file_size_ = metadata.file_size;
    generation_ = metadata.generation;
    content_crc32_ = metadata.content_crc32;
  }
}

bool SvgExportStore::ready() const { return partition_ != nullptr; }

bool SvgExportStore::has_file() const { return file_size_ > 0U; }

std::size_t SvgExportStore::size() const { return file_size_; }

bool SvgExportStore::read(std::size_t offset, std::span<std::uint8_t> output) const {
  if (!has_file() || offset > file_size_ || output.size() > file_size_ - offset) {
    return false;
  }
  return esp_partition_read(as_partition(partition_), kSvgOffset + offset, output.data(),
                            output.size()) == ESP_OK;
}

SvgExportStoreStats SvgExportStore::encode(const vector_v2::OperationLog& log,
                                           vector_v2::SvgExportProgress progress,
                                           void* progress_context) {
  SvgExportStoreStats stats;
  if (!ready() || !log.ready()) {
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

  const auto* partition = as_partition(partition_);
  file_size_ = 0U;
  content_crc32_ = 0U;
  const bool metadata_erased = esp_partition_erase_range(partition, 0, kPageBytes) == ESP_OK;
  PartitionSvgSink sink(partition, std::span(page, kPageBytes));
  ProgressAdapter progress_adapter{.progress = progress, .context = progress_context};
  const bool exported =
      metadata_erased &&
      vector_v2::export_svg(
          log, sink, {.progress = service_export_progress, .progress_context = &progress_adapter});
  const bool flushed = exported && sink.finish();
  bool committed = false;
  if (flushed && sink.bytes() <= static_cast<std::size_t>(UINT32_MAX)) {
    ExportMetadata metadata;
    metadata.file_size = static_cast<std::uint32_t>(sink.bytes());
    metadata.generation = generation_ + 1U;
    metadata.content_crc32 = sink.crc32();
    metadata.checksum = metadata_checksum(metadata);
    committed = esp_partition_write(partition, 0, &metadata, sizeof(metadata)) == ESP_OK;
    if (committed) {
      file_size_ = sink.bytes();
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
