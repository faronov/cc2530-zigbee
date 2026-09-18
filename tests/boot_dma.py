#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated, alias-aware synthetic DMA execution. NEVER flash this executable."""

import argparse
import hashlib
from pathlib import Path
import re
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, simulate, snapshot_commands,
    verify_component_layout,
)
from boot_timebase import GUARD_SFRS, READER_BYTES, READ_OFFSETS
from radio_fifo_fixture import FIFO_LENGTHS, instructions
from verify_firmware import (
    CLOCK_INSTRUCTION_LENGTHS, cdb_address, code_bytes, parse_ihex, parse_symbols,
    peripheral_accesses, require, verify_deadline_helper,
)


MODULE_SIZE = 2867
MODULE_HASH = "d5cc411d99fad73f654803263414deb629f988d95cc21dee9bd0f07ac54d9919"
IMAGE_HASH = "f93c6c50022a5914a085581d7339590ae95b0d8267c820dfd00a0bfccd268037"
ARM_BYTES = b"\x75\xd6\x01" + b"\0" * 9 + b"\x22"
LENGTHS = FIFO_LENGTHS | CLOCK_INSTRUCTION_LENGTHS | {
    0x00: 1, 0x48: 1, 0x4a: 1, 0x4c: 1, 0x7a: 2, 0x7c: 2, 0x92: 2, 0xaa: 2, 0xbd: 3,
}
READS = (0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e, 0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3)
FIELDS = ("elapsed_ticks", "polls", "timebase_status", "actions", "complete", "verified",
          "arm", "request", "irq", "ircon", "cfg0_low", "cfg0_high", "cfg1_low", "cfg1_high", "sample_valid")
SIZES = (4, 2) + (1,) * 13


