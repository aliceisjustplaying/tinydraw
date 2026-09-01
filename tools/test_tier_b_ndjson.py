#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from tier_b_ndjson import (
    PREFIX,
    CaptureValidator,
    CellContract,
    ManifestContract,
    ValidationError,
    validate_path,
)


def contract() -> ManifestContract:
    return ManifestContract(
        protocol_version=2,
        harness_version="0.2.0-review",
        chip_model="ESP32-S3",
        chip_revision=2,
        cells=(
            CellContract("store_hit_psram", 2, ("normal", "xip-psram")),
            CellContract("instruction_psram_hot", 1, ("xip-psram",)),
        ),
    )


def line(record: str, **fields: object) -> str:
    return PREFIX + json.dumps({"protocolVersion": 2, "record": record, **fields})


def counters(**updates: int) -> dict[str, int]:
    result = {
        "ibusAccesses": 0,
        "ibusMisses": 0,
        "dbusAccesses": 256,
        "dbusFlashMisses": 0,
        "dbusPsramMisses": 0,
    }
    result.update(updates)
    return result


def metadata(**updates: object) -> str:
    fields: dict[str, object] = {
        "suite": "tier-b",
        "harnessVersion": "0.2.0-review",
        "idfVersion": "v6.1",
        "gitCommit": "a" * 40,
        "gitDirty": False,
        "variant": "normal",
        "sdkconfigSha256": "b" * 64,
        "compilerVersion": "15.2.0",
        "elfSha256": "c" * 64,
        "chipModel": "ESP32-S3",
        "chipRevision": 2,
        "resetReason": 1,
        "bootId": "1-0123456789abcdef",
        "availableCells": ["store_hit_psram"],
        "selectedCells": ["store_hit_psram"],
    }
    fields.update(updates)
    return line("metadata", **fields)


def complete_lines() -> list[str]:
    return [
        metadata(),
        line("cell-start", cell="store_hit_psram", expectedSamples=2),
        line(
            "sample",
            cell="store_hit_psram",
            ordinal=0,
            cycles=17,
            baselineCycles=8,
            bytes=1024,
            startCore=0,
            endCore=0,
            cacheCounters=counters(),
            baselineCacheCounters=counters(dbusAccesses=0),
        ),
        line(
            "sample",
            cell="store_hit_psram",
            ordinal=1,
            cycles=18,
            baselineCycles=8,
            bytes=1024,
            startCore=0,
            endCore=0,
            cacheCounters=counters(),
            baselineCacheCounters=counters(dbusAccesses=0),
        ),
        line("cell-complete", cell="store_hit_psram", samples=2),
        line(
            "run-complete",
            selectedCells=1,
            completedCells=1,
            samples=2,
            refusals=0,
        ),
    ]


