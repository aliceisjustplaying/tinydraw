#!/usr/bin/env python3
"""Validate TinyDraw Tier-B timing NDJSON records and tally completeness."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PREFIX = "TINYDRAW_TIER_B_NDJSON "
PROTOCOL_VERSION = 1


class ValidationError(ValueError):
    """A malformed or incomplete Tier-B capture."""


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValidationError(f"{path} must be an object")
    return value


def _exact_keys(
    value: dict[str, Any], path: str, required: set[str], optional: set[str] | None = None
) -> None:
    optional = optional or set()
    missing = required - value.keys()
    extra = value.keys() - required - optional
    if missing:
        raise ValidationError(f"{path} missing keys: {', '.join(sorted(missing))}")
    if extra:
        raise ValidationError(f"{path} unexpected keys: {', '.join(sorted(extra))}")


def _string(value: Any, path: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValidationError(f"{path} must be a non-empty string")
    return value


def _integer(value: Any, path: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValidationError(f"{path} must be an integer >= {minimum}")
    return value


def _string_list(value: Any, path: str) -> list[str]:
    if not isinstance(value, list):
        raise ValidationError(f"{path} must be an array")
    result = [_string(item, f"{path}[{index}]") for index, item in enumerate(value)]
    if len(set(result)) != len(result):
        raise ValidationError(f"{path} contains duplicates")
    return result


@dataclass(frozen=True)
class CompletenessTally:
    expected_cells: int
    completed_cells: int
    expected_samples: int
    captured_samples: int
    refusals: int

    def as_dict(self) -> dict[str, int | bool]:
        complete = (
            self.completed_cells == self.expected_cells
            and self.captured_samples == self.expected_samples
        )
        return {
            "complete": complete,
            "expectedCells": self.expected_cells,
            "completedCells": self.completed_cells,
            "expectedSamples": self.expected_samples,
            "capturedSamples": self.captured_samples,
            "refusals": self.refusals,
        }


class CaptureValidator:
    """Stateful per-line validator for one boot's Tier-B protocol stream."""

    def __init__(self) -> None:
        self.available: list[str] | None = None
        self.selected: list[str] | None = None
        self.expected_per_cell: dict[str, int] = {}
        self.samples: dict[str, int] = {}
        self.refusals = 0
        self.active_cell: str | None = None
        self.completed: set[str] = set()
        self.run_complete = False

    def feed_line(self, line: str, line_number: int) -> bool:
        """Validate one capture line. Return true when the run is complete."""

        prefix_index = line.find(PREFIX)
        if prefix_index < 0:
            return False
        payload = line[prefix_index + len(PREFIX) :].strip()
        try:
            record = _object(json.loads(payload), f"line {line_number}")
        except json.JSONDecodeError as error:
            raise ValidationError(f"line {line_number} has malformed NDJSON: {error.msg}") from error

        if record.get("protocolVersion") != PROTOCOL_VERSION:
            raise ValidationError(
                f"line {line_number}.protocolVersion must be {PROTOCOL_VERSION}"
            )
        record_type = _string(record.get("record"), f"line {line_number}.record")
        path = f"line {line_number}"

        if record_type == "metadata":
            self._metadata(record, path)
        elif record_type == "cell-start":
            self._cell_start(record, path)
        elif record_type == "sample":
            self._sample(record, path)
        elif record_type == "refusal":
            self._refusal(record, path)
        elif record_type == "cell-complete":
            self._cell_complete(record, path)
        elif record_type == "run-complete":
            self._run_complete(record, path)
        else:
            raise ValidationError(f"{path}.record has unknown value {record_type!r}")
        return self.run_complete

    def _require_metadata(self, path: str) -> None:
        if self.selected is None:
            raise ValidationError(f"{path} appears before metadata")
        if self.run_complete:
            raise ValidationError(f"{path} appears after run-complete")

    def _metadata(self, record: dict[str, Any], path: str) -> None:
        _exact_keys(
            record,
            path,
            {
                "protocolVersion",
                "record",
                "suite",
                "harnessVersion",
                "idfVersion",
                "availableCells",
                "selectedCells",
            },
        )
        if self.selected is not None:
            raise ValidationError(f"{path} is a duplicate metadata record")
        if record["suite"] != "tier-b":
            raise ValidationError(f"{path}.suite must be tier-b")
        _string(record["harnessVersion"], f"{path}.harnessVersion")
        _string(record["idfVersion"], f"{path}.idfVersion")
        available = _string_list(record["availableCells"], f"{path}.availableCells")
        selected = _string_list(record["selectedCells"], f"{path}.selectedCells")
        unknown = set(selected) - set(available)
        if unknown:
            raise ValidationError(f"{path}.selectedCells unknown: {', '.join(sorted(unknown))}")
        if not selected:
            raise ValidationError(f"{path}.selectedCells must not be empty")
        self.available = available
        self.selected = selected

    def _cell_start(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(record, path, {"protocolVersion", "record", "cell", "expectedSamples"})
        cell = _string(record["cell"], f"{path}.cell")
        expected = _integer(record["expectedSamples"], f"{path}.expectedSamples", 1)
        assert self.selected is not None
        if cell not in self.selected:
            raise ValidationError(f"{path}.cell {cell!r} was not selected")
        if self.active_cell is not None:
            raise ValidationError(f"{path} starts {cell!r} while {self.active_cell!r} is active")
        if cell in self.expected_per_cell:
            raise ValidationError(f"{path}.cell {cell!r} started more than once")
        self.active_cell = cell
        self.expected_per_cell[cell] = expected
        self.samples[cell] = 0

    def _sample(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(
            record,
            path,
            {
                "protocolVersion",
                "record",
                "cell",
                "ordinal",
                "cycles",
                "bytes",
                "startCore",
                "endCore",
            },
            {"cacheCounters", "note"},
        )
        cell = _string(record["cell"], f"{path}.cell")
        if cell != self.active_cell:
            raise ValidationError(f"{path}.cell {cell!r} is not the active cell")
        ordinal = _integer(record["ordinal"], f"{path}.ordinal")
        if ordinal != self.samples[cell]:
            raise ValidationError(
                f"{path}.ordinal is {ordinal}, expected {self.samples[cell]} for {cell!r}"
            )
        _integer(record["cycles"], f"{path}.cycles", 1)
        _integer(record["bytes"], f"{path}.bytes")
        for key in ("startCore", "endCore"):
            core = _integer(record[key], f"{path}.{key}")
            if core not in (0, 1):
                raise ValidationError(f"{path}.{key} must be 0 or 1")
        if "cacheCounters" in record:
            _object(record["cacheCounters"], f"{path}.cacheCounters")
        if "note" in record:
            _string(record["note"], f"{path}.note")
        self.samples[cell] += 1

    def _refusal(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(
            record,
            path,
            {"protocolVersion", "record", "cell", "ordinal", "reason", "tierCandidate"},
        )
        cell = _string(record["cell"], f"{path}.cell")
        if cell != self.active_cell:
            raise ValidationError(f"{path}.cell {cell!r} is not the active cell")
        ordinal = _integer(record["ordinal"], f"{path}.ordinal")
        if ordinal != self.samples[cell]:
            raise ValidationError(
                f"{path}.ordinal is {ordinal}, expected {self.samples[cell]} for {cell!r}"
            )
        _string(record["reason"], f"{path}.reason")
        _string(record["tierCandidate"], f"{path}.tierCandidate")
        self.samples[cell] += 1
        self.refusals += 1

    def _cell_complete(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(record, path, {"protocolVersion", "record", "cell", "samples"})
        cell = _string(record["cell"], f"{path}.cell")
        if cell != self.active_cell:
            raise ValidationError(f"{path}.cell {cell!r} is not the active cell")
        samples = _integer(record["samples"], f"{path}.samples")
        expected = self.expected_per_cell[cell]
        if samples != self.samples[cell] or samples != expected:
            raise ValidationError(
                f"{path} reports {samples} samples for {cell!r}, captured {self.samples[cell]}, expected {expected}"
            )
        self.completed.add(cell)
        self.active_cell = None

    def _run_complete(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(
            record,
            path,
            {
                "protocolVersion",
                "record",
                "selectedCells",
                "completedCells",
                "samples",
                "refusals",
            },
        )
        if self.active_cell is not None:
            raise ValidationError(f"{path} appears while {self.active_cell!r} is active")
        assert self.selected is not None
        expected_samples = sum(self.expected_per_cell.values())
        values = {
            "selectedCells": len(self.selected),
            "completedCells": len(self.completed),
            "samples": sum(self.samples.values()),
            "refusals": self.refusals,
        }
        for key, expected in values.items():
            actual = _integer(record[key], f"{path}.{key}")
            if actual != expected:
                raise ValidationError(f"{path}.{key} is {actual}, expected {expected}")
        if set(self.selected) != self.completed:
            missing = set(self.selected) - self.completed
            raise ValidationError(f"{path} missing completed cells: {', '.join(sorted(missing))}")
        if values["samples"] != expected_samples:
            raise ValidationError(f"{path} sample total does not match cell contracts")
        self.run_complete = True

    def finalize(self) -> CompletenessTally:
        if self.selected is None:
            raise ValidationError("capture contains no Tier-B metadata record")
        expected_samples = sum(self.expected_per_cell.values())
        tally = CompletenessTally(
            expected_cells=len(self.selected),
            completed_cells=len(self.completed),
            expected_samples=expected_samples,
            captured_samples=sum(self.samples.values()),
            refusals=self.refusals,
        )
        if not self.run_complete:
            raise ValidationError(f"capture is incomplete: {json.dumps(tally.as_dict(), sort_keys=True)}")
        return tally


def validate_path(path: Path) -> CompletenessTally:
    validator = CaptureValidator()
    with path.open(encoding="utf-8", errors="replace") as capture:
        for line_number, line in enumerate(capture, 1):
            validator.feed_line(line, line_number)
    return validator.finalize()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    try:
        tally = validate_path(args.capture)
    except (OSError, ValidationError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 2
    print(json.dumps({"ok": True, **tally.as_dict()}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
