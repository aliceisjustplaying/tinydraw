#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SCRIPT = ROOT / "verify_elf.py"


def function(name: str, address: int, encodings: list[str]) -> str:
    lines = [f"{address:08x} <{name}>:"]
    current = address
    mnemonics = {
        "004136": "entry a1, 32",
        "f03d": "nop.n",
        "208880": "or a8, a8, a8",
        "01c882": "addi a8, a8, 1",
        "01c992": "addi a9, a9, 1",
        "01caa2": "addi a10, a10, 1",
        "01cbb2": "addi a11, a11, 1",
        "f01d": "retw.n",
        "0f9876": "loopnez a8, target",
    }
    for encoding in encodings:
        text = mnemonics[encoding]
        mnemonic, _, operands = text.partition(" ")
        lines.append(f" {current:08x}: {encoding:<8} {mnemonic:<10} {operands}")
        current += len(encoding) // 2
    return "\n".join(lines)


def valid_disassembly() -> str:
    blocks = [
        function("issue_narrow_block", 0x40377500, ["004136", *(["f03d"] * 256), "f01d"]),
        function("issue_wide_block", 0x40377708, ["004136", *(["208880"] * 256), "f01d"]),
        function(
            "issue_mixed_block",
            0x40377A10,
            ["004136", *(["f03d", "208880"] * 128), "f01d"],
        ),
        function(
            "issue_dependent_block", 0x40377C98, ["004136", *(["01c882"] * 256), "f01d"]
        ),
        function(
            "issue_independent_block",
            0x40377FA0,
            ["004136", *(["01c882", "01c992", "01caa2", "01cbb2"] * 64), "f01d"],
        ),
    ]
    loop_shapes = (
        (0, 0x403782E4, ["208880"]),
        (1, 0x403782A8, []),
        (2, 0x40378304, ["f03d", "208880"]),
        (3, 0x403782C4, ["f03d"]),
    )
    for residue, address, padding in loop_shapes:
        encodings = ["004136", "004136", *padding, "0f9876", *(["f03d"] * 8), "f01d"]
        blocks.append(function(f"loop_body_r{residue}", address, encodings))
    return "\n\n".join(blocks) + "\n"


class VerifyElfTest(unittest.TestCase):
    def run_gate(self, disassembly: str) -> tuple[subprocess.CompletedProcess[str], Path]:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        elf = root / "probe.elf"
        dump = root / "probe.dump"
        result = root / "verification.json"
        elf.write_bytes(b"fixture elf")
        dump.write_text(disassembly)
        process = subprocess.run(
            [sys.executable, str(SCRIPT), str(elf), str(result), "--disassembly", str(dump)],
            text=True,
            capture_output=True,
        )
        return process, result

    def test_valid_disassembly_writes_result(self) -> None:
        process, result = self.run_gate(valid_disassembly())
        self.assertEqual(process.returncode, 0, process.stderr)
        payload = json.loads(result.read_text())
        self.assertEqual([loop["residueMod4"] for loop in payload["loopBodies"]], [0, 1, 2, 3])

    def test_encoding_mismatch_leaves_no_result(self) -> None:
        process, result = self.run_gate(valid_disassembly().replace("208880", "f03d", 1))
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())

    def test_residue_mismatch_leaves_no_result(self) -> None:
        broken = valid_disassembly().replace(" 403782f0:", " 403782f1:", 1)
        process, result = self.run_gate(broken)
        self.assertEqual(process.returncode, 2)
        self.assertFalse(result.exists())


if __name__ == "__main__":
    unittest.main()
