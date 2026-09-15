#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the linked bootstrap with the CC2530 XDATA/IRAM alias modeled."""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_firmware import BOARDS, require, verify_artifacts, xdata_ranges


ALIAS = "memory create addressdecoder xram 0x1f00 0x1fff iram_chip 0"


def marker(number):
    return f"expression /0 0x2530{number:04x}"


def section(text, number):
    start = f"0x2530{number:04x}"
    end = f"0x2530{number + 1:04x}"
    match = re.search(rf"^{start}\r?\n(.*?)^{end}\r?$", text, re.MULTILINE | re.DOTALL)
    require(match is not None, f"Simulator did not emit section {number}")
    return match[1]


def memory_dump(text, start, size):
    values = {}
    for line in text.splitlines():
        match = re.match(r"^0x([0-9a-fA-F]+)\s+((?:[0-9a-fA-F]{2}(?:\s+|$))+)", line)
        if match:
            address = int(match[1], 16)
            for offset, value in enumerate(match[2].split()):
                values[address + offset] = int(value, 16)
    require(all(address in values for address in range(start, start + size)), "Incomplete simulator memory dump")
    return bytes(values[address] for address in range(start, start + size))


def simulate(simulator, commands, image=None):
    argv = [simulator, "-t", "C52", "-q", "-c", "-"]
    if image is not None:
        argv.append(str(image))
    result = subprocess.run(
        argv, input="\n".join(commands + ["quit", ""]), capture_output=True, text=True,
        timeout=15, check=True,
    )
    require(not re.search(r"Unknown command|No such command|Syntax error|Error:", result.stdout),
            "Simulator rejected a command")
    return result.stdout


def check_alias(simulator, alias=True):
    # Original synthetic MOVX fixture: overwriting XDATA 1F07 must overwrite R7.
    text = simulate(simulator, ([ALIAS] if alias else []) + [
        "set memory rom 0 0x7f 0x07 0x90 0x1f 0x07 0xe4 0xf0 0x80 0xfe",
        "step 4", marker(1), "dump /h iram 7 7", marker(2),
        "set memory iram 0xa0 0x96", marker(3),
        "dump /h xram 0x1fa0 0x1fa0", marker(4),
    ])
    require(memory_dump(section(text, 1), 7, 1) == b"\0", "Alias failed to corrupt the CPU register bank")
    require(memory_dump(section(text, 3), 0x1FA0, 1) == b"\x96", "Reverse IRAM/XDATA alias is missing")


def check_artifact_rejections(output, board):
    case = unittest.TestCase()
    with tempfile.TemporaryDirectory(prefix="cc2530-m0-artifacts-") as directory:
        work = Path(directory)
        for extension in ("ihx", "hex", "bin", "map", "mem", "cdb"):
            shutil.copyfile(output / f"bringup.{extension}", work / f"bringup.{extension}")
        other_board = next(name for name in BOARDS if name != board)
        with case.assertRaisesRegex(ValueError, "board identity"):
            verify_artifacts(work, other_board)
        mutations = (
            ("bin", lambda data: data + b"\0", "HEX/BIN"),
            ("map", lambda data: data.replace(b"00001E00", b"00001F00"), "alias"),
            ("cdb", lambda data: data.replace(b"{32}ST", b"{64}ST"), "ABI"),
            ("mem", lambda data: data.replace(b"248 bytes available", b"247 bytes available"), "stack"),
        )
        for extension, mutate, message in mutations:
            path = work / f"bringup.{extension}"
            original = path.read_bytes()
            modified = mutate(original)
            require(modified != original, f"Artifact mutation did not apply: {extension}")
            path.write_bytes(modified)
            with case.assertRaisesRegex(ValueError, message):
                verify_artifacts(work, board)
            path.write_bytes(original)
        for extension in ("ihx", "hex"):
            path = work / f"bringup.{extension}"
            original = path.read_text(encoding="ascii")
            modified = original.replace(":00000001FF", ":01800000007F\n:00000001FF")
            require(modified != original, "CODE-boundary mutation did not apply")
            path.write_text(modified, encoding="ascii")
        with case.assertRaisesRegex(ValueError, "unbanked CODE"):
            verify_artifacts(work, board)


def check_boot(simulator, output, board, symbols):
    main_address, tick_address = symbols["_main"], symbols["_bringup_tick"]
    commands = [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0x80 0xff", "set memory sfr 0x90 0xff", "set memory sfr 0xa0 0xff",
    ]
    for address in (0x8F, 0x9A, 0xA8, 0xB8, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xFD, 0xFE, 0xFF):
        commands.append(f"set memory sfr {address:#x} 0")
    commands += [
        "set memory sfr 0xc6 0xc9", "set memory sfr 0x9e 0xc9",
        f"run 0 {main_address:#x}",
        # Normal CRT clears IRAM; seed the unused upper stack guard afterwards.
        "fill iram 0x80 0xff 0xc7",
        f"run {main_address:#x} {tick_address:#x}",
        marker(10), "dump /h xram 0x1e00 0x1e1f", marker(11),
        "step 40", marker(12), "dump /h xram 0 0x1eff", marker(13),
        "dump /h iram 0x80 0xff", marker(14),
        "dump /h sfr 0x80 0xff", marker(15),
    ]
    text = simulate(simulator, commands, output / "bringup.ihx")
    first = memory_dump(section(text, 10), 0x1E00, 32)
    ram = memory_dump(section(text, 12), 0, 0x1F00)
    second = ram[0x1E00:0x1E20]
    lg = BOARDS[board]
    expected = bytes(
        [ord("M"), ord("0"), ord("C"), ord("C"), 1, 32, 2, lg, 0, lg]
        + ([0x43, 0xFD, 0xFF, 0xBC, 2, 0] if lg else [0xFF, 0xFF, 0xFF, 0, 0, 0])
        + [0, 0, 0, 0xBC if lg else 0, 0, 0, 0, 0, 0xC9, 0xC9, 0, 0, 0, 0, 0, 0]
    )
    require(first == expected, f"{board}: unexpected initial status {first.hex()}")
    require(0 < second[8] <= 40 and second[:8] + second[9:] == first[:8] + first[9:],
            "Heartbeat did not progress, or changed immutable status")
    allocated = set(range(0x1E00, 0x1E20))
    for start, end in xdata_ranges(symbols):
        allocated.update(range(start, end))
    require(all(value == 0xA5 for address, value in enumerate(ram) if address not in allocated),
            "Bootstrap wrote outside its allocated nonaliased XDATA")
    require(memory_dump(section(text, 13), 0x80, 128) == b"\xC7" * 128,
            "Bootstrap reached the upper IRAM stack guard")
    sfr = memory_dump(section(text, 14), 0x80, 128)
    require(sfr[0xA8 - 0x80] == sfr[0xB8 - 0x80] == sfr[0x9A - 0x80] == 0,
            "Interrupts became enabled")
    require(symbols["s_SSEG"] - 1 <= sfr[1] < 0x80, "Invalid final stack pointer")
    print(f"{board}: actual-image alias-aware boot, status, heartbeat, XDATA/stack guards PASS (simulation only).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    _, symbols = verify_artifacts(args.output, args.board)
    check_artifact_rejections(args.output, args.board)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    check_boot(args.simulator, args.output, args.board, symbols)


if __name__ == "__main__":
    main()
