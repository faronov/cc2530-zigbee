#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove the isolated flash reader offline; never flash this test image."""
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


IMAGE_SIZE = 1020
IMAGE_HASH = "7af42a853b7893c89986c23e2896207cd71d3a0b3f1ac1a91d35bdc5955c346e"
BEFORE, DONE = 0x360, 0x39b
SFR_READS = ((0x6b, 0xa8), (0x71, 0xb8), (0x77, 0x9a), (0x7d, 0xbe),
            (0x83, 0xc6), (0x89, 0x9e), (0x8f, 0xd6), (0x95, 0xd7), (0x12f, 0xc7))
IO_SITES = tuple(pc for pc, _ in SFR_READS) + (0x1f1, 0x1fb, 0x270, 0x312,
                                             0x11a, 0x201, 0x245, 0x24a, 0x24f, 0x2dc)
OBJECTS = {"fault": (0, 1), "reserved_end": (0x35, 1), "test_output": (0x36, 32),
           "test_pointer": (0x56, 2), "test_offset": (0x58, 2), "test_page": (0x5a, 1),
           "test_length": (0x5b, 1), "test_return": (0x5c, 1)}


def verify(image, symbols, debug, memory, listing):
    require(hashlib.sha256(code_bytes(image, IMAGE_SIZE)).hexdigest() == IMAGE_HASH,
            "Flash complete CODE changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "flash_test_result",
                                        ("flash.c", "test_flash.c"))
    require((cdb_address(debug, "L:Fflash$observe$0$0"),
             cdb_address(debug, "L:XG$flash_nv_read$0$0")+1) == (0x62, BEFORE),
            "Flash module extent changed")
    code = instructions(image, 0x62, BEFORE, PRNG_LENGTHS)
    listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.M)]
    require(list(code.items()) == listed, "Flash relocated listing differs from CODE")
    accesses = [(pc, bytes((0xe5, reg)), reg) for pc, reg in SFR_READS]
    accesses += [(0x1f1, b"\xa9\xc7", 0xc7), (0x1fb, b"\xe5\xc6", 0xc6),
                 (0x270, b"\x75\xc7\x07", 0xc7), (0x312, b"\x8c\xc7", 0xc7)]
    require(peripheral_accesses(code) == accesses, "Flash acquired unreviewed SFR access")
    require([(pc, raw) for pc, raw in code.items() if raw[0] == 0x12] ==
            [(pc, b"\x12\0\x62") for pc in (0x226, 0x282, 0x2ef, 0x31c)],
            "Flash acquired a runtime/helper call or lost an observation")
    high = [(pc, int.from_bytes(raw[1:], "big")) for pc, raw in code.items()
            if raw[0] == 0x90 and int.from_bytes(raw[1:], "big") >= 0x1e00]
    require(high == [(0x117, 0x6270), (0x1fe, 0x6270), (0x242, 0x624a),
                     (0x247, 0x6276), (0x24c, 0x6277)] and
            all(code[pc+3] == b"\xe0" for pc, _ in high),
            "Flash static peripheral accesses are not read-only identity/status")
    require(code[0x2dc] == b"\xe0" and code[0x34b] == b"\xf0", "Flash dynamic read/publication changed")
    for name, value in (("_flash_nv_read", 0x148), ("_flash_test_before", BEFORE),
                        ("_flash_test_done", DONE), ("_main", 0x39d), ("_SOC_MEMCTR", 0xc7),
                        ("s_XSEG", 0), ("l_XSEG", 0x5d), ("s_SSEG", 0xc), ("l_BSEG", 0),
                        ("_flash_nv_read_PARM_2", 0x2e), ("_flash_nv_read_PARM_3", 0x30),
                        ("_flash_nv_read_PARM_4", 0x32)):
        require(symbols.get(name) == value, "Flash linked symbol/allocation changed: " + name)
    require(not any(name in symbols for name in ("__gptrget", "__gptrput", "_pattern")),
            "Flash acquired a generic pointer or host model")
    for name, (address, size) in OBJECTS.items():
        require(symbols["_flash_"+name] == cdb_address(debug, "L:G$flash_"+name+"$0_0$0") == address,
                "Flash object location changed")
        sizes = re.findall(rf"^S:G\$flash_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "Flash object ABI size changed")
    for name, address, size in (("staging", 1, 32), ("saved_bank", 0x21, 1),
                                ("saved_clock", 0x22, 1), ("saved_cache", 0x23, 1)):
        require(cdb_address(debug, f"L:Fflash${name}$0_0$0") == address and
                re.search(rf"^S:Fflash\${name}\$[^(\n]+\(\{{{size}\}}", debug, re.M),
                "Flash retained private storage changed")
    expected_locals = {"flash_nv_read.sloc0": 8, "flash_nv_read.sloc1": 10,
                       "flash_nv_read.offset": 0x2e, "flash_nv_read.output": 0x30,
                       "flash_nv_read.length": 0x32, "flash_nv_read.page": 0x33,
                       "flash_nv_read.result": 0x34}
    expected_locals.update({"observe."+name: 0x24+i for i, name in enumerate(
        ("bank", "ien0", "ien1", "ien2", "sleep", "command", "status", "arm", "request", "mapping"))})
    actual_locals = {m[1]+"."+m[2]: int(m[3], 16) for m in re.finditer(
        r"^L:Lflash\.([^$]+)\$([^$]+)\$[^:\n]+:([0-9A-F]+)$", debug, re.M)}
    require(actual_locals == expected_locals, "Flash complete compiler-private allocation changed")
    segment = listing.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
    covered = set()
    for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
        region = set(range(int(address, 16), int(address, 16)+int(size)))
        require(region and not region & covered, "Flash private allocation overlaps")
        covered |= region
    require(covered == set(range(0x36)), "Flash private prefix escaped its guard")
    for name, shape in (("output", "{2}DX,SC:U"), ("offset", "{2}SI:U"),
                        ("length", "{1}SC:U"), ("page", "{1}SC:U")):
        require(re.search(r"^S:Lflash\.flash_nv_read\$"+name+r"\$[^(\n]+\("+re.escape(shape), debug, re.M),
                "Flash parameter type changed")
    require("F:G$flash_nv_read$0_0$0({2}DF,SC:U),Z,0,0,0,0,0" in debug, "Flash return ABI changed")
    return allocated


def rejections(image, symbols, debug, memory, listing):
    case = unittest.TestCase()
    for pc in image:
        with case.assertRaises(ValueError):
            verify(image | {pc: image[pc] ^ 1}, symbols, debug, memory, listing)
    for name in ("_flash_nv_read", "_flash_test_before", "_flash_test_done", "_main", "_SOC_MEMCTR",
                 "s_XSEG", "l_XSEG", "s_SSEG", "l_BSEG", "__XPAGE", "l_PSEG", "l_XISEG", "l_XABS",
                 "_flash_nv_read_PARM_2", "_flash_nv_read_PARM_3", "_flash_nv_read_PARM_4",
                 *("_flash_"+name for name in OBJECTS)):
        with case.assertRaises(ValueError, msg=name):
            verify(image, symbols | {name: symbols[name]+1}, debug, memory, listing)
    for match in re.finditer(r"^L:(?:Lflash\.|Fflash\$)[^:\n]+:([0-9A-F]+)$", debug, re.M):
        changed = debug[:match.start(1)] + f"{int(match[1], 16)+1:X}" + debug[match.end(1):]
        with case.assertRaises(ValueError):
            verify(image, symbols, changed, memory, listing)
    for old, new in (("({2}DX,SC:U)", "({3}DG,SC:U)"), ("({32}DA32d", "({31}DA32d"),
                     ("({2}DF,SC:U),Z", "({2}DF,SI:U),Z"), ("C$flash.c$", "C$other.c$")):
        require(old in debug, "Missing flash ABI mutation")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listing)
    for old, new in (("75 C7 07", "75 C7 0F"), (".ds 32", ".ds 33")):
        require(old in listing, "Missing flash listing mutation")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listing.replace(old, new))


