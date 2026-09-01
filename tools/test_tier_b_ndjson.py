#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from tier_b_ndjson import CaptureValidator, ValidationError, validate_path


PREFIX = "TINYDRAW_TIER_B_NDJSON "


def line(record: str, **fields: object) -> str:
    return PREFIX + json.dumps({"protocolVersion": 1, "record": record, **fields})


def complete_lines() -> list[str]:
    return [
        line(
            "metadata",
            suite="tier-b",
            harnessVersion="0.1.0-draft",
            idfVersion="v6.1",
            availableCells=["store_hit_psram"],
            selectedCells=["store_hit_psram"],
        ),
        line("cell-start", cell="store_hit_psram", expectedSamples=2),
        line(
            "sample",
            cell="store_hit_psram",
            ordinal=0,
            cycles=17,
            bytes=64,
            startCore=0,
            endCore=0,
        ),
        line(
            "refusal",
            cell="store_hit_psram",
            ordinal=1,
            reason="counter overflow",
            tierCandidate="exact",
        ),
        line("cell-complete", cell="store_hit_psram", samples=2),
        line(
            "run-complete",
            selectedCells=1,
            completedCells=1,
            samples=2,
            refusals=1,
        ),
    ]


class CaptureValidatorTest(unittest.TestCase):
    def test_complete_capture_tallies_samples_and_refusals(self) -> None:
        validator = CaptureValidator()
        for index, record in enumerate(complete_lines(), 1):
            validator.feed_line(record, index)
        tally = validator.finalize()
        self.assertEqual(tally.expected_cells, 1)
        self.assertEqual(tally.completed_cells, 1)
        self.assertEqual(tally.captured_samples, 2)
        self.assertEqual(tally.refusals, 1)

    def test_malformed_record_fails_on_its_line(self) -> None:
        validator = CaptureValidator()
        with self.assertRaisesRegex(ValidationError, "line 1 has malformed NDJSON"):
            validator.feed_line(PREFIX + "{", 1)

    def test_missing_sample_fails_closed(self) -> None:
        records = complete_lines()
        del records[2]
        validator = CaptureValidator()
        for index, record in enumerate(records[:2], 1):
            validator.feed_line(record, index)
        with self.assertRaisesRegex(ValidationError, "ordinal is 1, expected 0"):
            validator.feed_line(records[2], 3)

    def test_truncated_capture_is_incomplete(self) -> None:
        validator = CaptureValidator()
        for index, record in enumerate(complete_lines()[:-1], 1):
            validator.feed_line(record, index)
        with self.assertRaisesRegex(ValidationError, "capture is incomplete"):
            validator.finalize()

    def test_timestamped_log_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.log"
            path.write_text("".join(f"[12:00:00] {record}\n" for record in complete_lines()))
            self.assertTrue(validate_path(path).as_dict()["complete"])


if __name__ == "__main__":
    unittest.main()
