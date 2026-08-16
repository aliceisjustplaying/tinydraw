#ifndef TINYDRAW_ESP32_VECTOR_V2_SHIP_CONTRACT_H
#define TINYDRAW_ESP32_VECTOR_V2_SHIP_CONTRACT_H

#include <cstdint>

namespace tinydraw::esp32::vector_v2_ship_contract {

// Firmware mirrors of the frozen product thresholds in SHIP_CONTRACT.md.
// Keep pass/fail logic on these names so diagnostic alarms cannot masquerade
// as product acceptance.
inline constexpr std::int64_t kPanFrameP95RequiredUs = 41'700;
inline constexpr std::int64_t kPanFrameP95GuardUs = 38'000;
inline constexpr std::int64_t kColdViewportRequiredUs = 500'000;
inline constexpr std::int64_t kColdViewportGuardUs = 450'000;

// Owner decision 2026-08-16 (SHIP_CONTRACT.md "Owner decisions" #2): the
// frozen-corpus 400% cold wall is accepted at its 507.0 ms three-run
// development maximum until autosave exists; further regression is not.
// Ceiling = measured max + twice the observed <=1.5 ms run spread. The
// <=500 ms product line still governs the final autosave-enabled 20-run
// closure; only the 400% frozen-corpus gate may use this ceiling.
inline constexpr std::int64_t kColdViewport400HoldTheLineUs = 510'000;

}  // namespace tinydraw::esp32::vector_v2_ship_contract

#endif  // TINYDRAW_ESP32_VECTOR_V2_SHIP_CONTRACT_H
