#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Bank-aware CC2530 CODE execution with synthetic mapping/controller facts."""

import argparse
import hashlib
import json
import re
from pathlib import Path
from functools import lru_cache

from boot_image import (ALIAS, check_alias, check_pc, marker, memory_dump, section,
                        simulate, snapshot, snapshot_commands)
from banked_image import pack, verify_files
from boot_nwk_candidates import records
from boot_security_resident import LENGTHS, branch
from verify_firmware import cdb_address, parse_symbols, peripheral_accesses, require


MODULES = ("flash_exec", "banked", "banked_fixture", "banked_fixture_bank1",
           "banked_fixture_bank2", "banked_fixture_bank7")
PINS = (
    "b4a5af47d10eb028d8458b93306688180956c111b871cdea38722ffffae9dc60",
    "75238f64a69072c50e23d09b55aeec6af0e789b57e7d5923100f5cac5aecedc4",
    "abf0685f316f9041c584fb5eb821b1bc25785a4a6b25cab555c89b8e6662f2b3",
    "279382bf8bcb05ea2f55f071cd176b87b327c851b706c6d00271581e8381933c",
    "20d7c4dff73f3ffe64416f44d51fd4ab6adce0a3a7f95497e31c8a2885ba53d5",
    "ca895cc0bc1e4132056c22b40d65685ec0cd5a9a2a9982ff02927965c60d31be",
)
_pinned_artifacts = {}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def normalized_object(raw):
    require(isinstance(raw, bytes), "Relocatable object is not immutable bytes")
    if raw.startswith(b";!FILE "):
        _, newline, raw = raw.partition(b"\n")
        require(newline, "Incomplete relocatable path comment")
    require(raw.startswith(b"XH3\n"), "Relocatable format signature changed")
    return raw


def artifact_bytes(image, symbols, debug, memory, listings, objects, modules=MODULES):
    require(set(listings) == set(objects) == set(modules), "Banked object composition changed")
    return (
        b"".join(a.to_bytes(4, "big") + bytes((image[a],)) for a in sorted(image)),
        json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii"),
        debug, memory,
        b"".join(m.encode("ascii") + b"\0" + listings[m] for m in modules),
        b"".join(m.encode("ascii") + b"\0" + normalized_object(objects[m]) for m in modules),
    )


def pin_artifacts(raw, pins=PINS):
    require(len(raw) == len(pins) == 6, "Missing banked artifact")
    for name, content, expected in zip(("CODE", "map", "raw CDB", "memory", "listings", "objects"), raw, pins):
        require(isinstance(content, bytes), f"Complete banked {name} is not immutable bytes")
        if expected not in _pinned_artifacts:
            require(sha(content) == expected, f"Complete banked {name} changed")
            if len(_pinned_artifacts) == 12:
                _pinned_artifacts.clear()
            _pinned_artifacts[expected] = content
        require(content == _pinned_artifacts[expected], f"Complete banked {name} changed")


