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


def replace_in_function(text: str, name: str, old: str, new: str) -> str:
    start = text.index(f"<{name}>:")
    end = text.find("\n\n", start)
    if end < 0:
        end = len(text)
    body = text[start:end]
    replaced = body.replace(old, new, 1)
    if replaced == body:
        raise AssertionError(f"{old!r} not found in {name}")
    return text[:start] + replaced + text[end:]


def fixtures(xip: bool = False) -> tuple[str, str, str, str]:
    functions: list[str] = []
    symbols: list[str] = []
    ladder_addresses: dict[int, int] = {}
    address = 0x42000100
    for lines in (1, 2, 4, 8, 16):
        ladder_addresses[lines] = address
        name = f"tier_b_instruction_{lines}_lines"
        body = (
            [("002136", "entry a1, 16"), ("208880", "or a8, a8, a8")]
            + [("f03d", "nop.n")] * ((lines * 32 - 8) // 2)
            + [("f01d", "retw.n")]
        )
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
        body = (
            [("002136", "entry a1, 16"), ("208880", "or a8, a8, a8")]
            + [("f03d", "nop.n")] * 12
            + [("f01d", "retw.n")]
        )
        functions.append(function(name, address, body))
        symbols.append(f"{address:08x} g F .flash.text 00000020 {name}")
        address += 32
    address = (address + 31) & ~31
    store = (
        [("002136", "entry a1, 16")]
        + [("0239", "s32i.n a3, a2, 0")] * 256
        + [
            ("0020c0", "memw"),
            ("f01d", "retw.n"),
        ]
    )
    functions.append(function("tier_b_store_issue_block", address, store))
    symbols.extend(
        (
            f"{address:08x} g .iram0.text 00000000 tier_b_store_issue_block_start",
            f"{address:08x} g F .iram0.text 00000208 tier_b_store_issue_block",
            f"{address + 520:08x} g .iram0.text 00000000 tier_b_store_issue_block_end",
            "42000020 g *ABS* 00000000 _instruction_reserved_start",
            "42010000 g *ABS* 00000000 _instruction_reserved_end",
            "3c02ac00 l O .flash.rodata 00040000 _ZN12_GLOBAL__N_1L12g_flash_poolE",
        )
    )
    caller = []
    for index in range(4):
        caller.extend(
            (
                (
                    "000081",
                    f"l32r a8, target ({address:08x} <tier_b_store_issue_block>)",
                ),
                ("0008e0", "callx8 a8"),
                ("03e800", "rsr.ccount a8") if index in (1, 3) else ("f03d", "nop.n"),
            )
        )
    functions.append(function("probe_store_hit", 0x42020000, caller))
    functions.append(
        function(
            "probe_instruction_psram",
            0x42020100,
            [
                (
                    "000000",
                    f"call8 {ladder_addresses[8]:08x} <tier_b_instruction_8_lines>",
                ),
                ("f03d", "nop.n"),
                ("03e800", "rsr.ccount a8"),
                (
                    "000000",
                    f"call8 {ladder_addresses[8]:08x} <tier_b_instruction_8_lines>",
                ),
                ("03e900", "rsr.ccount a9"),
            ],
        )
    )
    functions.append(
        function(
            "probe_first_line_i_flash",
            0x42020200,
            [
                ("03e800", "rsr.ccount a8"),
                ("0005e0", "callx8 a5"),
                ("03e900", "rsr.ccount a9"),
            ],
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
    sdkconfig += "# CONFIG_SPIRAM_RODATA is not set\n"
    sdkconfig += (
        "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y\n"
        if xip
        else "# CONFIG_SPIRAM_FETCH_INSTRUCTIONS is not set\n"
    )
    sections = " 14 .flash.rodata 0006c000 3c020000 3c020000 00001140 2**6\n"
    return (
        "\n\n".join(functions) + "\n",
        "\n".join(symbols) + "\n",
        sections,
        sdkconfig,
    )


class VerifyTierBElfTest(unittest.TestCase):
    def run_gate(
        self,
        disassembly: str,
        symbols: str,
        sections: str,
        sdkconfig: str,
        variant: str = "normal",
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        elf = root / "probe.elf"
        config = root / "sdkconfig"
        dump = root / "probe.dump"
        table = root / "probe.symbols"
        section_table = root / "probe.sections"
        result = root / "verification.json"
        elf.write_bytes(b"fixture elf")
        config.write_text(sdkconfig)
        dump.write_text(disassembly)
        table.write_text(symbols)
        section_table.write_text(sections)
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
                "--sections",
                str(section_table),
                "--compiler-version",
                "15.2.0",
            ],
            text=True,
            capture_output=True,
        )
        return process, result

    def test_normal_fixture_writes_complete_preflight(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        process, result = self.run_gate(disassembly, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 0, process.stderr)
        payload = json.loads(result.read_text())
        self.assertEqual(payload["variant"], "normal")
        self.assertEqual(payload["idfVersion"], "v6.1")
        self.assertIs(payload["spiramRodata"], False)
        self.assertTrue(payload["fixture"])
        self.assertEqual(payload["issueBlocks"][0]["operations"], 256)
        self.assertEqual(payload["issueBlocks"][0]["abi"], "windowed-call8")
        self.assertEqual(payload["issueBlocks"][0]["verifiedCallSites"], 4)
        self.assertEqual(len(payload["firstLineInstructionPool"]), 5)
        self.assertEqual(
            payload["instructionCallAbi"]["firstLineInstruction"]["abi"],
            "windowed-callx8",
        )
        self.assertEqual(
            payload["dbusFlashClassifier"],
            {
                "alignmentBytes": 64,
                "end": 0x3C06ABFF,
                "section": ".flash.rodata",
                "sizeBytes": 0x40000,
                "start": 0x3C02AC00,
                "storage": "flash-rodata",
                "symbol": "_ZN12_GLOBAL__N_1L12g_flash_poolE",
                "xipPsram": False,
            },
        )
        self.assertEqual(
            [item["spanBytes"] for item in payload["instructionLadders"]],
            [32, 64, 128, 256, 512],
        )

    def test_xip_fixture_requires_psram_copy_symbols(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures(xip=True)
        process, result = self.run_gate(
            disassembly, symbols, sections, sdkconfig, "xip-psram"
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertIn("PSRAM", json.loads(result.read_text())["instructionPlacement"]["runtimePlacement"])

    def test_issue_encoding_mismatch_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = disassembly.replace("0239", "f03d", 1)
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_issue_missing_entry_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = replace_in_function(
            disassembly, "tier_b_store_issue_block", "002136", "000000"
        )
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("windowed ABI entry", process.stderr)
        self.assertFalse(result.exists())

    def test_issue_ret_n_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = replace_in_function(
            disassembly,
            "tier_b_store_issue_block",
            "f01d     retw.n",
            "f00d     ret.n",
        )
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("no exact retw.n endpoint", process.stderr)
        self.assertFalse(result.exists())

    def test_issue_nonwindowed_call_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = disassembly.replace("0008e0   callx8 a8", "0000c0   callx0 a8", 1)
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("not called with callx8", process.stderr)
        self.assertFalse(result.exists())

    def test_issue_timing_boundary_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = disassembly.replace("03e800   rsr.ccount a8", "f03d     nop.n", 1)
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("read CCOUNT immediately", process.stderr)
        self.assertFalse(result.exists())

    def test_ladder_missing_entry_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = replace_in_function(
            disassembly, "tier_b_instruction_1_lines", "002136", "000000"
        )
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("not exact entry", process.stderr)
        self.assertFalse(result.exists())

    def test_ladder_ret_n_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = replace_in_function(
            disassembly,
            "tier_b_instruction_1_lines",
            "f01d     retw.n",
            "f00d     ret.n",
        )
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("no exact retw.n endpoint", process.stderr)
        self.assertFalse(result.exists())

    def test_first_line_callx0_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = replace_in_function(
            disassembly, "probe_first_line_i_flash", "callx8", "callx0"
        )
        process, result = self.run_gate(broken, symbols, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("windowed callx8", process.stderr)
        self.assertFalse(result.exists())

    def test_ladder_span_drift_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = symbols.replace(
            "42000120 g .flash.text 00000000 tier_b_instruction_1_lines_end",
            "42000122 g .flash.text 00000000 tier_b_instruction_1_lines_end",
        )
        process, result = self.run_gate(disassembly, broken, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertIn("spans 34 bytes", process.stderr)
        self.assertFalse(result.exists())

    def test_ladder_residue_mismatch_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = symbols.replace("42000100 g .flash.text", "42000101 g .flash.text", 1)
        process, result = self.run_gate(disassembly, broken, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_variant_mismatch_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        process, result = self.run_gate(
            disassembly, symbols, sections, sdkconfig, "xip-psram"
        )
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_flash_pool_wrong_size_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = symbols.replace(".flash.rodata 00040000", ".flash.rodata 0003ffff")
        process, result = self.run_gate(disassembly, broken, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_flash_pool_unaligned_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = symbols.replace("3c02ac00 l O .flash.rodata", "3c02ac01 l O .flash.rodata")
        process, result = self.run_gate(disassembly, broken, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_flash_pool_psram_section_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = symbols.replace(
            "3c02ac00 l O .flash.rodata", "3c02ac00 l O .ext_ram.dummy"
        )
        process, result = self.run_gate(disassembly, broken, sections, sdkconfig)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_spiram_rodata_enabled_leaves_no_result(self) -> None:
        disassembly, symbols, sections, sdkconfig = fixtures()
        broken = sdkconfig.replace(
            "# CONFIG_SPIRAM_RODATA is not set", "CONFIG_SPIRAM_RODATA=y"
        )
        process, result = self.run_gate(disassembly, symbols, sections, broken)
        self.assertEqual(process.returncode, 2)
        self.assertIn("CONFIG_SPIRAM_RODATA must be disabled", process.stderr)
        self.assertFalse(result.exists())


if __name__ == "__main__":
    unittest.main()
