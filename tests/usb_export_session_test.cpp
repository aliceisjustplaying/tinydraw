#include "tinydraw/export/usb_export_session.h"

#include <doctest.h>

TEST_CASE("USB export session keeps an ejected medium absent until an explicit end") {
  tinydraw::UsbExportSession session;

  CHECK(session.state() == tinydraw::UsbExportSessionState::kInactive);
  CHECK_FALSE(session.active());
  CHECK_FALSE(session.media_present());

  CHECK(session.begin());
  CHECK(session.begin());
  CHECK(session.active());
  CHECK(session.media_present());

  session.note_host_ejected();
  session.note_host_ejected();
  CHECK(session.state() == tinydraw::UsbExportSessionState::kHostEjected);
  CHECK(session.active());
  CHECK_FALSE(session.media_present());
  CHECK(session.host_ejected());
  CHECK_FALSE(session.begin());

  session.end();
  session.end();
  CHECK_FALSE(session.active());
  CHECK(session.begin());
  CHECK(session.media_present());
}
