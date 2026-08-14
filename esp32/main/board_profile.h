#pragma once

#include <cstdint>

namespace tinydraw::esp32 {

enum class BoardRevision : std::uint8_t {
  kUnknown,
  kV1,
  kV2,
};

enum class BoardSelection : std::uint8_t {
  kAuto,
  kV1,
  kV2,
};

struct BoardProbeEvidence {
  bool cst820_at_0x15 = false;
  bool ft3168_at_0x38 = false;
};

struct BoardDriverProfile {
  const char* panel_name = "unknown";
  const char* touch_name = "unknown";
  int panel_clock_hz = 0;
  int panel_x_gap = 0;
  bool safe_frame_sync = false;
};

[[nodiscard]] constexpr BoardDriverProfile board_driver_profile(BoardRevision revision) {
  switch (revision) {
    case BoardRevision::kV1:
      return {.panel_name = "SH8601",
              .touch_name = "FT3168",
              .panel_clock_hz = 40 * 1000 * 1000,
              .panel_x_gap = 0,
              .safe_frame_sync = false};
    case BoardRevision::kV2:
      return {.panel_name = "CO5300",
              .touch_name = "CST820",
              .panel_clock_hz = 60 * 1000 * 1000,
              .panel_x_gap = 0x10,
              .safe_frame_sync = true};
    default:
      return {};
  }
}

enum class BoardProfileError : std::uint8_t {
  kNone,
  kNoMatch,
  kAmbiguous,
};

struct BoardProfileResolution {
  BoardRevision revision = BoardRevision::kUnknown;
  BoardProfileError error = BoardProfileError::kNone;
  BoardProbeEvidence evidence{};
  bool selected_by_override = false;

  [[nodiscard]] constexpr bool ready() const {
    return revision != BoardRevision::kUnknown && error == BoardProfileError::kNone;
  }
};

[[nodiscard]] constexpr BoardProfileResolution resolve_board_profile(BoardSelection selection,
                                                                     BoardProbeEvidence evidence) {
  if (selection == BoardSelection::kV1) {
    return {.revision = BoardRevision::kV1,
            .error = BoardProfileError::kNone,
            .evidence = evidence,
            .selected_by_override = true};
  }
  if (selection == BoardSelection::kV2) {
    return {.revision = BoardRevision::kV2,
            .error = BoardProfileError::kNone,
            .evidence = evidence,
            .selected_by_override = true};
  }
  if (evidence.cst820_at_0x15 == evidence.ft3168_at_0x38) {
    return {.revision = BoardRevision::kUnknown,
            .error = evidence.cst820_at_0x15 ? BoardProfileError::kAmbiguous
                                             : BoardProfileError::kNoMatch,
            .evidence = evidence,
            .selected_by_override = false};
  }
  return {.revision = evidence.cst820_at_0x15 ? BoardRevision::kV2 : BoardRevision::kV1,
          .error = BoardProfileError::kNone,
          .evidence = evidence,
          .selected_by_override = false};
}

[[nodiscard]] constexpr const char* board_revision_name(BoardRevision revision) {
  switch (revision) {
    case BoardRevision::kV1:
      return "V1 (SH8601 + FT3168)";
    case BoardRevision::kV2:
      return "V2 (CO5300 + CST820)";
    default:
      return "unknown";
  }
}

}  // namespace tinydraw::esp32
