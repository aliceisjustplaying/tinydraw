#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial==3.5"]
# ///
"""Capture one verified TinyDraw Tier-B image and emit a receipt sidecar."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import re
import time
from pathlib import Path
from typing import Any

import serial

from tier_b_ndjson import (
    DEFAULT_MANIFEST,
    CaptureValidator,
    ManifestContract,
    ValidationError,
)


READY = b"TINYDRAW_TIER_B_SELECT_READY"
FAIL_MARKERS = (
    b"Guru Meditation Error",
    b"Stack canary watchpoint triggered",
    b"assert failed:",
    b"abort() was called",
    b"TINYDRAW_TIER_B_FAILED",
)
BUILD_KEYS = {
    "ok",
    "fixture",
    "variant",
    "gitCommit",
    "gitDirty",
    "sdkconfigSha256",
    "compilerVersion",
    "elfSha256",
    "manifestSha256",
    "toolchain",
}
GIT_COMMIT = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def stamp(line: bytes) -> bytes:
    now = datetime.datetime.now().strftime("[%H:%M:%S] ").encode()
    return now + line.rstrip(b"\r") + b"\n"


def load_preflight(data: bytes, path: Path, variant: str, manifest_sha256: str) -> dict[str, Any]:
    try:
        payload = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot load ELF preflight {path}: {error}") from error
    if not isinstance(payload, dict) or BUILD_KEYS - payload.keys():
        raise ValidationError("ELF preflight is missing receipt fields")
    if payload["ok"] is not True:
        raise ValidationError("ELF preflight did not pass")
    if payload["fixture"] is not False:
        raise ValidationError("fixture ELF preflight cannot authorize capture")
    if payload["variant"] != variant:
        raise ValidationError(
            f"ELF preflight variant is {payload['variant']!r}, expected {variant!r}"
        )
    if payload["manifestSha256"] != manifest_sha256:
        raise ValidationError("ELF preflight does not match the committed manifest")
    if (
        not isinstance(payload["gitCommit"], str)
        or GIT_COMMIT.fullmatch(payload["gitCommit"]) is None
    ):
        raise ValidationError("ELF preflight gitCommit is not a full lowercase commit ID")
    if not isinstance(payload["gitDirty"], bool):
        raise ValidationError("ELF preflight gitDirty must be boolean")
    for key in ("sdkconfigSha256", "elfSha256", "manifestSha256"):
        if not isinstance(payload[key], str) or SHA256.fullmatch(payload[key]) is None:
            raise ValidationError(f"ELF preflight {key} is not a lowercase SHA-256")
    if not isinstance(payload["compilerVersion"], str) or not payload["compilerVersion"]:
        raise ValidationError("ELF preflight compilerVersion is missing")
    toolchain = payload["toolchain"]
    toolchain_keys = {"compiler", "compilerVersion", "objdump", "objdumpVersion"}
    if not isinstance(toolchain, dict) or set(toolchain) != toolchain_keys:
        raise ValidationError("ELF preflight toolchain is malformed")
    if any(not isinstance(toolchain[key], str) or not toolchain[key] for key in toolchain_keys):
        raise ValidationError("ELF preflight toolchain fields must be non-empty strings")
    if toolchain["compilerVersion"] != payload["compilerVersion"]:
        raise ValidationError("ELF preflight compiler versions disagree")
    return payload


def write_receipt(
    path: Path,
    capture: Path,
    manifest_sha256: str,
    preflight_sha256: str,
    archived_elf: Path,
    preflight: dict[str, Any],
    validator: CaptureValidator,
    tally: dict[str, int | bool],
) -> None:
    assert validator.metadata is not None
    payload = {
        "schemaVersion": 1,
        "suite": "tier-b",
        "capturedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "request": {"variant": validator.variant, "cells": validator.selected},
        "manifestSha256": manifest_sha256,
        "captureSha256": hashlib.sha256(capture.read_bytes()).hexdigest(),
        "preflightSha256": preflight_sha256,
        "archivedElf": str(archived_elf),
        "elfVerification": preflight,
        "runtimeMetadata": validator.metadata,
        "bootIdentity": validator.metadata["bootId"],
        "tally": tally,
    }
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("timeout_s", type=float)
    parser.add_argument("--variant", choices=("normal", "xip-psram"), required=True)
    parser.add_argument("--preflight", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument(
        "--archive-dir", type=Path, default=Path("~/Archives/esp32s3/tier-b").expanduser()
    )
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--cells", default="all", help="comma-separated cell IDs, or all")
    parser.add_argument("--tail-s", type=float, default=2.0)
    parser.add_argument("--no-reset", action="store_true")
    args = parser.parse_args()

    receipt = args.receipt or Path(str(args.output) + ".receipt.json")
    try:
        if args.output.resolve() == receipt.resolve():
            raise ValidationError("capture and receipt paths must differ")
        if args.output.exists() or receipt.exists():
            raise ValidationError("refusing to overwrite capture or receipt")
        manifest_data = args.manifest.read_bytes()
        manifest_sha256 = hashlib.sha256(manifest_data).hexdigest()
        contract = ManifestContract.from_bytes(manifest_data, str(args.manifest))
        preflight_data = args.preflight.read_bytes()
        preflight_sha256 = hashlib.sha256(preflight_data).hexdigest()
        preflight = load_preflight(
            preflight_data, args.preflight, args.variant, manifest_sha256
        )
        elf_data = args.elf.read_bytes()
        elf_sha256 = hashlib.sha256(elf_data).hexdigest()
        if elf_sha256 != preflight["elfSha256"]:
            raise ValidationError("capture ELF does not match the verified preflight")
        args.archive_dir.mkdir(parents=True, exist_ok=True)
        archived_elf = args.archive_dir / f"tier-b-{args.variant}-{elf_sha256}.elf"
        if archived_elf.exists():
            if archived_elf.read_bytes() != elf_data:
                raise ValidationError("archived ELF path contains different bytes")
        else:
            temporary_elf = archived_elf.with_name(archived_elf.name + ".tmp")
            temporary_elf.write_bytes(elf_data)
            temporary_elf.replace(archived_elf)
        validator = CaptureValidator(contract, args.variant, args.cells, preflight)
    except (OSError, ValidationError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, sort_keys=True))
        return 2

    validation_error: str | None = None
    hardware_failure = False
    selection_sent = False
    done_at: float | None = None
    line_number = 0
    buffer = b""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    device = serial.Serial(args.port, 115200, timeout=0.25)
    try:
        if not args.no_reset:
            device.dtr = False
            device.rts = True
            time.sleep(0.2)
            device.rts = False
        deadline = time.monotonic() + args.timeout_s
        with args.output.open("wb") as output:
            while time.monotonic() < deadline and validation_error is None:
                if done_at is not None and time.monotonic() >= done_at + args.tail_s:
                    break
                data = device.read(4096)
                if not data:
                    continue
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line_number += 1
                    output.write(stamp(line))
                    output.flush()
                    if any(marker in line for marker in FAIL_MARKERS):
                        hardware_failure = True
                    if not selection_sent and READY in line:
                        device.write(f"TIER_B_SELECT {args.cells}\n".encode())
                        device.flush()
                        selection_sent = True
                    try:
                        if validator.feed_line(line.decode(), line_number):
                            done_at = time.monotonic()
                    except (UnicodeDecodeError, ValidationError) as error:
                        validation_error = str(error)
                        break

            if buffer.strip():
                line_number += 1
                output.write(stamp(buffer))
                if any(marker in buffer for marker in FAIL_MARKERS):
                    hardware_failure = True
                try:
                    validator.feed_line(buffer.decode(), line_number)
                except (UnicodeDecodeError, ValidationError) as error:
                    validation_error = str(error)
    finally:
        device.close()

    if not selection_sent and validation_error is None:
        validation_error = "selection READY marker was not observed"
    if validation_error is None:
        try:
            tally = validator.finalize()
        except ValidationError as error:
            validation_error = str(error)

    if validation_error is not None or hardware_failure:
        print(
            json.dumps(
                {
                    "ok": False,
                    "output": str(args.output),
                    "error": validation_error,
                    "hardwareFailure": hardware_failure,
                },
                sort_keys=True,
            )
        )
        return 2

    tally_payload = tally.as_dict()
    write_receipt(
        receipt,
        args.output,
        manifest_sha256,
        preflight_sha256,
        archived_elf,
        preflight,
        validator,
        tally_payload,
    )
    print(
        json.dumps(
            {
                "ok": True,
                "output": str(args.output),
                "receipt": str(receipt),
                **tally_payload,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
