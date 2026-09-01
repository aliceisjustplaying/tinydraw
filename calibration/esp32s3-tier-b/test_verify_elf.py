#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SCRIPT = ROOT / "verify_elf.py"


def function(name: str, address: int, encodings: list[tuple[str, str]]) -> str:
    lines = [f"{address:08x} <{name}>:"]
    current = address
    for encoding, mnemonic in encodings:
        lines.append(f" {current:08x}: {encoding:<8} {mnemonic}")
        current += len(encoding) // 2
    return "\n".join(lines)


def fixtures(xip: bool = False) -> tuple[str, str, str]:
    functions: list[str] = []
    symbols: list[str] = []
    address = 0x42000100
    for lines in (1, 2, 4, 8, 16):
        name = f"tier_b_instruction_{lines}_lines"
        body = [("f03d", "nop.n")] * ((lines * 32 - 2) // 2) + [("f00d", "ret.n")]
        functions.append(function(name, address, body))
        symbols.extend(
            (
                f"{address:08x} g .flash.text 00000000 {name}_start",
                f"{address:08x} g F .flash.text {lines * 32:08x} {name}",
                f"{address + lines * 32:08x} g .flash.text 00000000 {name}_end",
            )
        )
        address += lines * 32
    for index in range(5):
        name = f"tier_b_first_line_i_{index}"
        body = [("f03d", "nop.n")] * 15 + [("f00d", "ret.n")]
        functions.append(function(name, address, body))
        symbols.append(f"{address:08x} g F .flash.text 00000020 {name}")
        address += 32
    address = (address + 31) & ~31
    store = [("0239", "s32i.n a3, a2, 0")] * 256 + [
        ("0020c0", "memw"),
        ("f00d", "ret.n"),
    ]
    functions.append(function("tier_b_store_issue_block", address, store))
    symbols.extend(
        (
            f"{address:08x} g .iram0.text 00000000 tier_b_store_issue_block_start",
            f"{address:08x} g F .iram0.text 00000205 tier_b_store_issue_block",
            f"{address + 517:08x} g .iram0.text 00000000 tier_b_store_issue_block_end",
            "42000020 g *ABS* 00000000 _instruction_reserved_start",
            "42010000 g *ABS* 00000000 _instruction_reserved_end",
        )
    )
    if xip:
        symbols.extend(
            (
                "3fc90000 l .dram0.bss 00000004 instruction_in_spiram",
                "40375000 g F .iram0.text 0000000a instruction_flash2spiram_offset",
                "40375100 g F .iram0.text 00000022 mmu_psram_check_ptr_addr_in_xip_psram_instruction_region",
            )
        )
    sdkconfig = "CONFIG_APP_RETRIEVE_LEN_ELF_SHA=64\n"
    sdkconfig += (
        "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y\n"
        if xip
        else "# CONFIG_SPIRAM_FETCH_INSTRUCTIONS is not set\n"
    )
    return "\n\n".join(functions) + "\n", "\n".join(symbols) + "\n", sdkconfig


class VerifyTierBElfTest(unittest.TestCase):
    def run_gate(
        self, disassembly: str, symbols: str, sdkconfig: str, variant: str = "normal"
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        elf = root / "probe.elf"
        config = root / "sdkconfig"
        dump = root / "probe.dump"
        table = root / "probe.symbols"
        result = root / "verification.json"
        elf.write_bytes(b"fixture elf")
        config.write_text(sdkconfig)
        dump.write_text(disassembly)
        table.write_text(symbols)
        process = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(elf),
                str(config),
                str(result),
                "--variant",
                variant,
                "--objdump",
                "fixture-objdump",
                "--disassembly",
                str(dump),
                "--symbols",
                str(table),
                "--compiler-version",
                "15.2.0",
            ],
            text=True,
            capture_output=True,
        )
        return process, result

    def test_normal_fixture_writes_complete_preflight(self) -> None:
        disassembly, symbols, sdkconfig = fixtures()
        process, result = self.run_gate(disassembly, symbols, sdkconfig)
        self.assertEqual(process.returncode, 0, process.stderr)
        payload = json.loads(result.read_text())
        self.assertEqual(payload["variant"], "normal")
        self.assertTrue(payload["fixture"])
        self.assertEqual(payload["issueBlocks"][0]["operations"], 256)
        self.assertEqual(len(payload["firstLineInstructionPool"]), 5)
        self.assertEqual(
            [item["spanBytes"] for item in payload["instructionLadders"]],
            [32, 64, 128, 256, 512],
        )

    def test_xip_fixture_requires_psram_copy_symbols(self) -> None:
        disassembly, symbols, sdkconfig = fixtures(xip=True)
        process, result = self.run_gate(disassembly, symbols, sdkconfig, "xip-psram")
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertIn("PSRAM", json.loads(result.read_text())["instructionPlacement"]["runtimePlacement"])

    def test_issue_encoding_mismatch_leaves_no_result(self) -> None:
        disassembly, symbols, sdkconfig = fixtures()
        broken = disassembly.replace("0239", "f03d", 1)
        process, result = self.run_gate(broken, symbols, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_ladder_residue_mismatch_leaves_no_result(self) -> None:
        disassembly, symbols, sdkconfig = fixtures()
        broken = symbols.replace("42000100 g .flash.text", "42000101 g .flash.text", 1)
        process, result = self.run_gate(disassembly, broken, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_variant_mismatch_leaves_no_result(self) -> None:
        disassembly, symbols, sdkconfig = fixtures()
        process, result = self.run_gate(disassembly, symbols, sdkconfig, "xip-psram")
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())


if __name__ == "__main__":
    unittest.main()
