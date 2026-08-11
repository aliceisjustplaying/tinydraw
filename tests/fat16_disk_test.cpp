#include "tinydraw/export/fat16_disk.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

class MemoryFile final : public tinydraw::ReadOnlyFile {
 public:
  explicit MemoryFile(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

  std::size_t size() const override { return bytes_.size(); }

  bool read(std::size_t offset, std::span<std::uint8_t> output) const override {
    if (offset > bytes_.size() || output.size() > bytes_.size() - offset) {
      return false;
    }
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), output.size(),
                output.begin());
    return true;
  }

 private:
  std::vector<std::uint8_t> bytes_;
};

std::uint16_t u16(const std::array<std::uint8_t, 512>& sector, std::size_t offset) {
  return static_cast<std::uint16_t>(sector[offset] | (sector[offset + 1U] << 8U));
}

std::uint32_t u32(const std::array<std::uint8_t, 512>& sector, std::size_t offset) {
  return static_cast<std::uint32_t>(u16(sector, offset)) |
         (static_cast<std::uint32_t>(u16(sector, offset + 2U)) << 16U);
}

}  // namespace

TEST_CASE("FAT16 export disk exposes DRAWING.PNG without a disk image buffer") {
  std::vector<std::uint8_t> image(700U);
  for (std::size_t index = 0; index < image.size(); ++index) {
    image[index] = static_cast<std::uint8_t>(index);
  }
  const MemoryFile file(image);
  const tinydraw::Fat16ExportDisk disk(file);
  std::array<std::uint8_t, 512> sector{};

  REQUIRE(disk.read(0, 0, sector));
  CHECK(u16(sector, 11) == 512U);
  CHECK(u16(sector, 19) == tinydraw::Fat16ExportDisk::kBlockCount);
  CHECK(u16(sector, 22) == 64U);
  CHECK(sector[510] == 0x55U);
  CHECK(sector[511] == 0xAAU);

  REQUIRE(disk.read(1, 0, sector));
  CHECK(u16(sector, 0) == 0xFFF8U);
  CHECK(u16(sector, 2) == 0xFFFFU);
  CHECK(u16(sector, 4) == 3U);
  CHECK(u16(sector, 6) == 0xFFFFU);

  REQUIRE(disk.read(129, 0, sector));
  CHECK(std::memcmp(sector.data() + 32, "DRAWING PNG", 11) == 0);
  CHECK(sector[43] == 0x21U);
  CHECK(u16(sector, 58) == 2U);
  CHECK(u32(sector, 60) == image.size());

  std::vector<std::uint8_t> restored(1024U, 0xFFU);
  REQUIRE(disk.read(130, 0, std::span(restored).first(512U)));
  REQUIRE(disk.read(131, 0, std::span(restored).subspan(512U, 512U)));
  CHECK(std::equal(image.begin(), image.end(), restored.begin()));
  CHECK(std::all_of(restored.begin() + 700, restored.end(),
                    [](std::uint8_t byte) { return byte == 0U; }));
}

TEST_CASE("FAT16 export disk supports partial sector reads and an empty volume") {
  const MemoryFile file({});
  const tinydraw::Fat16ExportDisk disk(file);
  std::array<std::uint8_t, 16> bytes{};

  REQUIRE(disk.read(0, 3, bytes));
  CHECK(std::memcmp(bytes.data(), "MSDOS5.0", 8) == 0);
  REQUIRE(disk.read(129, 32, bytes));
  CHECK(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t byte) { return byte == 0U; }));
  CHECK_FALSE(disk.read(tinydraw::Fat16ExportDisk::kBlockCount, 0, bytes));
  CHECK_FALSE(disk.read(0, 510, bytes));
}
