#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute linked non-RF fixtures with the CC2530 XDATA/IRAM alias modeled."""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_firmware import BOARDS, IMAGES, require, verify_artifacts, xdata_ranges
from debug_image import DebugImage, decode_bootstrap, decode_fixture, expected_fixture


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
    # A command file avoids concurrent stdin echo interleaving with memory dumps.
    with tempfile.TemporaryDirectory(prefix="cc2530-sim-") as directory:
        script = Path(directory) / "commands"
        script.write_text("\n".join(commands + ["quit", ""]), encoding="ascii")
        argv = [simulator, "-t", "C52", "-q", "-c", "-"]
        if image is not None:
            argv.append(str(image))
        result = subprocess.run(
            argv, input=f'exec "{script}"\n', capture_output=True, text=True,
            timeout=15, check=True,
        )
    require(not re.search(r"Unknown command|No such command|Syntax error|Error:", result.stdout, re.IGNORECASE),
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


def check_artifact_rejections(output, board, image_name="bringup"):
    case = unittest.TestCase()
    with tempfile.TemporaryDirectory(prefix="cc2530-m0-artifacts-") as directory:
        work = Path(directory)
        for extension in ("ihx", "hex", "bin", "map", "mem", "cdb"):
            shutil.copyfile(output / f"{image_name}.{extension}", work / f"{image_name}.{extension}")
        other_board = next(name for name in BOARDS if name != board)
        with case.assertRaisesRegex(ValueError, "board identity"):
            verify_artifacts(work, other_board, image_name)
        mutations = (
            ("bin", lambda data: data + b"\0", "HEX/BIN"),
            ("map", lambda data: data.replace(b"00001E00", b"00001F00"), "alias"),
            ("cdb", lambda data: data.replace(b"{32}ST", b"{64}ST"), "ABI"),
            ("mem", lambda data: data.replace(b"248 bytes available", b"247 bytes available"), "stack"),
        )
        if image_name == "debug_fixture":
            mutations += (
                ("cdb", lambda data: data.replace(b"{16}ST", b"{15}ST"), "Fixture debug ABI"),
                ("map", lambda data: data.replace(b"_debug_fixture_stop ", b"_missing_fixture_stop "), "symbol"),
            )
        for extension, mutate, message in mutations:
            path = work / f"{image_name}.{extension}"
            original = path.read_bytes()
            modified = mutate(original)
            require(modified != original, f"Artifact mutation did not apply: {extension}")
            path.write_bytes(modified)
            with case.assertRaisesRegex(ValueError, message):
                verify_artifacts(work, board, image_name)
            path.write_bytes(original)
        for extension in ("ihx", "hex"):
            path = work / f"{image_name}.{extension}"
            original = path.read_text(encoding="ascii")
            modified = original.replace(":00000001FF", ":01800000007F\n:00000001FF")
            require(modified != original, "CODE-boundary mutation did not apply")
            path.write_text(modified, encoding="ascii")
        with case.assertRaisesRegex(ValueError, "unbanked CODE"):
            verify_artifacts(work, board, image_name)


def boot_commands(symbols):
    commands = [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0x80 0xff", "set memory sfr 0x90 0xff", "set memory sfr 0xa0 0xff",
    ]
    for address in (0x8F, 0x9A, 0xA8, 0xB8, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xFD, 0xFE, 0xFF):
        commands.append(f"set memory sfr {address:#x} 0")
    commands += [
        "set memory sfr 0xc6 0xc9", "set memory sfr 0x9e 0xc9",
        f"run 0 {symbols['_main']:#x}",
        # Normal CRT clears IRAM; seed the unused upper stack guard afterwards.
        "fill iram 0x80 0xff 0xc7",
    ]
    return commands


def expected_status(board):
    lg = BOARDS[board]
    return bytes(
        [ord("M"), ord("0"), ord("C"), ord("C"), 1, 32, 2, lg, 0, lg]
        + ([0x43, 0xFD, 0xFF, 0xBC, 2, 0] if lg else [0xFF, 0xFF, 0xFF, 0, 0, 0])
        + [0, 0, 0, 0xBC if lg else 0, 0, 0, 0, 0, 0xC9, 0xC9, 0, 0, 0, 0, 0, 0]
    )


def check_guards(ram, upper_iram, sfr, symbols):
    allocated = set(range(0x1E00, 0x1E20))
    for start, end in xdata_ranges(symbols):
        allocated.update(range(start, end))
    require(all(value == 0xA5 for address, value in enumerate(ram) if address not in allocated),
            "Image wrote outside its allocated nonaliased XDATA")
    require(upper_iram == b"\xC7" * 128, "Image reached the upper IRAM stack guard")
    require(sfr[0xA8 - 0x80] == sfr[0xB8 - 0x80] == sfr[0x9A - 0x80] == 0,
            "Interrupts became enabled")
    require(symbols["s_SSEG"] - 1 <= sfr[1] < 0x80, "Invalid final stack pointer")


def check_boot(simulator, output, board, symbols):
    commands = boot_commands(symbols) + [
        f"run {symbols['_main']:#x} {symbols['_bringup_tick']:#x}",
        marker(10), "dump /h xram 0x1e00 0x1e1f", marker(11),
        "step 40", marker(12), "dump /h xram 0 0x1eff", marker(13),
        "dump /h iram 0x80 0xff", marker(14),
        "dump /h sfr 0x80 0xff", marker(15),
    ]
    text = simulate(simulator, commands, output / "bringup.ihx")
    first = memory_dump(section(text, 10), 0x1E00, 32)
    ram = memory_dump(section(text, 12), 0, 0x1F00)
    second = ram[0x1E00:0x1E20]
    require(first == expected_status(board), f"{board}: unexpected initial status {first.hex()}")
    require(0 < second[8] <= 40 and second[:8] + second[9:] == first[:8] + first[9:],
            "Heartbeat did not progress, or changed immutable status")
    check_guards(ram, memory_dump(section(text, 13), 0x80, 128),
                 memory_dump(section(text, 14), 0x80, 128), symbols)
    print(f"{board}: actual-image alias-aware boot, status, heartbeat, XDATA/stack guards PASS (simulation only).")


def snapshot_commands(number):
    return [
        marker(number), "state", "dump /h xram 0 0x1eff",
        marker(number + 1), "dump /h iram 0 0xff",
        marker(number + 2), "dump /h sfr 0x80 0xff", marker(number + 3),
    ]


def snapshot(text, number):
    return (memory_dump(section(text, number), 0, 0x1F00),
            memory_dump(section(text, number + 1), 0, 256),
            memory_dump(section(text, number + 2), 0x80, 128))


def check_pc(text, address):
    match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", text)
    require(match is not None and int(match[1], 16) == address, "Unexpected simulator PC")


def check_debug_fixture(simulator, output, board, symbols):
    start = symbols["_debug_fixture_state"]
    cycle_address = symbols["_debug_fixture_cycle"]
    stop = symbols["_debug_fixture_stop"]
    stages = [symbols[f"_debug_fixture_stage{i}"] for i in range(4)]
    commands = boot_commands(symbols) + [
        f"run {symbols['_main']:#x} {cycle_address:#x}",
        marker(20), "dump /h xram 0x1e00 0x1e1f",
        f"dump /h xram {start:#x} {start + 15:#x}", marker(21),
    ]
    previous = cycle_address
    for index, address in enumerate(stages):
        commands += [
            f"run {previous:#x} {address:#x}", marker(30 + index * 2),
            "state", "dump /h sfr 0x81 0x81", marker(31 + index * 2),
        ]
        previous = address
    commands += [f"run {previous:#x} {stop:#x}"] + snapshot_commands(40)
    commands += ["step 1"] + snapshot_commands(44)
    # Execute real returns/calls rather than restarting main or overwriting PC.
    for cycle in range(1, 257):
        number = 100 + cycle * 2
        commands += [
            f"run {stop + 1:#x} {stop:#x}", marker(number),
            f"dump /h xram {start:#x} {start + 15:#x}",
            "dump /h xram 0x1e08 0x1e08", marker(number + 1), "step 1",
        ]
    commands += snapshot_commands(620)
    text = simulate(simulator, commands, output / "debug_fixture.ihx")
    require(memory_dump(section(text, 20), 0x1E00, 32) == expected_status(board),
            "Debug fixture changed the M0 startup status")
    decode_bootstrap(memory_dump(section(text, 20), 0x1E00, 32), board)
    initial = b"M1DB" + bytes([1, 16, 1, 0, 0, 0, 0, 0, 0, 0, 0x69, 0x96])
    require(memory_dump(section(text, 20), start, 16) == initial, "Wrong fixture initialization")
    decode_fixture(memory_dump(section(text, 20), start, 16))
    for index, address in enumerate(stages):
        entry = section(text, 30 + index * 2)
        check_pc(entry, address)
        sp = memory_dump(entry, 0x81, 1)[0]
        require(sp == symbols["s_SSEG"] - 1 + 4 + index * 2, "Fixture call-chain depth changed")

    before, after = snapshot(text, 40), snapshot(text, 44)
    check_pc(section(text, 40), stop)
    check_pc(section(text, 44), stop + 1)
    require(before == after, "Probe NOP changed RAM/registers")
    ram, iram, sfr = before
    require(ram[start:start + 16] == expected_fixture(0), "Wrong first fixture cycle")
    require(sfr[0xE0 - 0x80] == 0xA5 and sfr[0xF0 - 0x80] == 0x3C
            and sfr[2:4] == b"\x34\x12" and iram[7] == 0x69
            and sfr[0xD0 - 0x80] & 0x98 == 0x80, "Wrong probe registers/bank/carry")
    require(sfr[1] == symbols["s_SSEG"] + 1, "Probe stack did not unwind")
    check_guards(ram, iram[128:], sfr, symbols)
    for cycle in range(1, 257):
        sample = section(text, 100 + cycle * 2)
        require(memory_dump(sample, start, 16) == expected_fixture(cycle),
                f"Fixture mismatch after resumed cycle {cycle}")
        decode_fixture(memory_dump(sample, start, 16))
        require(memory_dump(sample, 0x1E08, 1)[0] == (cycle + 1) & 255,
                "Fixture heartbeat mismatch")
    final_ram, final_iram, final_sfr = snapshot(text, 620)
    check_guards(final_ram, final_iram[128:], final_sfr, symbols)
    require(final_sfr[1] == sfr[1], "Resumed fixture leaked stack")
    require(final_ram[0x1E00:0x1E20] == ram[0x1E00:0x1E20], "M0 status changed across 256 cycles")
    print(f"{board}: debug fixture, four code locations, nested calls, NOP PC+1, "
          "registers, 257 cycles and alias/XDATA/stack guards PASS (simulation only).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, required=True)
    parser.add_argument("--image", choices=IMAGES, default="bringup")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    _, symbols = verify_artifacts(args.output, args.board, args.image)
    debug_image = DebugImage(args.output, args.board, args.image)
    require(debug_image.symbol("_m0_status").space == "XDATA", "M0 symbol space mismatch")
    require(debug_image.symbol("_SOC_P0").space == "SFR", "SFR symbol space mismatch")
    require(debug_image.symbol("_board_description").kind == "object", "CODE data misclassified")
    source_file = Path(__file__).resolve().parents[1] / "examples" / f"{args.image}.c"
    main_lines = debug_image.source_lines(pc=symbols["_main"])
    require(any(location.file == source_file.name for location in main_lines), "Main source mapping missing")
    if args.image == "debug_fixture":
        require(debug_image.symbol("_debug_fixture_state").size == 16, "M1 symbol size mismatch")
        pattern_source = (Path(__file__).resolve().parents[1] / "src" / "debug_pattern.c").read_text(
            encoding="ascii").splitlines()
        for slot in range(4):
            name = f"_debug_fixture_stage{slot}"
            require(debug_image.symbol(name).kind == "function", "Fixture function misclassified")
            require(debug_image.breakpoint(name, slot)["address"] == symbols[name], "Breakpoint lookup mismatch")
            line = next(index for index, text in enumerate(pattern_source, 1)
                        if text.startswith(f"uint8_t {name[1:]}("))
            locations = debug_image.source_lines(file="debug_pattern.c", line=line)
            require(any(location.address == symbols[name] for location in locations), "Stage source mapping mismatch")
    check_artifact_rejections(args.output, args.board, args.image)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    if args.image == "bringup":
        check_boot(args.simulator, args.output, args.board, symbols)
    else:
        check_debug_fixture(args.simulator, args.output, args.board, symbols)


if __name__ == "__main__":
    main()
