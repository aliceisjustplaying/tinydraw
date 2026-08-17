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
  tinydraw::Fat16ExportDisk disk(file);
  disk.set_modified_time(
      {.year = 2026, .month = 8, .day = 11, .hour = 17, .minute = 42, .second = 59});
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
  CHECK(u16(sector, 46) == 0x8D5DU);
  CHECK(u16(sector, 48) == 0x5D0BU);
  CHECK(u16(sector, 50) == 0x5D0BU);
  CHECK(u16(sector, 54) == 0x8D5DU);
  CHECK(u16(sector, 56) == 0x5D0BU);
  CHECK(u16(sector, 58) == 2U);
  CHECK(u32(sector, 60) == image.size());

  std::vector<std::uint8_t> restored(1024U, 0xFFU);
  REQUIRE(disk.read(130, 0, std::span(restored).first(512U)));
  REQUIRE(disk.read(131, 0, std::span(restored).subspan(512U, 512U)));
  CHECK(std::equal(image.begin(), image.end(), restored.begin()));
  CHECK(std::all_of(restored.begin() + 700, restored.end(),
                    [](std::uint8_t byte) { return byte == 0U; }));
}

TEST_CASE("FAT16 export disk exposes a caller-selected 8.3 filename") {
  const MemoryFile file({0x3CU, 0x73U, 0x76U, 0x67U, 0x3EU});
  const tinydraw::Fat16ExportDisk disk(file, tinydraw::kDrawingSvgName);
  std::array<std::uint8_t, 512> sector{};

  REQUIRE(disk.read(129, 0, sector));
  CHECK(std::memcmp(sector.data() + 32, "DRAWING SVG", 11) == 0);
  CHECK(u32(sector, 60) == file.size());
}

TEST_CASE("FAT16 export disk exposes SVG and PNG as separate contiguous files") {
  std::vector<std::uint8_t> svg(700U);
  std::vector<std::uint8_t> png(900U);
  for (std::size_t index = 0; index < svg.size(); ++index) {
    svg[index] = static_cast<std::uint8_t>(0x53U + index);
  }
  for (std::size_t index = 0; index < png.size(); ++index) {
    png[index] = static_cast<std::uint8_t>(0x89U + index * 3U);
  }
  const MemoryFile svg_file(svg);
  const MemoryFile png_file(png);
  const tinydraw::Fat16ExportDisk disk(svg_file, tinydraw::kDrawingSvgName, png_file,
                                       tinydraw::kDrawingPngName);
  std::array<std::uint8_t, 512> sector{};

  REQUIRE(disk.read(1, 0, sector));
  CHECK(u16(sector, 4) == 3U);
  CHECK(u16(sector, 6) == 0xFFFFU);
  CHECK(u16(sector, 8) == 5U);
  CHECK(u16(sector, 10) == 0xFFFFU);

  REQUIRE(disk.read(129, 0, sector));
  CHECK(std::memcmp(sector.data() + 32, "DRAWING SVG", 11) == 0);
  CHECK(u16(sector, 58) == 2U);
  CHECK(u32(sector, 60) == svg.size());
  CHECK(std::memcmp(sector.data() + 64, "DRAWING PNG", 11) == 0);
  CHECK(u16(sector, 90) == 4U);
  CHECK(u32(sector, 92) == png.size());

  std::vector<std::uint8_t> restored_svg(1'024U, 0xFFU);
  std::vector<std::uint8_t> restored_png(1'024U, 0xFFU);
  REQUIRE(disk.read(130, 0, std::span(restored_svg).first(512U)));
  REQUIRE(disk.read(131, 0, std::span(restored_svg).subspan(512U)));
  REQUIRE(disk.read(132, 0, std::span(restored_png).first(512U)));
  REQUIRE(disk.read(133, 0, std::span(restored_png).subspan(512U)));
  CHECK(std::equal(svg.begin(), svg.end(), restored_svg.begin()));
  CHECK(std::equal(png.begin(), png.end(), restored_png.begin()));
  CHECK(std::all_of(restored_svg.begin() + static_cast<std::ptrdiff_t>(svg.size()),
                    restored_svg.end(), [](std::uint8_t byte) { return byte == 0U; }));
  CHECK(std::all_of(restored_png.begin() + static_cast<std::ptrdiff_t>(png.size()),
                    restored_png.end(), [](std::uint8_t byte) { return byte == 0U; }));
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
