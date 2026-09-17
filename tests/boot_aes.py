#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated AES/DMA linked proof and synthetic execution. NEVER flash this image."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate, snapshot_commands,
    verify_component_layout,
)
from boot_dma import LENGTHS as DMA_LENGTHS
from boot_timebase import GUARD_SFRS, READER_BYTES, READ_OFFSETS
from radio_fifo_fixture import instructions
from verify_firmware import (
    cdb_address, parse_ihex, parse_symbols, peripheral_accesses, require, verify_deadline_helper,
)


IMAGE_SIZE = 6423
IMAGE_HASH = "49634cb2d84fe9391de9b394a47e118499434bb1cdca9c374b9a341678ba9cd2"
MODULE_SIZE = 5290
MODULE_HASH = "5763ff71d5a201f15d0f58a5c25a17abf0a9a73ceafdf1eaa3ee578c013787cc"
LENGTHS = DMA_LENGTHS | {
    0x7b: 2, 0xbb: 3, 0x49: 1, 0x14: 1, 0x05: 2, 0x2b: 1, 0xd5: 3,
    0x52: 2, 0x9b: 1, 0x3c: 1, 0x0d: 1, 0x0a: 1, 0x5a: 1, 0x39: 1,
}
AES_ALIAS = "memory create addressdecoder xram 0x70b1 0x70b2 sfr_chip 0x31"
READS = (0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e, 0xb3, 0x98, 0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3)
FIELDS = (
    "elapsed_ticks", "polls", "timebase_status", "phase", "submitted", "input_complete",
    "output_drained", "published", "configured", "arms", "ack_issued", "dma_acked",
    "enc_ack_issued", "enc_acked", "arm", "request", "irq", "ircon", "control", "enc_flags",
    "cfg0_low", "cfg0_high", "cfg1_low", "cfg1_high", "sample_valid",
)
SIZES = (4, 2) + (1,) * 23
SFR_FIELDS = dict(zip(READS[6:], ("control", "enc_flags", "arm", "request", "irq", "ircon",
                                  "cfg0_low", "cfg0_high", "cfg1_low", "cfg1_high")))
OBJECTS = {
    "diagnostics": (0xcf, 29), "key": (0xec, 16), "input": (0xfc, 32), "output": (0x11c, 18),
    "key_pointer": (0x12e, 3), "input_pointer": (0x131, 3), "out": (0x134, 2),
    "diag": (0x136, 2), "limit": (0x138, 2), "timeout": (0x13a, 4), "return": (0x13e, 1),
}
# FIPS 197 (2001) C.1 and SP 800-38A (2001) F.1.1; independently cross-checked
# against the real compiled CODE table as well as the original host oracle.
KATS = (
    ("000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff", "69c4e0d86a7b0430d8cdb78070b4c55a"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "6bc1bee22e409f96e93d7e117393172a", "3ad77bb40d7a3660a89ecaf32466ef97"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "ae2d8a571e03ac9c9eb76fac45af8e51", "f5d3d58503b9699de785895a96fdbaaf"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "30c81c46a35ce411e5fbc1191a0a52ef", "43b1cd7f598ece23881b00e3ed030688"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "f69f2445df4f9b17ad2b417be66c3710", "7b0c785e27e8ad3f8223207104725dd4"),
)


