#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check the isolated timebase executable offline; never flash it or access USB."""

import argparse
from pathlib import Path
import re
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
)
from verify_firmware import (
    CODE_LIMIT, STATUS_ADDRESS, STATUS_RESERVED, parse_ihex, parse_symbols, require, xdata_ranges,
    TIMEBASE_READER_BYTES,
)


# Reviewed SDCC 4.2.0 model-large instructions, not a peripheral model.
# Three MOV A,direct reads (95/96/97), each stored in compiler-owned XDATA
# 0/1/2; then zero-extension/packing into the DPL/DPH/B/A uint32_t return ABI.
# Lock every opcode/operand: no hidden call, branch, extra SFR read or MMIO write.
READER_BYTES = TIMEBASE_READER_BYTES
READ_OFFSETS = (3, 9, 15)
GUARD_SFRS = {
    0x80: 0x5a, 0x90: 0xa5, 0xa0: 0x69,
    0xa8: 0, 0xb8: 0, 0x9a: 0,
    0xfd: 0x43, 0xfe: 0xc1, 0xff: 0x08,
    0xf3: 0x43, 0xf4: 0xc1, 0xf5: 0x40,
    0x8f: 0xbc, 0xf6: 0xc0, 0xf7: 0x20, 0xf2: 0x43, 0xf1: 0x03,
    0xc6: 0xc9, 0x9e: 0xc9,
}
SAMPLES = (
    0, 1, 0xfe, 0xff, 0x100, 0x101, 0xfffe, 0xffff, 0x10000, 0x10001,
    0x123456, 0x7fffff, 0x800000, 0xfffffe, 0xffffff, 0, 1,
)


def verify_reader(image, symbols):
    names = ("_timebase_read_awake_ticks24", "_timebase_deadline_after", "_timebase_expired",
             "_main", "_timebase_test_sample", "_timebase_sample_ready", "_timebase_sample_done")
    require(all(name in symbols and symbols[name] in image for name in names),
            "Missing/out-of-image timebase code symbol")
    start = symbols["_timebase_read_awake_ticks24"]
    require(symbols["_timebase_deadline_after"] == start + len(READER_BYTES),
            "Timebase reader extent changed")
    require(all(image.get(start + offset) == value for offset, value in enumerate(READER_BYTES)),
            "Timebase reader opcodes/operands changed")
    require(all(symbols.get(f"_SOC_ST{i}") == 0x95 + i for i in range(3)),
            "Sleep Timer SFR addresses changed")
    require(all(image[symbols[name]] == 0 for name in ("_timebase_sample_ready", "_timebase_sample_done")),
            "Timebase sample stop is not a NOP")


