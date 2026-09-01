#!/usr/bin/env python3
"""Verify core-timing probe encodings and loop residues from an objdump."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


FUNCTION = re.compile(r"^([0-9a-fA-F]+) <([^>]+)>:$")
INSTRUCTION = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]+)\s+([a-zA-Z0-9_.]+)(?:\s+(.*))?$"
)


class VerificationError(ValueError):
    pass


@dataclass(frozen=True)
class Instruction:
    address: int
    encoding: str
    mnemonic: str
    operands: str


def parse_disassembly(text: str) -> dict[str, list[Instruction]]:
    functions: dict[str, list[Instruction]] = {}
    current: list[Instruction] | None = None
    for line in text.splitlines():
        function = FUNCTION.match(line.strip())
        if function:
            current = []
            functions[function.group(2)] = current
            continue
        instruction = INSTRUCTION.match(line)
        if instruction and current is not None:
            current.append(
                Instruction(
                    address=int(instruction.group(1), 16),
                    encoding=instruction.group(2).lower(),
                    mnemonic=instruction.group(3),
                    operands=(instruction.group(4) or "").strip(),
                )
            )
    return functions


def require_function(
    functions: dict[str, list[Instruction]], name: str
) -> list[Instruction]:
    instructions = functions.get(name)
    if not instructions:
        raise VerificationError(f"missing disassembly for {name}")
    return instructions


def verify_issue_block(
    functions: dict[str, list[Instruction]], name: str, body: list[str]
) -> dict[str, object]:
    instructions = require_function(functions, name)
    try:
        return_index = next(
            index for index, instruction in enumerate(instructions) if instruction.encoding == "f01d"
        )
    except StopIteration as error:
        raise VerificationError(f"{name} has no retw.n endpoint") from error
    instructions = instructions[: return_index + 1]
    encodings = [instruction.encoding for instruction in instructions]
    expected = ["004136", *body, "f01d"]
    if encodings != expected:
        mismatch = next(
            (
                index
                for index, pair in enumerate(zip(encodings, expected))
                if pair[0] != pair[1]
            ),
            min(len(encodings), len(expected)),
        )
        raise VerificationError(
            f"{name} encoding mismatch at instruction {mismatch}: "
            f"got {encodings[mismatch:mismatch + 1]}, expected {expected[mismatch:mismatch + 1]}; "
            f"got {len(encodings)} instructions, expected {len(expected)}"
        )
    return {
        "symbol": name,
        "address": instructions[0].address,
        "operations": len(body),
        "bodySha256": hashlib.sha256(bytes.fromhex("".join(body))).hexdigest(),
    }


def verify_loop(
    functions: dict[str, list[Instruction]], symbol: str, expected_residue: int
) -> dict[str, int | str]:
    instructions = require_function(functions, symbol)
    loop_index = next(
        (index for index, instruction in enumerate(instructions) if instruction.mnemonic == "loopnez"),
        None,
    )
    if loop_index is None or loop_index + 9 >= len(instructions):
        raise VerificationError(f"{symbol} has no complete loopnez body")
    body = instructions[loop_index + 1 : loop_index + 9]
    if [instruction.encoding for instruction in body] != ["f03d"] * 8:
        raise VerificationError(f"{symbol} loop body is not eight exact nop.n encodings")
    target = instructions[loop_index + 9]
    if target.encoding != "f01d":
        raise VerificationError(f"{symbol} loop target is not retw.n")
    body_address = body[0].address
    residue = body_address % 4
    if residue != expected_residue:
        raise VerificationError(
            f"{symbol} body residue is {residue}, expected {expected_residue}"
        )
    return {"symbol": symbol, "bodyAddress": body_address, "residueMod4": residue}


def verify(disassembly: str) -> dict[str, object]:
    functions = parse_disassembly(disassembly)
    issue_blocks = [
        verify_issue_block(functions, "issue_narrow_block", ["f03d"] * 256),
        verify_issue_block(functions, "issue_wide_block", ["208880"] * 256),
        verify_issue_block(functions, "issue_mixed_block", ["f03d", "208880"] * 128),
        verify_issue_block(functions, "issue_dependent_block", ["01c882"] * 256),
        verify_issue_block(
            functions,
            "issue_independent_block",
            ["01c882", "01c992", "01caa2", "01cbb2"] * 64,
        ),
    ]
    loops = [
        verify_loop(functions, "loop_body_r0", 0),
        verify_loop(functions, "loop_body_r1", 1),
        verify_loop(functions, "loop_body_r2", 2),
        verify_loop(functions, "loop_body_r3", 3),
    ]
    return {"ok": True, "issueBlocks": issue_blocks, "loopBodies": loops}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("result", type=Path)
    parser.add_argument("--objdump", default="xtensa-esp32s3-elf-objdump")
    parser.add_argument("--disassembly", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.result.exists():
        print(f"refusing to overwrite result: {args.result}", file=sys.stderr)
        return 2
    try:
        if args.disassembly is not None:
            disassembly = args.disassembly.read_text()
        else:
            disassembly = subprocess.run(
                [args.objdump, "-d", str(args.elf)],
                check=True,
                text=True,
                capture_output=True,
            ).stdout
        result = verify(disassembly)
        result["elf"] = str(args.elf)
        result["elfSha256"] = hashlib.sha256(args.elf.read_bytes()).hexdigest()
    except (OSError, subprocess.CalledProcessError, VerificationError) as error:
        print(f"ELF verification failed: {error}", file=sys.stderr)
        return 2
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"ELF verification passed: {args.result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
