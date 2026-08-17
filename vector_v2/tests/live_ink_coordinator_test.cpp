#include "tinydraw/vector_v2/live_ink_coordinator.h"

#include <doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace vector_v2 = tinydraw::vector_v2;

namespace {

tinydraw::InkPoint ink_point(float x, std::uint32_t timestamp_us) {
  return {
      .position = {.x = x, .y = 40.0F},
      .pressure = 0.5F,
      .radius = 3.0F,
      .distance = x,
      .running_length = x,
      .timestamp_us = timestamp_us,
  };
}

vector_v2::OperationPoint operation_point(float x, std::uint32_t timestamp_us) {
  return {.world_x = x, .world_y = 40.0F, .radius = 3.0F, .timestamp_us = timestamp_us};
}

bool reaches(const tinydraw::RibbonPrimitiveBatch& primitives, tinydraw::Point point) {
  for (const auto& primitive : primitives) {
    if (primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle && primitive.center.x == point.x &&
        primitive.center.y == point.y) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("live ink move publishes the newest tail before ready authority") {
  std::array<vector_v2::CompactOperationSample, 8> storage{};
  vector_v2::ChainedOperationBuilder builder(storage, 2U);
  REQUIRE(
      builder.begin(vector_v2::OperationTool::kPen, 0x001FU, 7U, operation_point(10.0F, 1'000U)));
  tinydraw::CurvedRibbonStream ribbon;
  static_cast<void>(ribbon.append(ink_point(10.0F, 1'000U), true));

  std::vector<char> order;
  std::uint32_t presented_event_us = 0;
  tinydraw::Point expected_tip = ink_point(20.0F, 2'000U).position;
  const auto present = [&](const tinydraw::RibbonUpdate& update, std::uint32_t event_us) {
    order.push_back('V');
    presented_event_us = event_us;
    return reaches(update.provisional, expected_tip);
  };
  const auto commit = [&]() -> std::optional<vector_v2::ChainedOperationStatus> {
    order.push_back('A');
    REQUIRE(builder.pending_append().has_value());
    return builder.acknowledge_commit();
  };

  const auto first =
      vector_v2::process_live_ink_move(ribbon, builder, ink_point(20.0F, 2'000U),
                                       operation_point(20.0F, 2'000U), expected_tip, 2'000U,
                                       present, commit);
  CHECK(first.status == vector_v2::ChainedOperationStatus::kAccepted);

  order.clear();
  const tinydraw::Point raw_tip{.x = 38.0F, .y = 40.0F};
  expected_tip = raw_tip;
  const auto second =
      vector_v2::process_live_ink_move(ribbon, builder, ink_point(30.0F, 3'000U),
                                       operation_point(30.0F, 3'000U), raw_tip, 3'000U, present,
                                       commit);
  CHECK(second.visual_passed);
  CHECK(second.status == vector_v2::ChainedOperationStatus::kAccepted);
  CHECK_FALSE(second.commit_failed);
  CHECK(order == std::vector<char>{'V', 'A'});
  CHECK(presented_event_us == 3'000U);
  CHECK(expected_tip.x == 38.0F);
}
