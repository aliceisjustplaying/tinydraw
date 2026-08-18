// Validates a captured authority-journal image (e.g. a raw dump of the
// device "drawing" partition) with the production recovery code and prints
// the recovered document's shape. Used to verify the owner-document battery
// corpus in testdata/documents/.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/authority_journal.h"
#include "tinydraw/vector_v2/operation.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

class BufferSource final : public vector_v2::AuthorityJournalSource {
 public:
  explicit BufferSource(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool read(std::size_t offset, std::span<std::byte> output) const override {
    // Reads beyond the captured image return erased flash (0xFF), matching
    // a trimmed partition dump.
    for (std::size_t i = 0; i < output.size(); ++i) {
      const std::size_t at = offset + i;
      output[i] = at < bytes_.size() ? bytes_[at] : std::byte{0xFF};
    }
    return true;
  }

 private:
  std::span<const std::byte> bytes_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    std::fprintf(stderr, "usage: %s JOURNAL_IMAGE [COMPACT_DOCUMENT_OUT]\n", argv[0]);
    return 2;
  }
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (raw.empty()) {
    std::fprintf(stderr, "empty or unreadable image\n");
    return 2;
  }
  const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(raw.data()),
                                         raw.size()};
  std::vector<vector_v2::OperationRecord> records(4000U);
  std::vector<vector_v2::CompactOperationSample> samples(80000U);
  const BufferSource source(bytes);
  const auto recovery = vector_v2::recover_authority_journal(source, 0x400000U, records, samples);
  std::size_t sample_total = 0;
  for (std::size_t i = 0; i < recovery.state.active_operation_count; ++i) {
    sample_total += records[i].sample_count;
  }
  std::printf(
      "status=%u transactions=%zu bytes_consumed=%zu sequence=%llu discarded_tail=%u "
      "active_operations=%zu active_samples=%zu\n",
      static_cast<unsigned>(recovery.status), recovery.transaction_count, recovery.bytes_consumed,
      static_cast<unsigned long long>(recovery.sequence), recovery.discarded_tail,
      recovery.state.active_operation_count, sample_total);
  if (recovery.status != vector_v2::JournalRecoveryStatus::kRecovered) {
    return 1;
  }
  if (argc == 3) {
    // Compact battery-corpus form ("TDOC"): active-prefix operations and
    // their samples only, replayed through log.append by consumers so the
    // production validation path rebuilds bounds and identity.
    std::ofstream out(argv[2], std::ios::binary);
    const std::uint32_t op_count =
        static_cast<std::uint32_t>(recovery.state.active_operation_count);
    const std::uint32_t total_samples = static_cast<std::uint32_t>(sample_total);
    out.write("TDOC", 4);
    out.write(reinterpret_cast<const char*>(&op_count), 4);
    out.write(reinterpret_cast<const char*>(&total_samples), 4);
    for (std::size_t i = 0; i < op_count; ++i) {
      const auto tool = static_cast<std::uint8_t>(records[i].tool);
      out.write(reinterpret_cast<const char*>(&tool), 1);
      out.write(reinterpret_cast<const char*>(&records[i].color), 2);
      out.write(reinterpret_cast<const char*>(&records[i].sample_count), 2);
    }
    for (std::size_t i = 0; i < op_count; ++i) {
      out.write(reinterpret_cast<const char*>(samples.data() + records[i].first_sample),
                static_cast<std::streamsize>(records[i].sample_count *
                                             sizeof(vector_v2::CompactOperationSample)));
    }
    if (!out.good()) {
      std::fprintf(stderr, "write failed\n");
      return 2;
    }
    std::printf("compact_document=%s ops=%u samples=%u\n", argv[2], op_count, total_samples);
  }
  return 0;
}
