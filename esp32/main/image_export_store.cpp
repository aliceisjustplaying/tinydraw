#include "image_export_store.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "tinydraw/export/png_encoder.h"
#include "tinydraw/graphics/world_canvas.h"

namespace tinydraw::esp32 {
namespace {

constexpr char kPartitionLabel[] = "export";
constexpr auto kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x41);
constexpr std::uint32_t kMetadataMagic = 0x474E5054U;
constexpr std::uint32_t kMetadataVersion = 1U;
constexpr std::size_t kPageBytes = 4096U;
constexpr std::size_t kPngOffset = kPageBytes;

struct ExportMetadata {
  std::uint32_t magic = kMetadataMagic;
  std::uint32_t version = kMetadataVersion;
  std::uint32_t width = WorldCanvas::kWidth;
  std::uint32_t height = WorldCanvas::kHeight;
  std::uint32_t image_size = 0;
  std::uint32_t generation = 0;
  std::uint32_t checksum = 0;
};

std::uint32_t metadata_checksum(const ExportMetadata& metadata) {
  return metadata.magic ^ metadata.version ^ metadata.width ^ metadata.height ^
         metadata.image_size ^ metadata.generation ^ 0x91E1'0DA5U;
}

bool valid_metadata(const ExportMetadata& metadata, std::size_t partition_size) {
  return metadata.magic == kMetadataMagic && metadata.version == kMetadataVersion &&
         metadata.width == static_cast<std::uint32_t>(WorldCanvas::kWidth) &&
         metadata.height == static_cast<std::uint32_t>(WorldCanvas::kHeight) &&
         metadata.image_size > 0U && metadata.image_size <= partition_size - kPngOffset &&
         metadata.checksum == metadata_checksum(metadata);
}

const esp_partition_t* as_partition(const void* partition) {
  return static_cast<const esp_partition_t*>(partition);
}

class PartitionOutput final : public PngOutput {
 public:
  PartitionOutput(const esp_partition_t* partition, std::span<std::uint8_t> first_page)
      : partition_(partition), first_page_(first_page) {}

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
    const std::size_t physical_offset = kPngOffset + offset;
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
    return bytes.empty() || esp_partition_read(partition_, kPngOffset + offset, bytes.data(),
                                               bytes.size()) == ESP_OK;
  }

 private:
  [[nodiscard]] bool valid_range(std::size_t offset, std::size_t size) const {
    const std::size_t capacity = partition_->size - kPngOffset;
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
  std::span<std::uint8_t> first_page_;
  std::size_t erased_through_ = kPngOffset + kPageBytes;
};

}  // namespace

ImageExportStore::ImageExportStore() {
  const auto* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
  if (partition == nullptr || partition->size <= kPngOffset) {
    return;
  }
  partition_ = partition;
  ExportMetadata metadata;
  if (esp_partition_read(partition, 0, &metadata, sizeof(metadata)) == ESP_OK &&
      valid_metadata(metadata, partition->size)) {
    image_size_ = metadata.image_size;
    generation_ = metadata.generation;
  }
}

bool ImageExportStore::ready() const { return partition_ != nullptr; }

bool ImageExportStore::has_image() const { return image_size_ > 0U; }

std::size_t ImageExportStore::image_size() const { return image_size_; }

std::uint32_t ImageExportStore::generation() const { return generation_; }

bool ImageExportStore::read(std::size_t offset, std::span<std::uint8_t> output) const {
  if (!has_image() || offset > image_size_ || output.size() > image_size_ - offset) {
    return false;
  }
  return esp_partition_read(as_partition(partition_), kPngOffset + offset, output.data(),
                            output.size()) == ESP_OK;
}

ImageExportStats ImageExportStore::encode(std::span<const std::uint16_t> world) {
  ImageExportStats stats;
  if (!ready() || world.size() < WorldCanvas::kRequiredPixels) {
    return stats;
  }
  const auto started = esp_timer_get_time();
  auto* workspace =
      heap_caps_malloc(png_encoder_workspace_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  auto* row = static_cast<std::uint8_t*>(heap_caps_malloc(
      png_encoder_row_bytes(WorldCanvas::kWidth), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* first_page =
      static_cast<std::uint8_t*>(heap_caps_malloc(kPageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const auto* partition = as_partition(partition_);
  if (workspace == nullptr || row == nullptr || first_page == nullptr) {
    heap_caps_free(first_page);
    heap_caps_free(row);
    heap_caps_free(workspace);
    return stats;
  }

  image_size_ = 0;
  std::fill_n(first_page, kPageBytes, 0xFFU);
  const bool erased = esp_partition_erase_range(partition, 0, kPngOffset + kPageBytes) == ESP_OK;
  PartitionOutput output(partition, std::span(first_page, kPageBytes));
  PngEncodeResult result;
  if (erased) {
    result = encode_png_rgb565(world, WorldCanvas::kWidth, WorldCanvas::kHeight, output, workspace,
                               png_encoder_workspace_bytes(),
                               std::span(row, png_encoder_row_bytes(WorldCanvas::kWidth)));
  }
  bool committed = result.success() &&
                   esp_partition_write(partition, kPngOffset, first_page, kPageBytes) == ESP_OK;
  if (committed) {
    std::fill_n(first_page, kPageBytes, 0xFFU);
    ExportMetadata metadata;
    metadata.image_size = static_cast<std::uint32_t>(result.bytes_written);
    metadata.generation = generation_ + 1U;
    metadata.checksum = metadata_checksum(metadata);
    std::memcpy(first_page, &metadata, sizeof(metadata));
    committed = esp_partition_write(partition, 0, first_page, kPageBytes) == ESP_OK;
    if (committed) {
      image_size_ = result.bytes_written;
      generation_ = metadata.generation;
    }
  }

  heap_caps_free(first_page);
  heap_caps_free(row);
  heap_caps_free(workspace);
  stats.bytes = image_size_;
  stats.elapsed_us = esp_timer_get_time() - started;
  stats.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  stats.success = committed;
  return stats;
}

}  // namespace tinydraw::esp32
