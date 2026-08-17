#ifndef TINYDRAW_VECTOR_V2_TEST_FIXTURES_H
#define TINYDRAW_VECTOR_V2_TEST_FIXTURES_H

#include <array>
#include <cstddef>

#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2::test {

template <std::size_t RecordCapacity, std::size_t SampleCapacity>
struct OperationLogFixture {
  std::array<OperationRecord, RecordCapacity> records{};
  std::array<CompactOperationSample, SampleCapacity> samples{};
  OperationLog log{records, samples};
};

}  // namespace tinydraw::vector_v2::test

#endif  // TINYDRAW_VECTOR_V2_TEST_FIXTURES_H
