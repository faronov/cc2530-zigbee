#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the actual copied flash engine with synthetic XMAP/controller events; never flash."""
import argparse
import hashlib
from pathlib import Path
import re
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate,
    snapshot, snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import instructions
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require


SIZE = 1451
DIGEST = "1565921ea42f0222f67a9bf9bc72865d1dfe98a112ab4616a4c2668677163926"
BEFORE, DONE, MAIN = 0x4fa, 0x545, 0x547
TEMPLATE, END, RAM, DELTA = 0x62, 0xdd, 0x8009, 0x7fa7
COMMAND, ACCEPT, POLL, STOP, RET = (pc+DELTA for pc in (0x85, 0x86, 0xa3, 0xbc, 0xdc))
LENGTHS = PRNG_LENGTHS | {0xb4: 3, 0x49: 1, 0x73: 1, 0x23: 1, 0x59: 1, 0xce: 1, 0x13: 1, 0xcb: 1}
XMAP = ("memory create addressdecoder rom 0x8000 0x9eff xram_chip 0",
        "memory create addressdecoder rom 0x9f00 0x9fff iram_chip 0")
SFR_READS = ((0xef, 0xa8), (0xf5, 0xb8), (0xfb, 0x9a), (0x101, 0xd6),
            (0x107, 0xd7), (0x10d, 0xbe), (0x113, 0xc6), (0x119, 0x9e), (0x124, 0xc7))
IO_SITES = tuple(pc for pc, _ in SFR_READS) + (
    0x275, 0x27f, 0x487, 0x489, 0x4da, 0x4dc, 0x11f, 0x285, 0x2be, 0x2c3, 0x2c8,
    0x45c, 0x463, 0x467, 0x46f,
)
OBJECTS = {"work": (0, 9), "ram": (9, 123), "reserved_end": (0x9a, 1),
           "test_word": (0x9b, 4), "test_pointer": (0x9f, 2), "test_offset": (0xa1, 2),
           "test_limit": (0xa3, 2), "test_operation": (0xa5, 1), "test_page": (0xa6, 1),
           "test_return": (0xa7, 1)}


