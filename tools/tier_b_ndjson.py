#!/usr/bin/env python3
"""Validate one manifest-bound TinyDraw Tier-B capture."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PREFIX = "TINYDRAW_TIER_B_NDJSON "
PROTOCOL_VERSION = 2
REQUIRED_IDF_VERSION = "v6.1"
DEFAULT_MANIFEST = (
    Path(__file__).resolve().parents[1]
    / "calibration"
    / "esp32s3-tier-b"
    / "probe-cells.json"
)
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class ValidationError(ValueError):
    """A malformed, refused, or incomplete Tier-B capture."""


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


def _boolean(value: Any, path: str) -> bool:
    if not isinstance(value, bool):
        raise ValidationError(f"{path} must be a boolean")
    return value


def _string_list(value: Any, path: str) -> list[str]:
    if not isinstance(value, list):
        raise ValidationError(f"{path} must be an array")
    result = [_string(item, f"{path}[{index}]") for index, item in enumerate(value)]
    if len(set(result)) != len(result):
        raise ValidationError(f"{path} contains duplicates")
    return result


def _sha256(value: Any, path: str) -> str:
    result = _string(value, path)
    if SHA256.fullmatch(result) is None:
        raise ValidationError(f"{path} must be a lowercase SHA-256")
    return result


@dataclass(frozen=True)
class CellContract:
    id: str
    samples: int
    variants: tuple[str, ...]
    status: str = "ready"


@dataclass(frozen=True)
class ManifestContract:
    protocol_version: int
    harness_version: str
    chip_model: str
    chip_revision: int
    cells: tuple[CellContract, ...]

    @classmethod
    def load(cls, path: Path = DEFAULT_MANIFEST) -> "ManifestContract":
        try:
            data = path.read_bytes()
        except OSError as error:
            raise ValidationError(f"cannot load manifest {path}: {error}") from error
        return cls.from_bytes(data, str(path))

    @classmethod
    def from_bytes(cls, data: bytes, label: str = "manifest") -> "ManifestContract":
        try:
            payload = _object(json.loads(data), label)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValidationError(f"cannot load manifest {label}: {error}") from error
        _exact_keys(
            payload,
            label,
            {"protocolVersion", "harnessVersion", "chipModel", "chipRevision", "cells"},
        )
        protocol_version = _integer(payload["protocolVersion"], "manifest.protocolVersion", 1)
        if protocol_version != PROTOCOL_VERSION:
            raise ValidationError(
                f"manifest.protocolVersion must be {PROTOCOL_VERSION}, got {protocol_version}"
            )
        raw_cells = payload["cells"]
        if not isinstance(raw_cells, list) or not raw_cells:
            raise ValidationError("manifest.cells must be a non-empty array")
        cells: list[CellContract] = []
        for index, raw_cell in enumerate(raw_cells):
            path_name = f"manifest.cells[{index}]"
            cell = _object(raw_cell, path_name)
            _exact_keys(cell, path_name, {"id", "family", "samples", "variants"}, {"status"})
            _string(cell["family"], f"{path_name}.family")
            if "status" in cell:
                status = _string(cell["status"], f"{path_name}.status")
            else:
                status = "ready"
            if status not in {"ready", "open-refusal"}:
                raise ValidationError(f"{path_name}.status has unsupported value {status!r}")
            variants = tuple(_string_list(cell["variants"], f"{path_name}.variants"))
            if not variants or set(variants) - {"normal", "xip-psram"}:
                raise ValidationError(f"{path_name}.variants contains an unsupported variant")
            cells.append(
                CellContract(
                    id=_string(cell["id"], f"{path_name}.id"),
                    samples=_integer(cell["samples"], f"{path_name}.samples", 1),
                    variants=variants,
                    status=status,
                )
            )
        ids = [cell.id for cell in cells]
        if len(ids) != len(set(ids)):
            raise ValidationError("manifest.cells contains duplicate IDs")
        return cls(
            protocol_version=protocol_version,
            harness_version=_string(payload["harnessVersion"], "manifest.harnessVersion"),
            chip_model=_string(payload["chipModel"], "manifest.chipModel"),
            chip_revision=_integer(payload["chipRevision"], "manifest.chipRevision"),
            cells=tuple(cells),
        )

    def available(self, variant: str) -> list[CellContract]:
        if variant not in {"normal", "xip-psram"}:
            raise ValidationError(f"unsupported variant {variant!r}")
        return [
            cell
            for cell in self.cells
            if variant in cell.variants and cell.status != "open-refusal"
        ]

    def select(self, variant: str, request: str) -> list[CellContract]:
        available = self.available(variant)
        if request == "all":
            if not available:
                raise ValidationError(f"manifest defines no available cells for {variant}")
            return available
        requested = request.split(",")
        if not requested or any(not item for item in requested):
            raise ValidationError("cell request must not be empty")
        if len(requested) != len(set(requested)):
            raise ValidationError("cell request contains duplicates")
        by_id = {cell.id: cell for cell in available}
        unknown = [item for item in requested if item not in by_id]
        if unknown:
            raise ValidationError(
                f"cell request is unavailable for {variant}: {', '.join(unknown)}"
            )
        return [by_id[item] for item in requested]


@dataclass(frozen=True)
class CompletenessTally:
    expected_cells: int
    completed_cells: int
    expected_samples: int
    captured_samples: int
    refusals: int = 0

    def as_dict(self) -> dict[str, int | bool]:
        return {
            "complete": (
                self.completed_cells == self.expected_cells
                and self.captured_samples == self.expected_samples
                and self.refusals == 0
            ),
            "expectedCells": self.expected_cells,
            "completedCells": self.completed_cells,
            "expectedSamples": self.expected_samples,
            "capturedSamples": self.captured_samples,
            "refusals": self.refusals,
        }


class CaptureValidator:
    """Validate exact cells and counts from a committed manifest and request."""

    def __init__(
        self,
        contract: ManifestContract,
        variant: str,
        request: str,
        expected_build: dict[str, Any] | None = None,
    ) -> None:
        self.contract = contract
        self.variant = variant
        self.request = request
        self.available = [cell.id for cell in contract.available(variant)]
        selected = contract.select(variant, request)
        self.selected = [cell.id for cell in selected]
        self.expected_samples = {cell.id: cell.samples for cell in selected}
        self.expected_build = expected_build
        self.metadata: dict[str, Any] | None = None
        self.samples = {cell.id: 0 for cell in selected}
        self.active_cell: str | None = None
        self.started: list[str] = []
        self.completed: list[str] = []
        self.run_complete = False

    def feed_line(self, line: str, line_number: int) -> bool:
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
        if self.run_complete:
            raise ValidationError(f"{path} contains a record after run-complete")
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
        if self.metadata is None:
            raise ValidationError(f"{path} appears before metadata")

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
                "gitCommit",
                "gitDirty",
                "variant",
                "sdkconfigSha256",
                "compilerVersion",
                "elfSha256",
                "chipModel",
                "chipRevision",
                "resetReason",
                "bootId",
                "availableCells",
                "selectedCells",
            },
        )
        if self.metadata is not None:
            raise ValidationError(f"{path} is a duplicate metadata record")
        expected = {
            "suite": "tier-b",
            "harnessVersion": self.contract.harness_version,
            "variant": self.variant,
            "chipModel": self.contract.chip_model,
            "chipRevision": self.contract.chip_revision,
            "availableCells": self.available,
            "selectedCells": self.selected,
        }
        for key, value in expected.items():
            if record[key] != value:
                raise ValidationError(f"{path}.{key} is {record[key]!r}, expected {value!r}")
        idf_version = _string(record["idfVersion"], f"{path}.idfVersion")
        if idf_version != REQUIRED_IDF_VERSION:
            raise ValidationError(
                f"{path}.idfVersion is {idf_version!r}, expected {REQUIRED_IDF_VERSION!r}"
            )
        _string(record["gitCommit"], f"{path}.gitCommit")
        _boolean(record["gitDirty"], f"{path}.gitDirty")
        _sha256(record["sdkconfigSha256"], f"{path}.sdkconfigSha256")
        _string(record["compilerVersion"], f"{path}.compilerVersion")
        _sha256(record["elfSha256"], f"{path}.elfSha256")
        _integer(record["resetReason"], f"{path}.resetReason")
        _string(record["bootId"], f"{path}.bootId")
        if self.expected_build is not None:
            for key in (
                "idfVersion",
                "gitCommit",
                "gitDirty",
                "variant",
                "sdkconfigSha256",
                "compilerVersion",
                "elfSha256",
            ):
                if record[key] != self.expected_build.get(key):
                    raise ValidationError(
                        f"{path}.{key} does not match the verified ELF preflight"
                    )
        self.metadata = record.copy()

    def _cell_start(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(record, path, {"protocolVersion", "record", "cell", "expectedSamples"})
        cell = _string(record["cell"], f"{path}.cell")
        if self.active_cell is not None:
            raise ValidationError(f"{path} starts {cell!r} while {self.active_cell!r} is active")
        if len(self.started) >= len(self.selected):
            raise ValidationError(f"{path} starts unexpected cell {cell!r}")
        expected_cell = self.selected[len(self.started)]
        if cell != expected_cell:
            raise ValidationError(f"{path}.cell is {cell!r}, expected {expected_cell!r}")
        expected_samples = _integer(record["expectedSamples"], f"{path}.expectedSamples", 1)
        if expected_samples != self.expected_samples[cell]:
            raise ValidationError(
                f"{path}.expectedSamples is {expected_samples}, expected {self.expected_samples[cell]}"
            )
        self.started.append(cell)
        self.active_cell = cell

    @staticmethod
    def _cache_counters(value: Any, path: str) -> dict[str, Any]:
        counters = _object(value, path)
        keys = {
            "ibusAccesses",
            "ibusMisses",
            "dbusAccesses",
            "dbusFlashMisses",
            "dbusPsramMisses",
        }
        _exact_keys(counters, path, keys)
        for key in keys:
            _integer(counters[key], f"{path}.{key}")
        return counters

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
                "cacheCounters",
            },
            {
                "baselineCycles",
                "baselineCacheCounters",
                "aggressorCore",
                "aggressorCacheCounters",
                "aggressorIterations",
                "note",
            },
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
        counters = self._cache_counters(record["cacheCounters"], f"{path}.cacheCounters")
        has_baseline_cycles = "baselineCycles" in record
        has_baseline_counters = "baselineCacheCounters" in record
        if has_baseline_cycles != has_baseline_counters:
            raise ValidationError(f"{path} must carry baseline cycles and counters together")
        if "baselineCycles" in record:
            _integer(record["baselineCycles"], f"{path}.baselineCycles", 1)
        baseline_counters = None
        if "baselineCacheCounters" in record:
            baseline_counters = self._cache_counters(
                record["baselineCacheCounters"], f"{path}.baselineCacheCounters"
            )
        if "aggressorIterations" in record:
            _integer(record["aggressorIterations"], f"{path}.aggressorIterations", 1)
        aggressor_counters = None
        if "aggressorCacheCounters" in record:
            aggressor_counters = self._cache_counters(
                record["aggressorCacheCounters"], f"{path}.aggressorCacheCounters"
            )
        if "aggressorCore" in record:
            aggressor_core = _integer(record["aggressorCore"], f"{path}.aggressorCore")
            if aggressor_core != 1:
                raise ValidationError(f"{path}.aggressorCore must be 1")
        if "note" in record:
            _string(record["note"], f"{path}.note")
        needs_contention = cell.startswith("arbitration_") or cell.endswith("_cross_core")
        if needs_contention and (
            baseline_counters is None
            or "aggressorIterations" not in record
            or "aggressorCore" not in record
            or aggressor_counters is None
        ):
            raise ValidationError(f"{path} lacks contention baseline or aggressor attribution")
        if cell == "store_hit_psram":
            if baseline_counters is None or record["bytes"] != 1024:
                raise ValidationError(f"{path} lacks the store-hit baseline or exact issue count")
            if counters["dbusAccesses"] == 0 or counters["dbusPsramMisses"] != 0:
                raise ValidationError(f"{path} does not report hit-only PSRAM store counters")
        if cell.startswith("arbitration_"):
            if counters["dbusAccesses"] == 0 or counters["dbusPsramMisses"] == 0:
                raise ValidationError(f"{path} lacks PSRAM victim counter attribution")
            if "flash_aggressor" in cell and (
                aggressor_counters is None
                or aggressor_counters["dbusAccesses"] == 0
                or aggressor_counters["dbusFlashMisses"] == 0
            ):
                raise ValidationError(f"{path} lacks core-1 flash aggressor attribution")
            if "psram_aggressor" in cell and (
                aggressor_counters is None
                or aggressor_counters["dbusAccesses"] == 0
                or aggressor_counters["dbusPsramMisses"] == 0
            ):
                raise ValidationError(f"{path} lacks core-1 PSRAM aggressor attribution")
        if cell in {"instruction_psram_hot", "instruction_psram_cold", "first_line_i_flash"}:
            if counters["ibusAccesses"] == 0:
                raise ValidationError(f"{path} lacks instruction-cache accesses")
            if cell != "instruction_psram_hot" and counters["ibusMisses"] == 0:
                raise ValidationError(f"{path} lacks the expected instruction-cache miss")
            if cell == "instruction_psram_hot" and counters["ibusMisses"] != 0:
                raise ValidationError(f"{path} hot instruction probe reports an I-cache miss")
        if cell == "first_line_d_flash" and (
            counters["dbusAccesses"] == 0 or counters["dbusFlashMisses"] == 0
        ):
            raise ValidationError(f"{path} lacks first-line D-flash counters")
        if cell in {"first_line_d_psram", "psram_bandwidth_cross_core"} and (
            counters["dbusAccesses"] == 0 or counters["dbusPsramMisses"] == 0
        ):
            raise ValidationError(f"{path} lacks D-PSRAM counters")
        if cell == "flash_bandwidth_cross_core" and (
            counters["dbusAccesses"] == 0 or counters["dbusFlashMisses"] == 0
        ):
            raise ValidationError(f"{path} lacks D-flash counters")
        if cell.endswith("_cross_core") and (
            aggressor_counters is None
            or aggressor_counters["dbusAccesses"] == 0
            or aggressor_counters["dbusPsramMisses"] == 0
        ):
            raise ValidationError(f"{path} lacks core-1 PSRAM aggressor attribution")
        self.samples[cell] += 1

    def _refusal(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(
            record,
            path,
            {"protocolVersion", "record", "cell", "ordinal", "reason", "tierCandidate"},
        )
        cell = _string(record["cell"], f"{path}.cell")
        ordinal = _integer(record["ordinal"], f"{path}.ordinal")
        reason = _string(record["reason"], f"{path}.reason")
        tier = _string(record["tierCandidate"], f"{path}.tierCandidate")
        raise ValidationError(
            f"{path} refused {cell!r} sample {ordinal}, tier candidate {tier}: {reason}"
        )

    def _cell_complete(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        _exact_keys(record, path, {"protocolVersion", "record", "cell", "samples"})
        cell = _string(record["cell"], f"{path}.cell")
        if cell != self.active_cell:
            raise ValidationError(f"{path}.cell {cell!r} is not the active cell")
        samples = _integer(record["samples"], f"{path}.samples")
        expected = self.expected_samples[cell]
        if samples != self.samples[cell] or samples != expected:
            raise ValidationError(
                f"{path} reports {samples} samples for {cell!r}, captured {self.samples[cell]}, expected {expected}"
            )
        self.completed.append(cell)
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
        expected_total = sum(self.expected_samples.values())
        values = {
            "selectedCells": len(self.selected),
            "completedCells": len(self.selected),
            "samples": expected_total,
            "refusals": 0,
        }
        for key, expected in values.items():
            actual = _integer(record[key], f"{path}.{key}")
            if actual != expected:
                raise ValidationError(f"{path}.{key} is {actual}, expected {expected}")
        if self.started != self.selected or self.completed != self.selected:
            raise ValidationError(f"{path} does not complete the exact requested cell sequence")
        self.run_complete = True

    def finalize(self) -> CompletenessTally:
        expected_total = sum(self.expected_samples.values())
        tally = CompletenessTally(
            expected_cells=len(self.selected),
            completed_cells=len(self.completed),
            expected_samples=expected_total,
            captured_samples=sum(self.samples.values()),
        )
        if not self.run_complete:
            raise ValidationError(
                f"capture is incomplete: {json.dumps(tally.as_dict(), sort_keys=True)}"
            )
        return tally


def validate_path(
    path: Path,
    contract: ManifestContract,
    variant: str,
    request: str,
    expected_build: dict[str, Any] | None = None,
) -> tuple[CompletenessTally, dict[str, Any]]:
    validator = CaptureValidator(contract, variant, request, expected_build)
    try:
        with path.open(encoding="utf-8") as capture:
            for line_number, line in enumerate(capture, 1):
                validator.feed_line(line, line_number)
    except UnicodeDecodeError as error:
        raise ValidationError(f"capture is not valid UTF-8: {error}") from error
    tally = validator.finalize()
    assert validator.metadata is not None
    return tally, validator.metadata


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--variant", choices=("normal", "xip-psram"), required=True)
    parser.add_argument("--cells", default="all")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()
    try:
        contract = ManifestContract.load(args.manifest)
        tally, _ = validate_path(args.capture, contract, args.variant, args.cells)
    except (OSError, ValidationError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        return 2
    print(json.dumps({"ok": True, **tally.as_dict()}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
