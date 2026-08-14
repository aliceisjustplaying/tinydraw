#include "../esp32/main/board_profile.h"

#include <doctest.h>

#include <string_view>

using tinydraw::esp32::board_driver_profile;
using tinydraw::esp32::BoardProbeEvidence;
using tinydraw::esp32::BoardProfileError;
using tinydraw::esp32::BoardRevision;
using tinydraw::esp32::BoardSelection;
using tinydraw::esp32::resolve_board_profile;

TEST_CASE("Waveshare board profile auto-detection is unambiguous") {
  const auto v1 = resolve_board_profile(BoardSelection::kAuto,
                                        {.cst820_at_0x15 = false, .ft3168_at_0x38 = true});
  CHECK(v1.ready());
  CHECK(v1.revision == BoardRevision::kV1);
  CHECK_FALSE(v1.selected_by_override);

  const auto v2 = resolve_board_profile(BoardSelection::kAuto,
                                        {.cst820_at_0x15 = true, .ft3168_at_0x38 = false});
  CHECK(v2.ready());
  CHECK(v2.revision == BoardRevision::kV2);
  CHECK_FALSE(v2.selected_by_override);
}

TEST_CASE("Waveshare board profile auto-detection fails closed") {
  const auto missing = resolve_board_profile(BoardSelection::kAuto, {});
  CHECK_FALSE(missing.ready());
  CHECK(missing.error == BoardProfileError::kNoMatch);

  const auto ambiguous = resolve_board_profile(BoardSelection::kAuto,
                                               {.cst820_at_0x15 = true, .ft3168_at_0x38 = true});
  CHECK_FALSE(ambiguous.ready());
  CHECK(ambiguous.error == BoardProfileError::kAmbiguous);
}

TEST_CASE("Waveshare board profiles route revision-specific drivers and timing") {
  const auto v1 = board_driver_profile(BoardRevision::kV1);
  CHECK(v1.panel_name == std::string_view("SH8601"));
  CHECK(v1.touch_name == std::string_view("FT3168"));
  CHECK(v1.panel_clock_hz == 40'000'000);
  CHECK(v1.panel_x_gap == 0);
  CHECK_FALSE(v1.safe_frame_sync);

  const auto v2 = board_driver_profile(BoardRevision::kV2);
  CHECK(v2.panel_name == std::string_view("CO5300"));
  CHECK(v2.touch_name == std::string_view("CST820"));
  CHECK(v2.panel_clock_hz == 60'000'000);
  CHECK(v2.panel_x_gap == 0x10);
  CHECK(v2.safe_frame_sync);

  const auto unknown = board_driver_profile(BoardRevision::kUnknown);
  CHECK(unknown.panel_clock_hz == 0);
  CHECK(unknown.panel_x_gap == 0);
  CHECK_FALSE(unknown.safe_frame_sync);
}

TEST_CASE("Waveshare board revision override is explicit and preserves evidence") {
  const BoardProbeEvidence v2_probe{.cst820_at_0x15 = true, .ft3168_at_0x38 = false};
  const auto forced_v1 = resolve_board_profile(BoardSelection::kV1, v2_probe);
  CHECK(forced_v1.ready());
  CHECK(forced_v1.revision == BoardRevision::kV1);
  CHECK(forced_v1.selected_by_override);
  CHECK(forced_v1.evidence.cst820_at_0x15);
  CHECK_FALSE(forced_v1.evidence.ft3168_at_0x38);

  const auto forced_v2 = resolve_board_profile(BoardSelection::kV2, {});
  CHECK(forced_v2.ready());
  CHECK(forced_v2.revision == BoardRevision::kV2);
  CHECK(forced_v2.selected_by_override);
}
