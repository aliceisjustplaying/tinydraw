#include "tinydraw/export/fat16_disk.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace tinydraw {
namespace {

constexpr std::uint32_t kReservedSectors = 1;
constexpr std::uint32_t kFatCopies = 2;
constexpr std::uint32_t kSectorsPerFat = 64;
constexpr std::uint32_t kRootSectors = 1;
constexpr std::uint32_t kFirstFat = kReservedSectors;
constexpr std::uint32_t kSecondFat = kFirstFat + kSectorsPerFat;
constexpr std::uint32_t kRootSector = kSecondFat + kSectorsPerFat;
constexpr std::uint32_t kFirstDataSector = kRootSector + kRootSectors;
constexpr std::size_t kMaximumFileBytes =
    static_cast<std::size_t>(Fat16ExportDisk::kBlockCount - kFirstDataSector) *
    Fat16ExportDisk::kBlockSize;

struct FileLayout {
  const ReadOnlyFile* file = nullptr;
  Fat83Name name{};
  std::size_t size = 0;
  std::uint32_t first_cluster = 0;
  std::uint32_t clusters = 0;
};

std::array<FileLayout, 2> layout_files(const std::array<const ReadOnlyFile*, 2>& files,
                                       const std::array<Fat83Name, 2>& names,
                                       std::size_t file_count) {
  std::array<FileLayout, 2> layout{};
  std::size_t remaining = kMaximumFileBytes;
  std::uint32_t next_cluster = 2U;
  for (std::size_t index = 0; index < file_count; ++index) {
    const std::size_t size = std::min(files[index]->size(), remaining);
    const auto clusters = static_cast<std::uint32_t>((size + Fat16ExportDisk::kBlockSize - 1U) /
                                                     Fat16ExportDisk::kBlockSize);
    layout[index] = {.file = files[index],
                     .name = names[index],
                     .size = size,
                     .first_cluster = clusters == 0U ? 0U : next_cluster,
                     .clusters = clusters};
    next_cluster += clusters;
    remaining -= static_cast<std::size_t>(clusters) * Fat16ExportDisk::kBlockSize;
  }
  return layout;
}

void put_u16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void put_u32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
  put_u16(bytes, offset, static_cast<std::uint16_t>(value));
  put_u16(bytes, offset + 2U, static_cast<std::uint16_t>(value >> 16U));
}

std::uint16_t fat_entry(std::uint32_t cluster, const std::array<FileLayout, 2>& files) {
  if (cluster == 0U) {
    return 0xFFF8U;
  }
  if (cluster == 1U) {
    return 0xFFFFU;
  }
  for (const FileLayout& file : files) {
    if (file.clusters == 0U || cluster < file.first_cluster ||
        cluster >= file.first_cluster + file.clusters) {
      continue;
    }
    return cluster + 1U < file.first_cluster + file.clusters
               ? static_cast<std::uint16_t>(cluster + 1U)
               : 0xFFFFU;
  }
  return 0U;
}