def verify(image, symbols, debug, memory, listings):
    require(hashlib.sha256(code_bytes(image, 5485)).hexdigest() == IMAGE_HASH,
            "DMA complete executable/caller/runtime bytes changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "dma_test_result",
                                         ("timebase.c", "dma.c", "test_dma.c"))
    start = cdb_address(debug, "L:Fdma$ordinary$0$0")
    end = cdb_address(debug, "L:XG$dma_copy_init$0$0") + 1
    data = bytes(image.get(a, 0xff) for a in range(start, end))
    require(len(data) == MODULE_SIZE and hashlib.sha256(data).hexdigest() == MODULE_HASH,
            "DMA linked instructions/relocations changed")
    code = instructions(image, start, end, LENGTHS)
    listed = {int(m[1], 16): bytes.fromhex(m[2]) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listings["dma"], re.MULTILINE)}
    require(code == listed, "DMA listing differs from actual linked instructions")
    accesses = peripheral_accesses(code)
    require(tuple(b for _, b, _ in accesses) == (b"\x75\xd6\x01",) +
            tuple(bytes((0xe5, r)) for r in READS) +
            (b"\x8e\xd5", b"\x8d\xd4", b"\x75\xd7\x01", b"\x75\xd1\x1e"),
            "DMA exact SFR access/owned W0 acknowledgment contract changed")
    sites = {("r" if b[0] == 0xe5 else "w", r): pc for pc, b, r in accesses}
    require(sites["w", 0xd4] == sites["w", 0xd5] + 2, "DMA adjacent configuration writes changed")
    arm = cdb_address(debug, "L:Fdma$arm0$0$0")
    require(bytes(image[arm + i] for i in range(13)) == ARM_BYTES and sites["w", 0xd6] == arm,
            "DMA nine-system-clock arm path changed")
    calls = [(pc, int.from_bytes(b[1:], "big")) for pc, b in code.items() if b[0] == 0x12]
    require([pc for pc, target in calls if target == arm] == [0xc40] and
            sites["w", 0xd7] == 0xc7c and code[0xc6c] == b"\x12\x03\xc2",
            "DMA unique arm caller/post-arm poll/request path changed")
    require(all(b[0] != 0x90 or int.from_bytes(b[1:], "big") < 0x1e00 for b in code.values()),
            "DMA module acquired status/alias/peripheral XDATA access")
    require(symbols["_dma_reserved_end"] == 0x5b and symbols["_dma_descriptor"] == 0x19 and
            symbols["_dma_fault"] == 0x21 and symbols["__gptrput_PARM_2"] == 0xbd,
            "DMA private/runtime storage boundary changed")
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 198, "DMA exact XDATA accounting changed")
    require(cdb_address(debug, "L:XFdma$arm0$0$0") == arm + 13 ==
            cdb_address(debug, "L:Fdma$observe$0$0"), "DMA arm function extent changed")
    for name, address in (("dma_copy_init", 0x707), ("dma_test_cycle", 0x12bb), ("main", 0x1333),
                          ("dma_descriptor", 0x19), ("dma_fault", 0x21), ("dma_reserved_end", 0x5b)):
        suffix = "$0$0" if name in ("dma_copy_init", "dma_test_cycle", "main") else "$0_0$0"
        require(symbols["_" + name] == cdb_address(debug, "L:G$" + name + suffix) == address,
                "DMA public symbol/CDB address mismatch")
    # Independently account actual assembler allocations, including SDCC
    # parameter slots missing from the public-symbol subset of a map.
    covered = set()
    for module in ("timebase", "dma"):
        segment = listings[module].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        ranges = re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.MULTILINE)
        require(ranges, "Missing DMA/timebase allocation listing")
        for address, size in ranges:
            addresses = set(range(int(address, 16), int(address, 16) + int(size)))
            require(not (addresses & covered) and addresses <= allocated and max(addresses) <= 0x5b,
                    "DMA/timebase scratch escapes private prefix")
            covered |= addresses
    require(covered == set(range(0x5c)), "DMA private-prefix accounting has a hole")
    for name, size in (("diagnostics", 19), ("source", 16), ("destination", 16),
                       ("src", 2), ("dst", 2), ("output", 2), ("timeout", 4),
                       ("length", 1), ("limit", 2), ("return", 1)):
        records = re.findall(rf"^S:G\$dma_test_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(records and all(int(n) == size for n in records), "DMA test-object ABI changed")
        addresses = set(range(symbols["_dma_test_" + name], symbols["_dma_test_" + name] + size))
        require(cdb_address(debug, "L:G$dma_test_" + name + "$0_0$0") == symbols["_dma_test_" + name],
                "DMA caller map/CDB address mismatch")
        require(addresses <= allocated and not addresses & (covered | {0xbd}),
                "DMA caller object overlaps private/helper storage")
    offset = 0
    expected = []
    for name, size in zip(FIELDS, SIZES):
        expected.append((offset, name, size))
        offset += size
    for module in ("dma", "test_dma"):
        records = re.findall(rf"^T:F{module}\$__00000000\[(.*)\]$", debug, re.MULTILINE)
        require(records, "Missing DMA diagnostic ABI")
        for record in records:
            fields = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
            require([(int(a), b, int(c)) for a, b, c in fields] == expected, "DMA diagnostic ABI changed")
    require("F:G$dma_copy_init$0_0$0({2}DF,SC:U),Z,0,0,0,0,0" in debug and
            re.search(r"S:Ldma.dma_copy_init\$source\$[^(]+\(\{2\}SI:U\)", debug) and
            re.search(r"S:Ldma.dma_copy_init\$d\$[^(]+\(\{2\}DX,ST", debug),
            "DMA numeric address / XDATA output / byte return ABI changed")
    require(symbols["_dma_test_cycle"] == symbols["_dma_test_before"] == 0x12bb and
            symbols["_dma_test_done"] == 0x1331 and image[0x12bb] == image[0x1331] == 0,
            "DMA genuine caller/checkpoints changed")
    for name, address in (("IRCON", 0xc0), ("DMAIRQ", 0xd1), ("DMA1CFGL", 0xd2),
                          ("DMA1CFGH", 0xd3), ("DMA0CFGL", 0xd4), ("DMA0CFGH", 0xd5),
                          ("DMAARM", 0xd6), ("DMAREQ", 0xd7), ("ST0", 0x95), ("ST1", 0x96), ("ST2", 0x97)):
        require(symbols.get("_SOC_" + name) == address, "DMA SFR address changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader + i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "DMA genuine awake reader changed")
    verify_deadline_helper(image, symbols, debug)
    sites.update({("r", 0x95 + i): reader + offset for i, offset in enumerate(READ_OFFSETS)})
    sites["ready", 0] = arm + 12
    return allocated, sites


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    # Reject every CODE byte, including vectors, helpers and complete caller,
    # rather than accepting a CDB/listing proof of a different executable.
    for address in image:
        changed = dict(image)
        changed[address] ^= 1
        with case.assertRaises(ValueError):
            verify(changed, symbols, debug, memory, listings)
    for name, value in (("_dma_reserved_end", 0x20), ("__gptrput_PARM_2", 0x84),
                        ("_dma_descriptor", 0x1f00), ("_dma_fault", 0x1e00),
                        ("_dma_test_source", 0x20), ("_dma_test_destination", 0x1e00),
                        ("_dma_copy_init", 0x708), ("_main", 0x1334),
                        ("_dma_test_result", 0x1f00), ("_SOC_DMAARM", 0xe1),
                        ("_dma_test_before", 0x12ba), ("l_XSEG", 449),
                        ("l_PSEG", 1), ("s_SSEG", 128), ("__XPAGE", 0xa0)):
        with case.assertRaises(ValueError):
            verify(image, symbols | {name: value}, debug, memory, listings)
    for old, new in (("C$dma.c$", "C$missing.c$"), ("{19}ST", "{18}ST"),
                     ("({2}DX,ST", "({3}DG,ST"), ("S:S$actions$", "S:S$changed$"),
                     ("L:Fdma$arm0$0$0:28A", "L:Fdma$arm0$0$0:28B")):
        require(old in debug, "DMA rejection input not found")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    with case.assertRaises(ValueError):
        verify(image, symbols, debug, memory, listings | {"dma": listings["dma"].replace("75 D6 01", "75 D6 81")})


def vectors():
    def make(name, **kwargs):
        item = dict(name=name, result=0, stop="ack", length=16, timeout=100, cap=32,
                    start=0, pattern=0x69, initial={}, changes={}, transfer_polls=1,
                    per_poll=16, hold=False, late={}, ack_foreign=0, ignore_ack=False,
                    transient=False, repeat=False, short_sample=False, lost_irq=False)
        item.update(kwargs)
        return item

    for length in range(1, 17):
        yield make(f"length-{length}", length=length, pattern=(length * 19) & 255)
    for pattern in (0, 0xff, 0x80, 0x55):
        yield make(f"pattern-{pattern}", pattern=pattern)
    yield make("XOSC32", initial={0xc6: 0x88, 0x9e: 0x88})
    yield make("maximum-deadline", timeout=0x7fffff)
    yield make("rollover-bytewise", start=0xfffff8, transfer_polls=16, per_poll=1)
    yield make("transient-completion-before-disarm", transfer_polls=2, transient=True)
    yield make("successful-reuse", repeat=True)
    for name, field, value, result in (
        ("zero-length", "length", 0, 1), ("large-length", "length", 17, 1),
        ("zero-timeout", "timeout", 0, 1), ("half-timeout", "timeout", 0x800000, 1),
        ("high-timeout", "timeout", 0x1000000, 1), ("zero-cap", "cap", 0, 1),
        ("alias-source", "src", 0x1f00, 2), ("status-destination", "dst", 0x1e00, 2),
        ("source-overflow", "src", 0xfff8, 2), ("source-overrun", "src", 0x1dff, 2),
        ("peripheral", "dst", 0x70d1, 2), ("private-source", "src", 0x19, 3),
        ("private-parameter", "dst", 0x3d, 3), ("timebase-scratch", "src", 7, 3),
        ("helper-scratch", "dst", 0xbd, 3), ("output-source", "src", 0x71, 3),
        ("output-destination", "dst", 0x71, 3), ("overlap", "dst", 0x85, 2),
        ("null-output", "output", 0, 1), ("output-range", "output", 0x1df0, 2),
        ("output-private", "output", 0x30, 3), ("output-helper", "output", 0xbd, 3),
        ("output-overlap", "output", 0x84, 3),
    ):
        yield make(name, stop="invalid", result=result, **{field: value})
    for reg, value, result in ((0xa8, 0x80, 4), (0xb8, 1, 4), (0x9a, 1, 4),
                               (0xbe, 5, 4), (0x9e, 0x88, 4), (0xd6, 1, 5),
                               (0xd6, 0x10, 5), (0xd6, 0x20, 4),
                               (0xd7, 1, 6), (0xd7, 0x10, 6), (0xd1, 1, 6),
                               (0xd1, 0x10, 6), (0xc0, 0xbf, 6)):
        yield make(f"entry-{reg:02x}", stop="entry", result=result, initial={reg: value},
                   short_sample=reg in READS[:6])
    for stop, obs in (("prepare", 2), ("config", 3), ("arm", 4), ("transfer", 5), ("ack", 6)):
        for reg, value, result in ((0xc6, 0x88, 4), (0xd1, 2, 7), (0xd3, 0x13, 7)):
            yield make(f"changed-{stop}-{reg:02x}", stop=stop, result=result, changes={obs: {reg: value}},
                       short_sample=reg in READS[:6])
    for stop, sample in (("prepare", 1), ("config", 2), ("arm", 3), ("transfer", 4), ("ack", 5)):
        yield make("late-" + stop, stop=stop, result=8, timeout=20, late={sample: 20})
    for stop, cap in (("prepare", 1), ("config", 2), ("arm", 3), ("transfer", 4)):
        yield make("cap-" + stop, stop=stop, cap=cap, result=9)
    yield make("timebase-ambiguous", stop="transfer", result=10, late={4: 0x800064})
    yield make("counter-half", stop="transfer", result=11, late={4: 0x800000})
    yield make("counter-backward", stop="transfer", result=11, late={4: 0xffffff})
    yield make("pending-request", stop="transfer", result=9, transfer_polls=2, hold=True, cap=5)
    yield make("stuck-in-progress", stop="transfer", result=9, transfer_polls=2, per_poll=0, cap=5)
    yield make("no-fresh-completion", stop="transfer", result=9, transfer_polls=2, lost_irq=True, cap=5)
    yield make("partial-then-timeout", stop="transfer", result=8, per_poll=3, late={4: 100})
    yield make("ack-ignored", result=7, ignore_ack=True)
    yield make("ack-preserves-new-foreign-flag", result=7, ack_foreign=0x10)
    yield make("configuration-readback", stop="config", result=7, changes={3: {0xd4: 0x1a}})
    yield make("arm-dropped", stop="arm", result=7, changes={4: {0xd6: 0}})
    yield make("unexpected-pre-request-completion", stop="arm", result=7, changes={4: {0xd1: 1}})


def execution(simulator, path, symbols, allocated, sites, v):
    before, done = (symbols["_dma_test_" + n] for n in ("before", "done"))
    output = symbols["_dma_test_diagnostics"]
    descriptor = symbols["_dma_descriptor"]
    src, dst = symbols["_dma_test_source"], symbols["_dma_test_destination"]
    values = dict(GUARD_SFRS) | {0xbe: 4, 0xc6: 0xc9, 0x9e: 0xc9, 0xc0: 0xbe,
                                0xd1: 0, 0xd2: 0x34, 0xd3: 0x12, 0xd4: 0, 0xd5: 0,
                                0xd6: 0, 0xd7: 0, 0xbf: 0x37, 0xd9: 0x59, 0xe1: 0x71}
    values.update(v["initial"])
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x70ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7"]
    for reg, value in values.items():
        commands.append(f"set memory sfr {reg:#x} {value:#x}")
    commands += [f"run {symbols['_main']:#x} {before:#x}"]
    inputs = dict(src=v.get("src", src), dst=v.get("dst", dst), output=v.get("output", output),
                  length=v["length"], timeout=v["timeout"], limit=v["cap"])
    for name, value in inputs.items():
        size = 4 if name == "timeout" else 1 if name == "length" else 2
        commands.append(f"set memory xram {symbols['_dma_test_' + name]:#x} " +
                        " ".join(hex(b) for b in value.to_bytes(size, "little")))
    payload = bytes(i ^ v["pattern"] for i in range(16))
    commands += [f"set memory xram {src:#x} " + " ".join(hex(b) for b in payload),
                 f"fill xram {output:#x} {output + 18:#x} 0xa5"]
    commands += [f"break {pc:#x}" for pc in dict.fromkeys(list(sites.values()) + [before, done])]
    commands += ["step 1"]
    checks, ram_checks, finishes = [], [], []
    number, observations, samples, copied = 10, 0, 0, 0
    actions, complete, diag = 0, 0, bytearray(19)
    expected_descriptor = (inputs["src"].to_bytes(2, "big") + inputs["dst"].to_bytes(2, "big") +
                           bytes((0, v["length"], 0x20, 0x51)))

    def event(kind, reg, value):
        nonlocal number
        pc = sites[kind, reg]
        # The preceding single step already lands ON the adjacent CFGL write.
        # s51 'run' would execute past a breakpoint at the current PC.
        commands.extend(([] if (kind, reg) == ("w", 0xd4) else ["run"]) +
                        [marker(number), "state", "step 1", marker(number + 1),
                         f"dump /h sfr {0xe0 if kind == 'r' else reg:#x} {0xe0 if kind == 'r' else reg:#x}",
                         marker(number + 2)])
        checks.append((number, pc, 0xe0 if kind == "r" else reg, value))
        number += 3

    def setreg(reg, value):
        values[reg] = value
        commands.append(f"set memory sfr {reg:#x} {value:#x}")

    def read(reg):
        event("r", reg, values[reg])

    def write(reg, value):
        event("w", reg, value)
        values[reg] = value

    def timer():
        nonlocal samples
        ticks = (v["start"] + v["late"].get(samples, samples)) & 0xffffff
        for i, byte in enumerate(ticks.to_bytes(3, "little")):
            setreg(0x95 + i, byte)
            read(0x95 + i)
        if samples:
            diag[0:4] = ((ticks - v["start"]) & 0xffffff).to_bytes(4, "little")
            diag[4:6] = samples.to_bytes(2, "little")
        samples += 1

    def copy_bytes(count):
        nonlocal copied, number
        cfg = "(sfr[0xd5]*256+sfr[0xd4])"
        sa = f"(xram[{cfg}]*256+xram[{cfg}+1])"
        da = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
        for i in range(copied, min(copied + count, v["length"])):
            commands.extend([f"expression xram[{da}+{i}]=xram[{sa}+{i}]", marker(number),
                             f"dump /h xram {dst+i:#x} {dst+i:#x}", marker(number + 1)])
            ram_checks.append((number, dst + i, payload[i:i + 1]))
            number += 2
            copied += 1

    def observe(stage):
        nonlocal observations, complete
        observations += 1
        if stage == "transfer" and not v["hold"]:
            copy_bytes(v["per_poll"])
            setreg(0xd7, values[0xd7] & 0xfe)
            if copied == v["length"]:
                setreg(0xd1, 0 if v["lost_irq"] else 1)
                setreg(0xd6, values[0xd6] if v["transient"] and observations == 5 else values[0xd6] & 0xfe)
        for reg, value in v["changes"].get(observations, {}).items():
            setreg(reg, value)
        unsupported = stage == v["stop"] and v["short_sample"]
        count = 6 if unsupported else 14
        for reg in READS[:count]:
            read(reg)
        diag[18] = int(not unsupported)
        if not unsupported:
            for i, reg in enumerate(READS[6:], 10):
                diag[i] = values[reg]
        if stage == "transfer" and not (stage == v["stop"] and v["result"] in (4, 7)) and (
                values[0xd6], values[0xd7], values[0xd1]) == (0, 0, 1):
            complete = 1
        diag[7], diag[8] = actions, complete
        if stage != "entry":
            timer()

    def finish(result, index, *, retained=False):
        nonlocal number
        commands.extend(["run"] + snapshot_commands(number))
        commands.extend([marker(number + 4), "dump /h xram 0x6000 0x70ff", marker(number + 5)])
        finishes.append((number, bytes(diag), result, copied, actions, index, retained, dict(values)))
        number += 6

    cycles = 2 if v["repeat"] else 1
    for cycle in range(cycles):
        if cycle:
            commands.extend(["step 1", "run", marker(number), "state", marker(number + 1)])
            checks.append((number, before, None, None))
            number += 2
            commands.append("step 1")
            observations = samples = copied = actions = complete = 0
            diag = bytearray(19)
        if v["stop"] == "invalid":
            diag[:] = b"\xa5" * 19
        else:
            for stage in ("entry", "prepare", "config", "arm", "transfer", "ack"):
                if stage == "config":
                    write(0xd5, descriptor >> 8)
                    write(0xd4, descriptor & 255)
                    actions = 1
                elif stage == "arm":
                    write(0xd6, 1)
                    # This real PC is reached only after the nine pinned NOPs.
                    commands.extend(["run", marker(number), "state",
                                     f"dump /h xram {descriptor:#x} {descriptor+7:#x}", marker(number + 1)])
                    checks.append((number, sites["ready", 0], None, None))
                    ram_checks.append((number, descriptor, expected_descriptor))
                    number += 2
                    commands.append("step 1")
                    actions = 3
                elif stage == "transfer":
                    write(0xd7, 1)
                    actions = 7
                elif stage == "ack":
                    write(0xd1, 0x1e)
                    setreg(0xd1, (1 | v["ack_foreign"]) & (0x1f if v["ignore_ack"] else 0x1e))
                    actions = 15
                for _ in range(v["transfer_polls"] if stage == "transfer" else 1):
                    observe(stage)
                if stage == v["stop"]:
                    break
                if stage == "entry":
                    timer()
        if v["result"] == 0:
            diag[9] = 1
        if v["result"] == 10:
            diag[6] = 2
        finish(v["result"], cycle)
    # A returned fault never licenses descriptor replacement, flag cleanup or
    # buffer release. Execute the genuine caller again with a different length.
    if v["result"] >= 4:
        commands += ["step 1", "run", marker(number), "state", marker(number + 1)]
        checks.append((number, before, None, None))
        number += 2
        commands += [f"set memory xram {symbols['_dma_test_length']:#x} 1", "step 1"]
        finish(v["result"], 1, retained=True)
        if actions == 7 and copied < v["length"] and v["result"] in (8, 9):
            # The synthetic engine can finish AFTER both error returns, using
            # the original persistent descriptor and still-owned buffers.
            copy_bytes(16)
            setreg(0xd6, 0)
            setreg(0xd7, 0)
            setreg(0xd1, 1)
    text = simulate(simulator, commands, path)
    peaks = [int(n, 16) for n in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "DMA crossed the reserved upper-IRAM stack guard")
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.MULTILINE)
    blocks = {int(parts[i], 16): parts[i + 1] for i in range(1, len(parts), 2)}
    require(len(blocks) == (len(parts) - 1) // 2, "Duplicate DMA simulator markers")
    for n, pc, reg, expected in checks:
        require(n in blocks, v["name"] + ": missing actual MMIO/checkpoint")
        try:
            check_pc(blocks[n], pc)
        except ValueError as error:
            raise ValueError(f"{v['name']}: event {n}, expected PC {pc:04x}\n{blocks[n]}") from error
        if reg is not None:
            require(memory_dump(blocks[n + 1], reg, 1)[0] == expected,
                    f"{v['name']}: wrong actual SFR access at {pc:04x}")
    for n, address, expected in ram_checks:
        require(memory_dump(blocks[n], address, len(expected)) == expected,
                v["name"] + ": wrong actual descriptor or synthetic copied byte/address")
    for n, expected_diag, result, count, issued, cycle, retained, expected_sfr in finishes:
        check_pc(blocks[n], done)
        ram = memory_dump(blocks[n], 0, 0x1f00)
        iram = memory_dump(blocks[n + 1], 0, 256)
        sfr = memory_dump(blocks[n + 2], 0x80, 128)
        require(ram[0x1e00:0x1e08] == b"DMAT\x01\x08\0\0", "DMA target argument self-test failed")
        require(ram[output:output + 19] == expected_diag,
                f"{v['name']}: diagnostics {ram[output:output+19].hex()} != {expected_diag.hex()}")
        require(ram[symbols["_dma_test_return"]] == result, v["name"] + ": wrong DMA result")
        require(ram[symbols["_dma_fault"]] == (result if result >= 4 else 0), "DMA lost original fault latch")
        require(ram[src:src + 16] == payload, "DMA changed source")
        expected_dest = payload[:count] + (payload[count:] if cycle and v["repeat"] else b"\xa5" * (16 - count))
        require(ram[dst:dst + 16] == expected_dest, "DMA destination/tail differs from independent reference")
        require(ram[descriptor:descriptor + 8] == (expected_descriptor if issued else b"\0" * 8),
                "DMA replaced/changed persistent descriptor")
        require(all(byte == 0xa5 for a, byte in enumerate(ram) if a not in allocated),
                "DMA wrote unallocated/status/alias XDATA")
        require(iram[128:] == b"\xc7" * 128 and sfr[1] == symbols["s_SSEG"] + 1,
                "DMA stack/upper-IRAM guard or unwind failed")
        for reg, value in expected_sfr.items():
            require(sfr[reg - 0x80] == value, f"DMA changed guarded SFR {reg:02x}")
        for name, value in inputs.items():
            size = 4 if name == "timeout" else 1 if name == "length" else 2
            if name == "length" and retained:
                value = 1
            address = symbols["_dma_test_" + name]
            require(ram[address:address + size] == value.to_bytes(size, "little"), "DMA changed caller inputs")
        for offset, reg in enumerate(READS[6:], 10):
            if expected_diag[18] == 1:
                require(sfr[reg - 0x80] == expected_diag[offset], "DMA controller state differs from diagnostics")
        require(memory_dump(blocks[n + 4], 0x6000, 0x1100) == b"\x69" * 0x1100,
                "DMA accessed radio/AES/peripheral XDATA")
    return max(peaks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "dma_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("." + ext).read_text() for ext in ("cdb", "mem"))
    listings = {name: (args.output / (name + ".rst")).read_text() for name in ("dma", "timebase")}
    allocated, sites = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    cases = list(vectors())
    peak = max(execution(args.simulator, path, symbols, allocated, sites, v) for v in cases)
    print(f"DMA: {len(image)} CODE ({MODULE_SIZE} driver), {len(allocated)-8} ordinary XDATA + "
          f"8 result/64 reserved; stack {symbols['s_SSEG']:02x}..ff ({symbols['l_SSEG']} bytes), peak {peak:02x}. "
          f"{len(cases)} linked scenarios; complete CODE/ABI/MMIO/private-prefix rejection, "
          "nine-NOP system-clock proof, descriptor-driven synthetic copies, retained fault/reuse "
          "and alias/stack guards PASS. No DMA silicon/timing or AES evidence.")


if __name__ == "__main__":
    main()
