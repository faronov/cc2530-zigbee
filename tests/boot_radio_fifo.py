#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated FIFO instruction/ABI checks with explicit synthetic radio effects."""

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
    CODE_LIMIT, cdb_address, parse_ihex, parse_symbols, peripheral_accesses, require,
)
from radio_fifo_fixture import verify_fifo_relocated


XREGS = (0x6189, 0x618a, 0x61e1, 0x6192, 0x618b, 0x6193,
         0x619b, 0x619c, 0x619d, 0x619e, 0x619f, 0x61a1, 0x61a2)
SFR_READS = (0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e, 0xbf)
FIELDS = ("elapsed_ticks", "polls", "timebase_status", "strobes", "confirmed",
          "bytes_written", "bytes_verified", "errors", "rx_count", "tx_count",
          "rx_first", "rx_last", "rx_packet", "tx_first", "tx_last", "fifo_signals", "sample_valid")
SIZES = (4, 2) + (1,) * 15
MODULE_SHA256 = "8893db269f9a60b12a57b9d74530bc0e0769661454b366c25bda5feb7e37079b"
MODULE_SIZE = 3042
# Pinned SDCC 4.2.0 model-large module, including branches and call relocations,
# in this isolated link. Also independently decode every peripheral operand.
LENGTHS = {
    op: size for size, text in (
        (1, "08 09 0b 0c 18 19 1a 1d 22 28 2a 2c 2e 33 3b 3d 3f 4b 4d 4e 4f "
            "98 99 9a 9c 9d 9e 9f a3 c3 e0 e4 e8 e9 ea eb ec ed ee ef f0 f8 f9 fa fb fc fd fe ff"),
        (2, "24 25 34 35 40 45 50 54 60 70 74 78 79 7d 7e 7f 80 88 89 8a 8b 8c 8d 8e 8f "
            "94 95 a2 a9 ab ac ad ae af c0 c2 d0 d2 e5 f5"),
        (3, "02 12 20 30 43 53 75 85 90 b5 b8 b9 ba be bf"),
    ) for op in bytes.fromhex(text)
}


def verify_module(image, symbols, debug):
    start = cdb_address(debug, "L:Fradio_fifo$observe$0$0")
    end = cdb_address(debug, "L:XG$radio_fifo_preload_init$0$0") + 1
    require(start < symbols["_radio_fifo_clear_init"] < symbols["_radio_fifo_preload_init"] < end < CODE_LIMIT,
            "FIFO code extent/ABI changed")
    data = bytes(image.get(i, 0xff) for i in range(start, end))
    require(len(data) == MODULE_SIZE and hashlib.sha256(data).hexdigest() == MODULE_SHA256,
            "FIFO exact linked instructions/relocations changed")
    instructions = {}
    pc = start
    while pc < end:
        size = LENGTHS.get(image[pc])
        require(size is not None and pc + size <= end, "Unreviewed FIFO instruction")
        instructions[pc] = bytes(image[i] for i in range(pc, pc + size))
        pc += size
    accesses = peripheral_accesses(instructions)
    require(tuple(data for _, data, _ in accesses) ==
            tuple(bytes((0xe5, reg)) for reg in SFR_READS) +
            (b"\x75\xe1\xed", b"\x75\xe1\xee", b"\x89\xd9"),
            "FIFO peripheral instruction contract changed")
    sites = {("read", reg): pc for pc, data, reg in accesses if data[0] == 0xe5}
    sites.update({("write", data[2] if reg == 0xe1 else 0xd9): pc
                  for pc, data, reg in accesses if data[0] != 0xe5})
    xaccesses = []
    for pc, data in instructions.items():
        if data[0] == 0x90 and int.from_bytes(data[1:], "big") >= 0x1e00:
            reg = int.from_bytes(data[1:], "big")
            require(instructions.get(pc + 3) == b"\xe0", "FIFO XREG is not a single MOVX read")
            xaccesses.append(reg)
            sites[("read", reg)] = pc + 3
    require(tuple(xaccesses) == XREGS, "FIFO XREG/identity/RAM access changed")
    return instructions, sites


