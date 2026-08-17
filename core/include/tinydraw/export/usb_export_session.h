#pragma once

#include <atomic>
#include <cstdint>

namespace tinydraw {

enum class UsbExportSessionState : std::uint8_t {
  kInactive,
  kPresenting,
  kHostEjected,
  kStopping,
};

// Cross-task lifecycle for a read-only USB export volume. Host eject removes
// the medium but deliberately keeps the session latched until the application
// explicitly ends it; a later host probe cannot silently present it again.
class UsbExportSession {
 public:
  [[nodiscard]] UsbExportSessionState state() const { return state_.load(); }
  [[nodiscard]] bool active() const { return state() != UsbExportSessionState::kInactive; }
  [[nodiscard]] bool media_present() const { return state() == UsbExportSessionState::kPresenting; }
  [[nodiscard]] bool host_ejected() const { return state() == UsbExportSessionState::kHostEjected; }

  // Idempotent while presenting. An ejected session must be ended before a
  // new presentation can begin.
  [[nodiscard]] bool begin() {
    UsbExportSessionState current = state_.load();
    for (;;) {
      if (current == UsbExportSessionState::kPresenting) {
        return true;
      }
      if (current != UsbExportSessionState::kInactive) {
        return false;
      }
      if (state_.compare_exchange_weak(current, UsbExportSessionState::kPresenting)) {
        return true;
      }
    }
  }

  void note_host_ejected() {
    UsbExportSessionState expected = UsbExportSessionState::kPresenting;
    static_cast<void>(
        state_.compare_exchange_strong(expected, UsbExportSessionState::kHostEjected));
  }

  // Removes the medium before stack shutdown begins. The session remains
  // active and cannot restart until the owner confirms shutdown with end().
  [[nodiscard]] bool begin_stop() {
    UsbExportSessionState current = state_.load();
    for (;;) {
      if (current == UsbExportSessionState::kStopping) {
        return true;
      }
      if (current == UsbExportSessionState::kInactive) {
        return false;
      }
      if (state_.compare_exchange_weak(current, UsbExportSessionState::kStopping)) {
        return true;
      }
    }
  }

  void end() { state_.store(UsbExportSessionState::kInactive); }

 private:
  std::atomic<UsbExportSessionState> state_{UsbExportSessionState::kInactive};
};

}  // namespace tinydraw
