#!/usr/bin/env python3
"""Validate one manifest-bound TinyDraw Tier-B capture."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Any


PREFIX = "TINYDRAW_TIER_B_NDJSON "
PROTOCOL_VERSION = 2
REQUIRED_IDF_VERSION = "v6.1"
ATTRIBUTION_ITERATIONS = 128
DBUS_FLASH_CLASSIFIER_BYTES = 0x40000
DBUS_FLASH_CLASSIFIER_ALIGNMENT = 64
ATTRIBUTION_CHECKSUMS = {
    "internal": 0x00003280,
    "flash": 0xE5C43380,
    "psram": 0x00003180,
}
DEFAULT_MANIFEST = (
    Path(__file__).resolve().parents[1]
    / "calibration"
    / "esp32s3-tier-b"
    / "probe-cells.json"
)
SHA256 = re.compile(r"^[0-9a-f]{64}$")
PSRAM_SERVICE_BYTES = 4096
PSRAM_CLOCKS = (40_000_000, 80_000_000)
MSYNC_BYTES = (64, 1024, 32768)
SPI2_CLOCKS = (20_000_000, 40_000_000)
SPI2_BYTES = (64, 4096, 32768)


class ValidationError(ValueError):
    """A malformed, refused, or incomplete Tier-B capture."""


def expected_aggressor_checksum(source: str, iterations: int) -> int:
    checksum = 0
    for iteration in range(iterations):
        offset = iteration * 64
        if source == "internal":
            value = ((offset & (64 * 1024 - 1)) * 29 + 5) & 0xFF
        elif source == "flash":
            index = (offset >> 2) & (64 * 1024 - 1)
            value = (index * 2_246_822_519 + 31) & 0xFFFFFFFF
        elif source == "psram":
            index = 512 * 1024 + (offset & (512 * 1024 - 1))
            value = (index * 17 + 3) & 0xFF
        else:
            raise ValidationError(f"unknown attribution source {source!r}")
        checksum = (checksum + value) & 0xFFFFFFFF
    return checksum


def _expected_attribution_source(cell: str) -> str | None:
    if cell.endswith("_cross_core") or "psram_aggressor" in cell:
        return "psram"
    if "flash_aggressor" in cell:
        return "flash"
    if cell.startswith("arbitration_"):
        return "internal"
    return None


def _validate_attribution_counters(
    source: str, counters: dict[str, int], path: str
) -> None:
    if source == "internal" and (
        counters["dbusAccesses"] != 0
        or counters["dbusFlashMisses"] != 0
        or counters["dbusPsramMisses"] != 0
    ):
        raise ValidationError(f"{path} internal attribution has external data-cache traffic")
    if source == "flash" and (
        counters["dbusAccesses"] == 0
        or counters["dbusFlashMisses"] == 0
        or counters["dbusPsramMisses"] != 0
    ):
        raise ValidationError(f"{path} lacks exclusive isolated flash attribution")
    if source == "psram" and (
        counters["dbusAccesses"] == 0
        or counters["dbusPsramMisses"] == 0
        or counters["dbusFlashMisses"] != 0
    ):
        raise ValidationError(f"{path} lacks exclusive isolated PSRAM attribution")


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValidationError(f"{path} must be an object")
    return value


def _dbus_flash_classifier(value: Any, path: str) -> dict[str, int]:
    classifier = _object(value, path)
    _exact_keys(classifier, path, {"start", "end"})
    start = _integer(classifier["start"], f"{path}.start")
    end = _integer(classifier["end"], f"{path}.end")
    if start == 0:
        raise ValidationError(f"{path}.start must not be the reset default")
    if start % DBUS_FLASH_CLASSIFIER_ALIGNMENT != 0:
        raise ValidationError(f"{path}.start is not 64-byte aligned")
    if end - start + 1 != DBUS_FLASH_CLASSIFIER_BYTES:
        raise ValidationError(f"{path} does not span the exact flash pool")
    return {"start": start, "end": end}


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
    family: str = ""
    factors: tuple[tuple[str, int], ...] = ()

    def factor_map(self) -> dict[str, int]:
        return dict(self.factors)


def _matrix_rank(rows: list[list[int]]) -> int:
    matrix = [[Fraction(value) for value in row] for row in rows]
    rank = 0
    columns = len(matrix[0]) if matrix else 0
    for column in range(columns):
        pivot = next(
            (row for row in range(rank, len(matrix)) if matrix[row][column] != 0),
            None,
        )
        if pivot is None:
            continue
        matrix[rank], matrix[pivot] = matrix[pivot], matrix[rank]
        divisor = matrix[rank][column]
        matrix[rank] = [value / divisor for value in matrix[rank]]
        for row in range(len(matrix)):
            if row == rank or matrix[row][column] == 0:
                continue
            scale = matrix[row][column]
            matrix[row] = [
                value - scale * pivot_value
                for value, pivot_value in zip(matrix[row], matrix[rank], strict=True)
            ]
        rank += 1
    return rank


def design_ranks(cells: tuple[CellContract, ...]) -> dict[str, int]:
    msync = [cell for cell in cells if cell.family == "msync-decomposition"]
    spi2 = [cell for cell in cells if cell.family == "spi2-decomposition"]
    ranks: dict[str, int] = {}
    if msync:
        if any(
            set(cell.factor_map()) != {"bytes", "dirtyLines", "psramClockHz"}
            for cell in msync
        ):
            raise ValidationError("manifest msync decomposition factors have unexpected keys")
        expected = {
            (byte_count, dirty_lines, clock_hz)
            for byte_count in MSYNC_BYTES
            for dirty_lines in (0, byte_count // 64)
            for clock_hz in PSRAM_CLOCKS
        }
        actual = {
            (
                cell.factor_map().get("bytes"),
                cell.factor_map().get("dirtyLines"),
                cell.factor_map().get("psramClockHz"),
            )
            for cell in msync
        }
        if actual != expected or len(msync) != len(expected):
            raise ValidationError("manifest msync decomposition factors are incomplete or duplicated")
        if any(cell.samples != 9 or set(cell.variants) != {"normal", "xip-psram"} for cell in msync):
            raise ValidationError("manifest msync decomposition provenance is inconsistent")
        rows = []
        for byte_count, dirty_lines, clock_hz in sorted(expected):
            lines = byte_count // 64
            slow = int(clock_hz == PSRAM_CLOCKS[0])
            rows.append([1, lines, dirty_lines, slow, lines * slow, dirty_lines * slow])
        ranks["msync-decomposition"] = _matrix_rank(rows)
        if ranks["msync-decomposition"] != 6:
            raise ValidationError("manifest msync decomposition design is rank deficient")
    if spi2:
        if any(set(cell.factor_map()) != {"bytes", "spiClockHz"} for cell in spi2):
            raise ValidationError("manifest SPI2 decomposition factors have unexpected keys")
        expected = {
            (byte_count, clock_hz)
            for byte_count in SPI2_BYTES
            for clock_hz in SPI2_CLOCKS
        }
        actual = {
            (cell.factor_map().get("bytes"), cell.factor_map().get("spiClockHz"))
            for cell in spi2
        }
        if actual != expected or len(spi2) != len(expected):
            raise ValidationError("manifest SPI2 decomposition factors are incomplete or duplicated")
        if any(cell.samples != 9 or set(cell.variants) != {"normal", "xip-psram"} for cell in spi2):
            raise ValidationError("manifest SPI2 decomposition provenance is inconsistent")
        rows = []
        for byte_count, clock_hz in sorted(expected):
            slow = int(clock_hz == SPI2_CLOCKS[0])
            rows.append([1, byte_count, slow, byte_count * slow, 0, 0, 0, 0])
            rows.append([0, 0, 0, 0, 1, byte_count, slow, byte_count * slow])
        ranks["spi2-decomposition"] = _matrix_rank(rows)
        if ranks["spi2-decomposition"] != 8:
            raise ValidationError("manifest SPI2 decomposition design is rank deficient")
    return ranks


def expected_psram_clock_register(clock_hz: int) -> int:
    if clock_hz not in PSRAM_CLOCKS:
        raise ValidationError(f"unsupported PSRAM clock {clock_hz}")
    divider = 160_000_000 // clock_hz
    return ((divider - 1) << 16) | ((divider // 2 - 1) << 8) | (divider - 1)


@dataclass(frozen=True)
class ManifestContract:
    protocol_version: int
    harness_version: str
    chip_model: str
    chip_revision: int
    cells: tuple[CellContract, ...]
    manifest_sha256: str = ""

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
            _exact_keys(
                cell,
                path_name,
                {"id", "family", "samples", "variants"},
                {"status", "factors"},
            )
            family = _string(cell["family"], f"{path_name}.family")
            if "status" in cell:
                status = _string(cell["status"], f"{path_name}.status")
            else:
                status = "ready"
            if status not in {"ready", "open-refusal"}:
                raise ValidationError(f"{path_name}.status has unsupported value {status!r}")
            variants = tuple(_string_list(cell["variants"], f"{path_name}.variants"))
            if not variants or set(variants) - {"normal", "xip-psram"}:
                raise ValidationError(f"{path_name}.variants contains an unsupported variant")
            factors: tuple[tuple[str, int], ...] = ()
            if "factors" in cell:
                raw_factors = _object(cell["factors"], f"{path_name}.factors")
                if not raw_factors:
                    raise ValidationError(f"{path_name}.factors must not be empty")
                factors = tuple(
                    sorted(
                        (
                            _string(key, f"{path_name}.factors key"),
                            _integer(value, f"{path_name}.factors.{key}"),
                        )
                        for key, value in raw_factors.items()
                    )
                )
            cells.append(
                CellContract(
                    id=_string(cell["id"], f"{path_name}.id"),
                    samples=_integer(cell["samples"], f"{path_name}.samples", 1),
                    variants=variants,
                    status=status,
                    family=family,
                    factors=factors,
                )
            )
        ids = [cell.id for cell in cells]
        if len(ids) != len(set(ids)):
            raise ValidationError("manifest.cells contains duplicate IDs")
        contract = cls(
            protocol_version=protocol_version,
            harness_version=_string(payload["harnessVersion"], "manifest.harnessVersion"),
            chip_model=_string(payload["chipModel"], "manifest.chipModel"),
            chip_revision=_integer(payload["chipRevision"], "manifest.chipRevision"),
            cells=tuple(cells),
            manifest_sha256=hashlib.sha256(data).hexdigest(),
        )
        design_ranks(contract.cells)
        return contract

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
        self.selected_contracts = {cell.id: cell for cell in selected}
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
                "spiramRodata",
                "sdkconfigSha256",
                "manifestSha256",
                "compilerVersion",
                "elfSha256",
                "dbusFlashClassifier",
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
        spiram_rodata = _boolean(record["spiramRodata"], f"{path}.spiramRodata")
        if spiram_rodata:
            raise ValidationError(f"{path}.spiramRodata must be false")
        _string(record["gitCommit"], f"{path}.gitCommit")
        _boolean(record["gitDirty"], f"{path}.gitDirty")
        _sha256(record["sdkconfigSha256"], f"{path}.sdkconfigSha256")
        manifest_sha256 = _sha256(record["manifestSha256"], f"{path}.manifestSha256")
        if self.contract.manifest_sha256 and manifest_sha256 != self.contract.manifest_sha256:
            raise ValidationError(f"{path}.manifestSha256 does not match the committed manifest")
        _string(record["compilerVersion"], f"{path}.compilerVersion")
        _sha256(record["elfSha256"], f"{path}.elfSha256")
        classifier = _dbus_flash_classifier(
            record["dbusFlashClassifier"], f"{path}.dbusFlashClassifier"
        )
        _integer(record["resetReason"], f"{path}.resetReason")
        _string(record["bootId"], f"{path}.bootId")
        if self.expected_build is not None:
            for key in (
                "idfVersion",
                "gitCommit",
                "gitDirty",
                "variant",
                "spiramRodata",
                "sdkconfigSha256",
                "manifestSha256",
                "compilerVersion",
                "elfSha256",
            ):
                if record[key] != self.expected_build.get(key):
                    raise ValidationError(
                        f"{path}.{key} does not match the verified ELF preflight"
                    )
            expected_classifier = self.expected_build.get("dbusFlashClassifier")
            if not isinstance(expected_classifier, dict) or classifier != {
                "start": expected_classifier.get("start"),
                "end": expected_classifier.get("end"),
            }:
                raise ValidationError(
                    f"{path}.dbusFlashClassifier does not match the verified ELF preflight"
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
                "attributionSource",
                "isolatedAttributionIterations",
                "isolatedAttributionChecksum",
                "isolatedAttributionCounters",
                "aggressorIterations",
                "aggressorChecksum",
                "dirtyLines",
                "psramClockHz",
                "psramClockRegister",
                "psramCoreClockRegister",
                "psramServiceBytes",
                "psramServiceCycles",
                "psramServiceCounters",
                "spiClockHz",
                "submissionCycles",
                "completionCycles",
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
        cycles = _integer(record["cycles"], f"{path}.cycles", 1)
        byte_count = _integer(record["bytes"], f"{path}.bytes")
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
        aggressor_iterations = None
        if "aggressorIterations" in record:
            aggressor_iterations = _integer(
                record["aggressorIterations"], f"{path}.aggressorIterations", 1
            )
        aggressor_checksum = None
        if "aggressorChecksum" in record:
            aggressor_checksum = _integer(
                record["aggressorChecksum"], f"{path}.aggressorChecksum"
            )
        attribution_counters = None
        if "isolatedAttributionCounters" in record:
            attribution_counters = self._cache_counters(
                record["isolatedAttributionCounters"],
                f"{path}.isolatedAttributionCounters",
            )
        attribution_source = record.get("attributionSource")
        if attribution_source is not None:
            attribution_source = _string(attribution_source, f"{path}.attributionSource")
            if attribution_source not in ATTRIBUTION_CHECKSUMS:
                raise ValidationError(f"{path}.attributionSource is unknown")
        if "isolatedAttributionIterations" in record:
            isolated_iterations = _integer(
                record["isolatedAttributionIterations"],
                f"{path}.isolatedAttributionIterations",
                1,
            )
            if isolated_iterations != ATTRIBUTION_ITERATIONS:
                raise ValidationError(
                    f"{path}.isolatedAttributionIterations must be {ATTRIBUTION_ITERATIONS}"
                )
        if "isolatedAttributionChecksum" in record:
            isolated_checksum = _integer(
                record["isolatedAttributionChecksum"],
                f"{path}.isolatedAttributionChecksum",
            )
            if (
                attribution_source is not None
                and isolated_checksum != ATTRIBUTION_CHECKSUMS[attribution_source]
            ):
                raise ValidationError(f"{path} isolated attribution checksum mismatch")
        if "note" in record:
            _string(record["note"], f"{path}.note")
        contract = self.selected_contracts[cell]
        factors = contract.factor_map()
        msync_fields = {
            "dirtyLines",
            "psramClockHz",
            "psramClockRegister",
            "psramCoreClockRegister",
            "psramServiceBytes",
            "psramServiceCycles",
            "psramServiceCounters",
        }
        spi2_fields = {"spiClockHz", "submissionCycles", "completionCycles"}
        if contract.family == "msync-decomposition":
            if msync_fields - record.keys():
                raise ValidationError(f"{path} lacks cache-msync decomposition evidence")
            if spi2_fields & record.keys():
                raise ValidationError(f"{path} mixes SPI2 phases into cache-msync evidence")
            expected_bytes = factors["bytes"]
            expected_dirty = factors["dirtyLines"]
            expected_clock = factors["psramClockHz"]
            if (
                byte_count != expected_bytes
                or _integer(record["dirtyLines"], f"{path}.dirtyLines") != expected_dirty
                or _integer(record["psramClockHz"], f"{path}.psramClockHz") != expected_clock
            ):
                raise ValidationError(f"{path} does not match its manifest msync factors")
            clock_register = _integer(
                record["psramClockRegister"], f"{path}.psramClockRegister"
            )
            if clock_register != expected_psram_clock_register(expected_clock):
                raise ValidationError(f"{path} PSRAM clock register does not match its factor")
            if (
                _integer(
                    record["psramCoreClockRegister"],
                    f"{path}.psramCoreClockRegister",
                )
                != 2
            ):
                raise ValidationError(f"{path} PSRAM core clock register is not 160 MHz")
            if (
                _integer(record["psramServiceBytes"], f"{path}.psramServiceBytes")
                != PSRAM_SERVICE_BYTES
            ):
                raise ValidationError(f"{path} PSRAM service control has the wrong byte count")
            _integer(record["psramServiceCycles"], f"{path}.psramServiceCycles", 1)
            service = self._cache_counters(
                record["psramServiceCounters"], f"{path}.psramServiceCounters"
            )
            if (
                service["dbusAccesses"] == 0
                or service["dbusPsramMisses"] == 0
                or service["dbusFlashMisses"] != 0
            ):
                raise ValidationError(f"{path} lacks exclusive PSRAM service evidence")
        elif contract.family == "spi2-decomposition":
            if spi2_fields - record.keys():
                raise ValidationError(f"{path} lacks separate SPI2 phase timing")
            if msync_fields & record.keys():
                raise ValidationError(f"{path} mixes cache-msync evidence into SPI2 phases")
            if (
                byte_count != factors["bytes"]
                or _integer(record["spiClockHz"], f"{path}.spiClockHz")
                != factors["spiClockHz"]
            ):
                raise ValidationError(f"{path} does not match its manifest SPI2 factors")
            submission = _integer(
                record["submissionCycles"], f"{path}.submissionCycles", 1
            )
            completion = _integer(
                record["completionCycles"], f"{path}.completionCycles", 1
            )
            if submission + completion != cycles:
                raise ValidationError(f"{path} SPI2 phases do not reconcile to total cycles")
        elif (msync_fields | spi2_fields) & record.keys():
            raise ValidationError(f"{path} carries decomposition evidence for a canonical cell")
        needs_contention = cell.startswith("arbitration_") or cell.endswith("_cross_core")
        if needs_contention and (
            baseline_counters is None
            or "aggressorIterations" not in record
            or "aggressorChecksum" not in record
            or attribution_source is None
            or "isolatedAttributionIterations" not in record
            or "isolatedAttributionChecksum" not in record
            or attribution_counters is None
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
        if cell in {"instruction_psram_cold", "first_line_i_flash"}:
            if counters["ibusAccesses"] == 0:
                raise ValidationError(f"{path} lacks instruction-cache accesses")
            if counters["ibusMisses"] == 0:
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
        if needs_contention:
            expected_source = _expected_attribution_source(cell)
            assert expected_source is not None
            if attribution_source != expected_source:
                raise ValidationError(
                    f"{path}.attributionSource is {attribution_source!r}, expected {expected_source!r}"
                )
            assert attribution_counters is not None
            assert aggressor_iterations is not None and aggressor_checksum is not None
            if aggressor_checksum != expected_aggressor_checksum(
                expected_source, aggressor_iterations
            ):
                raise ValidationError(f"{path} aggressor runtime checksum mismatch")
            _validate_attribution_counters(expected_source, attribution_counters, path)
        self.samples[cell] += 1

    def _refusal(self, record: dict[str, Any], path: str) -> None:
        self._require_metadata(path)
        diagnostic_keys = {
            "attributionSource",
            "isolatedAttributionIterations",
            "isolatedAttributionChecksum",
            "isolatedAttributionCounters",
        }
        runtime_keys = {"aggressorIterations", "aggressorChecksum"}
        _exact_keys(
            record,
            path,
            {"protocolVersion", "record", "cell", "ordinal", "reason", "tierCandidate"},
            diagnostic_keys | runtime_keys,
        )
        cell = _string(record["cell"], f"{path}.cell")
        ordinal = _integer(record["ordinal"], f"{path}.ordinal")
        reason = _string(record["reason"], f"{path}.reason")
        tier = _string(record["tierCandidate"], f"{path}.tierCandidate")
        present_diagnostics = diagnostic_keys & record.keys()
        present_runtime = runtime_keys & record.keys()
        if present_diagnostics and present_diagnostics != diagnostic_keys:
            raise ValidationError(f"{path} has incomplete isolated attribution diagnostics")
        if present_runtime and present_runtime != runtime_keys:
            raise ValidationError(f"{path} aggressor runtime fields must appear together")
        attribution_counters = None
        if present_diagnostics:
            source = _string(record["attributionSource"], f"{path}.attributionSource")
            if source not in ATTRIBUTION_CHECKSUMS:
                raise ValidationError(f"{path}.attributionSource is unknown")
            expected_source = _expected_attribution_source(cell)
            if expected_source is not None and source != expected_source:
                raise ValidationError(
                    f"{path}.attributionSource is {source!r}, expected {expected_source!r}"
                )
            iterations = _integer(
                record["isolatedAttributionIterations"],
                f"{path}.isolatedAttributionIterations",
                1,
            )
            checksum = _integer(
                record["isolatedAttributionChecksum"],
                f"{path}.isolatedAttributionChecksum",
            )
            attribution_counters = self._cache_counters(
                record["isolatedAttributionCounters"],
                f"{path}.isolatedAttributionCounters",
            )
            if iterations != ATTRIBUTION_ITERATIONS or checksum != ATTRIBUTION_CHECKSUMS[source]:
                raise ValidationError(f"{path} has invalid isolated attribution diagnostics")
        if present_runtime:
            if not present_diagnostics:
                raise ValidationError(f"{path} runtime evidence lacks attribution diagnostics")
            assert attribution_counters is not None
            _validate_attribution_counters(source, attribution_counters, path)
            runtime_iterations = _integer(
                record["aggressorIterations"], f"{path}.aggressorIterations", 1
            )
            runtime_checksum = _integer(
                record["aggressorChecksum"], f"{path}.aggressorChecksum"
            )
            if runtime_checksum != expected_aggressor_checksum(source, runtime_iterations):
                raise ValidationError(f"{path} aggressor runtime checksum mismatch")
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
