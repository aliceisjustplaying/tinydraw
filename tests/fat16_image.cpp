#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <vector>

#include "tinydraw/export/fat16_disk.h"

namespace {

class MemoryFile final : public tinydraw::ReadOnlyFile {
 public:
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
  std::vector<std::uint8_t> bytes_{0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU};
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return 2;
  }
  const MemoryFile file;
  const tinydraw::Fat16ExportDisk disk(file);
  std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
  std::array<std::uint8_t, tinydraw::Fat16ExportDisk::kBlockSize> sector{};
  for (std::uint32_t lba = 0; lba < tinydraw::Fat16ExportDisk::kBlockCount; ++lba) {
    if (!disk.read(lba, 0, sector)) {
      return 3;
    }
    output.write(reinterpret_cast<const char*>(sector.data()),
                 static_cast<std::streamsize>(sector.size()));
  }
  return output.good() ? 0 : 4;
}