def verify(image, symbols, debug, memory, listing):
    require(hashlib.sha256(code_bytes(image, SIZE)).hexdigest() == DIGEST,
            "Flash executor complete CODE changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "flash_exec_test_result",
                                        ("flash_exec.c", "test_flash_exec.c"))
    code = instructions(image, TEMPLATE, BEFORE, LENGTHS)
    listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.M)]
    require(list(code.items()) == listed, "Flash executor relocated listing differs from CODE")
    accesses = [(pc, bytes((0xe5, reg)), reg) for pc, reg in SFR_READS]
    accesses += [(pc, bytes.fromhex(raw), 0xc7 if pc != 0x27f else 0xc6) for pc, raw in (
        (0x275, "aa c7"), (0x27f, "e5 c6"), (0x487, "8e c7"),
        (0x489, "b5 c7 02"), (0x4da, "8f c7"), (0x4dc, "b5 c7 02"))]
    require(peripheral_accesses(code) == accesses, "Flash executor acquired unreviewed SFR access")
    require([(pc, raw) for pc, raw in code.items() if raw[0] == 0x12] ==
            [(pc, bytes.fromhex(raw)) for pc, raw in (
                (0x2a1, "12 00 e6"), (0x423, "12 00 e6"), (0x49b, "12 00 e6"),
                (0x4b0, "12 00 dd"), (0x4c6, "12 00 e6"))],
            "Flash executor runtime/helper/observation calls changed")
    high = [(pc, int.from_bytes(raw[1:], "big")) for pc, raw in code.items()
            if raw[0] == 0x90 and int.from_bytes(raw[1:], "big") >= 0x1e00]
    require(high == [(0x80, 0x6270), (0x91, 0x6273), (0xa0, 0x6270), (0xe1, RAM),
                     (0x11c, 0x6270), (0x282, 0x6270), (0x2bb, 0x624a), (0x2c0, 0x6276),
                     (0x2c5, 0x6277), (0x458, 0x6271), (0x45f, 0x6272),
                     (0x464, 0x6271), (0x46c, 0x6272)], "Flash executor peripheral addresses changed")
    require(code[0x351] == code[0x388] == b"\x93" and code[0x358] == b"\xf0" and
            code[0x379] == b"\xe0", "Flash RAM copy/physical readback vanished")
    require(code[0x45c] == code[0x463] == b"\xf0" and code[0x467] == code[0x46f] == b"\xe0",
            "Flash address-register write/readback sites changed")
    require(bytes(image[pc] for pc in range(0xdd, 0xe6)) == bytes.fromhex("7a 00 7b 00 90 80 09 e4 73"),
            "Flash RAM entry is not a tail jump with the genuine C call frame")
    core = instructions(image, TEMPLATE, END, LENGTHS)
    require(END-TEMPLATE == 123 and [pc for pc, raw in core.items() if raw == b"\x22"] == [0xdc] and
            core[0xbc] == b"\x80\xfe", "Flash RAM extent/return/fail-stop changed")
    require(not any(raw[0] in (0x02, 0x12, 0x32, 0x73, 0x83, 0x93) or raw[0] & 0x1f in (1, 0x11)
                    for raw in core.values()), "Flash RAM contains an absolute jump/call or CODE load")
    for pc, raw in core.items():
        if raw[0] in (0x20, 0x30, 0x60, 0x70, 0x80, 0xb4):
            destination = pc+len(raw)+int.from_bytes(raw[-1:], "big", signed=True)
            require(destination in core, "Flash RAM relative branch escapes its copied instructions")
    require(bytes(image[pc] for pc in range(0x94, 0x9c)) == bytes.fromhex("ec f0 ed f0 ee f0 ef f0"),
            "Flash word is not four back-to-back register/FWDATA pairs")
    # TI's tabulated best-case cycles, not a measured bound or uCsim timing.
    cycles = {0xf0: 4, 0xe0: 3, 0x65: 2, 0xb4: 4, 0xe5: 2, 0x30: 4, 0x90: 3,
              0xec: 1, 0xed: 1, 0xee: 1, 0xef: 1}
    require(sum(cycles[raw[0]] for pc, raw in core.items() if 0x85 <= pc <= 0x9b) == 42 and
            42 < 16*20, "Flash staged WRITE/data nominal instruction budget changed")
    expected = {"_flash_exec_command": 0x1cf, "_flash_exec_template_end": END,
                "_flash_exec_before": BEFORE, "_flash_exec_done": DONE, "_flash_exec_test_cycle": BEFORE,
                "_main": MAIN, "_SOC_MEMCTR": 0xc7, "s_XSEG": 0, "l_XSEG": 0xa8,
                "s_SSEG": 0x21, "l_BSEG": 1, "_flash_exec_command_PARM_2": 0x91,
                "_flash_exec_command_PARM_3": 0x92, "_flash_exec_command_PARM_4": 0x94,
                "_flash_exec_command_PARM_5": 0x96}
    for name, value in expected.items():
        require(symbols.get(name) == value, "Flash executor linked allocation changed: " + name)
    require(not any(name in symbols for name in ("__gptrget", "__gptrput", "_flash_exec_host_enter",
                                                "_flash_exec_host_template")),
            "Flash executor acquired a generic pointer or host model")
    for name, (address, size) in OBJECTS.items():
        require(symbols["_flash_exec_"+name] == cdb_address(debug, "L:G$flash_exec_"+name+"$0_0$0") == address,
                "Flash executor object location changed")
        sizes = re.findall(rf"^S:G\$flash_exec_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "Flash executor object ABI size changed")
    for name, address in (("mapping", 0x84), ("clock_command", 0x85), ("cache_mode", 0x86)):
        require(cdb_address(debug, f"L:Fflash_exec${name}$0_0$0") == address and
                re.search(rf"^S:Fflash_exec\${name}\$[^(\n]+\(\{{1\}}", debug, re.M),
                "Flash executor saved ownership storage changed")
    for name, address in (("flash_exec_template", TEMPLATE), ("enter_ram", END), ("idle", 0xe6)):
        require(cdb_address(debug, f"L:Fflash_exec${name}$0$0") == address,
                "Flash executor function boundary changed")
    require(cdb_address(debug, "L:XG$flash_exec_command$0$0")+1 == BEFORE,
            "Flash executor module extent changed")
    locals_expected = {"flash_exec_command.sloc"+str(i): 8+2*i for i in range(4)}
    locals_expected.update({"idle."+name: 0x87+i for i, name in enumerate(
        ("expected_mapping", "ien0", "ien1", "ien2", "arm", "request", "sleep", "command", "status", "bank"))})
    locals_expected.update({"flash_exec_command."+name: address for name, address in (
        ("page", 0x91), ("offset", 0x92), ("word", 0x94), ("poll_limit", 0x96),
        ("operation", 0x98), ("result", 0x99))})
    actual = {m[1]+"."+m[2]: int(m[3], 16) for m in re.finditer(
        r"^L:Lflash_exec\.([^$]+)\$([^$]+)\$[^:\n]+:([0-9A-F]+)$", debug, re.M)}
    require(actual == locals_expected, "Flash executor compiler-private allocation changed")
    segment = listing.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
    covered = set()
    for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
        region = set(range(int(address, 16), int(address, 16)+int(size)))
        require(region and not region & covered, "Flash executor private allocation overlaps")
        covered |= region
    require(covered == set(range(0x9b)), "Flash executor private prefix escaped its guard")
    for name, shape in (("word", "{2}DX,SC:U"), ("offset", "{2}SI:U"),
                        ("poll_limit", "{2}SI:U"), ("operation", "{1}SC:U"), ("page", "{1}SC:U")):
        require(re.search(r"^S:Lflash_exec\.flash_exec_command\$"+name+r"\$[^(\n]+\("+re.escape(shape), debug, re.M),
                "Flash executor parameter type changed")
    require("F:G$flash_exec_command$0_0$0({2}DF,SC:U),Z,0,0,0,0,0" in debug,
            "Flash executor return ABI changed")
    return allocated


def rejections(image, symbols, debug, memory, listing):
    case = unittest.TestCase()
    for pc in image:
        with case.assertRaises(ValueError):
            verify(image | {pc: image[pc] ^ 1}, symbols, debug, memory, listing)
    for name in ("_flash_exec_command", "_flash_exec_template_end", "_flash_exec_before", "_flash_exec_done",
                 "_main", "s_XSEG", "l_XSEG", "s_SSEG", "l_BSEG", "__XPAGE", "l_PSEG", "l_XISEG", "l_XABS",
                 "_flash_exec_command_PARM_2", "_flash_exec_command_PARM_3", "_flash_exec_command_PARM_4",
                 "_flash_exec_command_PARM_5", *("_flash_exec_"+name for name in OBJECTS)):
        with case.assertRaises(ValueError, msg=name):
            verify(image, symbols | {name: symbols[name]+1}, debug, memory, listing)
    for match in re.finditer(r"^L:(?:Lflash_exec\.|Fflash_exec\$)[^:\n]+:([0-9A-F]+)$", debug, re.M):
        changed = debug[:match.start(1)] + f"{int(match[1], 16)+1:X}" + debug[match.end(1):]
        with case.assertRaises(ValueError):
            verify(image, symbols, changed, memory, listing)
    for old, new in (("({2}DX,SC:U)", "({3}DG,SC:U)"), ("({123}DA123d", "({122}DA122d"),
                     ("({2}DF,SC:U),Z", "({2}DF,SI:U),Z"), ("C$flash_exec.c$", "C$other.c$")):
        require(old in debug, "Missing flash executor ABI mutation")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listing)
    for old, new in (("8E C7", "8E C6"), (".ds 123", ".ds 124")):
        require(old in listing, "Missing flash executor listing mutation")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listing.replace(old, new))


