#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove the real reader/RAM-engine/write-policy composition; never flash this image."""
import argparse
import hashlib
from pathlib import Path
import re
import unittest

import boot_flash
import boot_flash_exec as engine
from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate,
    snapshot, snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS
from radio_fifo_fixture import instructions
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

SIZE = 3345
DIGEST = "278d332847ba181a345c261f5fc0451ef17a3f2705ba578741c7a772c6af711a"
BEFORE, DONE, MAIN, READ = 0xc3f, 0xcaa, 0xcac, 0x774
LENGTHS = engine.LENGTHS | {0xc8: 1, 0x68: 1, 0x5a: 1}
READER_DELTA = 0x498
IO_SITES = engine.IO_SITES + tuple(pc+READER_DELTA for pc in boot_flash.IO_SITES)
MODULES = {
    "exec": (0x62, 0x4fa, "9391bf2c3ed0f928b81f92efe46e22db36615702ac3bae8b5ec7ef2fc422401b"),
    "reader": (0x4fa, 0x7f8, "12faaebc8614269a01d131bf57909167756ccf39cfd9bab2a99d8a687fcacd4a"),
    "service": (0x7f8, BEFORE, "1da0b78277caf140e3ef20fa0cf7f06f36289dfc217b9a6d68cb10caf7b463a5"),
}
OBJECTS = {
    "flash_exec_work": (0, 9), "flash_exec_ram": (9, 123), "flash_exec_reserved_end": (0x9a, 1),
    "flash_fault": (0x9b, 1), "flash_reserved_end": (0xd0, 1),
    "flash_write_status": (0xd1, 8), "flash_write_known": (0xd9, 1), "flash_write_used": (0xda, 128),
    "flash_write_word": (0x15a, 4), "flash_write_check": (0x15e, 32), "flash_write_reserved_end": (0x193, 1),
    "flash_write_test_word": (0x194, 4), "flash_write_test_pointer": (0x198, 2),
    "flash_write_test_offset": (0x19a, 2), "flash_write_test_limit": (0x19c, 2),
    "flash_write_test_operation": (0x19e, 1), "flash_write_test_page": (0x19f, 1),
    "flash_write_test_return": (0x1a0, 1),
}