def verify_image(image, symbols, debug, memory, listing):
    allocated = verify_component_layout(image, symbols, debug, memory, "radio_fifo_test_result",
                                         ("radio_fifo.c", "timebase.c", "test_radio_fifo.c"))
    instructions, sites = verify_module(image, symbols, debug)
    listed = {int(m[1], 16): bytes.fromhex(m[2]) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.MULTILINE)}
    require(instructions == listed, "FIFO listing differs from actual instructions")
    for name, reg in (("RFD", 0xd9), ("RFST", 0xe1), ("RFERRF", 0xbf),
                      ("ST0", 0x95), ("ST1", 0x96), ("ST2", 0x97)):
        require(symbols.get("_SOC_" + name) == reg, "FIFO SFR symbol changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(symbols["_timebase_deadline_after"] == reader + len(READER_BYTES) and
            bytes(image[reader + i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "FIFO real timebase reader changed")
    sites.update({("read", 0x95 + i): reader + offset for i, offset in enumerate(READ_OFFSETS)})
    for name in ("before", "done"):
        require(image.get(symbols["_radio_fifo_test_" + name]) == 0, "FIFO checkpoint is not NOP")
    require(symbols["_radio_fifo_test_cycle"] == symbols["_radio_fifo_test_before"],
            "FIFO test call/checkpoint changed")
    expected = []
    offset = 0
    for name, size in zip(FIELDS, SIZES):
        expected.append((offset, name, size))
        offset += size
    for module in ("radio_fifo", "test_radio_fifo"):
        records = re.findall(rf"^T:F{module}\$__00000000\[(.*)\]$", debug, re.MULTILINE)
        require(records, "Missing FIFO diagnostic ABI")
        for record in records:
            fields = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
            require([(int(a), b, int(c)) for a, b, c in fields] == expected, "FIFO diagnostic ABI changed")
    for name, size in (("diagnostics", 21), ("body", 125), ("action", 1), ("return", 1),
                       ("length", 2), ("timeout", 4), ("limit", 2)):
        symbol = "_radio_fifo_test_" + name
        records = re.findall(rf"^S:G\${symbol[1:]}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(records and all(int(n) == size for n in records), "FIFO test object ABI changed")
        require(set(range(symbols[symbol], symbols[symbol] + size)) <= allocated,
                "FIFO test object is outside ordinary XDATA")
    for name in ("clear", "preload"):
        require(f"F:G$radio_fifo_{name}_init$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug,
                "FIFO byte-return ABI changed")
    require(re.search(r"S:Lradio_fifo.radio_fifo_preload_init\$body\$[^(]+\(\{3\}DG,SC:U\)", debug)
            and re.search(r"S:Lradio_fifo.radio_fifo_preload_init\$diagnostics\$[^(]+\(\{2\}DX,ST", debug),
            "FIFO generic body / XDATA output ABI changed")
    return allocated, sites


def check_rejections(image, symbols, debug, memory, listing):
    case = unittest.TestCase()
    instructions, sites = verify_module(image, symbols, debug)
    # Every reviewed instruction byte is covered, not only representative strobes.
    for address in range(min(instructions), max(instructions) + len(instructions[max(instructions)])):
        changed = dict(image)
        changed[address] ^= 1
        with case.assertRaisesRegex(ValueError, "instructions"):
            verify_module(changed, symbols, debug)
    for key in sites:
        changed = dict(image)
        del changed[sites[key]]
        with case.assertRaises(ValueError):
            verify_module(changed, symbols, debug)
    for name, value in (
        ("_SOC_RFD", 0xe1), ("_SOC_RFST", 0xd9), ("_SOC_RFERRF", 0xe9),
        ("_radio_fifo_clear_init", CODE_LIMIT), ("_radio_fifo_test_result", 0x1f00),
        ("_radio_fifo_test_diagnostics", 0x1e00), ("_radio_fifo_test_body", 0x6000),
        ("s_XSEG", 0x1e00), ("l_XSEG", 449), ("l_XABS", 1), ("l_XISEG", 1),
        ("l_PSEG", 1), ("__XPAGE", 0xa0), ("s_SSEG", 0x80),
    ):
        with case.assertRaises(ValueError):
            verify_image(image, dict(symbols, **{name: value}), debug, memory, listing)
    for changed in (debug.replace("({21}ST", "({22}ST"),
                    debug.replace("{10}S:S$bytes_verified", "{9}S:S$bytes_verified"),
                    debug.replace("C$radio_fifo.c$", "C$absent.c$"),
                    debug.replace("({2}DX,ST", "({3}DG,ST")):
        with case.assertRaises(ValueError):
            verify_image(image, symbols, changed, memory, listing)
    for im, mem, lst in ((image | {CODE_LIMIT: 0}, memory, listing),
                         (image, memory.replace("bytes available", "bytes absent"), listing),
                         (image, memory, "")):
        with case.assertRaises(ValueError):
            verify_image(im, symbols, debug, mem, lst)


def baseline():
    values = dict(GUARD_SFRS)
    values.update({0xc6: 0x88, 0x9e: 0x88, 0xbe: 0x84, 0xbf: 0, 0xd9: 0x77, 0xe1: 0xd0})
    values.update({0x88: 0xaf, 0x91: 0x35, 0x9b: 3, 0xa9: 0x31, 0xb9: 0x0e,
                   0xc0: 0x82, 0xd8: 0x4f, 0xe9: 0xaa})
    values.update(dict.fromkeys(XREGS, 0))
    values.update({0x6189: 0x40, 0x618a: 1, 0x61e1: 23, 0x6192: 0x3f, 0x6193: 0x18})
    return values


def vector(name, *, action=0, length=1, timeout=100, limit=256, start=0xfffffe,
           initial=None, operations=(), result=0):
    return dict(name=name, action=action, length=length, timeout=timeout, limit=limit,
                start=start, initial=initial or {}, operations=operations, result=result)


def rx_reset():
    return dict.fromkeys((0x619b, 0x619d, 0x619e, 0x619f), 0) | {0x6193: 0x18}


def tx_reset():
    return dict.fromkeys((0x619c, 0x61a1, 0x61a2), 0)


def vectors():
    rx = {0x619b: 128, 0x619d: 127, 0x619e: 127, 0x619f: 128, 0x6193: 0xd8}
    tx = {0x619c: 128, 0x61a1: 7, 0x61a2: 128}
    yield vector("already reset", result=1)
    yield vector("flush both, delayed/mixed pointer observations", initial=rx | tx,
                 operations=[(0xed, [(0xffffff, {}), (0, {0x619b: 0}), (1, rx_reset())], True),
                             (0xee, [(2, {}), (3, tx_reset())], True)])
    yield vector("RX only", initial=rx, operations=[(0xed, [(0, rx_reset())], True)])
    yield vector("TX only", initial=tx, operations=[(0xee, [(0, tx_reset())], True)])
    yield vector("empty count but stale pointer", initial={0x61a2: 1},
                 operations=[(0xee, [(0, tx_reset())], True)])
    for length in (1, 2, 3, 125):
        payload = bytes(i ^ 0x69 for i in range(length))
        operations = []
        for i, byte in enumerate(bytes([length + 2]) + payload, 1):
            operations.append((byte, [((0xfffffe + i) & 0xffffff, {0x619c: i, 0x61a2: i})], True))
        yield vector(f"XDATA preload {length}", action=1, length=length, timeout=1000, operations=operations)
    yield vector("CODE preload with RX preserved", action=2, length=3, initial=rx,
                 operations=[(byte, [(i, {0x619c: i, 0x61a2: i})], True)
                             for i, byte in enumerate((5, 0x31, 0x69, 0xa5), 1)])
    yield vector("count alone cannot confirm byte", action=1, start=100,
                 operations=[(3, [(101, {0x619c: 1}), (102, {0x61a2: 1})], True),
                             (0x69, [(103, {0x619c: 2, 0x61a2: 2})], True)])
    for operation, initial in ((0xed, rx), (0xee, tx)):
        yield vector("flush stopped counter cap", initial=initial, limit=257,
                     operations=[(operation, [(0xfffffe, {})] * 257, False)], result=9)
        yield vector("flush zero count but pointers unconfirmed", initial=initial, limit=2,
                     operations=[(operation, [(0xffffff, {0x619b if operation == 0xed else 0x619c: 0}),
                                              (0, {})], False)], result=9)
        for error in (1, 2, 4, 8, 16, 32, 64):
            yield vector("flush controller error", initial=initial,
                         operations=[(operation, [(0, {0xbf: error})], False)], result=6)
    yield vector("partial clear no second strobe after cap", initial=rx | tx, limit=1,
                 operations=[(0xed, [(0, rx_reset())], True)], result=9)
    for error in (1, 2, 4, 8, 16, 32, 64, 127):
        yield vector("latched error no clearing", initial={0xbf: error}, result=6)
        yield vector("preload controller error retains written byte", action=1,
                     operations=[(3, [(0, {0xbf: error, 0x619c: 1, 0x61a2: 1})], False)], result=6)
    for reg, value, result in (
        (0xa8, 0x80, 3), (0xb8, 2, 3), (0x9a, 1, 3), (0xbe, 5, 3),
        (0xc6, 0xc9, 3), (0xc6, 0x89, 3), (0x9e, 0xc9, 3),
        (0x6189, 0x60, 3), (0x6189, 0, 3), (0x6189, 0x42, 3),
        (0x6189, 0x48, 3), (0x618a, 3, 3), (0x618a, 0x81, 3),
        (0x61e1, 0x20, 4), (0x61e1, 0x80, 3), (0x6192, 0x40, 4),
        (0x6192, 0x80, 3), (0x618b, 0x80, 4), (0x6193, 1, 4),
        (0x6193, 2, 4), (0x6193, 4, 4), (0x6193, 0x20, 4),
        (0x6193, 0x40, 6),
        (0xbf, 0x80, 3), (0x619d, 0x80, 3), (0x619e, 0x80, 3),
        (0x619b, 129, 7), (0x619c, 255, 7),
    ):
        yield vector("unsupported/active/corrupt entry", initial={reg: value}, result=result)
    for reg in (0x619c, 0x61a1, 0x61a2):
        yield vector("preload needs reset-empty TX", action=1, initial={reg: 1}, result=5)
    for options in (dict(timeout=0), dict(timeout=0x800000), dict(limit=0),
                    dict(action=1, length=0), dict(action=1, length=126), dict(action=1, length=65535)):
        yield vector("invalid output preserved", **options, result=2)
    for ticks, result in ((110, 8), (111, 8), (99, 11), (0x800064, 11), (0x80006e, 10)):
        yield vector("late/range/ambiguous accepted count not success", action=1, start=100, timeout=10,
                     operations=[(3, [(ticks, {0x619c: 1, 0x61a2: 1})], False)], result=result)
    yield vector("partial byte no extra write at cap", action=1, limit=1,
                 operations=[(3, [(0, {0x619c: 1, 0x61a2: 1})], True)], result=9)
    yield vector("backward observation within total window", action=1, start=100,
                 operations=[(3, [(102, {}), (101, {0x619c: 1, 0x61a2: 1})], False)], result=11)
    yield vector("max deadline across wrap", initial=tx, timeout=0x7fffff,
                 operations=[(0xee, [(0x7ffffc, tx_reset())], True)])
    for change, result in (({0x619c: 2}, 7), ({0x61a1: 1}, 7), ({0x61a2: 2}, 7),
                           ({0x619b: 1}, 12), ({0x61e1: 0x20}, 4),
                           ({0x6193: 0x40}, 6),
                           ({0xc6: 0x80, 0x9e: 0x80}, 12), ({0xc6: 0xc9, 0x9e: 0xc9}, 3)):
        yield vector("post-write lost ownership/count", action=1,
                     operations=[(3, [(0, change)], False)], result=result)


def execution(simulator, path, symbols, allocated, sites, v):
    values = baseline() | v["initial"]
    diag = bytearray(21)
    events = []

    def observe():
        diag[20] = 0
        for address in SFR_READS[:6]:
            events.append(("read", address, values[address]))
        if (values[0xa8] or values[0xb8] or values[0x9a] or values[0xbe] & 7 != 4
                or values[0xc6] & 0x47 or values[0xc6] != values[0x9e]):
            return
        for address in XREGS + (0xbf,):
            events.append(("read", address, values[address]))
        for offset, address in enumerate((0xbf, 0x619b, 0x619c, 0x619d, 0x619e, 0x619f,
                                          0x61a1, 0x61a2, 0x6193), 11):
            diag[offset] = values[address]
        diag[20] = 1

    def sample(ticks):
        for i, byte in enumerate(ticks.to_bytes(3, "little")):
            events.append(("read", 0x95 + i, byte))

    if v["result"] == 2:
        diag[:] = b"\xa5" * 21
    else:
        observe()
    if v["operations"]:
        sample(v["start"])
    polls = 0
    for operation, observations, confirmed in v["operations"]:
        is_flush = v["action"] == 0
        events.append(("write", operation if is_flush else 0xd9, operation))
        if is_flush:
            diag[7] |= 1 if operation == 0xed else 2
        else:
            diag[9] += 1
        for ticks, changed in observations:
            values.update(changed)
            observe()
            sample(ticks)
            polls += 1
            diag[0:4] = ((ticks - v["start"]) & 0xffffff).to_bytes(4, "little")
            diag[4:6] = polls.to_bytes(2, "little")
        if confirmed:
            if is_flush:
                diag[8] |= 1 if operation == 0xed else 2
            else:
                diag[10] += 1
    if v["result"] == 10:
        diag[6] = 2

    before, done = (symbols["_radio_fifo_test_" + s] for s in ("before", "done"))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x61ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7"]
    current = baseline() | v["initial"]
    for address, value in current.items():
        commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
    commands.append(f"run {symbols['_main']:#x} {before:#x}")
    for name, size in (("action", 1), ("length", 2), ("timeout", 4), ("limit", 2)):
        commands.append(f"set memory xram {symbols['_radio_fifo_test_' + name]:#x} " +
                        " ".join(hex(b) for b in v[name].to_bytes(size, "little")))
    output = symbols["_radio_fifo_test_diagnostics"]
    commands.append(f"fill xram {output:#x} {output + 20:#x} 0xa5")
    commands += [f"break {pc:#x}" for pc in list(sites.values()) + [done]]
    commands.append("step 1")
    checked = []
    for index, (kind, address, value) in enumerate(events):
        pc = sites[(kind, address)]
        number = 10 + index * 3
        commands += ["run", marker(number), "state"]
        if kind == "read":
            commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
            current[address] = value
            if address >= 256:
                commands.append("dump /h sfr 0x82 0x83")
        commands += [marker(number + 1), "step 1"]
        observed = 0xe0 if kind == "read" else 0xd9 if address == 0xd9 else 0xe1
        if kind == "write":
            current[observed] = value
        commands += [f"dump /h sfr {observed:#x} {observed:#x}", marker(number + 2)]
        checked.append((number, pc, address if kind == "read" and address >= 256 else None, observed, value))
    final = 12 + len(events) * 3
    commands += ["run"] + snapshot_commands(final)
    commands += [marker(final + 4), "dump /h xram 0x6000 0x61ff", marker(final + 5)]
    text = simulate(simulator, commands, path)
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.MULTILINE)
    numbers = [int(parts[i], 16) for i in range(1, len(parts), 2)]
    require(len(numbers) == len(set(numbers)), "Duplicate FIFO simulator marker")
    blocks = dict(zip(numbers, parts[2::2]))
    for number, pc, xreg, observed, value in checked:
        require(all(n in blocks for n in range(number, number + 3)), "Missing FIFO simulator marker")
        check_pc(blocks[number], pc)
        if xreg is not None:
            require(memory_dump(blocks[number], 0x82, 2) == xreg.to_bytes(2, "little"),
                    v["name"] + ": wrong actual MOVX address")
        actual = memory_dump(blocks[number + 1], observed, 1)[0]
        require(actual == value,
                f"{v['name']}: MMIO at {pc:04x}, register {observed:04x}: {actual:02x} != {value:02x}")
    check_pc(section(text, final), done)
    ram, iram, sfr = snapshot(text, final)
    require(ram[0x1e00:0x1e08] == b"RFQT\x01\x08\0\0", "SDCC FIFO argument self-test failed")
    require(ram[symbols["_radio_fifo_test_return"]] == v["result"],
            f"{v['name']}: unexpected result {ram[symbols['_radio_fifo_test_return']]}")
    require(ram[output:output + 21] == diag,
            f"{v['name']}: diagnostic ABI mismatch {ram[output:output + 21].hex()} != {diag.hex()}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "FIFO wrote unallocated/status/alias XDATA")
    require(iram[128:] == b"\xc7" * 128 and sfr[1] == symbols["s_SSEG"] + 1,
            "FIFO IRAM stack guard/unwind failed")
    for address, value in current.items():
        if address < 256:
            require(sfr[address - 0x80] == value, "FIFO changed guarded SFR")
    radio = memory_dump(section(text, final + 4), 0x6000, 512)
    require(all(byte == current.get(address, 0x69) for address, byte in enumerate(radio, 0x6000)),
            "FIFO changed radio RAM/identity/config outside synthetic effects")
    body = symbols["_radio_fifo_test_body"]
    require(ram[body:body + 125] == bytes(i ^ 0x69 for i in range(125)), "FIFO changed caller body")
    for name, size in (("action", 1), ("length", 2), ("timeout", 4), ("limit", 2)):
        address = symbols["_radio_fifo_test_" + name]
        require(ram[address:address + size] == v[name].to_bytes(size, "little"), "FIFO changed caller input")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "radio_fifo_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_text()
    memory = path.with_suffix(".mem").read_text()
    listing = (args.output / "radio_fifo.rst").read_text()
    allocated, sites = verify_image(image, symbols, debug, memory, listing)
    relocated, _ = verify_fifo_relocated(image, symbols, debug)
    require(relocated == verify_module(image, symbols, debug)[0], "FIFO relocation proof disagrees with standalone")
    check_rejections(image, symbols, debug, memory, listing)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    cases = list(vectors())
    for v in cases:
        execution(args.simulator, path, symbols, allocated, sites, v)
    print(f"Radio FIFO: {len(image)} CODE ({MODULE_SIZE} driver), {len(allocated) - 8} ordinary XDATA "
          f"+ 8 result/64 reserved, {symbols['l_SSEG']} IRAM stack reserved. "
          f"{len(cases)} linked scenarios; exact instructions/ABI/MMIO/layout rejection and alias/stack guards PASS. "
          "Synthetic FIFO/CSP effects only; no radio silicon or on-air evidence.")


if __name__ == "__main__":
    main()
