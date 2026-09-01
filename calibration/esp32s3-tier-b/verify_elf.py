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
    operands: str


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
                    operands=(instruction.group(4) or "").strip(),
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


def through_windowed_return(
    instructions: list[Instruction], symbol: str
) -> list[Instruction]:
    try:
        end = next(
            index
            for index, instruction in enumerate(instructions)
            if instruction.encoding == "f01d"
        )
    except StopIteration as error:
        raise VerificationError(f"{symbol} has no exact retw.n endpoint") from error
    return instructions[: end + 1]


def require_single_function_containing(
    functions: dict[str, list[Instruction]], fragment: str
) -> tuple[str, list[Instruction]]:
    matches = [
        (name, instructions)
        for name, instructions in functions.items()
        if fragment in name
    ]
    if len(matches) != 1:
        raise VerificationError(f"expected exactly one {fragment} disassembly")
    return matches[0]


def verify_instruction_callers(
    functions: dict[str, list[Instruction]],
) -> dict[str, dict[str, int | str]]:
    ladder_name, ladder_caller = require_single_function_containing(
        functions, "measure_instruction_8_lines"
    )
    ladder_calls = [
        index
        for index, instruction in enumerate(ladder_caller)
        if "<tier_b_instruction_8_lines>" in instruction.operands
        and instruction.mnemonic.startswith("call")
    ]
    if len(ladder_calls) != 1 or any(
        ladder_caller[index].mnemonic != "call8" for index in ladder_calls
    ):
        raise VerificationError(
            "measure_instruction_8_lines ladder call is not windowed call8"
        )
    timed_ladder_calls = [
        index
        for index in ladder_calls
        if index > 0
        and index + 1 < len(ladder_caller)
        and ladder_caller[index - 1].mnemonic == "rsr.ccount"
        and ladder_caller[index + 1].mnemonic == "rsr.ccount"
    ]
    if len(timed_ladder_calls) != 1:
        raise VerificationError(
            "measure_instruction_8_lines does not bracket its ladder call with CCOUNT"
        )

    first_line_name, first_line_caller = require_single_function_containing(
        functions, "probe_first_line_i_flash"
    )
    timed_first_line_calls = [
        index
        for index, instruction in enumerate(first_line_caller)
        if instruction.mnemonic.startswith("callx")
        and index > 0
        and index + 1 < len(first_line_caller)
        and first_line_caller[index - 1].mnemonic == "rsr.ccount"
        and first_line_caller[index + 1].mnemonic == "rsr.ccount"
    ]
    if len(timed_first_line_calls) != 1:
        raise VerificationError(
            "probe_first_line_i_flash does not bracket one indirect call with CCOUNT"
        )
    first_line_call = first_line_caller[timed_first_line_calls[0]]
    if first_line_call.mnemonic != "callx8":
        raise VerificationError(
            "probe_first_line_i_flash target is not called with windowed callx8"
        )
    return {
        "instructionLadder": {
            "caller": ladder_name,
            "abi": "windowed-call8",
            "target": "tier_b_instruction_8_lines",
            "targetCalls": len(ladder_calls),
            "timedCalls": len(timed_ladder_calls),
            "timedBoundary": "ccount-call8-ccount",
        },
        "firstLineInstruction": {
            "caller": first_line_name,
            "abi": "windowed-callx8",
            "timedCalls": len(timed_first_line_calls),
            "timedBoundary": "ccount-callx8-ccount",
        },
    }


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
        instructions = through_windowed_return(require_function(functions, name), name)
        if instructions[0].address != start.address:
            raise VerificationError(f"{name} disassembly does not start at its ELF symbol")
        expected_nops = (expected_bytes - 8) // 2
        expected = ["002136", "208880"] + ["f03d"] * expected_nops + ["f01d"]
        if [instruction.encoding for instruction in instructions] != expected:
            raise VerificationError(
                f"{name} is not exact entry, deterministic padding, nop.n ladder, retw.n"
            )
        if instructions[2].address % 32 != 6 or instructions[-1].address % 32 != 30:
            raise VerificationError(f"{name} body or return residue is wrong")
        results.append(
            {
                "symbol": name,
                "abi": "windowed-call8",
                "spanBytes": expected_bytes,
                "nopOperations": expected_nops,
                "prologueEncoding": "002136",
                "paddingEncoding": "208880",
                "returnEncoding": "f01d",
                "startResidueMod32": start.address % 32,
                "bodyResidueMod32": instructions[2].address % 32,
                "returnResidueMod32": instructions[-1].address % 32,
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
    instructions = require_function(functions, name)
    try:
        return_index = next(
            index
            for index, instruction in enumerate(instructions)
            if instruction.encoding == "f01d"
        )
    except StopIteration as error:
        raise VerificationError(f"{name} has no exact retw.n endpoint") from error
    instructions = instructions[: return_index + 1]
    if instructions[0].address != start.address:
        raise VerificationError(f"{name} disassembly does not start at its ELF symbol")
    expected = ["002136"] + ["0239"] * 256 + ["0020c0", "f01d"]
    if [instruction.encoding for instruction in instructions] != expected:
        raise VerificationError(
            f"{name} is not exact windowed ABI entry, 256 s32i.n, memw, retw.n"
        )
    if end.address - start.address != 520 or start.address % 32 != 0:
        raise VerificationError(f"{name} span or alignment is wrong")
    store_callers = [
        instructions
        for function_name, instructions in functions.items()
        if "probe_store_hit" in function_name
    ]
    if len(store_callers) != 1:
        raise VerificationError("expected exactly one probe_store_hit disassembly")
    caller = store_callers[0]
    target_loads = [
        index
        for index, instruction in enumerate(caller)
        if instruction.mnemonic == "l32r"
        and "<tier_b_store_issue_block>" in instruction.operands
    ]
    if len(target_loads) != 4:
        raise VerificationError("probe_store_hit does not reference the issue block four times")
    calls: list[int] = []
    for load_index in target_loads:
        call_index = load_index + 1
        if call_index >= len(caller):
            raise VerificationError("probe_store_hit issue-block target load has no call")
        call = caller[call_index]
        if (
            call.encoding != "0008e0"
            or call.mnemonic != "callx8"
            or call.operands != "a8"
        ):
            raise VerificationError("probe_store_hit issue block is not called with callx8 a8")
        calls.append(call_index)
    for timed_call in (calls[1], calls[3]):
        if (
            timed_call + 1 >= len(caller)
            or caller[timed_call + 1].mnemonic != "rsr.ccount"
        ):
            raise VerificationError(
                "probe_store_hit does not read CCOUNT immediately after timed call"
            )
    return [
        {
            "symbol": name,
            "abi": "windowed-call8",
            "operations": 256,
            "encoding": "0239",
            "startResidueMod32": start.address % 32,
            "spanBytes": end.address - start.address,
            "prologueEncoding": "002136",
            "completionBarrierEncoding": "0020c0",
            "returnEncoding": "f01d",
            "verifiedCallSites": len(calls),
        }
    ]


def verify_first_line_pool(
    functions: dict[str, list[Instruction]], symbols: dict[str, Symbol]
) -> list[dict[str, int | str]]:
    results: list[dict[str, int | str]] = []
    expected = ["002136", "208880"] + ["f03d"] * 12 + ["f01d"]
    for index in range(5):
        name = f"tier_b_first_line_i_{index}"
        symbol = require_symbol(symbols, name)
        if (
            symbol.section != ".flash.text"
            or symbol.address % 32 != 0
            or symbol.size != 32
        ):
            raise VerificationError(f"{name} is not an aligned flash-text target")
        instructions = through_windowed_return(require_function(functions, name), name)
        if [instruction.encoding for instruction in instructions] != expected:
            raise VerificationError(
                f"{name} is not exact entry, deterministic padding, one-line body, retw.n"
            )
        if instructions[2].address % 32 != 6 or instructions[-1].address % 32 != 30:
            raise VerificationError(f"{name} body or return residue is wrong")
        results.append(
            {
                "symbol": name,
                "abi": "windowed-callx8",
                "spanBytes": symbol.size,
                "nopOperations": 12,
                "prologueEncoding": "002136",
                "paddingEncoding": "208880",
                "returnEncoding": "f01d",
                "startResidueMod32": symbol.address % 32,
                "bodyResidueMod32": instructions[2].address % 32,
                "returnResidueMod32": instructions[-1].address % 32,
            }
        )
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


def verify_spiram_rodata_disabled(sdkconfig: str) -> bool:
    if "CONFIG_SPIRAM_RODATA=y" in sdkconfig:
        raise VerificationError("CONFIG_SPIRAM_RODATA must be disabled")
    if "# CONFIG_SPIRAM_RODATA is not set" not in sdkconfig:
        raise VerificationError("sdkconfig does not state that CONFIG_SPIRAM_RODATA is disabled")
    return False


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
        instruction_callers = verify_instruction_callers(functions)
        issue_blocks = verify_issue_blocks(functions, symbols)
        first_line_pool = verify_first_line_pool(functions, symbols)
        flash_classifier = verify_flash_pool(symbols, sections)
        sdkconfig = args.sdkconfig.read_text()
        spiram_rodata = verify_spiram_rodata_disabled(sdkconfig)
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
            "spiramRodata": spiram_rodata,
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
            "instructionCallAbi": instruction_callers,
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