def verify_image(image, symbols, debug, memory):
    require(image and min(image) == 0 and max(image) < CODE_LIMIT,
            "Timebase test CODE is not lower unbanked")
    require(image[0] == 2, "Timebase test reset vector is not LJMP")
    verify_reader(image, symbols)
    require(symbols.get("_timebase_test_result") == STATUS_ADDRESS, "Timebase result address changed")
    sizes = re.findall(r"^S:G\$timebase_test_result\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
    require(sizes and all(int(size) == 8 for size in sizes), "Timebase result debug ABI size changed")
    require("C$timebase.c$" in debug and "C$test_timebase.c$" in debug, "Missing timebase source records")
    require(symbols["__XPAGE"] == 0x93, "Timebase test must use CC2530 MPAGE")
    require(symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0,
            "Timebase test has unaccounted paged/initialized/absolute XDATA")
    ordinary = set()
    for start, end in xdata_ranges(symbols):
        require(0 <= start <= end <= STATUS_ADDRESS, "Timebase XDATA overlaps status/IRAM alias")
        require(not ordinary.intersection(range(start, end)), "Overlapping timebase XDATA areas")
        ordinary.update(range(start, end))
    require(len(ordinary) + STATUS_RESERVED <= 512, "Timebase exceeds 512-byte XDATA reservation budget")
    require({0, 1, 2} <= ordinary, "Timebase reader scratch is not allocated")
    for match in re.finditer(r"^S:G\$([^$]+)\$[^(\n]+\(\{(\d+)\}[^)\n]+\),F,", debug, re.MULTILINE):
        name, size = match[1], int(match[2])
        if name == "timebase_test_result":
            continue
        require("_" + name in symbols, f"Missing timebase XDATA symbol {name}")
        start = symbols["_" + name]
        require(set(range(start, start + size)) <= ordinary, "Unaccounted timebase XDATA object")
    require(set(range(symbols["_timebase_test_ticks"], symbols["_timebase_test_ticks"] + 4)) <= ordinary,
            "Timebase sample output is not allocated")
    stack = re.search(
        r"Stack starts at: 0x([0-9a-fA-F]+) \(sp set to 0x([0-9a-fA-F]+)\)"
        r" with (\d+) bytes available", memory,
    )
    require(stack is not None, "Missing timebase IRAM stack accounting")
    start, sp, size = int(stack[1], 16), int(stack[2], 16), int(stack[3])
    require(start == symbols["s_SSEG"] == symbols["__start__stack"]
            and size == symbols["l_SSEG"] and sp + 1 == start
            and 8 <= start < 128 and size >= 128 and start + size == 256,
            "Invalid timebase IRAM stack reservation")
    flash = re.search(
        r"ROM/EPROM/FLASH\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\d+)\s+(\d+)", memory,
    )
    require(flash is not None and int(flash[1], 16) == 0 and int(flash[2], 16) == max(image)
            and int(flash[3]) == len(image) and int(flash[4]) == CODE_LIMIT,
            "Timebase linked flash accounting mismatch")
    return ordinary | set(range(STATUS_ADDRESS, STATUS_ADDRESS + 8))


def check_snapshot(text, number, pc, symbols, allocated, expected_sfr):
    check_pc(section(text, number), pc)
    ram, iram, sfr = snapshot(text, number)
    require(ram[STATUS_ADDRESS:STATUS_ADDRESS + 6] == b"T24T\x01\x08", "Timebase result ABI mismatch")
    failure = int.from_bytes(ram[STATUS_ADDRESS + 6:STATUS_ADDRESS + 8], "little")
    require(failure == 0, f"SDCC timebase self-test failed at C source line {failure}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "Timebase test wrote outside allocated nonaliased XDATA")
    require(iram[128:] == b"\xc7" * 128, "Timebase test crossed upper IRAM stack guard")
    # Both sample stops are inside one real call from main; repeat via RET/LCALL.
    require(sfr[1] == symbols["s_SSEG"] + 1, "Timebase sample call leaked stack")
    require(all(sfr[address - 0x80] == value for address, value in expected_sfr.items()),
            "Timebase test changed guarded GPIO/clock/IRQ/Sleep Timer SFRs")
    return ram


def check_rejections(image, symbols, debug, memory):
    case = unittest.TestCase()
    start = symbols["_timebase_read_awake_ticks24"]
    for offset in range(len(READER_BYTES)):
        changed = dict(image)
        changed[start + offset] ^= 1
        with case.assertRaisesRegex(ValueError, "opcodes"):
            verify_reader(changed, symbols)
    changed = dict(image)
    del changed[start + 4]
    with case.assertRaisesRegex(ValueError, "opcodes"):
        verify_reader(changed, symbols)
    for name, value, message in (
        ("_timebase_read_awake_ticks24", CODE_LIMIT, "symbol"),
        ("_SOC_ST0", 0x96, "SFR"),
        ("_timebase_sample_done", start + len(READER_BYTES) - 1, "NOP"),
        ("_timebase_test_result", 0x1f00, "result address"),
        ("s_XSEG", 0x1e00, "overlaps"),
        ("l_XSEG", 449, "budget"),
        ("_timebase_test_ticks", 0x1f00, "XDATA object"),
        ("l_XABS", 1, "unaccounted"),
        ("l_XISEG", 1, "unaccounted"),
        ("l_PSEG", 1, "unaccounted"),
        ("__XPAGE", 0xa0, "MPAGE"),
        ("s_SSEG", 0x80, "stack"),
    ):
        with case.assertRaisesRegex(ValueError, message):
            verify_image(image, dict(symbols, **{name: value}), debug, memory)
    with case.assertRaisesRegex(ValueError, "unbanked"):
        verify_image(image | {CODE_LIMIT: 0}, symbols, debug, memory)
    with case.assertRaisesRegex(ValueError, "ABI size"):
        verify_image(image, symbols, debug.replace("({8}DA8d,SC:U)", "({9}DA8d,SC:U)"), memory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "timebase_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols((args.output / "timebase_test.map").read_text(encoding="utf-8"))
    debug = (args.output / "timebase_test.cdb").read_text(encoding="utf-8")
    memory = (args.output / "timebase_test.mem").read_text(encoding="utf-8")
    allocated = verify_image(image, symbols, debug, memory)
    check_rejections(image, symbols, debug, memory)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    ready, done = symbols["_timebase_sample_ready"], symbols["_timebase_sample_done"]
    reader = symbols["_timebase_read_awake_ticks24"]
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}",
                "fill iram 0x80 0xff 0xc7"]
    commands += [f"set memory sfr {address:#x} {value:#x}" for address, value in GUARD_SFRS.items()]
    commands += [f"run {symbols['_main']:#x} {ready:#x}"] + snapshot_commands(1)
    for index, ticks in enumerate(SAMPLES):
        if index:
            commands += [f"run {done:#x} {ready:#x}"]
        commands += [f"run {ready:#x} {reader:#x}",
                     "set memory sfr 0x95 " + " ".join(f"{value:#x}" for value in ticks.to_bytes(3, "little"))]
        previous = reader
        for byte, offset in enumerate(READ_OFFSETS):
            address = reader + offset
            # Synthetic prelatched bytes only: C52 has no CC2530 ticking/latching
            # model. Poison each SFR after its genuine linked read, not before.
            commands += [f"run {previous:#x} {address:#x}", "step 1",
                         f"set memory sfr {0x95 + byte:#x} {((ticks >> (8 * byte)) & 255) ^ 255:#x}"]
            previous = address + 2
        commands += [f"run {previous:#x} {done:#x}"] + snapshot_commands(10 + index * 4)
    text = simulate(args.simulator, commands, path)
    check_snapshot(text, 1, ready, symbols, allocated, GUARD_SFRS)
    for index, ticks in enumerate(SAMPLES):
        expected = dict(GUARD_SFRS)
        expected.update({0x95 + byte: value ^ 255 for byte, value in enumerate(ticks.to_bytes(3, "little"))})
        ram = check_snapshot(text, 10 + index * 4, done, symbols, allocated, expected)
        output = symbols["_timebase_test_ticks"]
        require(ram[output:output + 4] == ticks.to_bytes(4, "little"), "Wrong linked uint32_t sample/return ABI")
    print(f"Timebase: {len(image)} CODE bytes; {len(allocated) - 8} ordinary XDATA + 8 result/"
          f"{STATUS_RESERVED} reserved; {symbols['l_SSEG']} IRAM stack reserved. "
          "Linked read order, deadline/error vectors and alias/XDATA/stack guards PASS "
          "(synthetic SFR simulation only; no physical timer/timing evidence).")


if __name__ == "__main__":
    main()
