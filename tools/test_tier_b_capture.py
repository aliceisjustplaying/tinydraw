#!/usr/bin/env python3

import importlib.util
import json
import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("tier_b_capture", TOOLS / "tier-b-capture.py")
assert SPEC is not None and SPEC.loader is not None
CAPTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAPTURE)


def preflight(**updates: object) -> dict[str, object]:
    payload: dict[str, object] = {
        "ok": True,
        "fixture": False,
        "variant": "normal",
        "idfVersion": "v6.1",
        "gitCommit": "a" * 40,
        "gitDirty": False,
        "sdkconfigSha256": "b" * 64,
        "compilerVersion": "15.2.0",
        "elfSha256": "c" * 64,
        "manifestSha256": "d" * 64,
        "toolchain": {
            "compiler": "xtensa-esp32s3-elf-gcc",
            "compilerVersion": "15.2.0",
            "objdump": "xtensa-esp32s3-elf-objdump",
            "objdumpVersion": "2.45",
        },
        "dbusFlashClassifier": {
            "alignmentBytes": 64,
            "end": 0x3C06ABFF,
            "section": ".flash.rodata",
            "sizeBytes": 0x40000,
            "start": 0x3C02AC00,
            "storage": "flash-rodata",
            "symbol": "g_flash_pool",
            "xipPsram": False,
        },
    }
    payload.update(updates)
    return payload


class RestartMarkerTest(unittest.TestCase):
    def test_hardware_failure_marker_is_recognized(self) -> None:
        line = b"TINYDRAW_TIER_B_FAILED invalid selection"
        self.assertEqual(CAPTURE.failure_marker(line), b"TINYDRAW_TIER_B_FAILED")

    def test_normal_line_has_no_hardware_failure_marker(self) -> None:
        self.assertIsNone(CAPTURE.failure_marker(b"ordinary log line"))

    def test_initial_ready_is_allowed(self) -> None:
        self.assertIsNone(CAPTURE.restart_marker(CAPTURE.READY, False))

    def test_second_ready_fails(self) -> None:
        self.assertEqual(CAPTURE.restart_marker(CAPTURE.READY, True), CAPTURE.READY)

    def test_rom_and_reset_markers_fail_after_selection(self) -> None:
        for line in (
            b"ESP-ROM:esp32s3-20210327",
            b"rst:0x3 (RTC_SW_SYS_RST)",
            b"Saved PC:0x4037beef",
            b"Rebooting...",
            b"Brownout detector was triggered",
            b"CPU reset by watchdog",
        ):
            with self.subTest(line=line):
                self.assertIsNotNone(CAPTURE.restart_marker(line, True))

    def test_normal_tail_line_is_allowed(self) -> None:
        self.assertIsNone(CAPTURE.restart_marker(b"ordinary log line", True))

    def test_preflight_requires_exact_idf_version(self) -> None:
        with self.assertRaisesRegex(CAPTURE.ValidationError, "expected 'v6.1'"):
            CAPTURE.load_preflight(
                json.dumps(preflight(idfVersion="v6.0.2")).encode(),
                Path("preflight.json"),
                "normal",
                "d" * 64,
            )

    def test_preflight_rejects_reset_default_classifier(self) -> None:
        payload = preflight()
        payload["dbusFlashClassifier"] = {
            **payload["dbusFlashClassifier"],
            "start": 0,
            "end": 0,
        }
        with self.assertRaisesRegex(CAPTURE.ValidationError, "range is invalid"):
            CAPTURE.load_preflight(
                json.dumps(payload).encode(), Path("preflight.json"), "normal", "d" * 64
            )


if __name__ == "__main__":
    unittest.main()