def verify(image, symbols, debug, memory, listings):
    # Cheap full-CODE rejection first; then independently prove the accepted
    # image's allocation, typed caller, actual instructions and physical MMIO.
    require(set(image) == set(range(IMAGE_SIZE)), "AES complete CODE extent changed")
    require(hashlib.sha256(bytes(image[i] for i in range(IMAGE_SIZE))).hexdigest() == IMAGE_HASH,
            "AES complete executable/caller/constants/runtime changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "aes_test_result",
                                         ("timebase.c", "aes.c", "test_aes.c"))
    require("C$aes_reference.c$" not in debug and "_aes_reference_encrypt" not in symbols,
            "Host software reference entered target")
    start = cdb_address(debug, "L:Faes$pointer_location$0$0")
    end = cdb_address(debug, "L:XG$aes128_encrypt_block$0$0") + 1
    require((start, end) == (0x1f6, 0x16a0) and end - start == MODULE_SIZE and
            hashlib.sha256(bytes(image[i] for i in range(start, end))).hexdigest() == MODULE_HASH,
            "AES complete driver changed")
    code = instructions(image, start, end, LENGTHS)
    listed = {int(m[1], 16): bytes.fromhex(m[2]) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listings["aes"], re.MULTILINE)}
    require(code == listed, "AES listing differs from real linked instructions")
    expected_accesses = (b"\x75\xd6\x01", b"\x75\xd6\x02") + tuple(bytes((0xe5, r)) for r in READS) + (
        b"\xe5\xc6", b"\x8b\xd5", b"\x8c\xd4", b"\x89\xd3", b"\x8b\xd2",
        b"\x8a\xb3", b"\x75\xd1\x1c", b"\x75\xd1\x1e", b"\xf5\x98",
    )
    accesses = peripheral_accesses(code)
    require(tuple(raw for _, raw, _ in accesses) == expected_accesses,
            "AES exact SFR reads/configuration/owned acknowledgment changed")
    require(all(raw[0] != 0x90 or int.from_bytes(raw[1:], "big") < 0x1e00 for raw in code.values()),
            "AES gained direct status/alias/peripheral XDATA access")
    sites = {("r", reg): pc for pc, raw, reg in accesses[2:18]}
    for pc, raw, reg in accesses[19:]:
        sites["block_ack" if raw == b"\x75\xd1\x1c" else "w", reg] = pc
    sites["clock", 0] = accesses[18][0]
    calls = [(pc, int.from_bytes(raw[1:], "big")) for pc, raw in code.items() if raw[0] == 0x12]
    for name, bit, address, caller in (("input", 1, 0x3ec, 0x11ba), ("output", 2, 0x3f9, 0x1052)):
        require(cdb_address(debug, f"L:Faes$arm_{name}$0$0") == address and
                cdb_address(debug, f"L:XFaes$arm_{name}$0$0") == address + 13,
                "AES individual arm function extent changed")
        require(bytes(image[address + i] for i in range(13)) == bytes((0x75, 0xd6, bit)) + b"\0" * 9 + b"\x22" and
                [pc for pc, target in calls if target == address] == [caller],
                "AES one-at-a-time nine-system-clock path/caller changed")
        sites["arm", bit], sites["ready", bit], sites["return", bit] = address, address + 12, caller + 3
    require([pc for pc, target in calls if target == symbols["__gptrget"]] == [0xe95, 0xeb7],
            "AES genuine generic key/input staging path changed")
    require(symbols["__gptrget"] == 0x1807 and symbols["__gptrput"] == 0x17ec and
            symbols["__gptrput_PARM_2"] == 0x13f, "AES generic runtime/helper extent changed")
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 320 and symbols["s_SSEG"] == 0x3f,
            "AES exact XDATA/IRAM accounting changed")
    for name, address, size in (
        ("dma0", 0x19, 8), ("dma1", 0x21, 32), ("key", 0x41, 16), ("iv", 0x51, 16),
        ("input", 0x61, 16), ("output", 0x71, 16), ("fault", 0x81, 1), ("used", 0x82, 1),
        ("reserved_end", 0xce, 1),
    ):
        require(symbols["_aes_" + name] == cdb_address(debug, f"L:G$aes_{name}$0_0$0") == address,
                "AES private map/CDB address changed")
        sizes = re.findall(rf"^S:G\$aes_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(sizes and all(int(n) == size for n in sizes), "AES persistent object size changed")
    covered = set()
    for module in ("timebase", "aes"):
        segment = listings[module].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        ranges = re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.MULTILINE)
        require(ranges, "Missing AES/timebase actual scratch allocations")
        for address, size in ranges:
            addresses = set(range(int(address, 16), int(address, 16) + int(size)))
            require(addresses and not addresses & covered and addresses <= allocated and max(addresses) <= 0xce,
                    "AES/timebase scratch escapes or overlaps private prefix")
            covered |= addresses
    require(covered == set(range(0xcf)), "AES/timebase private allocation has a hole")
    for name, (address, size) in OBJECTS.items():
        require(symbols["_aes_test_" + name] == cdb_address(debug, f"L:G$aes_test_{name}$0_0$0") == address,
                "AES caller object map/CDB mismatch")
        sizes = re.findall(rf"^S:G\$aes_test_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(sizes and all(int(n) == size for n in sizes), "AES caller object size changed")
        addresses = set(range(address, address + size))
        require(addresses <= allocated and not addresses & (covered | {0x13f}),
                "AES caller overlaps protected/helper storage")
    expected, offset = [], 0
    for name, size in zip(FIELDS, SIZES):
        expected.append((offset, name, size)); offset += size
    require(offset == 29, "AES wire record size changed")
    for module in ("aes", "test_aes"):
        records = re.findall(rf"^T:F{module}\$__00000000\[(.*)\]$", debug, re.MULTILINE)
        require(records, "Missing AES diagnostic ABI")
        for record in records:
            fields = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
            require([(int(a), b, int(c)) for a, b, c in fields] == expected, "AES diagnostic ABI changed")
    for name, shape in (("key", "{3}DG,SC:U"), ("input", "{3}DG,SC:U"), ("output", "{2}DX,SC:U"),
                        ("d", "{2}DX,ST"), ("timeout", "{4}SL:U"), ("limit", "{2}SI:U")):
        require(re.search(rf"^S:Laes.aes128_encrypt_block\${name}\$[^(\n]+\(" + re.escape(shape), debug, re.MULTILINE),
                "AES generic/XDATA/scalar argument ABI changed")
    require("F:G$aes128_encrypt_block$0_0$0({2}DF,SC:U),Z,0,0,0,0,0" in debug and
            re.search(r"S:Laes.pointer_location\$p\$[^(\n]+\(\{3\}DG,SC:U", debug) and
            re.search(r"S:Laes.pointer_location\$value\$[^(\n]+\(\{3\}ST", debug),
            "AES byte-return/generic representation changed")
    for name, address in (("aes128_encrypt_block", 0x926), ("aes_test_cycle", 0x16a0), ("main", 0x1731)):
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") == address, "AES function/caller changed")
    require(symbols["_aes_test_before"] == 0x16a0 and symbols["_aes_test_done"] == 0x172f and
            image[0x16a0] == image[0x172f] == 0, "AES genuine caller markers changed")
    table = cdb_address(debug, "L:Ftest_aes$aes_test_vectors$0_0$0")
    require(table == 0x1827 and bytes(image[table+i] for i in range(240)) ==
            b"".join(bytes.fromhex(text) for kat in KATS for text in kat), "AES compiled public CODE vectors changed")
    for name, address in (("ENCDI", 0xb1), ("ENCDO", 0xb2), ("ENCCS", 0xb3), ("S0CON", 0x98),
                          ("DMAARM", 0xd6), ("DMAREQ", 0xd7), ("DMAIRQ", 0xd1),
                          ("DMA0CFGL", 0xd4), ("DMA0CFGH", 0xd5), ("DMA1CFGL", 0xd2), ("DMA1CFGH", 0xd3)):
        require(symbols["_SOC_" + name] == address, "AES physical SFR address changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader+i] for i in range(len(READER_BYTES))) == READER_BYTES, "AES awake reader changed")
    verify_deadline_helper(image, symbols, debug)
    sites.update({("r", 0x95+i): reader + offset for i, offset in enumerate(READ_OFFSETS)})
    sites["vectors", 0] = table
    return allocated, sites, code


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for address in image:
        with case.assertRaises(ValueError):
            verify(image | {address: image[address] ^ 1}, symbols, debug, memory, listings)
    for name in ("_aes_dma0", "_aes_dma1", "_aes_key", "_aes_output", "_aes_fault", "_aes_reserved_end",
                 "__gptrput_PARM_2", "_aes_test_key", "_aes_test_diagnostics", "_aes_test_input_pointer",
                 "_aes_test_result", "_aes128_encrypt_block", "_aes_test_before", "_aes_test_done",
                 "_SOC_ENCDI", "_SOC_S0CON", "l_XSEG", "l_PSEG", "s_SSEG", "__XPAGE"):
        with case.assertRaises(ValueError):
            verify(image, symbols | {name: symbols[name] + 1}, debug, memory, listings)
    for old, new in (("C$aes.c$", "C$missing.c$"), ("({3}DG,SC:U)", "({2}DX,SC:U)"),
                     ("{29}ST", "{28}ST"), ("S:S$submitted$", "S:S$wrong$"),
                     ("L:Faes$arm_input$0$0:3EC", "L:Faes$arm_input$0$0:3ED")):
        require(old in debug, "Missing AES rejection input")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    for old, new in (("75 D6 01", "75 D6 03"), (".ds 32", ".ds 31"), (".ds 16", ".ds 17")):
        require(old in listings["aes"], "Missing AES listing rejection input")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {"aes": listings["aes"].replace(old, new)})


def oracle(path, key, data):
    result = subprocess.run([str(path), key.hex(), data.hex()], text=True, capture_output=True, check=True, timeout=5)
    require(re.fullmatch(r"[0-9a-f]{32}\n", result.stdout) is not None, "Malformed independent AES oracle output")
    return bytes.fromhex(result.stdout)


def vectors():
    def make(name, **changes):
        return dict(name=name, kat=0, code_key=True, code_input=False, result=0, initial={},
                    timeout=1000, cap=128, per_poll=16, start=0, jump={}, changes={},
                    stall_phase=0, stall_after=17, stall_output=17, lost_irq=0, ignore_write=0,
                    finish_delay=0, hold_start_phase=0, defer_observation=0,
                    repeat=False, overlap=None, arguments={}, load_rdy=True, stale_rdy=False) | changes
    for kat in range(5):
        for mode in range(4):
            yield make(f"KAT-{kat}-spaces-{mode}", kat=kat, code_key=bool(mode & 1), code_input=bool(mode & 2))
    yield make("XOSC32-bytewise-rollover", initial={0xc6: 0x88, 0x9e: 0x88},
               per_poll=1, start=0xfffff8, repeat=True, load_rdy=False, stale_rdy=True)
    yield make("maximum-bounded-timeout", timeout=0x7fffff)
    yield make("last-permitted-poll", cap=16)
    yield make("late-ENC-flags-after-drain", finish_delay=4)
    yield make("mixed-key-ARM-IRQ-snapshot", defer_observation=6)
    yield make("mixed-output-ARM-IRQ-snapshot", defer_observation=15)
    for offset in (0, 1, 15):
        yield make(f"in-place-offset-{offset}", overlap=offset)
    for field, value, result in (
        ("key_pointer", 0, 1), ("input_pointer", 0, 1), ("out", 0, 1), ("diag", 0, 1),
        ("timeout", 0, 1), ("timeout", 0x800000, 1), ("timeout", 0x1000000, 1), ("limit", 0, 1),
        ("key_pointer", 0x400001, 2), ("key_pointer", 0x807ff1, 2), ("input_pointer", 0x1f00, 2),
        ("key_pointer", 0x70b1, 2), ("out", 0x1dff, 2), ("diag", 0x1e00, 2),
        ("key_pointer", 0x41, 3), ("input_pointer", 0xb, 3), ("out", 0x13f, 3),
        ("out", 0xec, 3), ("diag", 0xfc, 3), ("diag", 0xce, 3), ("diag", 0x13f, 3),
    ):
        yield make(f"invalid-{field}-{value:x}", result=result, arguments={field: value}, code_key=False)
    for reg, value, result in ((0xa8, 0x10, 4), (0xb8, 1, 4), (0x9a, 1, 4), (0xbe, 5, 4),
                               (0x9e, 0x88, 4), (0xb3, 0x48, 4), (0xb3, 0, 5), (0xb3, 9, 5),
                               (0x98, 0xa5, 6), (0x98, 0xa7, 6), (0xd6, 2, 5), (0xd7, 1, 6),
                               (0xd1, 3, 6), (0xc0, 0xbf, 6), (0xd2, 0x21, 4)):
        yield make(f"entry-{reg:x}-{value:x}", initial={reg: value}, result=result)
    for phase in (1, 2, 3):
        for count in (0, 7, 15):
            yield make(f"partial-input-{phase}-{count}", stall_phase=phase, stall_after=count, timeout=40, result=8)
    for count in (0, 7, 15):
        yield make(f"partial-output-{count}-late-effect", stall_output=count, timeout=40, result=8)
    for lost in (1, 2, 4, 8):
        yield make(f"lost-fresh-flag-{lost}", lost_irq=lost, timeout=40, result=8, stale_rdy=lost != 8)
    for phase in (1, 2, 3):
        yield make(f"stuck-start-{phase}", hold_start_phase=phase, timeout=40, result=8)
    for limit in (1, 4, 8, 14, 15):
        yield make(f"poll-cap-{limit}", cap=limit, result=9)
    for timeout in (1, 4, 8, 14, 15, 16):
        yield make(f"deadline-{timeout}", timeout=timeout, result=8)
    yield make("ambiguous-counter", jump={1: 0x800064}, timeout=100, result=10)
    yield make("backward-counter", jump={2: 0}, result=11)
    for write in (2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15):
        yield make(f"ignored-write-{write}", ignore_write=write, result=7)
    for obs in (2, 4, 8, 13, 15, 16, 17):
        for reg, value, result in ((0xd7, 1, 7), (0xd1, 4, 7), (0xd3, 0xdd, 7), (0x98, 0xb4, 7),
                                   (0xb3, 0x80, 7), (0x9a, 1, 4)):
            yield make(f"changed-{obs}-{reg:x}", changes={obs: {reg: value}}, result=result)


class StopModel(Exception):
    def __init__(self, result):
        self.result = result


def execution(simulator, path, reference, symbols, allocated, sites, code, v):
    before, done = symbols["_aes_test_before"], symbols["_aes_test_done"]
    desc0, desc1 = symbols["_aes_dma0"], symbols["_aes_dma1"]
    key, plain = (bytes.fromhex(text) for text in KATS[v["kat"]][:2])
    cipher = oracle(reference, key, plain)
    require(cipher.hex() == KATS[v["kat"]][2], "Independent oracle failed primary KAT")
    args = dict(key_pointer=(0x800000 + sites["vectors", 0] + 48*v["kat"]) if v["code_key"] else 0xec,
                input_pointer=(0x800000 + sites["vectors", 0] + 48*v["kat"] + 16) if v["code_input"] else 0xfc,
                out=0x11d if v["overlap"] is None else 0xfc + v["overlap"], diag=0xcf,
                timeout=v["timeout"], limit=v["cap"]) | v["arguments"]
    values = dict(GUARD_SFRS) | {0xbe: 4, 0xc6: 0xc9, 0x9e: 0xc9, 0xc0: 0xbe,
                                0xb1: 0x69, 0xb2: 0x69, 0xb3: 8, 0x98: 0xa4,
                                0xd1: 0, 0xd2: 0, 0xd3: 0, 0xd4: 0, 0xd5: 0, 0xd6: 0, 0xd7: 0,
                                0xbf: 0x37, 0xd9: 0x59, 0xe1: 0x71}
    values.update(v["initial"])
    commands = [ALIAS, AES_ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x70ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7"]
    commands += [f"set memory sfr {reg:#x} {value:#x}" for reg, value in values.items()]
    commands += [f"run {symbols['_main']:#x} {before:#x}"]
    for name, value in args.items():
        address, size = OBJECTS[name]
        commands.append(f"set memory xram {address:#x} " + " ".join(hex(b) for b in value.to_bytes(size, "little")))
    commands += ["set memory xram 0xec " + " ".join(hex(b) for b in key),
                 "fill xram 0xfc 0x11b 0xa5", "set memory xram 0xfc " + " ".join(hex(b) for b in plain),
                 "fill xram 0xcf 0xeb 0xa5"]
    breakpoints = {pc for (kind, _), pc in sites.items() if kind not in ("vectors", "return")} | {before, done}
    commands += [f"break {pc:#x}" for pc in sorted(breakpoints)] + ["step 1"]
    checks, ram_checks, finishes = [], [], []
    number, current = 10, before + 1
    diag = dict.fromkeys(FIELDS, 0)
    observations = samples = writes = received = sent = phase = 0
    previous, command_control, enc_upper, ircon = v["start"], values[0xb3] & 0xf6, values[0x98], values[0xc0]
    used, encrypted, status_posted = False, False, False
    finish_delay = v["finish_delay"]
    private_out = bytearray(16)
    descriptor0 = descriptor1 = None

    def event(pc, reg=None, value=None):
        nonlocal number, current
        commands.extend(([] if current == pc else ["run"]) + [marker(number), "state", "step 1", marker(number+1)])
        if reg is not None:
            commands.append(f"dump /h sfr {reg:#x} {reg:#x}")
        commands.append(marker(number+2))
        checks.append((number, pc, reg, value))
        number += 3
        current = pc + len(code[pc]) if pc in code else pc + (2 if reg is not None else 1)

    def setreg(reg, value):
        values[reg] = value
        commands.append(f"set memory sfr {reg:#x} {value:#x}")

    def read(reg):
        event(sites["r", reg], 0xe0, values[reg])

    def write(reg, value, kind="w"):
        nonlocal writes
        old = values[reg]
        event(sites[kind, reg], reg, value)
        writes += 1
        accepted = writes != v["ignore_write"]
        setreg(reg, value if accepted else old)
        return accepted, old

    def dump_check(space, address, expected):
        nonlocal number
        commands.extend([marker(number), f"dump /h {space} {address:#x} {address+len(expected)-1:#x}", marker(number+1)])
        ram_checks.append((number, address, bytes(expected))); number += 2

    def timer(initial=False, final=False):
        nonlocal samples, previous
        offset = v["jump"].get(samples, samples)
        now = (v["start"] + offset) & 0xffffff
        for i, byte in enumerate(now.to_bytes(3, "little")):
            setreg(0x95+i, byte); read(0x95+i)
        samples += 1
        if initial:
            return
        elapsed = (now - v["start"]) & 0xffffff
        delta = (now - ((v["start"] + args["timeout"]) & 0xffffff)) & 0xffffff
        diag["elapsed_ticks"], diag["sample_valid"] = elapsed, 3
        if delta == 0x800000:
            diag["timebase_status"] = 2; raise StopModel(10)
        if elapsed >= 0x800000 or (now - previous) & 0xffffff >= 0x800000:
            raise StopModel(11)
        if delta < 0x800000:
            raise StopModel(8)
        if diag["polls"] == args["limit"] and not final:
            raise StopModel(9)
        previous = now

    def engine(late=False):
        nonlocal received, sent, encrypted, status_posted, finish_delay
        budget = 16 if late else v["per_poll"]
        if not phase:
            return
        while budget and received < 16 and not (v["stall_phase"] == phase and received == v["stall_after"]):
            cfg = "(sfr[0xd5]*256+sfr[0xd4])"
            source = f"(xram[{cfg}]*256+xram[{cfg}+1])"
            destination = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
            commands.append(f"expression xram[{destination}]=xram[{source}+{received}]")
            byte = (key if phase == 1 else bytes(16) if phase == 2 else plain)[received]
            values[0xb1] = byte
            dump_check("sfr", 0xb1, bytes((byte,)))
            dump_check("xram", 0x70b1, bytes((byte,)))
            received += 1; budget -= 1
            if received == 16:
                setreg(0xd6, values[0xd6] & ~1)
                if not v["lost_irq"] & 1: setreg(0xd1, values[0xd1] | 1)
        if phase != 3 or received != 16:
            return
        if not encrypted:
            encrypted = True
        if not status_posted:
            if finish_delay:
                finish_delay -= 1
            else:
                status_posted = True
                if not v["lost_irq"] & 8: setreg(0xb3, values[0xb3] | 8)
                if not v["lost_irq"] & 4: setreg(0x98, values[0x98] | 3)
        while budget and sent < 16 and (late or sent != v["stall_output"]):
            cfg = "(sfr[0xd3]*256+sfr[0xd2])"
            source = f"(xram[{cfg}]*256+xram[{cfg}+1])"
            destination = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
            setreg(0xb2, cipher[sent])
            commands.append(f"expression xram[{destination}+{sent}]=xram[{source}]")
            private_out[sent] = cipher[sent]
            dump_check("xram", 0x71+sent, cipher[sent:sent+1])
            sent += 1; budget -= 1
            if sent == 16:
                setreg(0xd6, values[0xd6] & ~2)
                if not v["lost_irq"] & 2: setreg(0xd1, values[0xd1] | 2)

    def observe(initial=False, final=False):
        nonlocal observations
        if not initial:
            if diag["polls"] == args["limit"]: raise StopModel(9)
            diag["polls"] += 1
        observations += 1
        if observations != v["defer_observation"]: engine()
        for reg, value in v["changes"].get(observations, {}).items(): setreg(reg, value)
        for reg in READS[:6]: read(reg)
        diag["sample_valid"] = 0
        cmd = values[0xc6]
        if values[0xa8] or values[0xb8] or values[0x9a] or values[0xbe] & 7 != 4 or values[0x9e] != cmd or (
                cmd & 7 != (1 if cmd & 0x40 else 0)) or (cmd & 0x40 and not cmd & 0x38):
            raise StopModel(4)
        if cmd != (v["initial"].get(0xc6, 0xc9)): raise StopModel(7)
        for reg in READS[6:]:
            read(reg); diag[SFR_FIELDS[reg]] = values[reg]
            if reg == 0xd6 and observations == v["defer_observation"]: engine()
        diag["sample_valid"] = 1
        if initial:
            if values[0xd6] or not values[0xb3] & 8 or values[0xb3] & 1: raise StopModel(5)
            if values[0xd7] or values[0xd1] or values[0xc0] & 1 or values[0x98] & 3: raise StopModel(6)
            if values[0xb3] != (0x48 if used else 8) or (
                    values[0xd5]*256 + values[0xd4] != (desc0 if used else 0)) or (
                    values[0xd3]*256 + values[0xd2] != (desc1 if used else 0)):
                raise StopModel(4)
            return
        if values[0xd6] & 0xfc or values[0xd7] or values[0xd1] & 0xfc or values[0xc0] != ircon or (
                values[0x98] & 0xfc != enc_upper) or values[0xb3] & 0xf6 != command_control or (
                values[0xd5]*256 + values[0xd4] != (desc0 if diag["configured"] & 1 or used else 0)) or (
                values[0xd3]*256 + values[0xd2] != (desc1 if diag["configured"] & 2 or used else 0)):
            raise StopModel(7)
        if not diag["submitted"] and values[0xb3] != (0x48 if used else 8): raise StopModel(7)
        if 1 <= diag["phase"] <= 3 and diag["submitted"] & (1 << (diag["phase"]-1)):
            if not diag["arm"] & 1 and diag["irq"] & 1: diag["input_complete"] |= 1 << (diag["phase"]-1)
            if diag["phase"] == 3 and not diag["arm"] & 2 and diag["irq"] & 2: diag["output_drained"] = 1
        timer(final=final)

    def idle(arm):
        if values[0xd6] != arm or values[0xd1] or values[0x98] != enc_upper or values[0xb3] & 1:
            raise StopModel(7)

    def arm(bit):
        nonlocal current, writes
        old = values[0xd6]
        event(sites["arm", bit], 0xd6, bit)
        writes += 1
        accepted = writes != v["ignore_write"]
        setreg(0xd6, old | bit if accepted else old)
        event(sites["ready", bit])
        current = sites["return", bit]
        dump_check("xram", desc0, descriptor0); dump_check("xram", desc1, descriptor1)
        diag["arms"] += 1
        observe(); idle(3 if bit == 1 else 2)

    def finish(result, retained=False):
        nonlocal number, current
        commands.extend(["run"] + snapshot_commands(number))
        commands.extend([marker(number+4), "dump /h xram 0x6000 0x70ff", marker(number+5)])
        packed = b"\xa5"*29 if result in (1, 2, 3) else b"".join(diag[name].to_bytes(size, "little") for name, size in zip(FIELDS, SIZES))
        finishes.append((number, result, packed, bytes(private_out), dict(values), retained, descriptor0, descriptor1))
        number += 6; current = done

    for cycle in range(2 if v["repeat"] else 1):
        if cycle:
            commands.extend(["step 1", "run", marker(number), "state", marker(number+1)])
            checks.append((number, before, None, None)); number += 2
            commands.append("step 1"); current = before + 1
            v = v | {"start": (v["start"] + samples + 1) & 0xffffff}
            observations = samples = writes = received = sent = phase = 0
            diag = dict.fromkeys(FIELDS, 0); previous = v["start"]; encrypted = status_posted = False
            command_control = 0x40
        try:
            if v["arguments"]:
                raise StopModel(v["result"])
            event(sites["clock", 0], 0xe0, values[0xc6])
            observe(initial=True); timer(initial=True); observe(); idle(0)
            descriptor0 = symbols["_aes_key"].to_bytes(2, "big") + bytes.fromhex("70b100101d41")
            descriptor1 = bytes.fromhex("70b2") + symbols["_aes_output"].to_bytes(2, "big") + bytes.fromhex("00101e11") + bytes(24)
            for reg, value in ((0xd5, desc0 >> 8), (0xd4, desc0 & 255), (0xd3, desc1 >> 8), (0xd2, desc1 & 255)):
                write(reg, value)
            diag["configured"] = 3
            observe(); idle(0)
            arm(2)
            for p in (1, 2, 3):
                diag["phase"] = p
                if p != 1:
                    descriptor0 = symbols["_aes_iv" if p == 2 else "_aes_input"].to_bytes(2, "big") + bytes.fromhex("70b100101d41")
                    observe(); idle(2)
                arm(1)
                accepted, _ = write(0xb3, (0x45, 0x47, 0x41)[p-1])
                command_control = (0x44, 0x46, 0x40)[p-1]
                if accepted:
                    phase = p; received = 0
                    setreg(0xb3, command_control | (8 if (v["stale_rdy"] if p == 3 else v["load_rdy"]) else 0) |
                           (1 if p == v["hold_start_phase"] else 0))
                diag["submitted"] |= 1 << (p-1)
                while True:
                    observe()
                    if p != 3:
                        if not values[0xd6] & 2 or values[0xd1] & 2 or values[0x98] != enc_upper: raise StopModel(7)
                        if diag["input_complete"] & (1 << (p-1)) and not values[0xb3] & 1: break
                    else:
                        if values[0x98] & 3 not in (0, 3): raise StopModel(7)
                        if diag["input_complete"] == 7 and diag["output_drained"] and not values[0xd6] and (
                                values[0xd1] == 3 and values[0xb3] & 9 == 8 and values[0x98] & 3 == 3): break
                accepted, old = write(0xd1, 0x1c if p == 3 else 0x1e, "block_ack" if p == 3 else "w")
                setreg(0xd1, old & (0x1c if p == 3 else 0x1e) if accepted else old)
                diag["ack_issued"] += 1
                observe()
                if values[0xd1] or values[0xd6] != (0 if p == 3 else 2) or values[0xb3] & 1 or (
                        p == 3 and values[0xb3] != 0x48) or values[0x98] != enc_upper | (3 if p == 3 else 0):
                    raise StopModel(7)
                diag["dma_acked"] |= 1 << (p-1)
            diag["phase"] = 4
            write(0x98, enc_upper); diag["enc_ack_issued"] = 1
            observe(final=True)
            if values[0xd6] or values[0xd1] or values[0xb3] != 0x48 or values[0x98] != enc_upper: raise StopModel(7)
            diag["enc_acked"] = diag["published"] = 1; used = True
            result = 0
        except StopModel as stop:
            result = stop.result
        require(result == v["result"], f"{v['name']}: synthetic event plan predicted {result}, expected {v['result']}")
        finish(result)
    if result >= 4:
        if result in (8, 9) and phase == 3 and received == 16 and sent < 16 and encrypted:
            # A separate synthetic late drain changes only retained PRIVATE
            # output/controller state; the original failure/diagnostics remain.
            engine(late=True)
        commands.extend(["step 1", "run", marker(number), "state", marker(number+1)])
        checks.append((number, before, None, None)); number += 2
        commands.append("step 1"); current = before + 1
        finish(result, retained=True)
    text = simulate(simulator, commands, path)
    peaks = [int(n, 16) for n in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "AES crossed upper-IRAM guard")
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.MULTILINE)
    blocks = {int(parts[i], 16): parts[i+1] for i in range(1, len(parts), 2)}
    require(len(blocks) == (len(parts)-1)//2, "Duplicate AES simulation markers")
    for n, pc, reg, expected in checks:
        try:
            check_pc(blocks[n], pc)
        except (KeyError, ValueError) as error:
            raise ValueError(f"{v['name']}: event {n}, expected {pc:04x}\n{blocks.get(n, '')}") from error
        if reg is not None:
            require(memory_dump(blocks[n+1], reg, 1) == bytes((expected,)), f"{v['name']}: real SFR instruction mismatch at {pc:04x}")
    for n, address, expected in ram_checks:
        require(memory_dump(blocks[n], address, len(expected)) == expected, v["name"] + ": descriptor/actual DMA byte or alias mismatch")
    for n, result, packed, private, expected_sfr, retained, d0, d1 in finishes:
        check_pc(blocks[n], done)
        ram = memory_dump(blocks[n], 0, 0x1f00)
        iram = memory_dump(blocks[n+1], 0, 256)
        sfr = memory_dump(blocks[n+2], 0x80, 128)
        require(ram[0x1e00:0x1e08] == b"AEST\x01\x08\0\0", "AES isolated startup/status failed")
        require(ram[0xcf:0xec] == packed, f"{v['name']}: diagnostics {ram[0xcf:0xec].hex()} != {packed.hex()}")
        require(ram[0x13e] == result and ram[0x81] == (result if result >= 4 else 0), "AES wrong return/original terminal latch")
        expected_buffers = bytearray(key + plain + b"\xa5"*16 + b"\xa5"*18)
        if result == 0:
            expected_buffers[args["out"]-0xec:args["out"]-0xec+16] = cipher
        require(ram[0xec:0x12e] == expected_buffers, v["name"] + ": caller key/input/output/tail/guard or failure publication changed")
        require(ram[0x71:0x81] == private, "AES private output differs from actual synthetic drain")
        require(ram[desc0:desc0+8] == (d0 if d0 is not None else bytes(8)) and
                ram[desc1:desc1+32] == (d1 if d1 is not None else bytes(32)),
                v["name"] + ": changed persistent descriptors")
        require(all(byte == 0xa5 for address, byte in enumerate(ram) if address not in allocated), "AES wrote unallocated/status/alias XDATA")
        require(iram[128:] == b"\xc7"*128 and sfr[1] == symbols["s_SSEG"] + 1, "AES stack guard/unwind failed")
        for reg, value in expected_sfr.items():
            require(sfr[reg-0x80] == value, f"{v['name']}: changed guarded SFR {reg:02x}")
        peripheral = bytearray(b"\x69"*0x1100)
        peripheral[0x10b1] = expected_sfr[0xb1]; peripheral[0x10b2] = expected_sfr[0xb2]
        require(memory_dump(blocks[n+4], 0x6000, 0x1100) == peripheral, "AES touched non-AES peripheral XDATA")
        for name, value in args.items():
            address, size = OBJECTS[name]
            require(ram[address:address+size] == value.to_bytes(size, "little"), "AES changed caller scalar/generic-pointer inputs")
    return max(peaks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "aes_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("." + ext).read_text() for ext in ("cdb", "mem"))
    listings = {name: (args.output / (name + ".rst")).read_text() for name in ("aes", "timebase")}
    allocated, sites, code = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    alias = simulate(args.simulator, [AES_ALIAS, "set memory xram 0x70b1 0x52 0x93",
                                     marker(1), "dump /h sfr 0xb1 0xb2", marker(2),
                                     "set memory sfr 0xb2 0x7a", marker(3), "dump /h xram 0x70b1 0x70b2", marker(4)])
    require(memory_dump(section(alias, 1), 0xb1, 2) == b"\x52\x93" and
            memory_dump(section(alias, 3), 0x70b1, 2) == b"\x52\x7a", "AES physical alias model failed")
    cases = list(vectors())
    peak = max(execution(args.simulator, path, args.output / "aes-reference", symbols, allocated, sites, code, v) for v in cases)
    print(f"AES: {len(image)} CODE ({MODULE_SIZE} driver), {len(allocated)-8} ordinary XDATA + 8 result/64 reserved; "
          f"stack {symbols['s_SSEG']:02x}..ff ({symbols['l_SSEG']} reserved), peak {peak:02x}. "
          f"{len(cases)} linked scenarios; complete CODE/ABI/MMIO/private-prefix rejection, four individually "
          "ready arm paths, actual descriptor/AES-alias routing, fresh completion/retained-fault and alias/stack guards PASS. "
          "Synthetic AES/DMA effects only; no silicon, timing or key-management evidence.")


if __name__ == "__main__":
    main()