void make_boot_sector(std::span<std::uint8_t> sector) {
  sector[0] = 0xEBU;
  sector[1] = 0x3CU;
  sector[2] = 0x90U;
  std::memcpy(sector.data() + 3, "MSDOS5.0", 8);
  put_u16(sector, 11, Fat16ExportDisk::kBlockSize);
  sector[13] = 1U;
  put_u16(sector, 14, static_cast<std::uint16_t>(kReservedSectors));
  sector[16] = static_cast<std::uint8_t>(kFatCopies);
  put_u16(sector, 17, 16U);
  put_u16(sector, 19, static_cast<std::uint16_t>(Fat16ExportDisk::kBlockCount));
  sector[21] = 0xF8U;
  put_u16(sector, 22, static_cast<std::uint16_t>(kSectorsPerFat));
  put_u16(sector, 24, 32U);
  put_u16(sector, 26, 64U);
  sector[36] = 0x80U;
  sector[38] = 0x29U;
  put_u32(sector, 39, 0x5444'5241U);
  std::memcpy(sector.data() + 43, "TINYDRAW   ", 11);
  std::memcpy(sector.data() + 54, "FAT16   ", 8);
  sector[510] = 0x55U;
  sector[511] = 0xAAU;
}

void make_fat_sector(std::span<std::uint8_t> sector, std::uint32_t fat_sector,
                     const std::array<FileLayout, 2>& files) {
  const std::uint32_t first_entry = fat_sector * (Fat16ExportDisk::kBlockSize / 2U);
  for (std::uint32_t index = 0; index < Fat16ExportDisk::kBlockSize / 2U; ++index) {
    put_u16(sector, static_cast<std::size_t>(index) * 2U, fat_entry(first_entry + index, files));
  }
}

bool valid_fat_time(const FatDateTime& time) {
  return time.year >= 1980U && time.year <= 2107U && time.month >= 1U && time.month <= 12U &&
         time.day >= 1U && time.day <= 31U && time.hour <= 23U && time.minute <= 59U &&
         time.second <= 59U;
}

std::uint16_t fat_date(const FatDateTime& time) {
  const auto encoded = ((static_cast<std::uint32_t>(time.year) - 1980U) << 9U) |
                       (static_cast<std::uint32_t>(time.month) << 5U) |
                       static_cast<std::uint32_t>(time.day);
  return static_cast<std::uint16_t>(encoded);
}

std::uint16_t fat_time(const FatDateTime& time) {
  const auto encoded = (static_cast<std::uint32_t>(time.hour) << 11U) |
                       (static_cast<std::uint32_t>(time.minute) << 5U) |
                       (static_cast<std::uint32_t>(time.second) / 2U);
  return static_cast<std::uint16_t>(encoded);
}

void make_root_sector(std::span<std::uint8_t> sector, const std::array<FileLayout, 2>& files,
                      const FatDateTime& modified_time) {
  std::memcpy(sector.data(), "TINYDRAW   ", 11);
  sector[11] = 0x08U;
  std::size_t entry = 1U;
  for (const FileLayout& layout : files) {
    if (layout.size == 0U) {
      continue;
    }
    auto file = sector.subspan(entry * 32U, 32U);
    std::copy(layout.name.bytes.begin(), layout.name.bytes.end(), file.begin());
    file[11] = 0x21U;
    if (valid_fat_time(modified_time)) {
      const auto date = fat_date(modified_time);
      const auto time = fat_time(modified_time);
      put_u16(file, 14, time);
      put_u16(file, 16, date);
      put_u16(file, 18, date);
      put_u16(file, 22, time);
      put_u16(file, 24, date);
    }
    put_u16(file, 26, static_cast<std::uint16_t>(layout.first_cluster));
    put_u32(file, 28, static_cast<std::uint32_t>(layout.size));
    ++entry;
  }
}

}  // namespace

Fat16ExportDisk::Fat16ExportDisk(const ReadOnlyFile& file, Fat83Name name)
    : files_{&file, nullptr}, names_{name, {}}, file_count_(1U) {}

Fat16ExportDisk::Fat16ExportDisk(const ReadOnlyFile& first_file, Fat83Name first_name,
                                 const ReadOnlyFile& second_file, Fat83Name second_name)
    : files_{&first_file, &second_file}, names_{first_name, second_name}, file_count_(2U) {}

bool Fat16ExportDisk::read(std::uint32_t lba, std::uint32_t offset,
                           std::span<std::uint8_t> output) const {
  if (lba >= kBlockCount || offset >= kBlockSize || output.size() > kBlockSize - offset) {
    return false;
  }
  const auto files = layout_files(files_, names_, file_count_);
  if (lba >= kFirstDataSector) {
    std::fill(output.begin(), output.end(), 0U);
    const std::uint32_t cluster = lba - kFirstDataSector + 2U;
    for (const FileLayout& file : files) {
      if (file.clusters == 0U || cluster < file.first_cluster ||
          cluster >= file.first_cluster + file.clusters) {
        continue;
      }
      const std::size_t file_offset =
          static_cast<std::size_t>(cluster - file.first_cluster) * kBlockSize + offset;
      if (file_offset >= file.size) {
        return true;
      }
      return file.file->read(file_offset,
                             output.first(std::min(output.size(), file.size - file_offset)));
    }
    return true;
  }

  std::array<std::uint8_t, kBlockSize> sector{};
  if (lba == 0U) {
    make_boot_sector(sector);
  } else if (lba >= kFirstFat && lba < kSecondFat) {
    make_fat_sector(sector, lba - kFirstFat, files);
  } else if (lba >= kSecondFat && lba < kRootSector) {
    make_fat_sector(sector, lba - kSecondFat, files);
  } else if (lba == kRootSector) {
    make_root_sector(sector, files, modified_time_);
  }
  std::copy_n(sector.begin() + static_cast<std::ptrdiff_t>(offset), output.size(), output.begin());
  return true;
}

}  // namespace tinydraw
