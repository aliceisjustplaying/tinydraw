#include "tinydraw/vector_v2/rerender_ledger.h"

#include <doctest.h>

#include <array>
#include <vector>

namespace vector_v2 = tinydraw::vector_v2;

namespace {

std::vector<vector_v2::RerenderLedgerEntry> ledger_storage() {
  return std::vector<vector_v2::RerenderLedgerEntry>(vector_v2::kRerenderLedgerEntryCount);
}

}  // namespace

TEST_CASE("rerender ledger classifies the first render as a cold miss") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);
  REQUIRE(ledger.ready());

  const auto cause = ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 4, 6,
                                                vector_v2::DocumentRevision{7});
  CHECK(cause == vector_v2::RerenderCause::kColdMiss);
  const auto totals = ledger.totals();
  CHECK(totals.renders == 1U);
  CHECK(totals.unique_groups == 1U);
  CHECK(totals.cold_miss == 1U);
  CHECK(totals.amplification() == doctest::Approx(1.0));
}

TEST_CASE("an identical repeat render is unexplained — the déjà-vu signal") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);

  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k100Percent, 2, 2,
                                               vector_v2::DocumentRevision{3}));
  const auto cause = ledger.record_group_render(vector_v2::ZoomLevel::k100Percent, 2, 2,
                                                vector_v2::DocumentRevision{3});
  CHECK(cause == vector_v2::RerenderCause::kUnexplained);
  CHECK(ledger.totals().unexplained == 1U);
  CHECK(ledger.totals().amplification() == doctest::Approx(2.0));
}

TEST_CASE("world damage explains re-renders at every zoom it touches") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);

  // Group (0,0) at 400% covers world pixels [0,32); group (5,5) covers world
  // [160,192) — outside the damaged rect.
  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 0, 0,
                                               vector_v2::DocumentRevision{1}));
  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 10, 10,
                                               vector_v2::DocumentRevision{1}));
  ledger.mark_world_damage({0, 0, 10, 10});

  const auto damaged = ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 0, 0,
                                                  vector_v2::DocumentRevision{2});
  CHECK(damaged == vector_v2::RerenderCause::kExpectedDamage);
  // The far group was not damaged; its revision-driven re-render is the
  // spatially unnecessary case revision-keyed accounting could not see.
  const auto stale = ledger.record_group_render(vector_v2::ZoomLevel::k400Percent, 10, 10,
                                                vector_v2::DocumentRevision{2});
  CHECK(stale == vector_v2::RerenderCause::kStaleRevision);
  CHECK(ledger.totals().expected_damage == 1U);
  CHECK(ledger.totals().stale_revision == 1U);
}

TEST_CASE("eviction explains a re-render after cached content was displaced") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);

  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k200Percent, 8, 4,
                                               vector_v2::DocumentRevision{5}));
  ledger.mark_evicted({.zoom = vector_v2::ZoomLevel::k200Percent, .column = 9, .row = 5});
  const auto cause = ledger.record_group_render(vector_v2::ZoomLevel::k200Percent, 8, 4,
                                                vector_v2::DocumentRevision{5});
  CHECK(cause == vector_v2::RerenderCause::kEviction);
  CHECK(ledger.totals().eviction == 1U);
}

TEST_CASE("damage outranks eviction because the document change forced the render") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);

  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k50Percent, 0, 0,
                                               vector_v2::DocumentRevision{1}));
  ledger.mark_evicted({.zoom = vector_v2::ZoomLevel::k50Percent, .column = 0, .row = 0});
  ledger.mark_world_damage({0, 0, 300, 300});
  const auto cause = ledger.record_group_render(vector_v2::ZoomLevel::k50Percent, 0, 0,
                                                vector_v2::DocumentRevision{2});
  CHECK(cause == vector_v2::RerenderCause::kExpectedDamage);
}

TEST_CASE("a render clears prior damage and eviction state") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);

  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k100Percent, 0, 0,
                                               vector_v2::DocumentRevision{1}));
  ledger.mark_world_damage({0, 0, 200, 200});
  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k100Percent, 0, 0,
                                               vector_v2::DocumentRevision{2}));
  const auto cause = ledger.record_group_render(vector_v2::ZoomLevel::k100Percent, 0, 0,
                                                vector_v2::DocumentRevision{2});
  CHECK(cause == vector_v2::RerenderCause::kUnexplained);
}

TEST_CASE("rerender ledger reset restores a fresh session") {
  auto storage = ledger_storage();
  vector_v2::RerenderLedger ledger(storage);

  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k25Percent, 0, 0,
                                               vector_v2::DocumentRevision{1}));
  ledger.reset();
  CHECK(ledger.totals().renders == 0U);
  const auto cause = ledger.record_group_render(vector_v2::ZoomLevel::k25Percent, 0, 0,
                                                vector_v2::DocumentRevision{1});
  CHECK(cause == vector_v2::RerenderCause::kColdMiss);
}

TEST_CASE("short ledger storage is not ready and never classifies") {
  std::array<vector_v2::RerenderLedgerEntry, 4> tiny{};
  vector_v2::RerenderLedger ledger(tiny);
  CHECK(!ledger.ready());
  static_cast<void>(ledger.record_group_render(vector_v2::ZoomLevel::k25Percent, 0, 0,
                                               vector_v2::DocumentRevision{1}));
  CHECK(ledger.totals().renders == 0U);
}