def execute(simulator, path, allocated, *, page=0, offset=0, length=32, pointer=0x36,
            bank=2, clock=0xc9, cache=4, result=0, sfr_changes=None, xchanges=None,
            injection=None, trace=False):
    sfr = GUARD_SFRS | {0xbe: 4, 0xc7: bank, 0xd6: 0, 0xd7: 0, 0xc6: clock, 0x9e: clock}
    sfr.update(sfr_changes or {})
    peripheral = bytearray(b"\x69"*0x2000)
    for address, value in {0x6270: cache, 0x624a: 0xa5, 0x6276: 0x44, 0x6277: 0xff,
                           **(xchanges or {})}.items():
        peripheral[address-0x6000] = value
    flash = bytes(((address >> 8)*37 + (address & 255)*13) & 255 for address in range(0xe800, 0xf800))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x7fff 0x69",
                "fill xram 0xe7f0 0xf80f 0x69", "run 0 0x39d", "fill iram 0x80 0xff 0xc7",
                f"run 0x39d {BEFORE:#x}"]
    commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in sfr.items()]
    commands += [f"set memory xram {a:#x} {peripheral[a-0x6000]:#x}" for a in (0x6270, 0x624a, 0x6276, 0x6277)]
    for start in range(0, len(flash), 128):
        commands.append(f"set memory xram {0xe800+start:#x} " + " ".join(f"{v:#x}" for v in flash[start:start+128]))
    arguments = pointer.to_bytes(2, "little") + offset.to_bytes(2, "little") + bytes((page, length))
    commands.append("set memory xram 0x56 " + " ".join(f"{v:#x}" for v in arguments))
    current = BEFORE
    checkpoints = []
    if result in (1, 2, 3):
        commands += [f"break {pc:#x}" for pc in IO_SITES]
    if trace:
        commands += [f"run {current:#x} 0x270", "step 1", marker(10), "dump /h sfr 0xc7 0xc7", marker(11)]
        current = 0x273
        for i in range(length):
            commands += [f"run {current:#x} 0x2dc", marker(20+i*2), "state",
                         "dump /h sfr 0x82 0x83", "dump /h sfr 0xc7 0xc7", marker(21+i*2), "step 1"]
            checkpoints.append((20+i*2, 0x2dc, 0xe800+page*2048+offset+i))
            current = 0x2dd
        commands += [f"run {current:#x} 0x312", "step 1", marker(90), "dump /h sfr 0xc7 0xc7", marker(91)]
        current = 0x314
    if injection:
        pc, count, space, address, value = injection
        for i in range(count):
            commands.append(f"run {current:#x} {pc:#x}")
            if i+1 < count:
                commands.append("step 1")
                current = pc+1
        commands += [marker(92), "state", marker(93), f"set memory {space} {address:#x} {value:#x}"]
        if space == "sfr": sfr[address] = value
        else: peripheral[address-0x6000] = value
        current = pc
    commands += [f"run {current:#x} {DONE:#x}"] + snapshot_commands(100)
    commands += [marker(104), "dump /h xram 0x6000 0x7fff", marker(105),
                 "dump /h xram 0xe7f0 0xf80f", marker(106)]
    if result >= 4:
        commands += [f"break {pc:#x}" for pc in IO_SITES]
        commands += [f"run {DONE:#x} {BEFORE:#x}", "step 1", f"run {BEFORE+1:#x} {DONE:#x}"]
        commands += snapshot_commands(110)
    text = simulate(simulator, commands, path)
    check_pc(section(text, 100), DONE)
    ram, iram, registers = snapshot(text, 100)
    require(ram[0x1e00:0x1e08] == b"FLSH\x01\x08\0\0" and ram[0x5c] == result,
            "Flash result/status differs")
    require(ram[0] == (result if result >= 4 else 0), "Flash first fault not retained")
    expected = flash[page*2048+offset:page*2048+offset+length] if result == 0 else b""
    require(ram[0x36:0x56] == expected + b"\xa5"*(32-len(expected)), "Flash partial/incorrect publication")
    require(ram[0x56:0x5c] == arguments, "Flash mutated caller arguments")
    require(all(byte == 0xa5 for a, byte in enumerate(ram) if a not in allocated),
            "Flash wrote unallocated/status/alias XDATA")
    require(iram[0x80:] == b"\xc7"*128 and registers[1] == 0x0d, "Flash stack guard/unwind changed")
    mapping = (sfr[0xc7] if injection and injection[2:4] == ("sfr", 0xc7) else
               7 if injection else bank)
    sfr[0xc7] = bank if result == 0 else mapping
    require(all(registers[a-0x80] == v for a, v in sfr.items()), "Flash touched guarded SFR")
    require(memory_dump(section(text, 104), 0x6000, 0x2000) == peripheral, "Flash wrote peripheral/information XDATA")
    require(memory_dump(section(text, 105), 0xe7f0, 0x1020) == b"\x69"*16+flash+b"\x69"*16,
            "Flash wrote reserved pages or neighboring regions")
    if trace:
        require(memory_dump(section(text, 10), 0xc7, 1) == b"\x07" and
                memory_dump(section(text, 90), 0xc7, 1) == bytes((bank,)), "Flash mapping writes differ")
    for n, pc, address in checkpoints:
        part = section(text, n); check_pc(part, pc)
        require(memory_dump(part, 0x82, 2) == address.to_bytes(2, "little") and
                memory_dump(part, 0xc7, 1) == b"\x07", "Flash actual MOVX address/bank differs")
    if injection: check_pc(section(text, 92), injection[0])
    if result >= 4:
        check_pc(section(text, 110), DONE)
        retained, _, retained_sfr = snapshot(text, 110)
        require(retained == ram and retained_sfr[0x47] == registers[0x47],
                "Flash retained call changed state/output/mapping")
    peaks = [int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "Flash stack crossed upper IRAM")
    return max(peaks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output/"flash_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("."+ext).read_text() for ext in ("cdb", "mem"))
    listing = (args.output/"flash_test.reader.rst").read_text()
    allocated = verify(image, symbols, debug, memory, listing)
    rejections(image, symbols, debug, memory, listing)
    check_alias(args.simulator)
    cases = [dict(page=p, offset=o, length=n, trace=True, bank=b, clock=c, cache=cache)
             for p, o, n, b, c, cache in ((0, 0, 32, 0, 0xc9, 0), (1, 2016, 32, 7, 0x88, 12),
                                          (0, 2047, 1, 3, 0x88, 8), (1, 0, 1, 2, 0xc9, 4))]
    cases += [dict(**values, result=result) for values, result in (
        (dict(pointer=0), 1), (dict(length=0), 1), (dict(length=33), 1), (dict(page=2), 2),
        (dict(page=125), 2), (dict(offset=2048), 2), (dict(offset=2017), 2),
        (dict(pointer=0x35), 3), (dict(pointer=0x1de1), 2), (dict(pointer=0x1f00), 2),
        (dict(bank=8), 5), (dict(xchanges={0x624a: 0xb5}), 4), (dict(xchanges={0x6276: 0x34}), 4),
        (dict(xchanges={0x6277: 6}), 4), (dict(cache=0x84), 6), (dict(cache=0x44), 6),
        (dict(cache=0x24), 6), (dict(cache=0x14), 6), (dict(cache=6), 6), (dict(cache=5), 6))]
    cases += [dict(sfr_changes={reg: value}, result=5) for reg, value in
              ((0xa8, 0x80), (0xb8, 1), (0x9a, 1), (0xd6, 1), (0xd7, 1), (0xbe, 5), (0x9e, 0x88))]
    cases += [dict(injection=(0x2dc, n, "xram", 0x6270, 0x84), result=6) for n in (1, 16, 32)]
    cases += [dict(injection=(0x273, 1, "sfr", 0xc7, 2), result=7),
              dict(injection=(0x314, 1, "sfr", 0xc7, 7), result=7),
              dict(injection=(0x2dc, 16, "sfr", 0xc7, 3), result=7)]
    peak = max(execute(args.simulator, path, allocated, **case) for case in cases)
    print(f"Flash read: {len(image)} CODE bytes, 93 ordinary XDATA +64 reserved; peak SP={peak:02x}. "
          f"{len(cases)} linked cases, exact MMIO/ABI/private prefix, every CODE mutation, "
          "bank/source trace, nonpublication, retained faults and alias/stack guards PASS. "
          "Synthetic flash window only; no erase/program, silicon or persistence evidence.")


if __name__ == "__main__":
    main()