def verify(image, symbols, debug, memory, listings, objects):
    pin_artifacts(artifact_bytes(image, symbols, debug, memory, listings, objects))
    require(len(image) == 4676 and len(pack(image)) == 4676, "Banked physical CODE accounting changed")
    areas = ("HOME", "GSINIT0", "GSINIT1", "GSINIT2", "GSINIT3", "GSINIT4", "GSINIT5",
             "GSINIT", "GSFINAL", "CSEG", "CONST", "BANK1", "BANK2", "BANK7",
             "BANK1_CONST", "BANK2_CONST", "BANK7_CONST")
    allocated = set()
    for area in areas:
        span = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(not span & allocated, "Overlapping banked CODE areas")
        allocated |= span
    require(set(image) == allocated, "Unassigned bytes or missing bytes in banked CODE")
    require((symbols["s_SSEG"], symbols["l_SSEG"], symbols["s_XSEG"], symbols["l_XSEG"],
             symbols["s_OSEG"], symbols["l_OSEG"], symbols["_banked_depth"], symbols["_banked_fault"],
             symbols["s_BSEG_BYTES"], symbols["s_BIT_BANK"]) ==
            (0x22, 0x5B, 0, 224, 0x12, 6, 0x10, 0x11, 0x20, 0x21),
            "Banked physical RAM allocation changed")
    require(all(symbols["l_"+a] == 0 for a in ("XABS", "XISEG", "XINIT", "PSEG", "ISEG", "IABS")),
            "Unaccounted banked storage")
    require(symbols["_banked_fixture_status"] == 0x1E00 and symbols["__XPAGE"] == 0x93,
            "Banked status/MPAGE ABI changed")
    require(b"16 bit mode initial stack starts at: 0x22 (sp set to 0x21) with 91 bytes available." in memory,
            "Banked link silently changed the CPU return-address/stack ABI")
    text = debug.decode("ascii")
    require("S:G$banked_fixture_status$0_0$0({64}DA64d,SC:U),F,0,0" in text,
            "Banked status size/space changed")
    require(cdb_address(text, "L:G$banked_fixture_const7$0_0$0") == 0x7E7F8,
            "Banked CDB lost the physical bank identity")
    require(not any(n.startswith(("_host_", "_aes_reference")) for n in symbols),
            "Synthetic success/model entered banked CODE")
    decoded, covered = {}, set()
    for module in MODULES:
        listing = listings[module].decode("ascii")
        for pc, raw in records(listing):
            require(raw[0] != 0xA5 and len(raw) == LENGTHS[raw[0]], "Malformed banked instruction")
            span = set(range(pc, pc+len(raw)))
            require(not span & covered and bytes(image.get(pc+i, 0) for i in range(len(raw))) == raw,
                    "Banked relocated instruction differs or overlaps")
            covered |= span
            decoded[pc] = raw
    for pc, raw in decoded.items():
        target = branch(pc, raw)
        if target is not None:
            if raw[0] in (2, 0x12) and target >= 0x8000:
                require(pc >= 0x10000, "Common absolute transfer depends on the selected bank")
                target |= pc & 0x70000
            require(target in decoded or target < 0x65, "Banked transfer has no instruction destination")
        if pc >= 0x10000:
            require(raw[0] not in (0x22, 0x32, 0x73, 0x83, 0x93),
                    "Banked function uses an ordinary return, unreviewed indirect transfer or CODE pointer")
    require([(pc, raw) for pc, raw in decoded.items() if raw[0] == 0x32] == [(0x79C, b"\x32")],
            "Banked ISR no longer executes the sole real RETI")
    require([(pc, raw) for pc, raw in decoded.items() if raw[0] == 0x73] == [(0xE8, b"\x73")],
            "Unreviewed indirect jump in banked image")
    require([pc for pc, raw in decoded.items() if raw[0] in (0x83, 0x93)] == [0x354, 0x38B, 0x6E8],
            "Unreviewed source bank for a CODE read")
    fmap = [(pc, raw) for pc, raw, sfr in peripheral_accesses(decoded) if sfr == 0x9F and
            (raw[0] in (0x75, 0x85, 0xF5, 0xD0) or 0x88 <= raw[0] <= 0x8F)]
    require(fmap == [(0x54F, b"\x8a\x9f"), (0x5B0, b"\x88\x9f"),
                     (0x6B1, b"\x8a\x9f"), (0x6FA, b"\x8f\x9f"), (0x77E, b"\xd0\x9f")],
            "Unreviewed FMAP writer")
    require(sha(bytes(image[a] for a in range(0x65, 0xE0))) ==
            "87ac19a19ee72052c542b9b159adc1d1f2618e506324522ffb0ebfd8db504f8d",
            "Genuine copied flash engine changed")
    require(sha(objects["flash_exec"].split(b"\n", 1)[1]) ==
            "52ceded037af553b1058950185143bb0175dfb7a7dcd350bf41f8c02e9dec2f2",
            "Banked profile changed the production flash object")
    return decoded