def verify(image, symbols, debug, memory, listings):
    require(hashlib.sha256(code_bytes(image, SIZE)).hexdigest() == DIGEST, "Flash writer complete CODE changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "flash_write_test_result",
        ("flash_exec.c", "flash.c", "flash_write.c", "test_flash_write.c"))
    covered = set()
    for name, (start, end, digest) in MODULES.items():
        code = instructions(image, start, end, LENGTHS)
        require(hashlib.sha256(bytes(image[a] for a in range(start, end))).hexdigest() == digest,
                "Flash writer component instructions changed")
        listing = listings[name]
        actual = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.M)]
        require(list(code.items()) == actual, "Flash writer listing snapshot differs from linked CODE")
        segment = listing.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            region = set(range(int(address, 16), int(address, 16)+int(size)))
            require(region and not region & covered, "Flash writer private allocations overlap")
            covered |= region
        if name == "service":
            require(not peripheral_accesses(code), "Flash write policy bypasses the hardware services")
            require([(pc, int.from_bytes(raw[1:], "big")) for pc, raw in code.items() if raw[0] == 0x12] ==
                    [(0x9c1, 0x5e0), (0xa64, 0x1cf), (0xad3, 0x5e0), (0xbf3, 0x7f8), (0xc37, 0x7f8)],
                    "Flash writer acquired a helper or bypassed real preflight/command/readback")
            require(all(int.from_bytes(raw[1:], "big") < 0x194 for raw in code.values() if raw[0] == 0x90),
                    "Flash writer static DPTR escaped the complete private prefix")
    require(covered == set(range(0x194)), "Flash writer entire private prefix escaped its fence")
    expected = {"_flash_exec_command": 0x1cf, "_flash_exec_template_end": 0xdd, "_flash_nv_read": 0x5e0,
        "_flash_nv_erase": 0xbc6, "_flash_nv_program": 0xbf7, "_flash_write_diagnostic": 0xc3b,
        "_flash_write_before": BEFORE, "_flash_write_done": DONE, "_flash_write_test_cycle": BEFORE,
        "_main": MAIN, "s_XSEG": 0, "l_XSEG": 0x1a1, "s_SSEG": 0x21, "l_BSEG": 1,
        "_flash_exec_command_PARM_2": 0x91, "_flash_exec_command_PARM_3": 0x92,
        "_flash_exec_command_PARM_4": 0x94, "_flash_exec_command_PARM_5": 0x96,
        "_flash_nv_read_PARM_2": 0xc9, "_flash_nv_read_PARM_3": 0xcb, "_flash_nv_read_PARM_4": 0xcd,
        "_flash_nv_erase_PARM_2": 0x189, "_flash_nv_program_PARM_2": 0x18c,
        "_flash_nv_program_PARM_3": 0x18e, "_flash_nv_program_PARM_4": 0x190}
    for name, value in expected.items():
        require(symbols.get(name) == value, "Flash writer linked symbol changed: "+name)
    for name, (address, size) in OBJECTS.items():
        require(symbols["_"+name] == cdb_address(debug, f"L:G${name}$0_0$0") == address,
                "Flash writer object address changed")
        sizes = re.findall(rf"^S:G\${name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "Flash writer object ABI size changed")
    private = {
        "flash_exec.flash_exec_command": (("sloc0", 8), ("sloc1", 10), ("sloc2", 12), ("sloc3", 14),
            ("page", 0x91), ("offset", 0x92), ("word", 0x94), ("poll_limit", 0x96),
            ("operation", 0x98), ("result", 0x99)),
        "flash_exec.idle": tuple((name, 0x87+i) for i, name in enumerate(
            ("expected_mapping", "ien0", "ien1", "ien2", "arm", "request", "sleep", "command", "status", "bank"))),
        "flash.flash_nv_read": (("sloc0", 0x10), ("sloc1", 0x12), ("offset", 0xc9),
            ("output", 0xcb), ("length", 0xcd), ("page", 0xce), ("result", 0xcf)),
        "flash.observe": tuple((name, 0xbf+i) for i, name in enumerate(
            ("bank", "ien0", "ien1", "ien2", "sleep", "command", "status", "arm", "request", "mapping"))),
        "flash_write.operate": (("sloc0", 0x14), ("sloc1", 0x15), ("sloc2", 0x16), ("sloc3", 0x18),
            ("page", 0x17e), ("offset", 0x17f), ("word", 0x181), ("poll_limit", 0x183),
            ("operation", 0x185), ("position", 0x186), ("result", 0x188)),
        "flash_write.flash_nv_erase": (("poll_limit", 0x189), ("page", 0x18b)),
        "flash_write.flash_nv_program": (("offset", 0x18c), ("word", 0x18e), ("poll_limit", 0x190), ("page", 0x192)),
    }
    expected_private = {module+"."+name: address for module, fields in private.items() for name, address in fields}
    actual_private = {m[1]+"."+m[2]: int(m[3], 16) for m in re.finditer(
        r"^L:L(flash(?:_exec|_write)?\.[^$]+)\$([^$]+)\$[^:\n]+:([0-9A-F]+)$", debug, re.M)}
    require(actual_private == expected_private, "Flash writer compiler-private storage changed")
    for module, name, address in (
        ("flash_exec", "mapping", 0x84), ("flash_exec", "clock_command", 0x85),
        ("flash_exec", "cache_mode", 0x86), ("flash", "staging", 0x9c),
        ("flash", "saved_bank", 0xbc), ("flash", "saved_clock", 0xbd), ("flash", "saved_cache", 0xbe)):
        require(cdb_address(debug, f"L:F{module}${name}$0_0$0") == address,
                "Flash writer service-private state changed")
    for module, name, address in (("flash_exec", "flash_exec_template", 0x62),
        ("flash_exec", "enter_ram", 0xdd), ("flash_exec", "idle", 0xe6),
        ("flash", "observe", 0x4fa), ("flash_write", "operate", 0x7f8)):
        require(cdb_address(debug, f"L:F{module}${name}$0$0") == address,
                "Flash writer private function extent changed")
    for name, shape in (("page", "{1}SC:U"), ("offset", "{2}SI:U"),
                        ("word", "{2}DX,SC:U"), ("poll_limit", "{2}SI:U")):
        require(re.search(r"^S:Lflash_write.flash_nv_program\$"+name+r"\$[^(\n]+\("+re.escape(shape), debug, re.M),
                "Flash writer typed public ABI changed")
    for name in ("flash_nv_program", "flash_nv_erase"):
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug, "Flash writer result ABI changed")
    require(bytes(image[a] for a in range(0xc3b, 0xc3f)) == b"\x90\0\xd1\x22" and
            "F:G$flash_write_diagnostic$0_0$0({2}DF,DX," in debug, "Flash diagnostic accessor ABI changed")
    require(not any(name in symbols for name in ("__gptrget", "__gptrput", "_flash_exec_host_enter",
        "_host_flash_engine_stop", "_flash_exec_host_template")), "Flash writer acquired native/generic helpers")
    return allocated


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for pc in image:
        with case.assertRaises(ValueError):
            verify(image | {pc: image[pc] ^ 1}, symbols, debug, memory, listings)
    names = [name for name in symbols if name.startswith("_flash") or name in
             ("_main", "s_XSEG", "l_XSEG", "s_SSEG", "l_BSEG", "__XPAGE", "l_PSEG", "l_XISEG", "l_XABS")]
    for name in names:
        with case.assertRaises(ValueError, msg=name):
            verify(image, symbols | {name: symbols[name]+1}, debug, memory, listings)
    for match in re.finditer(r"^L:(?:Lflash(?:_exec|_write)?\.|Fflash(?:_exec|_write)?\$)[^:\n]+:([0-9A-F]+)$", debug, re.M):
        changed = debug[:match.start(1)]+f"{int(match[1], 16)+1:X}"+debug[match.end(1):]
        with case.assertRaises(ValueError):
            verify(image, symbols, changed, memory, listings)
    for old, new in (("({128}DA128d", "({127}DA127d"), ("({2}DX,SC:U)", "({3}DG,SC:U)"),
                     ("({2}DF,SC:U),Z", "({2}DF,SI:U),Z"), ("C$flash_write.c$", "C$other.c$")):
        require(old in debug, "Missing flash writer debug mutation")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    for name, text in listings.items():
        match = re.search(r"^(\s+[0-9A-F]{6} )([0-9A-F]{2})(?= [0-9A-F ])", text, re.M)
        require(match is not None, "Missing writer listing mutation")
        changed = text[:match.start(2)]+f"{int(match[2], 16)^1:02X}"+text[match.end(2):]
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {name: changed})


