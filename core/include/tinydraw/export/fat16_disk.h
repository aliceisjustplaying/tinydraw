#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tinydraw {

struct FatDateTime {
  std::uint16_t year = 0;
  std::uint8_t month = 0;
  std::uint8_t day = 0;
  std::uint8_t hour = 0;
  std::uint8_t minute = 0;
  std::uint8_t second = 0;
};

class ReadOnlyFile {
 public:
  virtual ~ReadOnlyFile() = default;
  [[nodiscard]] virtual std::size_t size() const = 0;
  [[nodiscard]] virtual bool read(std::size_t offset, std::span<std::uint8_t> output) const = 0;
};

struct Fat83Name {
  std::array<char, 11> bytes{};
};

inline constexpr Fat83Name kDrawingPngName{
    .bytes = {'D', 'R', 'A', 'W', 'I', 'N', 'G', ' ', 'P', 'N', 'G'}};
inline constexpr Fat83Name kDrawingSvgName{
    .bytes = {'D', 'R', 'A', 'W', 'I', 'N', 'G', ' ', 'S', 'V', 'G'}};

// A fixed, read-only FAT16 volume containing one or two caller-named files.
// Sectors are synthesized on demand; no disk-sized RAM buffer is needed. The
// one-file constructor retains the original layout exactly.
class Fat16ExportDisk {
 public:
  static constexpr std::uint16_t kBlockSize = 512;
  // Matches the colonized 0xA20000-byte export partition (2026-08-18):
  // 20,736 x 512 B = 10.125 MiB. Cluster count stays well inside FAT16's
  // 4,085..65,524 validity window at one sector per cluster.
  static constexpr std::uint32_t kBlockCount = 20'736;

  explicit Fat16ExportDisk(const ReadOnlyFile& file, Fat83Name name = kDrawingPngName);
  Fat16ExportDisk(const ReadOnlyFile& first_file, Fat83Name first_name,
                  const ReadOnlyFile& second_file, Fat83Name second_name);

  void set_modified_time(FatDateTime time) { modified_time_ = time; }
  [[nodiscard]] bool read(std::uint32_t lba, std::uint32_t offset,
                          std::span<std::uint8_t> output) const;

 private:
  std::array<const ReadOnlyFile*, 2> files_{};
  std::array<Fat83Name, 2> names_{};
  std::size_t file_count_ = 0;
  FatDateTime modified_time_{};
};

}  // namespace tinydraw
