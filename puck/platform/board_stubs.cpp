// The board's I2C neighbours, for the wasm build: the touch controller, the
// PMIC, the RTC, the Wi-Fi time sync, and the flash autosave journal. Same
// product headers, unmodified; different bodies.
//
// SPDX-License-Identifier: MIT
// Part of TinyDraw's puck module. See ../README.md.
//
// Each of these is a place where the honest answer differs, so each says which
// kind of answer it is giving:
//
//   PhysicalTouch  REAL, in the only sense available. It reports the contact
//                  the browser latched through emu_touch, at the same 1 ms
//                  poll the CST820 is read at. What it cannot reproduce is
//                  controller MISBEHAVIOUR (dropped contacts mid-stroke,
//                  strays), and a lot of firmware exists only because of that.
//                  emu_abi.h names this exact gap; puck's own page can inject
//                  those defects, off by default and labelled.
//
//   PowerManager   NOT PRESENT. There is no battery behind a browser tab, so
//                  read() reports an invalid status and ready() is false. The
//                  app already handles a board whose PMIC did not come up: it
//                  skips the periodic battery sample entirely, and the chrome
//                  draws no percentage. Inventing "87%" would be a number a
//                  person could believe.
//
//   RtcClock       REAL BUT COARSE. The only clock here is emu_tick(nowMs),
//                  which is milliseconds since the trace started, not a wall
//                  calendar. It reports a fixed date advanced by that clock,
//                  so file timestamps move forward and never claim to be
//                  today.
//
//   TimeSync       UNAVAILABLE, reported as such. Wi-Fi and NTP do not exist
//                  here; available() is false, so the app sets
//                  chrome.can_sync_time = false and the control is drawn
//                  unavailable, which is the same path a board with no
//                  credentials compiled in takes.
//
//   Autosave       UNAVAILABLE, reported as such. The journal is 4 MiB of
//                  flash partition with erase/write/readback transactions;
//                  there is no flash. restore() returns kUnavailable, which
//                  makes the app print TINYDRAW_AUTOSAVE_DISABLED and run
//                  with an empty document, exactly as it does on a board
//                  whose partition is missing. A drawing does not survive a
//                  page reload, and ../README.md says so plainly.

#include <algorithm>

#include "esp_timer.h"
#include "physical_touch.h"
#include "power_manager.h"
#include "puck_platform.h"
#include "rtc_clock.h"
#include "time_sync.h"
#include "vector_v2_autosave_store.h"

namespace tinydraw::esp32 {

// ---- PhysicalTouch ---------------------------------------------------------

PhysicalTouch::PhysicalTouch() {
  // The bus handle is only ever passed to the PMIC and RTC adapters, which do
  // not use it here. A non-null value keeps their "was I given a bus" checks
  // meaningful rather than accidentally exercising a null path the board
  // never takes.
  bus_ = reinterpret_cast<i2c_master_bus_handle_t>(1);
  ready_ = true;
}

bool PhysicalTouch::ready() const { return ready_; }

i2c_master_bus_handle_t PhysicalTouch::bus() const { return bus_; }

TouchRead PhysicalTouch::read(Point& point) {
  float x = 0.0F;
  float y = 0.0F;
  if (!puck::sample_contact(x, y)) return TouchRead::kNoTouch;
  point.x = x;
  point.y = y;
  return TouchRead::kPoint;
}

// ---- PowerManager ----------------------------------------------------------

PowerManager::PowerManager(i2c_master_bus_handle_t) {}
PowerManager::~PowerManager() = default;

PowerStatus PowerManager::read() const {
  return {};  // valid == false: no battery behind a browser tab.
}

bool PowerManager::configure_power_button() const { return false; }
bool PowerManager::read_register(std::uint8_t, std::uint8_t&) const { return false; }
bool PowerManager::read_registers(std::uint8_t, std::uint8_t*, std::size_t) const { return false; }
bool PowerManager::write_register(std::uint8_t, std::uint8_t) const { return false; }

// ---- RtcClock --------------------------------------------------------------

namespace {
// A fixed origin the emulator's own milliseconds are added to. Not "now": a
// replay must produce the same bytes on any machine at any later date, and a
// host wall clock would break exactly that.
constexpr std::uint16_t kEpochYear = 2026;
constexpr std::uint8_t kEpochMonth = 8;
constexpr std::uint8_t kEpochDay = 20;
}  // namespace

RtcClock::RtcClock(i2c_master_bus_handle_t) { ready_ = true; }
RtcClock::~RtcClock() = default;

bool RtcClock::read(FatDateTime& time) {
  const std::int64_t seconds = esp_timer_get_time() / 1'000'000;
  time.year = kEpochYear;
  time.month = kEpochMonth;
  time.day = kEpochDay;
  time.hour = static_cast<std::uint8_t>((seconds / 3600) % 24);
  time.minute = static_cast<std::uint8_t>((seconds / 60) % 60);
  time.second = static_cast<std::uint8_t>(seconds % 60);
  return true;
}

bool RtcClock::set(const FatDateTime&) {
  // Nothing to write to, and saying "saved" would be a lie the chrome would
  // then show as a success toast.
  return false;
}

// ---- TimeSyncController ----------------------------------------------------

bool TimeSyncController::available() const { return false; }
bool TimeSyncController::start() { return false; }
void TimeSyncController::dismiss() { status_.store(TimeSyncStatus::kIdle); }
void TimeSyncController::task_entry(void*) {}
void TimeSyncController::run() {}
bool start_time_sync(RtcClock&) { return false; }

// ---- VectorV2AutosaveStore -------------------------------------------------

struct VectorV2AutosaveStore::Impl {};

VectorV2AutosaveStore::VectorV2AutosaveStore() = default;
VectorV2AutosaveStore::~VectorV2AutosaveStore() = default;

bool VectorV2AutosaveStore::ready() const { return false; }

VectorV2AutosaveRestoreStatus VectorV2AutosaveStore::restore(vector_v2::OperationLog&) {
  return VectorV2AutosaveRestoreStatus::kUnavailable;
}

bool VectorV2AutosaveStore::submit(vector_v2::JournalChange, const vector_v2::OperationLog&) {
  return false;
}

bool VectorV2AutosaveStore::submit_checkpoint(const vector_v2::OperationLog&) { return false; }
bool VectorV2AutosaveStore::checkpoint_required() const { return false; }
bool VectorV2AutosaveStore::checkpoint_staging() const { return false; }
bool VectorV2AutosaveStore::flush(std::uint32_t) { return true; }

}  // namespace tinydraw::esp32