def artifact_negatives(raw, pins=PINS):
    count = 0
    # Every linked address and byte participates in the CODE identity.
    positions = range(len(raw[0]))
    for offset in positions:
        changed = list(raw)
        content = bytearray(raw[0]); content[offset] ^= 1
        changed[0] = bytes(content)
        try:
            pin_artifacts(changed, pins)
        except ValueError:
            count += 1
        else:
            raise ValueError("Altered banked CODE/address identity passed")
    for index in range(1, len(raw)):
        for replacement in (raw[index][1:], raw[index]+b"\0", raw[index].replace(b"\n", b"\r\n"),
                            raw[index][:len(raw[index])//2] + b"!" + raw[index][len(raw[index])//2+1:]):
            if replacement == raw[index]:
                continue
            changed = list(raw); changed[index] = replacement
            try:
                pin_artifacts(changed, pins)
            except ValueError:
                count += 1
            else:
                raise ValueError("Altered banked metadata passed")
    return count


XMAP = ("memory create addressdecoder rom 0x8000 0x9eff xram_chip 0",
        "memory create addressdecoder rom 0x9f00 0x9fff iram_chip 0")


@lru_cache(maxsize=1)
def numeric_debugger(simulator):
    text = simulate(simulator, ["info var"])
    names = re.findall(r"^(\S+) \S+\[0x[0-9a-f]+\].* = ", text, re.M)
    require(len(names) == len(set(names)) == 140, "Pinned C52 debugger name table changed")
    return ["set option analyzer false"] + [f'rmvar "{name}"' for name in names]


def run_model(simulator, commands):
    # The pinned banker's label lookup dereferences a null memchip. Clear only
    # simulator names; never replace banked CODE, CPU registers or instructions.
    # Use only for audited bank-window stops; common-only sweeps keep native names.
    return simulate(simulator, numeric_debugger(simulator) + commands)


def code_banks(start=0x8000, *, fixed=False):
    reset = ["memory create addressdecoder rom 0x8000 0xffff flash 0"] if start == 0x8000 else []
    return reset + [f"memory create banker sfr 0x9f 7 rom {start:#x} 0xffff"] + [
        f"memory create bank rom {start:#x} {bank} flash "
        f"{(1 if fixed else bank) * 0x8000 + start - 0x8000:#x}"
        for bank in range(8)]


def xmap():
    # Replace the WHOLE banker first: uCsim's partial banker splitting is unsafe.
    return ["memory create addressdecoder rom 0x8000 0xffff flash 0"] + list(XMAP) + code_banks(0xA000)


def model(image, *, fixed=False):
    commands = [
        ALIAS, "memory create chip flash 0x40000 8", "fill flash 0 0x3ffff 0xff",
        "memory create addressdecoder rom 0 0x7fff flash 0",
    ] + code_banks(fixed=fixed) + ["memory create banker sfr 0xc7 7 xram 0x8000 0xffff"]
    commands += [f"memory create bank xram 0x8000 {bank} flash {bank * 0x8000:#x}"
                 for bank in range(8)]
    physical = pack(image)
    addresses = sorted(physical)
    index = 0
    while index < len(addresses):
        start = addresses[index]
        data = [physical[start]]
        index += 1
        while index < len(addresses) and len(data) < 32 and addresses[index] == start + len(data):
            data.append(physical[addresses[index]])
            index += 1
        commands.append(f"set memory flash {start:#x} " + " ".join(f"{b:#x}" for b in data))
    commands += ["set memory sfr 0x9f 1", "set memory sfr 0xc7 0"]
    return commands


def check_mapping(simulator, *, code=True, alias=True):
    # Identical logical addresses must select physically distinct cells, including bank0.
    commands = model({0: 0x69}, fixed=not code)
    if not alias:
        commands.append("memory create addressdecoder xram 0x1f00 0x1fff xram_chip 0x1f00")
    for bank in range(8):
        commands += [f"set memory flash {bank * 0x8000 + 0x1234:#x} {0x50 + bank}",
                     f"set memory flash {bank * 0x8000 + 0x2345:#x} {0x60 + bank}"]
    for bank in range(8):
        commands += [
            f"set memory sfr 0x9f {bank}", f"set memory sfr 0xc7 {7-bank}",
            marker(10+bank*2), "dump /h rom 0x9234 0x9234", "dump /h xram 0xa345 0xa345",
            "dump /h rom 0 0", marker(11+bank*2),
        ]
    if not code:
        text = simulate(simulator, commands)
        require(memory_dump(section(text, 10), 0x9234, 1) == b"\x50", "FMAP CODE bank not selected")
        return
    commands += ["set memory sfr 0x9f 2", "set memory sfr 0xc7 0xb"] + xmap()
    commands += [
        "set memory xram 0x1234 0x96", "set memory iram 0xa0 0x87",
        marker(40), "dump /h rom 0x9234 0x9234", "dump /h rom 0x9fa0 0x9fa0",
        "dump /h xram 0x1fa0 0x1fa0", "dump /h rom 0xa345 0xa345", marker(41),
        "set memory sfr 0x9f 7", marker(42), "dump /h rom 0x9234 0x9234",
        "dump /h rom 0xa345 0xa345", marker(43),
        "set memory sfr 0xc7 3",
    ] + code_banks() + [marker(44), "dump /h rom 0x9234 0x9234", marker(45),
                       marker(46), "dump /h xram 0xa345 0xa345", marker(47)]
    commands += ["set memory sfr 0x9f 4", marker(48), "dump /h rom 0x9234 0x9234", marker(49)]
    text = simulate(simulator, commands)
    for bank in range(8):
        part = section(text, 10+bank*2)
        require(memory_dump(part, 0x9234, 1) == bytes((0x50+bank,)), "FMAP CODE bank not selected")
        require(memory_dump(part, 0xA345, 1) == bytes((0x67-bank,)), "XBANK is not independent of FMAP")
        require(memory_dump(part, 0, 1) == b"\x69", "Common CODE changed with bank")
    part = section(text, 40)
    require(memory_dump(part, 0x9234, 1) == b"\x96"
            and memory_dump(part, 0x9FA0, 1) == b"\x87"
            and memory_dump(part, 0x1FA0, 1) == b"\x87", "XMAP/IRAM alias mapping missing")
    require(memory_dump(part, 0xA345, 1) == b"\x62", "XMAP unexpectedly obscures CODE A000..FFFF")
    require(memory_dump(section(text, 46), 0xA345, 1) == b"\x63", "XBANK changed during XMAP")
    require(memory_dump(section(text, 42), 0x9234, 1) == b"\x96"
            and memory_dump(section(text, 42), 0xA345, 1) == b"\x67",
            "FMAP write destroyed XMAP or failed to switch the remaining flash")
    require(memory_dump(section(text, 44), 0x9234, 1) == b"\x57", "XMAP restoration lost FMAP")
    require(memory_dump(section(text, 48), 0x9234, 1) == b"\x54", "Restored FMAP banker is not live")


def stop_at(pc, number, hit=1):
    stopped = ([f"break {pc:#x} {hit}", "commands analyze", "run", f"clear {pc:#x}"]
               if pc >= 0x8000 else [f"tbreak {pc:#x} {hit}", "run"])
    return (["analyze"] if pc >= 0x8000 else []) + stopped + [marker(number), "state",
            "dump /h sfr 0x9f 0x9f", "dump /h sfr 0xc7 0xc7", marker(number+1)]


def start(image, symbols):
    return model(image) + [
        "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x7fff 0x69",
        "set memory sfr 0x9a 0", f"run 0 {symbols['_main']:#x}",
        "fill iram 0x7d 0xff 0xc7",
        "fill iram 0x18 0x1f 0xd3",
        f"break {symbols['_banked_stop']:#x}", f"break {symbols['_banked_fixture_failed']:#x}",
    ]


def check_debugger_equivalence(simulator, image, symbols):
    commands = start(image, symbols) + [f"run {symbols['_main']:#x} {symbols['_banked_fixture_after_calls']:#x}"]
    commands += snapshot_commands(20)
    plain = simulate(simulator, commands)
    numeric = run_model(simulator, commands)
    check_pc(section(plain, 20), symbols["_banked_fixture_after_calls"])
    check_pc(section(numeric, 20), symbols["_banked_fixture_after_calls"])
    require(snapshot(plain, 20) == snapshot(numeric, 20),
            "Numeric debugger workaround changed executed CPU/RAM state")


def runtime_faults(simulator, image, symbols):
    call, ret, stop = (symbols[n] for n in ("__sdcc_banked_call", "__sdcc_banked_ret", "_banked_stop"))
    cases = [
        (call, "set memory iram 2 0", 1), (call, "set memory iram 2 8", 1),
        (call, "set memory iram 2 255", 1), (call, "set memory iram 1 0x7f", 1),
        (call, "set memory iram 1 0xe8 7", 1),
        (call, "set memory sfr 0x92 1", 2), (call, "set memory sfr 0xc7 8", 2),
        (call, "set memory sfr 0xc7 0x80", 2), (call, "set memory sfr 0x9f 8", 2),
        (call, "set memory sfr 0xd0 8", 2),
        (call, "set memory iram 0x10 8", 3), (call, "set memory iram 0x10 255", 3),
        (ret, "set memory iram 0x10 0", 3), (ret, "set memory iram 0x10 9", 3),
        (ret, "set memory sfr 0xc7 8", 2), (ret, "set memory sfr 0x92 1", 2),
        (call, "set memory sfr 0x81 0x22", 4), (call, "set memory sfr 0x81 0x7a", 4),
        (call, "set memory sfr 0x81 0xff", 4),
        (ret, "set memory sfr 0x81 0x23", 4), (ret, "set memory sfr 0x81 0x7b", 4),
        (ret, "set memory sfr 0x81 0xff", 4),
        (0x551, "set memory sfr 0x9f 2", 5), (0x5B2, "set memory sfr 0x9f 2", 5),
        (0x6B3, "set memory sfr 0x9f 2", 5), (0x6FC, "set memory sfr 0x9f 2", 5),
    ]
    for entry, mutation, fault in cases:
        commands = start(image, symbols) + stop_at(entry, 10)
        commands += [mutation, "set memory sfr 0xa8 0x80"] + snapshot_commands(20)
        commands += stop_at(stop, 30) + snapshot_commands(40) + ["step 32"] + snapshot_commands(50)
        text = simulate(simulator, commands)
        check_pc(section(text, 10), entry)
        check_pc(section(text, 30), stop)
        ram, iram, sfr = snapshot(text, 40)
        require(iram[0x11] == fault and not sfr[0xA8-0x80] & 0x80, "Bank fault did not fail closed")
        require(snapshot(text, 50) == (ram, iram, sfr), "Bank fault resumed or changed retained state")
        before = snapshot(text, 20)
        if fault == 4:
            # The admission check must reject even SPff BEFORE its first push.
            require(iram[:3] == before[1][:3] and iram[5:] == before[1][5:0x11]
                    + bytes((fault,)) + before[1][0x12:], "Stack rejection wrote through wrapped SP")
    return len(cases)


def interrupt_boundaries(simulator, image, symbols, decoded):
    boundaries = [pc for pc in decoded if 0x503 <= pc <= 0x55C or 0x576 <= pc <= 0x5B9]
    cases = [(hit, pc) for hit in range(1, 6) for pc in boundaries
             if hit in (4, 5) or pc not in (0x540, 0x541, 0x543)]
    for hit, pc in cases:
        occurrence = hit-3 if pc in (0x540, 0x541, 0x543) else hit
        commands = start(image, symbols) + stop_at(pc, 10, occurrence)
        commands += ["set memory sfr 0xa8 0x81", "set memory sfr 0x88 3"]
        commands += stop_at(symbols["_banked_fixture_irq_enter"], 12)
        commands += stop_at(symbols["_banked_fixture_irq_exit"], 14)
        commands += stop_at(symbols["_banked_fixture_after_calls"], 16) + snapshot_commands(20)
        text = simulate(simulator, commands)
        check_pc(section(text, 10), pc)
        check_pc(section(text, 12), symbols["_banked_fixture_irq_enter"])
        check_pc(section(text, 14), symbols["_banked_fixture_irq_exit"])
        check_pc(section(text, 16), symbols["_banked_fixture_after_calls"])
        ram, iram, sfr = snapshot(text, 20)
        require(ram[0x1E08:0x1E17] == bytes.fromhex("1aca5fd52c3354810dd06d916a4101"),
                f"Interrupt at trampoline {pc:04x} corrupted argument/return/ISR state")
        require(iram[0x10:0x12] == b"\0\0" and sfr[1] == 0x21 and sfr[0x9F-0x80] == 1,
                "Interrupt corrupted bank frame/depth/unwind")
        require(iram[0x7D:] == b"\xc7"*131 and iram[0x18:0x20] == b"\xd3"*8,
                "Interrupt used unallocated IRAM")
        peaks = [int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
        require(peaks and max(peaks) <= 0x7C, "Interrupted bank trampoline exceeded stack")
    return len(cases)


def constant_errors(simulator, image, symbols):
    cases = (
        ("set memory sfr 0x82 8", 1), ("set memory sfr 0x82 255", 1),
        ("set memory xram 0x9b 0xff 0x7f 2", 1),
        ("set memory sfr 0x82 1\nset memory xram 0x9b 0xff 0x7f 1", 1),
        ("set memory sfr 0x82 7\nset memory xram 0x9b 0xff 0xe7 2", 1),
        ("set memory xram 0x9d 0", 1), ("set memory xram 0x9d 33", 1),
        ("set memory xram 0x9e 0 0", 2), ("set memory xram 0x9e 0xa2 0", 2),
        ("set memory xram 0x9e 0xff 0x1d", 2), ("set memory xram 0x9e 0 0x1e", 2),
        ("set memory xram 0x9e 0 0x1f", 2), ("set memory xram 0x9e 0xff 0xff", 2),
        ("set memory sfr 0x92 1", 3), ("set memory sfr 0x9f 8", 3),
        ("set memory sfr 0xc7 0x80", 3),
    )
    for mutation, result in cases:
        commands = start(image, symbols) + stop_at(symbols["_banked_code_read"], 10)
        commands += mutation.splitlines() + snapshot_commands(20)
        commands += stop_at(0x8CC, 30) + snapshot_commands(40)
        text = simulate(simulator, commands)
        check_pc(section(text, 30), 0x8CC)
        before = snapshot(text, 20); after = snapshot(text, 40)
        require(after[2][2] == result,
                f"Constant helper returned {after[2][2]}, wanted {result}: {mutation}")
        require(after[0][0xA3:] == before[0][0xA3:] and after[0][:0x9B] == before[0][:0x9B],
                "Rejected constant read changed caller/flash storage")
        require(after[2][0x9F-0x80] == before[2][0x9F-0x80], "Rejected constant read changed FMAP")
    return len(cases)


def constant_interrupts(simulator, image, symbols, decoded):
    sites = [pc for pc in decoded if 0x6A6 <= pc <= 0x709 and pc not in (0x6B8, 0x6BA, 0x701, 0x703)]
    for call, pc in ((call, pc) for call in (0x8C9, 0x924, 0x97F, 0x9DA) for pc in sites):
        commands = start(image, symbols) + stop_at(call, 8) + stop_at(pc, 10)
        commands += ["set memory sfr 0xa8 0x81", "set memory sfr 0x88 3"]
        commands += stop_at(symbols["_banked_fixture_irq_enter"], 12)
        commands += stop_at(symbols["_banked_fixture_irq_exit"], 14)
        commands += stop_at(call+3, 16) + ["set memory sfr 0xa8 0"]
        commands += stop_at(0xC6B, 18) + ["step 1"] + xmap()
        commands += stop_at(0xC8E, 20) + ["step 1"] + code_banks()
        commands += stop_at(symbols["_banked_fixture_after_constants"], 22) + snapshot_commands(30)
        text = simulate(simulator, commands)
        for number, target in ((10, pc), (12, symbols["_banked_fixture_irq_enter"]),
                               (14, symbols["_banked_fixture_irq_exit"]), (16, call+3),
                               (18, 0xC6B), (20, 0xC8E), (22, symbols["_banked_fixture_after_constants"])):
            check_pc(section(text, number), target)
        ram, iram, sfr = snapshot(text, 30)
        require(ram[0x1E20:0x1E29] == bytes.fromhex("d36ea14c8729f05b0c") and ram[0x1E16] == 1,
                "Interrupt corrupted foreign-bank constants or error publication")
        require(ram[0x1E07] == 0 and iram[0x10:0x12] == b"\0\0" and sfr[1] == 0x21
                and sfr[0x9F-0x80] == 1, "Constant-read IRQ failed to restore context")
        bank = memory_dump(section(text, 12), 0x9F, 1)
        require(memory_dump(section(text, 14), 0x9F, 1) == bank and ram[0x1E2C:0x1E2D] == bank,
                "ISR failed to preserve the in-progress constant reader's bank")
        require(iram[0x7D:] == b"\xc7"*131, "Constant-read IRQ escaped stack reservation")
    return len(sites)*4


def prefix(image, symbols):
    commands = start(image, symbols)
    commands += stop_at(symbols["_banked_fixture_after_calls"], 100)
    commands += stop_at(0xC6B, 102) + ["step 1"] + xmap()
    commands += stop_at(0xC8E, 104) + ["step 1"] + code_banks()
    commands += stop_at(symbols["_banked_fixture_after_constants"], 106)
    commands += stop_at(symbols["_banked_fixture_irq_window"] & 0xFFFF, 108)
    # C52 edge-triggered INT0 supplies an actual vector/RETI CPU transaction,
    # not a claim about the CC2530's RFERR source/acknowledgment registers.
    commands += ["set memory sfr 0x88 3"] + stop_at(symbols["_banked_fixture_irq_enter"], 110)
    commands += stop_at(symbols["_banked_fixture_irq_exit"], 112)
    commands += stop_at(symbols["_banked_fixture_after_irq"], 114)
    commands += [
        "set memory sfr 0xc6 0xc9", "set memory sfr 0x9e 0xc9", "set memory sfr 0xbe 4",
        "set memory sfr 0xd6 0", "set memory sfr 0xd7 0", "set memory sfr 0xc7 2",
        "set memory xram 0x624a 0xa5", "set memory xram 0x6276 0x44 0xff",
        "set memory xram 0x6270 4",
    ] + stop_at(symbols["_banked_fixture_flash_before"], 116)
    return commands


def check_prefix(text, symbols):
    for number, pc, bank, mapping in (
            (100, symbols["_banked_fixture_after_calls"], 1, 0), (102, 0xC6B, 1, 0),
            (104, 0xC8E, 1, 8), (106, symbols["_banked_fixture_after_constants"], 1, 0),
            (108, symbols["_banked_fixture_irq_window"] & 0xFFFF, 2, 0),
            (110, symbols["_banked_fixture_irq_enter"], 2, 0),
            (112, symbols["_banked_fixture_irq_exit"], 2, 0),
            (114, symbols["_banked_fixture_after_irq"], 1, 0),
            (116, symbols["_banked_fixture_flash_before"], 7, 2)):
        part = section(text, number)
        check_pc(part, pc)
        require(memory_dump(part, 0x9F, 1) == bytes((bank,)), "Unexpected phase FMAP")
        require(memory_dump(part, 0xC7, 1) == bytes((mapping,)), "Unexpected phase MEMCTR")


def execute(simulator, image, symbols, *, mode="normal"):
    commands = prefix(image, symbols)
    commands += stop_at(0x48A, 118) + ["step 1"] + xmap()
    commands += stop_at(0x8009, 120)
    commands += [marker(122), "dump /h rom 0x8009 0x8083", marker(123)]
    commands += stop_at(0x802C, 124) + [
        "step 1", marker(126), "dump /h xram 0x6270 0x6273", marker(127),
        "set memory xram 0x6270 0x86",
    ]
    for i, pc in enumerate((0x803C, 0x803E, 0x8040, 0x8042)):
        commands += stop_at(pc, 130+i*4) + [
            "step 1", marker(132+i*4), "dump /h xram 0x6273 0x6273", marker(133+i*4)]
    commands += stop_at(0x804A, 150)
    if mode == "stuck":
        commands += stop_at(0x8063, 152) + snapshot_commands(200)
        commands += ["step 32"] + snapshot_commands(210)
        commands += ["set memory xram 0x6270 4", "step 32"] + snapshot_commands(220)
    else:
        commands += ["step 1"] + stop_at(0x804A, 152)
        commands += ["set memory xram 0x6270 " + ("0x24" if mode == "abort" else "4")]
        commands += stop_at(0x8083, 154) + ["step 1", marker(156), "state", marker(157)]
        if mode == "normal":
            commands += stop_at(0x4DD, 158) + ["step 1"] + code_banks()
            commands += stop_at(symbols["_banked_fixture_flash_after"], 160)
            commands += stop_at(symbols["_banked_fixture_done"], 162)
        else:
            commands += stop_at(symbols["_banked_fixture_failed"], 162)
        commands += snapshot_commands(200)
    text = run_model(simulator, commands)
    check_prefix(text, symbols)
    ram, iram, sfr = snapshot(text, 200)
    require(memory_dump(section(text, 122), 0x8009, 123) ==
            bytes(image[a] for a in range(0x65, 0xE0)), "RAM CODE differs from the real copied flash engine")
    require(memory_dump(section(text, 126), 0x6270, 4)[:3] == b"\x06\x00\xfa",
            "Flash command/address was not genuinely emitted")
    for i, byte in enumerate(b"\x12\x34\x56\x78"):
        require(memory_dump(section(text, 132+i*4), 0x6273, 1) == bytes((byte,)),
                "Flash word byte was not genuinely emitted")
    require(iram[0x7D:] == b"\xc7" * 131, "Banked stack/IRAM guard changed")
    require(iram[0x18:0x20] == b"\xd3" * 8, "Banked execution used unallocated lower IRAM")
    require(all(b == 0xA5 for a, b in enumerate(ram) if not (a < 224 or 0x1E00 <= a < 0x1E40)),
            "Banked execution wrote outside allocated XDATA")
    require(sfr[0x9F-0x80] == (1 if mode == "normal" else 7), "Banked final FMAP is wrong")
    require(sfr[0xC7-0x80] == (2 if mode == "normal" else 10), "Unsafe XMAP restoration")
    require(sfr[0xA8-0x80] == sfr[0xB8-0x80] == sfr[0x9A-0x80] == 0,
            "Interrupts enabled during/after flash command")
    if mode == "normal":
        expected = bytearray(64)
        expected[:6] = b"BNK1\x01\x40"
        expected[6] = 5
        expected[8:22] = bytes.fromhex("1aca5fd52c3354810dd06d916a41")
        expected[22:28] = b"\x01"*6
        expected[29:32] = b"\x07\x07\x02"
        expected[32:40] = bytes.fromhex("d36ea14c8729f05b")
        expected[40] = 12
        expected[43:45] = b"\x0f\x02"
        expected[48:52] = b"\xff"*4
        require(ram[0x1E00:0x1E40] == expected, "Complete banked fixture result differs")
        require(iram[0x10:0x12] == b"\0\0" and sfr[1] == 0x21, "Banked frame did not unwind")
        check_pc(section(text, 200), symbols["_banked_fixture_done"])
    elif mode == "stuck":
        check_pc(section(text, 200), 0x8063)
        require(ram[7] == 7 and ram[0x1E06:0x1E08] == b"\x04\0",
                "Busy exhaustion did not retain genuine RAM_STOP")
        for number in (210, 220):
            require(snapshot(text, number) == (ram, iram, sfr), "RAM fail-stop released ownership")
    else:
        check_pc(section(text, 200), symbols["_banked_fixture_failed"])
        require(ram[7] == 5 and ram[0x1E07] == 6 and ram[0x1E1C] == 5,
                "Idle flash error escaped the common failure wrapper")
    peaks = [int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) <= 0x7C, "Banked execution exceeded the stack ceiling")
    return max(peaks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    output = args.output / "banked"
    image = verify_files(output)
    symbols = parse_symbols((output / "banked.map").read_text(encoding="ascii"))
    debug, memory = ((output / ("banked."+ext)).read_bytes() for ext in ("cdb", "mem"))
    listings = {m: (output / f"banked.{m}.rst").read_bytes() for m in MODULES}
    objects = {m: (output / f"{m}.rel").read_bytes() for m in MODULES}
    decoded = verify(image, symbols, debug, memory, listings, objects)
    negatives = artifact_negatives(artifact_bytes(image, symbols, debug, memory, listings, objects))
    check_debugger_equivalence(args.simulator, image, symbols)
    check_alias(args.simulator)
    check_mapping(args.simulator)
    for kwargs, message in (({"code": False}, "FMAP"), ({"alias": False}, "alias")):
        try:
            check_mapping(args.simulator, **kwargs)
        except ValueError as error:
            require(message in str(error), "Wrong bank-model negative control")
        else:
            raise ValueError("Broken bank/alias model unexpectedly passed")
    peak = max(execute(args.simulator, image, symbols, mode=mode)
               for mode in ("normal", "stuck", "abort"))
    faults = runtime_faults(args.simulator, image, symbols)
    errors = constant_errors(args.simulator, image, symbols)
    interrupts = interrupt_boundaries(args.simulator, image, symbols, decoded)
    interrupts += constant_interrupts(args.simulator, image, symbols, decoded)
    print(f"Banked CODE: {negatives} artifact negatives, {faults} terminal negatives; "
          f"{errors} returned errors, {interrupts} IRQ boundary cases; "
          f"real nested/pointer/IRQ calls, constants, flash return/fail-stop; normal SP {peak:02x}/7c")


if __name__ == "__main__":
    main()
