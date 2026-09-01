#!/usr/bin/env python3
"""Fail-closed ELF preflight for a TinyDraw Tier-B capture."""

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
LADDER_LINES = (1, 2, 4, 8, 16)
FLASH_POOL_BYTES = 0x40000
FLASH_POOL_ALIGNMENT = 64
REPO = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).resolve().with_name("probe-cells.json")
REQUIRED_IDF_VERSION = "v6.1"


class VerificationError(ValueError):
    pass


@dataclass(frozen=True)
class Instruction:
    address: int
    encoding: str
    mnemonic: str


@dataclass(frozen=True)
class Symbol:
    address: int
    section: str
    size: int


@dataclass(frozen=True)
class Section:
    address: int
    size: int


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
                )
            )
    return functions


def parse_symbols(text: str) -> dict[str, Symbol]:
    symbols: dict[str, Symbol] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 4 or re.fullmatch(r"[0-9a-fA-F]+", parts[0]) is None:
            continue
        section = next(
            (part for part in parts[1:-1] if part.startswith(".") or part == "*ABS*"), None
        )
        if section is not None:
            try:
                size = int(parts[-2], 16)
            except ValueError:
                continue
            symbols[parts[-1]] = Symbol(int(parts[0], 16), section, size)
    return symbols


def parse_sections(text: str) -> dict[str, Section]:
    sections: dict[str, Section] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 4 or not parts[0].isdigit() or not parts[1].startswith("."):
            continue
        try:
            size = int(parts[2], 16)
            address = int(parts[3], 16)
        except ValueError:
            continue
        sections[parts[1]] = Section(address, size)
    return sections


def require_function(
    functions: dict[str, list[Instruction]], name: str
) -> list[Instruction]:
    instructions = functions.get(name)
    if not instructions:
        raise VerificationError(f"missing disassembly for {name}")
    return instructions


def require_symbol(symbols: dict[str, Symbol], name: str) -> Symbol:
    symbol = symbols.get(name)
    if symbol is None:
        raise VerificationError(f"missing ELF symbol {name}")
    return symbol


def verify_flash_pool(
    symbols: dict[str, Symbol], sections: dict[str, Section]
) -> dict[str, int | str | bool]:
    matches = [
        (name, symbol)
        for name, symbol in symbols.items()
        if name == "g_flash_pool" or name.endswith("g_flash_poolE")
    ]
    if len(matches) != 1:
        raise VerificationError(f"expected one g_flash_pool ELF symbol, found {len(matches)}")
    symbol_name, pool = matches[0]
    if pool.section != ".flash.rodata":
        raise VerificationError("g_flash_pool is not linked in .flash.rodata")
    if pool.size != FLASH_POOL_BYTES:
        raise VerificationError(
            f"g_flash_pool is {pool.size:#x} bytes, expected {FLASH_POOL_BYTES:#x}"
        )
    if pool.address % FLASH_POOL_ALIGNMENT != 0:
        raise VerificationError("g_flash_pool is not 64-byte aligned")
    section = sections.get(".flash.rodata")
    if section is None:
        raise VerificationError("ELF has no .flash.rodata section header")
    pool_end = pool.address + pool.size
    section_end = section.address + section.size
    if not section.address <= pool.address < pool_end <= section_end:
        raise VerificationError("g_flash_pool falls outside .flash.rodata")
    return {
        "symbol": symbol_name,
        "section": pool.section,
        "storage": "flash-rodata",
        "xipPsram": False,
        "start": pool.address,
        "end": pool_end - 1,
        "sizeBytes": pool.size,
        "alignmentBytes": FLASH_POOL_ALIGNMENT,
    }


def through_return(instructions: list[Instruction], symbol: str) -> list[Instruction]:
    try:
        end = next(index for index, instruction in enumerate(instructions) if instruction.encoding == "f00d")
    except StopIteration as error:
        raise VerificationError(f"{symbol} has no exact ret.n endpoint") from error
    return instructions[: end + 1]