def execute(simulator, path, image, allocated, operations, *, initial=0, trace_reads=False):
    sfr = GUARD_SFRS | {0xbe: 4, 0xc7: 2, 0xd6: 0, 0xd7: 0, 0xc6: 0xc9, 0x9e: 0xc9, 0x92: 0, 0x9f: 3}
    peripheral = bytearray(b"\x69"*0x2000)
    for address, value in {0x6270: 4, 0x624a: 0xa5, 0x6276: 0x44, 0x6277: 0xff}.items():
        peripheral[address-0x6000] = value
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x7fff 0x69",
                "fill xram 0xe7f0 0xf80f 0x69", f"fill xram 0xe800 0xf7ff {initial:#x}",
                "fill rom 0x8000 0x9fff 0xa6", f"run 0 {MAIN:#x}",
                "fill iram 0x80 0xff 0xc7", f"run {MAIN:#x} {BEFORE:#x}"]
    commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in sfr.items()]
    commands += [f"set memory xram {a:#x} {peripheral[a-0x6000]:#x}" for a in (0x6270, 0x624a, 0x6276, 0x6277)]
    nv, known, used, diagnostic = bytearray([initial]*4096), 0, bytearray(128), b"\0"*8
    records, traces = [], []
    current = BEFORE
    for number, options in enumerate(operations):
        base = 100+number*100
        operation, page, offset = options.get("operation", 2), options.get("page", 0), options.get("offset", 0)
        limit, pointer = options.get("limit", 3), options.get("pointer", 0x194)
        word, result, mode = options.get("word", b"\x12\x34\x56\x78"), options.get("result", 0), options.get("mode", "normal")
        if options.get("reset"):
            commands += ["delete", f"run 0 {MAIN:#x}", "fill iram 0x80 0xff 0xc7", f"run {MAIN:#x} {BEFORE:#x}"]
            commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in sfr.items()]
            commands += ["set memory sfr 0xc7 2", "set memory xram 0x6270 4"]
            peripheral[0x270] = 4
            known, used, diagnostic, current = 0, bytearray(128), b"\0"*8, BEFORE
            sfr[0xc7] = 2
        elif current == DONE:
            commands += ["delete", f"run {DONE:#x} {BEFORE:#x}"]
            current = BEFORE
        if "damage" in options:
            address = page*2048+options["damage"]
            nv[address] = 0
            commands += [f"set memory xram {0xe800+address:#x} 0"]
        arguments = pointer.to_bytes(2, "little")+offset.to_bytes(2, "little")+limit.to_bytes(2, "little")+bytes((operation, page))
        commands += ["set memory xram 0x198 "+" ".join(f"{v:#x}" for v in arguments),
                     "set memory xram 0x194 "+" ".join(f"{v:#x}" for v in word)]
        benign = 1 <= result <= 5 or mode == "retained"
        preflight = mode in ("not-erased", "bad-state")
        terminal = mode == "stuck"
        if mode == "bad-state":
            commands += ["set memory sfr 0xd6 1"]
            sfr[0xd6] = 1
        if benign:
            commands += [f"break {pc:#x}" for pc in IO_SITES]
        else:
            diagnostic = bytes((10, operation, page, offset & 255, offset >> 8, 1, 255, 0))
        if not benign and not preflight:
            if operation == 1: known &= ~(1 << page)
            else: used[page*64+(offset >> 5)] |= 1 << ((offset >> 2) & 7)
            diagnostic = diagnostic[:5]+bytes((2, 255, 0))
            commands += [f"run {current:#x} 0x487", "step 1", marker(base), "state",
                         "dump /h sfr 0xc7 0xc7", marker(base+1)]
            commands += list(engine.XMAP)
            commands += [f"run 0x489 {engine.RAM:#x}", marker(base+2), "state",
                         "dump /h xram 0 8", "dump /h xram 0xd1 0x159",
                         "dump /h sfr 0x81 0x81", "dump /h iram 0x21 0x3f",
                         "dump /h rom 0x8009 0x8083", marker(base+3)]
            records.append(("entry", base, diagnostic, known, bytes(used), operation, page, offset, limit, word))
            address = 0xfa00+page*512+(offset >> 2)
            commands += [f"run {engine.RAM:#x} {engine.COMMAND:#x}", "step 1",
                         marker(base+4), "dump /h xram 0x6270 0x6273", marker(base+5)]
            peripheral[0x271:0x273] = address.to_bytes(2, "little")
            accepted = mode not in ("ignored", "abort")
            control = (4 | operation | 0x80) if accepted else (0x24 if mode == "abort" else 4)
            commands += [f"set memory xram 0x6270 {control:#x}", "break 0x4b3"]
            current = engine.ACCEPT
            if operation == 2 and accepted:
                for i, pc in enumerate((0x95, 0x97, 0x99, 0x9b)):
                    pc += engine.DELTA
                    commands += [f"run {current:#x} {pc:#x}", marker(base+6+4*i), "state",
                                 "dump /h sfr 0x82 0x83", "dump /h sfr 0xe0 0xe0", marker(base+7+4*i),
                                 "step 1", marker(base+8+4*i), "dump /h xram 0x6273 0x6273", marker(base+9+4*i)]
                    current = pc+1
                peripheral[0x273] = word[-1]
                records.append(("data", base, word))
            else:
                commands += [f"break {pc+engine.DELTA:#x}" for pc in (0x95, 0x97, 0x99, 0x9b)]
            commands += [f"run {current:#x} {engine.POLL:#x}"]
            current = engine.POLL
            if terminal:
                commands += [f"run {current:#x} {engine.STOP:#x}"]
                peripheral[0x270] = control
                sfr[0xc7] = 10
            else:
                if accepted:
                    commands += ["step 1", f"run {engine.POLL+1:#x} {engine.POLL:#x}", "set memory xram 0x6270 4"]
                    control = 4
                    if operation == 1:
                        nv[page*2048:(page+1)*2048] = b"\xff"*2048
                        commands += [f"fill xram {0xe800+page*2048:#x} {0xefff+page*2048:#x} 0xff"]
                        if mode == "residue":
                            nv[page*2048+options["bad_byte"]] = 0
                            commands += [f"set memory xram {0xe800+page*2048+options['bad_byte']:#x} 0"]
                    elif mode != "drop":
                        length = 1 if mode == "partial" else 4
                        for i in range(length): nv[page*2048+offset+i] &= word[i]
                        commands += [f"set memory xram {0xe800+page*2048+offset:#x} "+
                                     " ".join(f"{v:#x}" for v in nv[page*2048+offset:page*2048+offset+length])]
                commands += [f"run {current:#x} {engine.RET:#x}", marker(base+24), "state",
                             "dump /h xram 0x6270 0x6270", marker(base+25), "step 1",
                             marker(base+26), "state", marker(base+27), "clear 0x4b3"]
                records.append(("return", base))
                current = 0x4b3
                diagnostic = diagnostic[:6]+bytes((0 if accepted else 5 if mode == "abort" else 6, 0))
                peripheral[0x270] = control
                sfr[0xc7] = 2 if accepted else 10
                if accepted:
                    diagnostic = diagnostic[:5]+bytes((3, 0, 0))
                    if trace_reads and result == 0:
                        for i in range(2048 if operation == 1 else 4):
                            ident = 40000+len(traces)*2
                            commands += [f"run {current:#x} {READ:#x}", marker(ident),
                                         "dump /h sfr 0x82 0x83", "dump /h sfr 0xc7 0xc7", marker(ident+1), "step 1"]
                            traces.append((ident, 0xe800+page*2048+offset+i))
                            current = READ+1
                    if mode == "reader-post":
                        commands += [f"run {current:#x} {READ:#x}", "step 1", "set memory xram 0x6270 0"]
                        current = READ+1
                        peripheral[0x270] = 0; sfr[0xc7] = 7
                        diagnostic = diagnostic[:7]+b"\x06"
        if not terminal:
            commands += [f"run {current:#x} {DONE:#x}"]
            current = DONE
            if not benign:
                if result == 0:
                    if operation == 1:
                        used[page*64:(page+1)*64] = b"\0"*64
                        known |= 1 << page
                    diagnostic = bytes((0,))+diagnostic[1:5]+bytes((4, 0, 0))
                else:
                    diagnostic = bytes((result,))+diagnostic[1:]
                    if mode == "bad-state": diagnostic = diagnostic[:7]+bytes((5,))
        commands += [marker(base+30), "state", "dump /h xram 0 0x1a0",
                     marker(base+31), "dump /h sfr 0x80 0xff", marker(base+32)]
        records.append(("outcome", base, result, diagnostic, known, bytes(used), dict(sfr),
                        arguments, word, terminal))
        if terminal:
            commands += snapshot_commands(30000)+["step 64"]+snapshot_commands(30004)
            commands += ["set memory xram 0x6270 4", "step 64"]+snapshot_commands(30008)
            peripheral[0x270] = 4
            require(number == len(operations)-1, "Terminal scenario must end its sequence")
        else:
            commands += snapshot_commands(30000) if number == len(operations)-1 else []
    commands += [marker(30012), "dump /h xram 0x6000 0x7fff", marker(30013),
                 "dump /h xram 0xe7f0 0xf80f", marker(30014)]
    text = simulate(simulator, commands, path)
    markers = list(re.finditer(r"^0x2530([0-9a-f]{4})\r?\n", text, re.M))
    indexed = {}
    for start, end in zip(markers, markers[1:]):
        number = int(start[1], 16)
        require(number not in indexed, "Duplicate flash writer transcript marker")
        indexed[number] = text[start.start():end.end()]

    def segment(number):
        require(number in indexed, "Missing flash writer transcript marker")
        return section(indexed[number], number)

    for record in records:
        kind, base, *values = record
        if kind == "entry":
            diagnostic, known, used, operation, page, offset, limit, word = values
            require(memory_dump(segment(base), 0xc7, 1) == b"\x0a", "Synthetic XMAP preceded real MEMCTR")
            part = segment(base+2); check_pc(part, engine.RAM)
            require(memory_dump(part, engine.RAM, 123) == bytes(image[a] for a in range(0x62, 0xdd)),
                    "Combined driver did not execute actual C-copied RAM code")
            require(memory_dump(part, 0xd1, 137) == diagnostic+bytes((known,))+used,
                    "History invalidation/attempt consumption was not committed before command entry")
            require(memory_dump(part, 0, 9) == bytes((4|operation,))+
                    (word if operation == 2 else b"\0"*4)+limit.to_bytes(2, "little")+b"\xff\0",
                    "Combined RAM engine inputs differ")
            sp = memory_dump(part, 0x81, 1)[0]
            require(memory_dump(part, sp-1, 2) == b"\xb3\x04", "Combined engine lost genuine common-C return frame")
            require(memory_dump(segment(base+4), 0x6270, 3) ==
                    bytes((4|operation,))+(0xfa00+page*512+(offset >> 2)).to_bytes(2, "little"),
                    "Actual erase/program FCTL/FADDR values differ")
        elif kind == "data":
            word, = values
            for i, pc in enumerate((0x95, 0x97, 0x99, 0x9b)):
                part = segment(base+6+4*i); check_pc(part, pc+engine.DELTA)
                require(memory_dump(part, 0x82, 2) == b"\x73\x62" and
                        memory_dump(part, 0xe0, 1) == word[i:i+1] and
                        memory_dump(segment(base+8+4*i), 0x6273, 1) == word[i:i+1],
                        "Combined engine programmed wrong ordered bytes")
        elif kind == "return":
            part = segment(base+24); check_pc(part, engine.RET)
            require(memory_dump(part, 0x6270, 1)[0] & 0x83 == 0, "Combined engine returned into active flash")
            check_pc(segment(base+26), 0x4b3)
        else:
            result, diagnostic, known, used, sfr, arguments, word, terminal = values
            part = segment(base+30); check_pc(part, engine.STOP if terminal else DONE)
            ram = memory_dump(part, 0, 0x1a1)
            registers = memory_dump(segment(base+31), 0x80, 128)
            require(ram[0xd1:0x15a] == diagnostic+bytes((known,))+used,
                    f"Flash write result/history/diagnostic differs at {base}: {ram[0xd1:0xda].hex()} wanted {diagnostic.hex()}/{known}")
            require(terminal or ram[0x1a0] == result, "Flash writer public result differs")
            require(ram[0x194:0x198] == word and ram[0x198:0x1a0] == arguments, "Flash writer changed caller source/args")
            require(all(registers[a-0x80] == v for a, v in sfr.items()), "Flash writer changed guarded SFR")
            require(terminal or registers[1] == 0x22, "Flash writer failed to unwind caller stack")
    ram, iram, registers = snapshot(text, 30000)
    require(ram[0x1e00:0x1e08] == b"FWRT\x01\x08\0\0", "Flash writer status ABI differs")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "Flash writer escaped ordinary/private/status allocation")
    require(iram[0x80:] == b"\xc7"*128, "Flash writer damaged upper IRAM/alias")
    require(memory_dump(segment(30012), 0x6000, 0x2000) == peripheral, "Flash writer changed unexpected peripheral/information bytes")
    require(memory_dump(segment(30013), 0xe7f0, 0x1020) == b"\x69"*16+nv+b"\x69"*16,
            "Flash writer changed non-commanded flash/neighbor data")
    if operations[-1].get("mode") == "stuck":
        require(ram[7] == 7 and ram[0xd1] == 10 and ram[0xd6] == 2 and registers[0x47] == 10,
                "RAM fail-stop did not leave the public operation pending/diagnosable")
        for ident in (30004, 30008):
            check_pc(segment(ident), engine.STOP)
            require(snapshot(text, ident) == (ram, iram, registers), "Combined RAM fail-stop resumed or changed state")
    for ident, address in traces:
        require(memory_dump(segment(ident), 0x82, 2) == address.to_bytes(2, "little") and
                memory_dump(segment(ident), 0xc7, 1) == b"\x07", "Flash page verification skipped/repeated an actual source address")
    peaks = [int(value, 16) for value in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "Flash writer exceeded upper-IRAM stack boundary")
    return max(peaks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output/"flash_write_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("."+ext).read_text() for ext in ("cdb", "mem"))
    listings = {name: (args.output/f"flash_write-{name}.rst").read_text() for name in MODULES}
    allocated = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    erase = dict(operation=1)
    cases = [
        ([dict(result=4)], {}),
        ([dict(result=4)], dict(initial=255)),
        ([erase, {}, dict(result=5), dict(offset=4), dict(result=4, reset=True)], dict(trace_reads=True)),
        ([erase, dict(word=b"\xff"*4), dict(word=b"\xff"*4, result=5), erase, {}], {}),
        ([erase, dict(operation=1, page=1), {}, dict(page=1), erase, dict(page=1, result=5)], {}),
        ([dict(operation=1, mode="ignored", result=6), dict(mode="retained", result=6)], dict(initial=255)),
    ]
    for page in (0, 1):
        cases.append(([dict(operation=1, page=page)]+
            [dict(page=page, offset=offset) for offset in list(range(0, 2048, 32))+list(range(4, 32, 4))+[2044]]+
            [dict(page=page, offset=2044, result=5)], {}))
        for offset in (0, 31, 32, 1023, 1024, 2047):
            cases.append(([dict(operation=1, page=page, mode="residue", bad_byte=offset, result=8),
                           dict(mode="retained", result=8)], {}))
    for mode, result in (("ignored", 6), ("abort", 6), ("drop", 8), ("partial", 8), ("reader-post", 7), ("stuck", 10)):
        operations = [erase, dict(mode=mode, result=result)]
        if mode != "stuck": operations.append(dict(operation=1, mode="retained", result=result))
        cases.append((operations, {}))
    cases += [
        ([erase, dict(operation=1, mode="stuck", result=10, limit=1)], {}),
        ([erase, dict(offset=4, damage=4, mode="not-erased", result=9), dict(mode="retained", result=9)], {}),
        ([dict(operation=1, mode="bad-state", result=7), dict(mode="retained", result=7)], {}),
    ]
    for values, result in (
        (dict(limit=0), 1), (dict(pointer=0), 1), (dict(page=2), 2), (dict(page=125), 2),
        (dict(offset=1), 2), (dict(offset=2048), 2), (dict(pointer=0x193), 3),
        (dict(pointer=0x1dfd), 2), (dict(pointer=0x1f00), 2)):
        cases.append(([dict(**values, result=result)], {}))
    peak = 0
    for number, (operations, options) in enumerate(cases):
        try:
            peak = max(peak, execute(args.simulator, path, image, allocated, operations, **options))
        except (ValueError, AssertionError) as error:
            raise ValueError(f"Flash writer scenario {number}: {operations}: {error}") from error
    print(f"Flash writer: {len(cases)} linked sequences, all128 bitmap bytes/eight bit positions, "
          f"full-page actual MOVX trace, genuine RAM commands/return/fail-stop, history/readback/retention "
          f"and allocation/alias guards PASS; stack peak {peak:#x} (synthetic flash/controller only)")


if __name__ == "__main__":
    main()