def execute(simulator, path, image, allocated, *, operation=2, page=0, offset=0, limit=3,
            pointer=0x9b, bank=2, clock=0xc9, cache=4, mode="normal", result=0,
            sfr_changes=None, xchanges=None, corrupt=None, busy_polls=1):
    sfr = GUARD_SFRS | {0xbe: 4, 0xc7: bank, 0xd6: 0, 0xd7: 0, 0xc6: clock, 0x9e: clock, 0x92: 0, 0x9f: 3}
    sfr.update(sfr_changes or {})
    peripheral = bytearray(b"\x69"*0x2000)
    initial = {0x6270: cache, 0x624a: 0xa5, 0x6276: 0x44, 0x6277: 0xff, **(xchanges or {})}
    for address, value in initial.items(): peripheral[address-0x6000] = value
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x7fff 0x69",
                "fill xram 0xe7f0 0xf80f 0x69", "fill rom 0x8000 0x9fff 0xa6",
                f"run 0 {MAIN:#x}", "fill iram 0x80 0xff 0xc7", f"run {MAIN:#x} {BEFORE:#x}"]
    commands += [f"set memory sfr {address:#x} {value:#x}" for address, value in sfr.items()]
    commands += [f"set memory xram {address:#x} {value:#x}" for address, value in initial.items()]
    arguments = pointer.to_bytes(2, "little")+offset.to_bytes(2, "little")+limit.to_bytes(2, "little")+bytes((operation, page))
    commands += ["set memory xram 0x9f "+" ".join(f"{value:#x}" for value in arguments)]
    word = bytes((0x69, 0x7a, 0x8b, 0x9c))
    caller = set(range(0x9b, 0x9f))
    if result == 0 and pointer != 0x9b:
        commands += [f"set memory xram {pointer:#x} "+" ".join(f"{value:#x}" for value in word)]
        caller |= set(range(pointer, pointer+4))
    if result == 1:
        commands += [f"break {pc:#x}" for pc in IO_SITES]
    current = BEFORE
    enters = mode not in ("preflight", "bad-copy", "bad-address", "bad-map", "late-state")
    if corrupt is not None:
        commands += [f"run {current:#x} 0x35e", marker(10), "state", marker(11),
                     f"set memory xram {9+corrupt:#x} {image[TEMPLATE+corrupt]^1:#x}"]
        current = 0x35e
    if mode == "bad-address":
        commands += [f"run {current:#x} 0x464", "set memory xram 0x6272 0x69"]
        current = 0x464
    if mode == "late-state":
        commands += [f"run {current:#x} 0x423", "set memory sfr 0xd6 1"]
        sfr[0xd6] = 1
        current = 0x423
    if enters or mode == "bad-map":
        commands += [f"run {current:#x} 0x487", "step 1", marker(12), "state",
                     "dump /h sfr 0xc7 0xc7", marker(13)]
        current = 0x489
        if mode == "bad-map":
            commands += [f"set memory sfr 0xc7 {bank:#x}"]
        else:
            # Follow the genuine MEMCTR write; C still performs its own readback.
            commands += list(XMAP)
            commands += [f"run {current:#x} {RAM:#x}", marker(14), "state",
                         "dump /h rom 0x8009 0x8083", "dump /h xram 0 8",
                         "dump /h iram 0x21 0x26", marker(15)]
            current = RAM
    if enters:
        target = 0xfa00+page*512+(offset >> 2)
        peripheral[0x271:0x273] = target.to_bytes(2, "little")
        commands += [f"run {current:#x} {COMMAND:#x}", marker(16), "state",
                     "dump /h sfr 0x82 0x83", "dump /h sfr 0xe0 0xe0", marker(17),
                     "step 1", marker(18), "dump /h xram 0x6270 0x6273", marker(19)]
        accepted = mode not in ("ignored", "immediate-abort", "bad-accept", "rejected-busy", "rejected-stuck")
        control = cache | operation | 0x80 if accepted else (
            cache | 0x20 if mode == "immediate-abort" else cache | 0x40 if mode == "bad-accept" else
            cache | operation | 0xc0 if mode.startswith("rejected-") else cache)
        commands += [f"set memory xram 0x6270 {control:#x}", f"break {0x4b3:#x}"]
        current = ACCEPT
        if operation == 2 and accepted:
            for i, pc in enumerate((0x95, 0x97, 0x99, 0x9b)):
                pc += DELTA
                commands += [f"run {current:#x} {pc:#x}", marker(20+4*i), "state",
                             "dump /h sfr 0x82 0x83", "dump /h sfr 0xe0 0xe0", marker(21+4*i),
                             "step 1", marker(22+4*i), "dump /h xram 0x6273 0x6273", marker(23+4*i)]
                current = pc+1
            peripheral[0x273] = word[-1]
        else:
            commands += [f"break {pc+DELTA:#x}" for pc in (0x95, 0x97, 0x99, 0x9b)]
        commands += [f"run {current:#x} {POLL:#x}", marker(40), "state", marker(41)]
        current = POLL
        if mode in ("stuck", "active-no-busy", "rejected-stuck"):
            if mode == "active-no-busy": control &= 0x7f
            commands += [f"set memory xram 0x6270 {control:#x}", f"break xram r 0x6270 {limit}",
                         f"run {current:#x} {STOP:#x}", marker(46), "state", "dump /h iram 0 1", marker(47),
                         "delete", "break 0x4b3", f"run {POLL+1:#x} {STOP:#x}"]
            commands += snapshot_commands(100)
            commands += ["step 64"] + snapshot_commands(110)
            commands += [f"set memory xram 0x6270 {cache:#x}", "step 64"] + snapshot_commands(120)
            peripheral[0x270] = cache
        else:
            if accepted or mode == "rejected-busy":
                for _ in range(busy_polls):
                    commands += ["step 1", f"run {POLL+1:#x} {POLL:#x}"]
                control = cache | (0x20 if mode == "abort" else 0x40 if mode == "full" else 0)
                if mode == "cache-change": control ^= 4
                commands += [f"set memory xram 0x6270 {control:#x}"]
            commands += [f"run {current:#x} {RET:#x}", marker(42), "state",
                         "dump /h xram 0 8", "dump /h xram 0x6270 0x6270",
                         "dump /h sfr 0x81 0x81", "dump /h iram 0x25 0x26", marker(43),
                         "step 1", marker(44), "state", marker(45), "clear 0x4b3"]
            current = 0x4b3
            peripheral[0x270] = control
            if mode == "bad-restore":
                commands += [f"run {current:#x} 0x4da", "step 1", f"set memory sfr 0xc7 {bank|8:#x}"]
                current = 0x4dc
            if mode == "post-state":
                commands += [f"run {current:#x} 0x4c6", "set memory sfr 0xd6 1"]
                sfr[0xd6] = 1
                current = 0x4c6
    terminal = mode in ("stuck", "active-no-busy", "rejected-stuck")
    if not terminal:
        commands += [f"run {current:#x} {DONE:#x}"] + snapshot_commands(100)
        if result >= 2:
            commands += [f"break {pc:#x}" for pc in IO_SITES]
            commands += [f"run {DONE:#x} {BEFORE:#x}", "step 1", f"run {BEFORE+1:#x} {DONE:#x}"]
            commands += snapshot_commands(110)
    commands += [marker(130), "dump /h xram 0x6000 0x7fff", marker(131),
                 "dump /h xram 0xe7f0 0xf80f", marker(132)]
    text = simulate(simulator, commands, path)
    check_pc(section(text, 100), STOP if terminal else DONE)
    ram, iram, registers = snapshot(text, 100)
    require(ram[0x1e00:0x1e08] == b"FEXC\x01\x08\0\0", "Flash executor status ABI changed")
    require(ram[7] == (result if result >= 2 else 0) and (terminal or ram[0xa7] == result),
            f"Flash executor result/latch differs: wanted {result}, got {ram[7]}/{ram[0xa7]}")
    require(ram[0x9b:0x9f] == word and ram[0x9f:0xa7] == arguments, "Flash executor mutated caller state")
    if pointer != 0x9b and result == 0:
        require(ram[pointer:pointer+4] == word, "Flash executor changed external caller word")
    require(all(value == 0xa5 for a, value in enumerate(ram) if a not in allocated | caller),
            "Flash executor wrote unallocated/status/alias XDATA")
    require(iram[0x80:] == b"\xc7"*128 and registers[1] == (0x26 if terminal else 0x22),
            "Flash executor stack guard/genuine unwind changed")
    sfr[0xc7] = bank | 8 if enters and result else bank
    require(all(registers[address-0x80] == value for address, value in sfr.items()),
            "Flash executor touched guarded SFR")
    if mode == "bad-address":
        peripheral[0x271] = 0
    if mode == "bad-map":
        peripheral[0x271:0x273] = (0xfa00+page*512+(offset >> 2)).to_bytes(2, "little")
    require(memory_dump(section(text, 130), 0x6000, 0x2000) == peripheral,
            "Flash executor wrote an unexpected peripheral/information location")
    require(memory_dump(section(text, 131), 0xe7f0, 0x1020) == b"\x69"*0x1020,
            "Flash executor directly wrote mapped flash/neighbor XDATA")
    if corrupt is not None:
        check_pc(section(text, 10), 0x35e)
        require(ram[9+corrupt] == image[TEMPLATE+corrupt] ^ 1, "Flash RAM corruption was not exercised")
    if enters:
        check_pc(section(text, 12), 0x489)
        require(memory_dump(section(text, 12), 0xc7, 1) == bytes((bank|8,)), "XMAP enabled synthetically too early")
        part = section(text, 14)
        check_pc(part, RAM)
        require(memory_dump(part, RAM, 123) == bytes(image[pc] for pc in range(TEMPLATE, END)),
                "Actual XMAP CODE differs from bytes copied by C")
        require(memory_dump(part, 0, 9) == bytes((cache|operation,))+
                (word if operation == 2 else b"\0"*4)+limit.to_bytes(2, "little")+b"\xff\0",
                "Flash RAM input staging/finite poll ABI changed")
        frame = memory_dump(part, 0x21, 6)
        require(frame[4:] == b"\xb3\x04", "Flash RAM did not receive the genuine common-C return address")
        part = section(text, 16)
        check_pc(part, COMMAND)
        require(memory_dump(part, 0x82, 2) == b"\x70\x62" and
                memory_dump(part, 0xe0, 1) == bytes((cache|operation,)), "Flash command MOVX differs")
        require(memory_dump(section(text, 18), 0x6270, 4) ==
                bytes((cache|operation,))+target.to_bytes(2, "little")+b"\x69",
                "Flash actual FCTL/FADDR writes differ")
        if operation == 2 and accepted:
            for i, pc in enumerate((0x95, 0x97, 0x99, 0x9b)):
                part = section(text, 20+4*i)
                check_pc(part, pc+DELTA)
                require(memory_dump(part, 0x82, 2) == b"\x73\x62" and
                        memory_dump(part, 0xe0, 1) == word[i:i+1] and
                        memory_dump(section(text, 22+4*i), 0x6273, 1) == word[i:i+1],
                        "Flash actual ordered FWDATA MOVX differs")
        check_pc(section(text, 40), POLL)
        if not terminal:
            check_pc(section(text, 42), RET)
            require(memory_dump(section(text, 42), 0x6270, 1)[0] & 0x83 == 0 and
                    memory_dump(section(text, 42), 0x25, 2) == b"\xb3\x04",
                    "Flash engine returned before controller quiescence or lost its caller frame")
            check_pc(section(text, 44), 0x4b3)
    if terminal:
        check_pc(section(text, 46), POLL+1)
        require(memory_dump(section(text, 46), 0, 2) == b"\x01\0",
                "Flash poll count differs from the exact FCTL-read breakpoint count")
        for number in (110, 120):
            check_pc(section(text, number), STOP)
            require(snapshot(text, number) == (ram, iram, registers),
                    "Flash retained RAM-only fail-stop resumed or mutated CPU/RAM")
        require(iram[:2] == b"\0\0" and ram[8] == control,
                "Flash fail-stop lost exact poll exhaustion/status")
    elif result >= 2:
        check_pc(section(text, 110), DONE)
        retained, _, retained_sfr = snapshot(text, 110)
        require(retained == ram and retained_sfr[0x47] == registers[0x47],
                "Flash retained call changed state/mapping")
    peaks = [int(value, 16) for value in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "Flash executor stack crossed upper IRAM")
    return max(peaks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output/"flash_exec_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("."+ext).read_text() for ext in ("cdb", "mem"))
    listing = (args.output/"flash_exec_test.exec.rst").read_text()
    allocated = verify(image, symbols, debug, memory, listing)
    rejections(image, symbols, debug, memory, listing)
    check_alias(args.simulator)
    cases = [dict(operation=operation, page=page, offset=offset, bank=bank, clock=clock, cache=cache)
             for operation, page, offset, bank, clock, cache in (
                 (1, 0, 0, 0, 0xc9, 0), (1, 1, 0, 7, 0x88, 12),
                 (2, 0, 0, 3, 0xc9, 4), (2, 1, 2044, 2, 0x88, 8))]
    cases += [dict(pointer=0x1dfc), dict(limit=1, busy_polls=0), dict(limit=3, busy_polls=2),
              dict(mode="ignored", result=6), dict(mode="immediate-abort", result=5),
              dict(mode="abort", result=5), dict(mode="bad-accept", result=6), dict(mode="full", result=6),
              dict(mode="cache-change", result=6), dict(mode="bad-map", result=3),
              dict(mode="bad-address", result=6), dict(mode="bad-restore", result=3),
              dict(mode="rejected-busy", result=6), dict(mode="rejected-stuck", result=7),
              dict(mode="late-state", result=2), dict(mode="post-state", result=2)]
    cases += [dict(mode=mode, limit=limit, result=7) for mode in ("stuck", "active-no-busy")
              for limit in (1, 255, 256, 257, 65535)]
    cases += [dict(mode="preflight", **values, result=result) for values, result in (
        (dict(operation=0), 1), (dict(operation=3), 1), (dict(page=2), 1),
        (dict(offset=2048), 1), (dict(offset=1), 1), (dict(operation=1, offset=4), 1),
        (dict(limit=0), 1), (dict(pointer=0), 1), (dict(pointer=0x9a), 1),
        (dict(pointer=0x1dfd), 1), (dict(pointer=0x1f00), 1), (dict(bank=8), 2),
        (dict(cache=0x84), 2), (dict(cache=0x44), 2), (dict(cache=0x24), 2),
        (dict(cache=0x14), 2), (dict(cache=6), 2), (dict(cache=5), 2),
        (dict(xchanges={0x624a: 0xb5}), 2), (dict(xchanges={0x6276: 0x34}), 2),
        (dict(xchanges={0x6277: 6}), 2))]
    cases += [dict(mode="preflight", sfr_changes={reg: value}, result=2) for reg, value in (
        (0xa8, 0x80), (0xb8, 1), (0x9a, 1), (0xd6, 1), (0xd7, 1), (0xbe, 5), (0x9e, 0x88))]
    cases += [dict(mode="bad-copy", corrupt=offset, result=4) for offset in range(123)]
    peak = 0
    for index, case in enumerate(cases):
        try:
            peak = max(peak, execute(args.simulator, path, image, allocated, **case))
        except (ValueError, AssertionError) as error:
            raise ValueError(f"Flash executor scenario {index}: {case}: {error}") from error
    print(f"Flash executor: {len(cases)} linked cases, every CODE/RAM-copy byte mutation, "
          f"123-byte real RAM execution, finite polls/retained fail-stop, ABI/MMIO/alias guards PASS; "
          f"stack peak {peak:#x} (synthetic XMAP/controller only, never physical flash contents)")


if __name__ == "__main__":
    main()