def verify_ladders(
    functions: dict[str, list[Instruction]], symbols: dict[str, Symbol]
) -> list[dict[str, int | str]]:
    results: list[dict[str, int | str]] = []
    for lines in LADDER_LINES:
        name = f"tier_b_instruction_{lines}_lines"
        start = require_symbol(symbols, name + "_start")
        end = require_symbol(symbols, name + "_end")
        expected_bytes = lines * 32
        if start.section != ".flash.text" or end.section != ".flash.text":
            raise VerificationError(f"{name} is not wholly linked in .flash.text")
        if end.address - start.address != expected_bytes:
            raise VerificationError(
                f"{name} spans {end.address - start.address} bytes, expected {expected_bytes}"
            )
        if start.address % 32 != 0 or end.address % 32 != 0:
            raise VerificationError(f"{name} boundaries are not cache-line aligned")
        instructions = through_return(require_function(functions, name), name)
        if instructions[0].address != start.address:
            raise VerificationError(f"{name} disassembly does not start at its ELF symbol")
        expected = ["f03d"] * ((expected_bytes - 2) // 2) + ["f00d"]
        if [instruction.encoding for instruction in instructions] != expected:
            raise VerificationError(f"{name} does not contain exact nop.n ladder bytes")
        results.append(
            {
                "symbol": name,
                "spanBytes": expected_bytes,
                "startResidueMod32": start.address % 32,
                "endResidueMod32": end.address % 32,
            }
        )
    return results


def verify_issue_blocks(
    functions: dict[str, list[Instruction]], symbols: dict[str, Symbol]
) -> list[dict[str, int | str]]:
    name = "tier_b_store_issue_block"
    start = require_symbol(symbols, name + "_start")
    end = require_symbol(symbols, name + "_end")
    if start.section != ".iram0.text" or end.section != ".iram0.text":
        raise VerificationError(f"{name} is not wholly linked in internal instruction RAM")
    instructions = through_return(require_function(functions, name), name)
    if instructions[0].address != start.address:
        raise VerificationError(f"{name} disassembly does not start at its ELF symbol")
    expected = ["0239"] * 256 + ["0020c0", "f00d"]
    if [instruction.encoding for instruction in instructions] != expected:
        raise VerificationError(f"{name} does not contain 256 exact s32i.n encodings")
    if end.address - start.address != 517 or start.address % 32 != 0:
        raise VerificationError(f"{name} span or alignment is wrong")
    return [
        {
            "symbol": name,
            "operations": 256,
            "encoding": "0239",
            "startResidueMod32": start.address % 32,
            "spanBytes": end.address - start.address,
            "completionBarrierEncoding": "0020c0",
        }
    ]


def verify_first_line_pool(
    functions: dict[str, list[Instruction]], symbols: dict[str, Symbol]
) -> list[dict[str, int | str]]:
    results: list[dict[str, int | str]] = []
    expected = ["f03d"] * 15 + ["f00d"]
    for index in range(5):
        name = f"tier_b_first_line_i_{index}"
        symbol = require_symbol(symbols, name)
        if symbol.section != ".flash.text" or symbol.address % 32 != 0:
            raise VerificationError(f"{name} is not an aligned flash-text target")
        instructions = through_return(require_function(functions, name), name)
        if [instruction.encoding for instruction in instructions] != expected:
            raise VerificationError(f"{name} is not an exact one-line instruction target")
        results.append({"symbol": name, "spanBytes": 32, "startResidueMod32": 0})
    return results


def verify_placement(
    variant: str, sdkconfig: str, symbols: dict[str, Symbol]
) -> dict[str, int | str]:
    xip_enabled = "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y" in sdkconfig
    if (variant == "xip-psram") != xip_enabled:
        raise VerificationError(f"sdkconfig does not match {variant} variant")
    if "CONFIG_APP_RETRIEVE_LEN_ELF_SHA=64" not in sdkconfig:
        raise VerificationError("sdkconfig does not retain the full runtime ELF SHA-256")
    reserved_start = require_symbol(symbols, "_instruction_reserved_start").address
    reserved_end = require_symbol(symbols, "_instruction_reserved_end").address
    for lines in LADDER_LINES:
        start = require_symbol(symbols, f"tier_b_instruction_{lines}_lines_start").address
        end = require_symbol(symbols, f"tier_b_instruction_{lines}_lines_end").address
        if not reserved_start <= start < end <= reserved_end:
            raise VerificationError("instruction ladder falls outside the reserved instruction range")
    if variant == "xip-psram":
        require_symbol(symbols, "instruction_in_spiram")
        require_symbol(symbols, "instruction_flash2spiram_offset")
        require_symbol(symbols, "mmu_psram_check_ptr_addr_in_xip_psram_instruction_region")
        runtime = "reserved flash-text range copied and MMU-mapped from PSRAM"
    else:
        if "instruction_in_spiram" in symbols:
            raise VerificationError("normal ELF unexpectedly includes instruction-in-PSRAM state")
        runtime = "flash-mapped instruction range"
    return {
        "runtimePlacement": runtime,
        "reservedStart": reserved_start,
        "reservedEnd": reserved_end,
    }


def run(command: list[str]) -> str:
    return subprocess.run(command, check=True, text=True, capture_output=True).stdout


def git_metadata() -> tuple[str, bool]:
    commit = run(["git", "-C", str(REPO), "rev-parse", "HEAD"]).strip()
    dirty = bool(
        run(["git", "-C", str(REPO), "status", "--porcelain", "--untracked-files=normal"])
    )
    return commit, dirty


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("sdkconfig", type=Path)
    parser.add_argument("result", type=Path)
    parser.add_argument("--variant", choices=("normal", "xip-psram"), required=True)
    parser.add_argument("--objdump", default="xtensa-esp32s3-elf-objdump")
    parser.add_argument("--compiler")
    parser.add_argument("--disassembly", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--symbols", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--sections", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--compiler-version", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.result.exists():
        print(f"refusing to overwrite result: {args.result}", file=sys.stderr)
        return 2
    try:
        fixtures = (args.disassembly, args.symbols, args.sections)
        if any(value is not None for value in fixtures) and not all(
            value is not None for value in fixtures
        ):
            raise VerificationError(
                "disassembly, symbol, and section fixtures must be supplied together"
            )
        if args.disassembly is not None:
            disassembly = args.disassembly.read_text()
            symbol_text = args.symbols.read_text()
            section_text = args.sections.read_text()
        else:
            disassembly = run([args.objdump, "-d", str(args.elf)])
            symbol_text = run([args.objdump, "-t", str(args.elf)])
            section_text = run([args.objdump, "-h", str(args.elf)])
        functions = parse_disassembly(disassembly)
        symbols = parse_symbols(symbol_text)
        sections = parse_sections(section_text)
        ladders = verify_ladders(functions, symbols)
        issue_blocks = verify_issue_blocks(functions, symbols)
        first_line_pool = verify_first_line_pool(functions, symbols)
        flash_classifier = verify_flash_pool(symbols, sections)
        sdkconfig = args.sdkconfig.read_text()
        placement = verify_placement(args.variant, sdkconfig, symbols)
        compiler = args.compiler or str(Path(args.objdump).with_name("xtensa-esp32s3-elf-gcc"))
        compiler_version = args.compiler_version or run(
            [compiler, "-dumpfullversion", "-dumpversion"]
        ).strip()
        objdump_version = (
            "fixture"
            if args.disassembly is not None
            else run([args.objdump, "--version"]).splitlines()[0]
        )
        commit, dirty = git_metadata()
        result = {
            "ok": True,
            "fixture": args.disassembly is not None,
            "variant": args.variant,
            "idfVersion": REQUIRED_IDF_VERSION,
            "gitCommit": commit,
            "gitDirty": dirty,
            "sdkconfigSha256": hashlib.sha256(args.sdkconfig.read_bytes()).hexdigest(),
            "compilerVersion": compiler_version,
            "elfSha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
            "manifestSha256": hashlib.sha256(MANIFEST.read_bytes()).hexdigest(),
            "toolchain": {
                "compiler": compiler,
                "compilerVersion": compiler_version,
                "objdump": args.objdump,
                "objdumpVersion": objdump_version,
            },
            "issueBlocks": issue_blocks,
            "firstLineInstructionPool": first_line_pool,
            "instructionLadders": ladders,
            "instructionPlacement": placement,
            "dbusFlashClassifier": flash_classifier,
        }
    except (OSError, subprocess.CalledProcessError, VerificationError) as error:
        print(f"Tier-B ELF verification failed: {error}", file=sys.stderr)
        return 2
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"Tier-B ELF verification passed: {args.result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
