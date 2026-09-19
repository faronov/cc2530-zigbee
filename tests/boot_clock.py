#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated linked clock checks with synthetic SFRs, never oscillator hardware."""

import argparse
import hashlib
from pathlib import Path
import re
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate, snapshot,
    snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS, READ_OFFSETS, READER_BYTES
from verify_firmware import (
    CODE_LIMIT, parse_ihex, parse_symbols, require, cdb_address, verify_clock_diagnostics,
)
from clock_fixture import verify_clock_code, LENGTHS as INSTRUCTION_LENGTHS


def verify_clock(image, symbols, debug, memory, listing):
    allocated = verify_component_layout(image, symbols, debug, memory, "clock_test_result",
                                         ("clock.c", "timebase.c", "test_clock.c"))
    require(set(image) == set(range(3402)) and
            hashlib.sha256(bytes(image[a] for a in range(3402))).hexdigest() ==
            "82d47ac15fe82617a3ad86052e785cfa57b2e61e028f63113487e468efa128d8",
            "Clock complete standalone instructions/CODE/caller/runtime changed")
    names = ("_main", "_clock_select_init", "_clock_test_cycle", "_clock_test_before", "_clock_test_done",
             "_timebase_read_awake_ticks24", "_timebase_deadline_after", "_timebase_expired")
    require(all(name in symbols and symbols[name] in image for name in names)
            and len({symbols[name] for name in names}) == len(names) - 1,
            "Missing/overlapping clock code symbols")
    # cycle and before intentionally name the same first NOP.
    require(symbols["_clock_test_cycle"] == symbols["_clock_test_before"], "Clock cycle entry changed")
    require(all(image[symbols[name]] == 0 for name in ("_clock_test_before", "_clock_test_done")),
            "Clock checkpoint is not NOP")
    sfrs = {"CLKCONCMD": 0xc6, "CLKCONSTA": 0x9e, "SLEEPCMD": 0xbe,
            "IEN0": 0xa8, "IEN1": 0xb8, "IEN2": 0x9a, "ST0": 0x95, "ST1": 0x96, "ST2": 0x97}
    require(all(symbols.get("_SOC_" + name) == address for name, address in sfrs.items()),
            "Clock SFR address changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(symbols["_timebase_deadline_after"] == reader + len(READER_BYTES)
            and all(image.get(reader + i) == value for i, value in enumerate(READER_BYTES)),
            "Clock test reader opcodes/extent changed")
    require({0, 1, 2} <= allocated, "Clock test reader scratch is not allocated")
    for name, size in (("clock_test_diagnostics", 19), ("clock_test_source", 1),
                       ("clock_test_timeout", 4), ("clock_test_limit", 2), ("clock_test_return", 1)):
        records = re.findall(rf"^S:G\${name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(records and all(int(value) == size for value in records), "Clock test debug ABI changed")
        address = symbols.get("_" + name, CODE_LIMIT)
        require(set(range(address, address + size)) <= allocated and address + size <= 0x1e00,
                "Clock test object is not ordinary XDATA")
    verify_clock_diagnostics(debug, ("clock", "test_clock"))

    start = cdb_address(debug, "L:Fclock$effective_status$0$0")
    end = cdb_address(debug, "L:XG$clock_select_init$0$0") + 3
    require(symbols["_timebase_expired"] < start < symbols["_clock_select_init"] < end,
            "Clock module extent changed")
    instructions = {}
    for match in re.finditer(r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.MULTILINE):
        address, data = int(match[1], 16), bytes.fromhex(match[2])
        require(address not in instructions, "Duplicate linked clock instruction")
        require(INSTRUCTION_LENGTHS.get(data[0]) == len(data), "Unreviewed clock instruction/length")
        instructions[address] = data
    require(instructions and min(instructions) == start and max(instructions) < end,
            "Clock listing extent mismatch")
    covered = {}
    for address, data in instructions.items():
        for offset, value in enumerate(data):
            require(address + offset not in covered, "Overlapping linked clock instructions")
            covered[address + offset] = value
    require(set(covered) == set(range(start, end))
            and all(image.get(address) == value for address, value in covered.items()),
            "Clock listing differs from actual linked bytes")
    require(image[end - 1] == 0x22, "Clock module does not end in RET")

    decoded, sites = verify_clock_code(image, symbols, debug)
    require(list(decoded.items()) == list(instructions.items()),
            "Ordered clock listing differs from linked CODE")
    return allocated, sites


def check_rejections(image, symbols, debug, memory, listing):
    case = unittest.TestCase()
    _, sites = verify_clock(image, symbols, debug, memory, listing)
    for address in list(sites.values()) + [symbols["_timebase_read_awake_ticks24"]]:
        for offset in range(2):
            changed = dict(image)
            changed[address + offset] ^= 1
            with case.assertRaises(ValueError):
                verify_clock(changed, symbols, debug, memory, listing)
    for operand in (0x95, 0x96, 0x97, 0xbe, 0xa8, 0x9e, 0x80):
        changed = dict(image)
        changed[sites[(0x8f, 0xc6)] + 1] = operand
        with case.assertRaisesRegex(ValueError, "instructions|peripheral"):
            verify_clock(changed, symbols, debug, memory, listing.replace("8F C6", f"8F {operand:02X}"))
    for name, value in (
        ("_SOC_SLEEPCMD", 0x9d), ("_SOC_CLKCONSTA", 0xc6), ("_clock_select_init", CODE_LIMIT),
        ("_clock_test_done", symbols["_clock_test_done"] + 1), ("_clock_test_result", 0x1f00),
        ("_clock_test_diagnostics", 0x1e10), ("s_XSEG", 0x1e00), ("l_XSEG", 449),
        ("l_XISEG", 1), ("l_XABS", 1), ("l_PSEG", 1), ("__XPAGE", 0xa0), ("s_SSEG", 0x80),
    ):
        with case.assertRaises(ValueError):
            verify_clock(image, dict(symbols, **{name: value}), debug, memory, listing)
    for changed in (debug.replace("C$clock.c$", "C$missing.c$"),
                    debug.replace("({19}ST", "({20}ST"),
                    debug.replace("{18}S:S$rollback_result", "{17}S:S$rollback_result"),
                    debug + "\nL:Fclock$effective_status$0$0:8000\n"):
        with case.assertRaises(ValueError):
            verify_clock(image, symbols, changed, memory, listing)
    with case.assertRaises(ValueError):
        verify_clock(image | {CODE_LIMIT: 0}, symbols, debug, memory, listing)
    with case.assertRaises(ValueError):
        verify_clock(image, symbols, debug, memory.replace("bytes available", "bytes absent"), listing)
    with case.assertRaises(ValueError):
        verify_clock(image, symbols, debug, memory, "")
    lines = listing.splitlines(keepends=True)
    rows = [index for index, line in enumerate(lines) if re.match(
        r"^\s+[0-9A-F]{6} (?:[0-9A-F]{2} ){1,3}\s+\[\s*\d+\]", line)]
    require(len(rows) > 1, "Missing clock listing mutation sites")
    dropped, duplicate, reordered = lines.copy(), lines.copy(), lines.copy()
    del dropped[rows[0]]
    duplicate.insert(rows[0], lines[rows[0]])
    reordered[rows[0]], reordered[rows[1]] = reordered[rows[1]], reordered[rows[0]]
    for changed in (dropped, duplicate, reordered):
        with case.assertRaises(ValueError):
            verify_clock(image, symbols, debug, memory, "".join(changed))


def vectors():
    # Phases: command, start, [(observed CMD, STA, ticks)], result.
    for saved, target, status in ((0xc1, 0x80, 0xc9), (0x80, 0xc1, 0x80), (0x39, 0x39, 0x39)):
        if saved == 0x39:
            yield ("divided entry", saved, status, 0, 10, 3, [], 2, {})
        else:
            target_status = 0xc9 if target == 0xc1 else target
            yield ("switch", saved, status, int(target == 0x80), 10, 3,
                   [(target, 0xfffffe, [(target, status, 0xffffff), (target, target_status, 0)], 0)], 0, {})
    yield ("idempotent clamped", 0xc1, 0xc9, 0, 0, 1, [], 0, {})
    for sfr, value in ((0xa8, 0x80), (0xb8, 1), (0x9a, 1), (0xbe, 0x05), (0xbe, 0)):
        yield ("unsupported IRQ/power", 0xc9, 0xc9, 1, 10, 3, [], 2, {sfr: value})
    yield ("pending entry", 0x88, 0xc9, 1, 10, 3, [], 2, {})
    yield ("invalid source", 0xc9, 0xc9, 2, 10, 3, [], 1, {})
    yield ("invalid cap", 0xc9, 0xc9, 1, 10, 0, [], 1, {})
    yield ("invalid timeout", 0xc9, 0xc9, 1, 0x800000, 3, [], 1, {})
    for ticks, sta, result in ((110, 0x88, 0), (110, 0xc9, 3), (111, 0x88, 3),
                                (99, 0x88, 6), (0x800064, 0x88, 6), (0x80006e, 0x88, 5)):
        phases = [(0x88, 100, [(0x88, sta, ticks)], result)]
        if result:
            phases.append((0xc9, 200, [(0xc9, 0x88, 201), (0xc9, 0xc9, 202)], 0))
        yield ("deadline/range", 0xc9, 0xc9, 1, 10, 3, phases, result, {})
    yield ("backward within window", 0xc9, 0xc9, 1, 10, 3,
           [(0x88, 100, [(0x88, 0xc9, 102), (0x88, 0x88, 101)], 6),
            (0xc9, 200, [(0xc9, 0xc9, 200)], 0)], 6, {})
    yield ("changed command", 0xc9, 0xc9, 1, 10, 3,
           [(0x88, 100, [(0x89, 0x88, 101)], 7),
            (0xc9, 200, [(0xc9, 0xc9, 200)], 0)], 7, {})
    for ticks, cmd, result in ((211, 0xc9, 9), (199, 0xc9, 6), (0x8000d2, 0xc9, 5), (201, 0x88, 7)):
        yield ("rollback failure", 0xc9, 0xc9, 1, 10, 3,
               [(0x88, 100, [(0x88, 0xc9, 110)], 3), (0xc9, 200, [(cmd, 0xc9, ticks)], result)], 3, {})
    yield ("both stopped, 16-bit poll counts", 0xc9, 0xc9, 1, 10, 257,
           [(0x88, 100, [(0x88, 0xc9, 100)] * 257, 4),
            (0xc9, 100, [(0xc9, 0x88, 100)] * 257, 4)], 4, {})
    yield ("last poll success", 0xc9, 0xc9, 1, 10, 3,
           [(0x88, 100, [(0x88, 0xc9, 101), (0x88, 0xc9, 102), (0x88, 0x88, 103)], 0)], 0, {})
    yield ("maximum timeout", 0x80, 0x80, 0, 0x7fffff, 1,
           [(0xc1, 0xffffff, [(0xc1, 0xc9, 0x7ffffe)], 0)], 0, {})
    for status, result in ((0x88, 0), (0xc9, 3)):
        phases = [(0x88, 100, [(0x88, status, 100)], result)]
        if result:
            phases.append((0xc9, 100, [(0xc9, 0xc9, 100)], 9))
        yield ("zero timeout", 0xc9, 0xc9, 1, 0, 1, phases, result, {})
    for saved, target, delayed in ((0xc9, 0x88, 0x89), (0x88, 0xc9, 0xc9)):
        for count in (1, 2, 16):
            polls = [(saved, saved, (0xfffff0 + i) & 0xffffff) for i in range(1, count + 1)]
            polls += [(saved, delayed, (0xfffff1 + count) & 0xffffff),
                      (saved, saved, (0xfffff2 + count) & 0xffffff)]
            yield ("old-match then delayed source then restored", saved, saved, int(target == 0x88), 64, 20,
                   [(target, 100, [(target, saved, 165)], 3), (saved, 0xfffff0, polls, 0)], 3, {})
    for polls in ([(0xc9, 0xc9, 210)], [(0xc9, 0xc9, 200)] * 257):
        yield ("never-departed cancellation is unconfirmed", 0xc9, 0xc9, 1, 10, 257,
               [(0x88, 100, [(0x88, 0xc9, 111)], 3), (0xc9, 200, polls, 9)], 3, {})
    yield ("late confirmed source then restored", 0xc9, 0xc9, 1, 10, 1,
           [(0x88, 100, [(0x88, 0x88, 111)], 3),
            (0xc9, 200, [(0xc9, 0xc9, 210)], 0)], 3, {})
    yield ("departure at last poll is not restored", 0xc9, 0xc9, 1, 10, 2,
           [(0x88, 100, [(0x88, 0xc9, 111)], 3),
            (0xc9, 200, [(0xc9, 0xc9, 201), (0xc9, 0x89, 202)], 4)], 3, {})


def check_execution(simulator, path, symbols, allocated, sites, vector):
    name, saved, initial_status, source, timeout, limit, phases, result, overrides = vector
    before, done = symbols["_clock_test_before"], symbols["_clock_test_done"]
    reader = symbols["_timebase_read_awake_ticks24"]
    reads = {operand: address for (opcode, operand), address in sites.items() if opcode == 0xe5}
    reads.update({0x95 + i: reader + offset for i, offset in enumerate(READ_OFFSETS)})
    write = sites[(0x8f, 0xc6)]
    guards = dict(GUARD_SFRS)
    guards.update({0xc6: saved, 0x9e: initial_status, 0xbe: 0x84, 0x9d: 0x60})
    guards.update(overrides)
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}",
                "fill iram 0x80 0xff 0xc7"]
    commands += [f"set memory sfr {address:#x} {value:#x}" for address, value in guards.items()]
    commands += [f"run {symbols['_main']:#x} {before:#x}"]
    for symbol, value, size in (("source", source, 1), ("timeout", timeout, 4), ("limit", limit, 2)):
        commands += [f"set memory xram {symbols['_clock_test_' + symbol]:#x} " +
                     " ".join(f"{byte:#x}" for byte in value.to_bytes(size, "little"))]
    diag_address = symbols["_clock_test_diagnostics"]
    commands += [f"fill xram {diag_address:#x} {diag_address + 18:#x} 0xa5"]
    commands += [f"break {address:#x}" for address in list(reads.values()) + [write, done]]
    commands += ["step 1"]
    events = []
    if result != 1:
        events += [("read", address, guards[address]) for address in (0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e)]
    expected_diag = bytearray(19)
    target = (saved & 0xb8) | (0x41 if source == 0 else 0)
    expected_diag[14:] = bytes([saved, target, saved, initial_status, 8])
    for phase_index, (command, start, polls, cause) in enumerate(phases):
        events += [("read", 0x95 + i, byte) for i, byte in enumerate(start.to_bytes(3, "little"))]
        events.append(("write", 0xc6, command))
        current_command = command
        for observed_command, status, ticks in polls:
            if observed_command != current_command:
                events.append(("inject", 0xc6, observed_command))
                current_command = observed_command
            events += [("read", 0xc6, observed_command), ("read", 0x9e, status)]
            events += [("read", 0x95 + i, byte) for i, byte in enumerate(ticks.to_bytes(3, "little"))]
        elapsed = (polls[-1][2] - start) & 0xffffff
        expected_diag[phase_index * 7:phase_index * 7 + 7] = (
            elapsed.to_bytes(4, "little") + len(polls).to_bytes(2, "little") + bytes([2 if cause == 5 else 0]))
        expected_diag[16:18] = bytes(polls[-1][:2])
        if phase_index:
            expected_diag[18] = cause
    expected_events = []
    for kind, operand, value in events:
        if kind == "inject":
            commands += [f"set memory sfr {operand:#x} {value:#x}"]
            guards[operand] = value
            continue
        address = write if kind == "write" else reads[operand]
        number = 10 + len(expected_events) * 2
        commands += ["run", marker(number), "state", marker(number + 1)]
        if kind == "read" and operand in (0x95, 0x96, 0x97, 0x9e):
            commands += [f"set memory sfr {operand:#x} {value:#x}"]
        commands += ["step 1", f"dump /h sfr {0xc6 if kind == 'write' else 0xe0:#x} "
                     f"{0xc6 if kind == 'write' else 0xe0:#x}", marker(number + 2)]
        expected_events.append((number, address, 0xc6 if kind == "write" else 0xe0, value))
        if kind == "write" or operand in (0x95, 0x96, 0x97, 0x9e):
            guards[operand] = value
    final = 12 + len(expected_events) * 2
    commands += ["run"] + snapshot_commands(final)
    text = simulate(simulator, commands, path)
    # Index the unchanged complete transcript once, not once per MMIO event.
    split = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    blocks = {int(split[i], 16): split[i+1] for i in range(1, len(split), 2)}
    for number, address, sfr, value in expected_events:
        check_pc(blocks[number], address)
        require(memory_dump(blocks[number + 1], sfr, 1) == bytes([value]),
                f"{name}: linked MMIO read/write value mismatch")
    check_pc(section(text, final), done)
    ram, iram, sfr = snapshot(text, final)
    require(ram[0x1e00:0x1e08] == b"CLKT\x01\x08\0\0", f"{name}: SDCC argument self-test/ABI failed")
    require(ram[symbols["_clock_test_return"]] == result, f"{name}: wrong clock result")
    for symbol, value, size in (("source", source, 1), ("timeout", timeout, 4), ("limit", limit, 2)):
        address = symbols["_clock_test_" + symbol]
        require(ram[address:address + size] == value.to_bytes(size, "little"),
                f"{name}: clock changed caller input/diagnostic guard")
    require(ram[diag_address:diag_address + 19] == (b"\xa5" * 19 if result == 1 else expected_diag),
            f"{name}: wrong complete clock diagnostics: {ram[diag_address:diag_address + 19].hex()}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            f"{name}: clock wrote outside allocated XDATA")
    require(iram[128:] == b"\xc7" * 128 and sfr[1] == symbols["s_SSEG"] + 1,
            f"{name}: clock stack guard/unwind failed")
    require(all(sfr[address - 0x80] == value for address, value in guards.items()),
            f"{name}: clock changed guarded SFR")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "clock_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols((args.output / "clock_test.map").read_text(encoding="utf-8"))
    debug = (args.output / "clock_test.cdb").read_bytes().decode("utf-8")
    memory = (args.output / "clock_test.mem").read_text(encoding="utf-8")
    # This link's listing must match; board inspection uses its own IHX/CDB,
    # not a listing that a later standalone link may overwrite.
    listing = (args.output / "clock_test.clock.rst").read_text(encoding="utf-8")
    allocated, sites = verify_clock(image, symbols, debug, memory, listing)
    check_rejections(image, symbols, debug, memory, listing)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    cases = list(vectors())
    for vector in cases:
        check_execution(args.simulator, path, symbols, allocated, sites, vector)
    print(f"Clock: {len(image)} CODE; {len(allocated) - 8} ordinary XDATA + 8 result/64 reserved; "
          f"{symbols['l_SSEG']} IRAM stack reserved. {len(cases)} linked scenarios, exact MMIO/calls, "
          "diagnostic ABI and alias/stack guards PASS (synthetic clocks; no analogue startup evidence).")


if __name__ == "__main__":
    main()
