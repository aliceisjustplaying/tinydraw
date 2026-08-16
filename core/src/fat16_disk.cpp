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

void put_u16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void put_u32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
  put_u16(bytes, offset, static_cast<std::uint16_t>(value));
  put_u16(bytes, offset + 2U, static_cast<std::uint16_t>(value >> 16U));
}

std::uint16_t fat_entry(std::uint32_t cluster, std::uint32_t file_clusters) {
  if (cluster == 0U) {
    return 0xFFF8U;
  }
  if (cluster == 1U) {
    return 0xFFFFU;
  }
  if (cluster < file_clusters + 1U) {
    return static_cast<std::uint16_t>(cluster + 1U);
  }
  if (cluster == file_clusters + 1U && file_clusters > 0U) {
    return 0xFFFFU;
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
                     std::uint32_t file_clusters) {
  const std::uint32_t first_entry = fat_sector * (Fat16ExportDisk::kBlockSize / 2U);
  for (std::uint32_t index = 0; index < Fat16ExportDisk::kBlockSize / 2U; ++index) {
    put_u16(sector, static_cast<std::size_t>(index) * 2U,
            fat_entry(first_entry + index, file_clusters));
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

void make_root_sector(std::span<std::uint8_t> sector, std::size_t file_size,
                      const FatDateTime& modified_time, Fat83Name name) {
  std::memcpy(sector.data(), "TINYDRAW   ", 11);
  sector[11] = 0x08U;
  if (file_size == 0U) {
    return;
  }
  auto file = sector.subspan(32U, 32U);
  std::copy(name.bytes.begin(), name.bytes.end(), file.begin());
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
  put_u16(file, 26, 2U);
  put_u32(file, 28, static_cast<std::uint32_t>(file_size));
}

}  // namespace

bool Fat16ExportDisk::read(std::uint32_t lba, std::uint32_t offset,
                           std::span<std::uint8_t> output) const {
  if (lba >= kBlockCount || offset >= kBlockSize || output.size() > kBlockSize - offset) {
    return false;
  }
  const std::size_t file_size = std::min(file_.size(), kMaximumFileBytes);
  if (lba >= kFirstDataSector) {
    std::fill(output.begin(), output.end(), 0U);
    const std::size_t file_offset =
        static_cast<std::size_t>(lba - kFirstDataSector) * kBlockSize + offset;
    if (file_offset >= file_size) {
      return true;
    }
    return file_.read(file_offset, output.first(std::min(output.size(), file_size - file_offset)));
  }

  std::array<std::uint8_t, kBlockSize> sector{};
  if (lba == 0U) {
    make_boot_sector(sector);
  } else if (lba >= kFirstFat && lba < kSecondFat) {
    const auto clusters = static_cast<std::uint32_t>((file_size + kBlockSize - 1U) / kBlockSize);
    make_fat_sector(sector, lba - kFirstFat, clusters);
  } else if (lba >= kSecondFat && lba < kRootSector) {
    const auto clusters = static_cast<std::uint32_t>((file_size + kBlockSize - 1U) / kBlockSize);
    make_fat_sector(sector, lba - kSecondFat, clusters);
  } else if (lba == kRootSector) {
    make_root_sector(sector, file_size, modified_time_, name_);
  }
  std::copy_n(sector.begin() + static_cast<std::ptrdiff_t>(offset), output.size(), output.begin());
  return true;
}

}  // namespace tinydraw
