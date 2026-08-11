#pragma once

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

// A fixed, read-only FAT16 volume containing at most one DRAWING.PNG file.
// Sectors are synthesized on demand; no disk-sized RAM buffer is needed.
class Fat16ExportDisk {
 public:
  static constexpr std::uint16_t kBlockSize = 512;
  static constexpr std::uint32_t kBlockCount = 16'384;

  explicit Fat16ExportDisk(const ReadOnlyFile& file) : file_(file) {}

  void set_modified_time(FatDateTime time) { modified_time_ = time; }
  [[nodiscard]] bool read(std::uint32_t lba, std::uint32_t offset,
                          std::span<std::uint8_t> output) const;

 private:
  const ReadOnlyFile& file_;
  FatDateTime modified_time_{};
};

}  // namespace tinydraw