class CaptureValidatorTest(unittest.TestCase):
    def validator(self, expected_build: dict[str, object] | None = None) -> CaptureValidator:
        return CaptureValidator(contract(), "normal", "store_hit_psram", expected_build)

    def test_complete_capture_uses_manifest_counts(self) -> None:
        validator = self.validator()
        for index, record in enumerate(complete_lines(), 1):
            validator.feed_line(record, index)
        tally = validator.finalize()
        self.assertEqual(tally.expected_cells, 1)
        self.assertEqual(tally.expected_samples, 2)
        self.assertTrue(tally.as_dict()["complete"])

    def test_committed_manifest_defines_variant_cells(self) -> None:
        root = Path(__file__).resolve().parents[1]
        actual = ManifestContract.load(
            root / "calibration" / "esp32s3-tier-b" / "probe-cells.json"
        )
        self.assertIn("first_line_i_flash", [cell.id for cell in actual.available("normal")])
        self.assertNotIn("instruction_psram_hot", [cell.id for cell in actual.available("normal")])
        self.assertIn("instruction_psram_hot", [cell.id for cell in actual.available("xip-psram")])
        self.assertNotIn("gpio21_edge", [cell.id for cell in actual.available("normal")])

    def test_refusal_fails_immediately(self) -> None:
        validator = self.validator()
        validator.feed_line(metadata(), 1)
        validator.feed_line(
            line("cell-start", cell="store_hit_psram", expectedSamples=2), 2
        )
        with self.assertRaisesRegex(ValidationError, "refused 'store_hit_psram'"):
            validator.feed_line(
                line(
                    "refusal",
                    cell="store_hit_psram",
                    ordinal=0,
                    reason="counter mismatch",
                    tierCandidate="exact",
                ),
                3,
            )

    def test_duplicate_request_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValidationError, "request contains duplicates"):
            CaptureValidator(
                contract(), "normal", "store_hit_psram,store_hit_psram"
            )

    def test_missing_sample_fails_closed(self) -> None:
        records = complete_lines()
        del records[2]
        validator = self.validator()
        for index, record in enumerate(records[:2], 1):
            validator.feed_line(record, index)
        with self.assertRaisesRegex(ValidationError, "ordinal is 1, expected 0"):
            validator.feed_line(records[2], 3)

    def test_metadata_must_match_exact_manifest_availability(self) -> None:
        validator = self.validator()
        with self.assertRaisesRegex(ValidationError, "availableCells"):
            validator.feed_line(metadata(availableCells=[]), 1)

    def test_runtime_build_must_match_preflight(self) -> None:
        expected = {
            "idfVersion": "v6.1",
            "gitCommit": "a" * 40,
            "gitDirty": False,
            "variant": "normal",
            "sdkconfigSha256": "b" * 64,
            "compilerVersion": "15.2.0",
            "elfSha256": "d" * 64,
        }
        with self.assertRaisesRegex(ValidationError, "verified ELF preflight"):
            self.validator(expected).feed_line(metadata(), 1)

    def test_runtime_requires_exact_idf_version(self) -> None:
        with self.assertRaisesRegex(ValidationError, "expected 'v6.1'"):
            self.validator().feed_line(metadata(idfVersion="v6.0.2"), 1)

    def test_hot_instruction_sample_rejects_any_icache_miss(self) -> None:
        validator = CaptureValidator(contract(), "xip-psram", "instruction_psram_hot")
        validator.feed_line(
            metadata(
                variant="xip-psram",
                availableCells=["store_hit_psram", "instruction_psram_hot"],
                selectedCells=["instruction_psram_hot"],
            ),
            1,
        )
        validator.feed_line(
            line("cell-start", cell="instruction_psram_hot", expectedSamples=1), 2
        )
        with self.assertRaisesRegex(ValidationError, "reports an I-cache miss"):
            validator.feed_line(
                line(
                    "sample",
                    cell="instruction_psram_hot",
                    ordinal=0,
                    cycles=9,
                    bytes=256,
                    startCore=0,
                    endCore=0,
                    cacheCounters=counters(ibusAccesses=8, ibusMisses=1, dbusAccesses=0),
                ),
                3,
            )

    def test_post_completion_record_fails_tail(self) -> None:
        validator = self.validator()
        for index, record in enumerate(complete_lines(), 1):
            validator.feed_line(record, index)
        with self.assertRaisesRegex(ValidationError, "after run-complete"):
            validator.feed_line(metadata(), len(complete_lines()) + 1)

    def test_malformed_record_fails_on_its_line(self) -> None:
        with self.assertRaisesRegex(ValidationError, "line 1 has malformed NDJSON"):
            self.validator().feed_line(PREFIX + "{", 1)

    def test_truncated_capture_is_incomplete(self) -> None:
        validator = self.validator()
        for index, record in enumerate(complete_lines()[:-1], 1):
            validator.feed_line(record, index)
        with self.assertRaisesRegex(ValidationError, "capture is incomplete"):
            validator.finalize()

    def test_timestamped_offline_log_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.log"
            path.write_text("".join(f"[12:00:00] {record}\n" for record in complete_lines()))
            tally, runtime = validate_path(
                path, contract(), "normal", "store_hit_psram"
            )
            self.assertTrue(tally.as_dict()["complete"])
            self.assertEqual(runtime["bootId"], "1-0123456789abcdef")

    def test_invalid_utf8_is_malformed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.log"
            path.write_bytes(b"TINYDRAW_TIER_B_NDJSON \xff\n")
            with self.assertRaisesRegex(ValidationError, "not valid UTF-8"):
                validate_path(path, contract(), "normal", "store_hit_psram")

    def test_all_refuses_a_variant_with_no_available_cells(self) -> None:
        unavailable = ManifestContract(
            protocol_version=2,
            harness_version="0.2.0-review",
            chip_model="ESP32-S3",
            chip_revision=2,
            cells=(
                CellContract(
                    "gpio21_edge", 1, ("normal",), status="open-refusal"
                ),
            ),
        )
        with self.assertRaisesRegex(ValidationError, "no available cells"):
            CaptureValidator(unavailable, "normal", "all")

    def test_store_sample_requires_paired_baseline(self) -> None:
        records = complete_lines()
        payload = json.loads(records[2][len(PREFIX) :])
        del payload["baselineCacheCounters"]
        records[2] = PREFIX + json.dumps(payload)
        validator = self.validator()
        validator.feed_line(records[0], 1)
        validator.feed_line(records[1], 2)
        with self.assertRaisesRegex(ValidationError, "baseline cycles and counters"):
            validator.feed_line(records[2], 3)


if __name__ == "__main__":
    unittest.main()
