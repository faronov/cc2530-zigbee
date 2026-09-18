#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Real two-page journal/flash/RAM-engine composition; synthetic, NEVER flash."""
import argparse
import hashlib
from pathlib import Path
import re
import unittest
import zlib

import boot_flash_exec as engine
from boot_flash_write import MODULES as FLASH_MODULES
from boot_image import ALIAS, check_alias, check_pc, marker, memory_dump, simulate, snapshot_commands, verify_component_layout
from boot_timebase import GUARD_SFRS
from radio_fifo_fixture import instructions
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

SIZE = 7046
DIGEST = "7214d763451bc53d74ca14f9a29fe06e8eb1524cf6088c5db41b97754ffc6d12"
PRIVATE = "30f1fc54fab8362424fc7028b9df6bc829f20786a99e4e0db57dd4eebb5ac99f"
BEFORE, DONE, MAIN = 0x1aad, 0x1b0c, 0x1b0e
READ_CALL, PROGRAM_CALL = 0xe92, 0x14bc
LENGTHS = engine.LENGTHS | {
    0xc8: 1, 0x68: 1, 0x5a: 1, 0x6b: 1, 0x6c: 1, 0x6d: 1, 0x6e: 1,
    0xdf: 2, 0x63: 3, 0xa4: 1, 0x6f: 1, 0x3c: 1, 0x69: 1, 0x2b: 1,
}
OBJECTS = {
    "fault": (0x194, 1), "diagnostic": (0x195, 15), "stage": (0x1a4, 128),
    "chunk": (0x224, 32), "word": (0x244, 4), "reserved_end": (0x294, 1),
    "test_buffer": (0x295, 128), "test_action": (0x315, 1), "test_length": (0x316, 1),
    "test_recovery": (0x317, 1), "test_return": (0x318, 1),
    "test_limit": (0x319, 2), "test_pointer": (0x31b, 2),
}


def verify(image, symbols, debug, memory, listings):
    require(hashlib.sha256(code_bytes(image, SIZE)).hexdigest() == DIGEST, "NV complete CODE changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "nv_record_test_result",
        ("flash_exec.c", "flash.c", "flash_write.c", "nv_record.c", "test_nv_record.c"), xdata_budget=1024)
    private = "\n".join(sorted(set(line for line in debug.splitlines() if re.match(
        r"[SLT]:(?:L(?:flash_exec|flash|flash_write|nv_record)\.|F(?:flash_exec|flash|flash_write|nv_record)\$)", line))))
    require(hashlib.sha256(private.encode()).hexdigest() == PRIVATE, "NV complete private ABI/storage changed")
    covered = set()
    modules = {name: FLASH_MODULES[key] for name, key in
               (("flash_exec", "exec"), ("flash", "reader"), ("flash_write", "service"))}
    modules["nv_record"] = (0xc3f, BEFORE, None)
    for name, (start, end, digest) in modules.items():
        if digest:
            require(hashlib.sha256(bytes(image[a] for a in range(start, end))).hexdigest() == digest,
                    "NV no longer contains the exact published flash service instructions")
        code = instructions(image, start, end, LENGTHS)
        listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listings[name], re.M)]
        require(list(code.items()) == listed, "NV per-link listing differs from actual CODE")
        segment = listings[name].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            region = set(range(int(a, 16), int(a, 16)+int(n)))
            require(region and not region & covered, "NV private allocation overlaps")
            covered |= region
        if name == "nv_record":
            require(not peripheral_accesses(code), "NV policy bypassed flash services")
            external = [(pc, int.from_bytes(raw[1:], "big")) for pc, raw in code.items()
                        if raw[0] == 0x12 and not start <= int.from_bytes(raw[1:], "big") < end]
    require(covered == set(range(0x295)), "NV compiler-private prefix escaped fence")
    require([(pc, target) for pc, target in external if target != 0xbc6] ==
            [(READ_CALL, 0x5e0), (PROGRAM_CALL, 0xbf7)] and
            len([pc for pc, target in external if target == 0xbc6]) == 1,
            "NV helper/service call graph changed")
    erase_call = next(pc for pc, target in external if target == 0xbc6)
    for name, value in {
        "_flash_exec_command": 0x1cf, "_flash_exec_template_end": 0xdd,
        "_flash_nv_read": 0x5e0, "_flash_nv_erase": 0xbc6, "_flash_nv_program": 0xbf7,
        "_flash_write_reserved_end": 0x193, "_nv_record_load": 0x131f,
        "_nv_record_replace": 0x14d7, "_nv_record_status": 0x1aa9,
        "_nv_record_before": BEFORE, "_nv_record_done": DONE, "_main": MAIN,
        "s_XSEG": 0, "l_XSEG": 797, "s_SSEG": 0x38, "l_OSEG": 4, "s_OSEG": 0x19, "l_BSEG": 2,
        "_nv_record_load_PARM_2": 0x284, "_nv_record_replace_PARM_2": 0x28c,
        "_nv_record_replace_PARM_3": 0x28d, "_nv_record_replace_PARM_4": 0x28f,
        "_flash_nv_read_PARM_2": 0xc9, "_flash_nv_read_PARM_3": 0xcb, "_flash_nv_read_PARM_4": 0xcd,
        "_flash_nv_erase_PARM_2": 0x189, "_flash_nv_program_PARM_2": 0x18c,
        "_flash_nv_program_PARM_3": 0x18e, "_flash_nv_program_PARM_4": 0x190,
    }.items():
        require(symbols.get(name) == value, "NV symbol/parameter ABI changed: "+name)
    for name, (a, size) in OBJECTS.items():
        name = "nv_record_"+name
        require(symbols.get("_"+name) == cdb_address(debug, f"L:G${name}$0_0$0") == a, "NV object address changed")
        sizes = re.findall(rf"^S:G\${name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "NV object type/size changed")
    require(not any("gptr" in name or name.startswith("_host_") for name in symbols),
            "NV acquired generic scratch or a native model")
    for name, pointer in (("load", "output"), ("replace", "data")):
        require(re.search(rf"^S:Lnv_record.nv_record_{name}\${pointer}\$[^(\n]+\(\{{2\}}DX,SC:U\)", debug, re.M),
                "NV caller pointer is not the exact XDATA ABI")
    return allocated, erase_call


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for a in image:
        with case.assertRaises(ValueError):
            verify(image | {a: image[a] ^ 1}, symbols, debug, memory, listings)
    for name in ["_nv_record_"+name for name in OBJECTS]+[
        "s_XSEG", "l_XSEG", "s_SSEG", "l_PSEG", "l_XISEG", "l_XABS", "__XPAGE",
        "_flash_nv_read_PARM_3", "_nv_record_load_PARM_2", "_nv_record_replace_PARM_3",
    ]:
        with case.assertRaises(ValueError):
            verify(image, symbols | {name: symbols[name]+1}, debug, memory, listings)
    for old, new in (("({15}ST", "({16}ST"), ("({128}DA128d", "({127}DA127d"),
                     ("C$nv_record.c$", "C$absent.c$"), ("({2}DX,SC:U)", "({3}DG,SC:U)")):
        require(old in debug, "Missing NV ABI rejection fixture")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    for name in listings:
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {name: ""})


def record(generation, body):
    require(1 <= generation <= 0xffffffff and 1 <= len(body) <= 128, "Bad synthetic record")
    prefix = b"NVR1\x01\0"+bytes((len(body), 0))+generation.to_bytes(4, "little")+body
    prefix += b"\xff"*(2040-len(prefix))
    return prefix+zlib.crc32(prefix).to_bytes(4, "little")+b"CMT1"


def page_state(data):
    if data == b"\xff"*2048: return 1
    if data[2044:] != b"CMT1": return 3
    if data[:4] != b"NVR1": return 4
    if data[4] != 1: return 6
    length = data[6]
    if data[5] or data[7] or not 1 <= length <= 128 or not int.from_bytes(data[8:12], "little") or \
            data[12+length:2040] != b"\xff"*(2028-length): return 4
    return 2 if zlib.crc32(data[:2040]) == int.from_bytes(data[2040:2044], "little") else 5


def execute(simulator, path, image, allocated, erase_call, initial, operations):
    nv = bytearray(initial)
    require(len(nv) == 4096, "Synthetic NV extent changed")
    sfr = GUARD_SFRS | {0xbe: 4, 0xc7: 2, 0xd6: 0, 0xd7: 0, 0xc6: 0xc9, 0x9e: 0xc9, 0x92: 0, 0x9f: 3}
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x7fff 0x69",
                "fill xram 0xe7f0 0xf80f 0x69", "fill rom 0x8000 0x9fff 0xa6"]
    for offset in range(0, 4096, 128):
        commands.append(f"set memory xram {0xe800+offset:#x} "+" ".join(hex(b) for b in nv[offset:offset+128]))
    records, ident, mapping, quotas, current_mapping = [], 10, False, [0, 0], 2
    peripheral = {0x624a: 0xa5, 0x6270: 4, 0x6276: 0x44, 0x6277: 0xff}
    call_count, command_count = 0, 0

    def start():
        commands.extend(["delete", f"run 0 {MAIN:#x}", "fill iram 0x80 0xff 0xc7",
                         f"run {MAIN:#x} {BEFORE:#x}"])
        commands.extend(f"set memory sfr {a:#x} {v:#x}" for a, v in sfr.items())
        commands.extend(f"set memory xram {a:#x} {v:#x}" for a, v in peripheral.items())
        commands.extend(f"break {pc:#x}" for pc in (READ_CALL, PROGRAM_CALL, erase_call, DONE, 0x487))

    def point(pc, dumps, kind, data=None, run=True):
        nonlocal ident
        n = ident; ident += 2
        if run: commands.append("run")
        commands.extend([marker(n), "state"]+dumps+[marker(n+1)])
        records.append((kind, n, pc, data))
        return n

    def scan(page):
        nonlocal call_count
        for offset in range(0, 2048, 32):
            point(READ_CALL, ["dump /h xram 0xc9 0xcd", "dump /h sfr 0x82 0x82"],
                  "read", (page, offset))
            commands.append("step 1"); call_count += 1

    def physical(operation, page, offset=0, word=b"\0"*4, phase=3, mode="normal"):
        nonlocal mapping, command_count, call_count, current_mapping
        point(erase_call if operation == 1 else PROGRAM_CALL,
              ["dump /h xram 0x189 0x190", "dump /h sfr 0x82 0x82",
               "dump /h xram 0x195 0x1a3", "dump /h xram 0x244 0x247"],
              "api", (operation, page, offset, word, phase, tuple(quotas)))
        commands.append("step 1"); call_count += 1
        point(0x487, [], "pc")
        commands.append("step 1")
        point(0x489, ["dump /h sfr 0xc7 0xc7"], "map", run=False)
        current_mapping = 10
        if not mapping: commands.extend(engine.XMAP); mapping = True
        commands.append(f"break {engine.RAM:#x}")
        point(engine.RAM, ["dump /h xram 0 8", "dump /h sfr 0x81 0x81",
                          "dump /h iram 0x38 0x7f", "dump /h rom 0x8009 0x8083",
                          "dump /h xram 0xd1 0x159"],
              "ram", (operation, page, offset, word))
        commands.extend([f"clear {engine.RAM:#x}", f"break {engine.COMMAND:#x}", "step 1"])
        point(engine.COMMAND, [], "pc")
        commands.extend([f"clear {engine.COMMAND:#x}", "step 1"])
        point(engine.COMMAND+1, ["dump /h xram 0x6270 0x6273"], "command", (operation, page, offset), run=False)
        active = 4 if mode == "ignored" else 0x84 | operation
        commands.append(f"set memory xram 0x6270 {active:#x}")
        peripheral[0x6270] = active
        peripheral[0x6271], peripheral[0x6272] = (0xfa00+page*512+(offset >> 2)).to_bytes(2, "little")
        if operation == 2 and mode != "ignored":
            for index, pc in enumerate((0x95, 0x97, 0x99, 0x9b)):
                pc += engine.DELTA
                commands.append(f"break {pc:#x}")
                point(pc, ["dump /h sfr 0x82 0x83", "dump /h sfr 0xe0 0xe0"], "data", word[index])
                commands.extend([f"clear {pc:#x}", "step 1"])
                point(pc+1, ["dump /h xram 0x6273 0x6273"], "written", word[index], run=False)
            peripheral[0x6273] = word[-1]
        commands.append(f"break {engine.POLL:#x}")
        point(engine.POLL, [], "pc")
        commands.append("step 1")
        command_count += 1
        if mode == "stuck":
            commands.extend([f"clear {engine.POLL:#x}", f"break {engine.STOP:#x}"])
            point(engine.STOP, [], "pc")
            return True
        if mode != "ignored":
            point(engine.POLL, [], "pc")
            if operation == 1:
                nv[page*2048:(page+1)*2048] = b"\xff"*2048
                commands.append(f"fill xram {0xe800+page*2048:#x} {0xefff+page*2048:#x} 0xff")
            else:
                nv[page*2048+offset:page*2048+offset+4] = word
                commands.append(f"set memory xram {0xe800+page*2048+offset:#x} "+" ".join(hex(b) for b in word))
        commands.extend(["set memory xram 0x6270 4", f"clear {engine.POLL:#x}", f"break {engine.RET:#x}"])
        peripheral[0x6270] = 4
        # The ignored command already read idle on its first poll.
        if mode != "ignored": commands.append("step 1")
        point(engine.RET, ["dump /h xram 0x6270 0x6270"], "idle-return")
        if mode == "cut": return True
        commands.extend([f"clear {engine.RET:#x}", "step 1"])
        point(0x4b3, [], "pc", run=False)
        if mode == "normal": current_mapping = 2
        return False

    start()
    terminal = False
    for index, options in enumerate(operations):
        terminal = False
        if options.get("reset"):
            quotas = [0, 0]; peripheral[0x6270] = 4; current_mapping = 2; start()
        elif index: commands.extend(["step 1", f"run {DONE+1:#x} {BEFORE:#x}"])
        if "quotas" in options:
            quotas = list(options["quotas"])
            commands.append("set memory xram 0x1a2 "+" ".join(str(n) for n in quotas))
        body = options.get("body", bytes(i ^ 0x69 for i in range(128)))
        action, length, recovery = options.get("action", 0), options.get("length", 128), options.get("recovery", 0)
        pointer, limit = options.get("pointer", 0x295), options.get("limit", 3)
        args = bytes((action, length, recovery, 0xaa))+limit.to_bytes(2, "little")+pointer.to_bytes(2, "little")
        caller = body+b"\xa5"*(128-len(body)) if action else b"\xa5"*128
        commands.extend(["set memory xram 0x315 "+" ".join(hex(b) for b in args),
                         "set memory xram 0x295 "+" ".join(hex(b) for b in caller), "step 1"])
        if not options.get("benign") and not options.get("retained"):
            scan(0); scan(1)
            if action and options["result"] in (0, 1, 12, 13):
                page, generation = options["page"], options["generation"]
                page_bytes = record(generation, body[:length])
                quotas[page] += 1
                steps = [(1, 0, b"\0"*4, 3)]
                steps += [(2, a, page_bytes[a:a+4], 4) for a in range(0, 12+length, 4)]
                steps += [(2, 2040, page_bytes[2040:2044], 5), (2, 2044, b"CMT1", 6)]
                for count, (operation, offset, word, phase) in enumerate(steps, 1):
                    mode = options.get("mode", "normal") if count == options.get("at", 1) else "normal"
                    terminal = physical(operation, page, offset, word, phase, mode)
                    if mode != "normal": break
                else: scan(page)
            elif not action and options["result"] in (0, 1):
                page = options["page"]; scan(page)
                data = nv[page*2048:(page+1)*2048]
                caller = bytes(data[12:12+data[6]])+b"\xa5"*(128-data[6])
        checkpoint = engine.RET if terminal and options.get("mode") == "cut" else engine.STOP if terminal else DONE
        point(checkpoint, [], "pc", run=not terminal)
        n = ident; ident += 6
        commands.extend(snapshot_commands(n)+[marker(n+4), "dump /h xram 0xe7f0 0xf80f", marker(n+5)])
        records.append(("outcome", n, checkpoint,
                        (options, caller, bytes(nv), tuple(quotas), current_mapping)))
        if terminal and options.get("mode") == "cut":
            require(index+1 < len(operations) and operations[index+1].get("reset"),
                    "Interrupted journal must restart through full synthetic reset")
        elif terminal:
            commands.extend(["step 64", "set memory xram 0x6270 4", "step 64"])
            point(engine.STOP, ["dump /h xram 0x194 0x1a3", "dump /h sfr 0x81 0x81",
                               "dump /h sfr 0xc7 0xc7"], "stopped", run=False)
            peripheral[0x6270] = 4
            require(index == len(operations)-1, "RAM stop must end sequence")
    n = ident; commands.extend([marker(n), "dump /h xram 0x6000 0x7fff", marker(n+1)])
    text = simulate(simulator, commands, path)
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    keys = [int(parts[i], 16) for i in range(1, len(parts), 2)]
    require(len(keys) == len(set(keys)), "Duplicate NV simulator marker")
    blocks = dict(zip(keys, parts[2::2]))
    for kind, number, pc, data in records:
        part = blocks[number]; check_pc(part, pc)
        if kind == "read":
            page, offset = data
            require(memory_dump(part, 0x82, 1) == bytes((page,)) and
                    memory_dump(part, 0xc9, 5) == offset.to_bytes(2, "little")+b"\x24\x02\x20",
                    "NV did not read every page chunk in order through the genuine reader")
        elif kind == "api":
            op, page, offset, word, phase, quota = data
            require(memory_dump(part, 0x82, 1) == bytes((page,)), "NV flash page argument differs")
            args = memory_dump(part, 0x189, 8)
            require((args[:2] == b"\x03\0") if op == 1 else
                    args[3:] == offset.to_bytes(2, "little")+b"\x44\x02\x03",
                    "NV flash offset/source/poll ABI differs")
            d = memory_dump(part, 0x195, 15)
            require(d[4] == 13 and d[7] == phase and d[9] == 10 and d[13:] == bytes(quota),
                    "NV failed to publish pending phase/consume erase admission before the command")
            if op == 2: require(memory_dump(part, 0x244, 4) == word, "NV commit/header/body word differs")
        elif kind == "map":
            require(memory_dump(part, 0xc7, 1) == b"\x0a", "NV XMAP model preceded actual MEMCTR")
        elif kind == "ram":
            op, page, offset, word = data
            require(memory_dump(part, 0, 9) == bytes((4|op,))+word+b"\x03\0\xff\0",
                    "NV RAM command inputs differ")
            require(memory_dump(part, engine.RAM, 123) == bytes(image[a] for a in range(0x62, 0xdd)),
                    "NV did not execute the actual copied RAM engine")
            sp = memory_dump(part, 0x81, 1)[0]
            require(memory_dump(part, sp-1, 2) == b"\xb3\x04", "NV lost genuine common-C return frame")
            f = memory_dump(part, 0xd1, 137)
            require(f[0] == 10 and f[1] == op and f[2] == page and f[5] == 2,
                    "NV bypassed real write policy")
            require(not (f[8] & (1 << page)) if op == 1 else
                    f[9+page*64+(offset >> 5)] & (1 << ((offset >> 2) & 7)),
                    "NV imported history or skipped attempted-word consumption")
        elif kind == "command":
            op, page, offset = data
            require(memory_dump(part, 0x6270, 3) == bytes((4|op,))+
                    (0xfa00+page*512+(offset >> 2)).to_bytes(2, "little"), "NV actual FCTL/FADDR differs")
        elif kind == "data":
            require(memory_dump(part, 0x82, 2) == b"\x73\x62" and
                    memory_dump(part, 0xe0, 1) == bytes((data,)), "NV actual staged FWDATA operand differs")
        elif kind == "written":
            require(memory_dump(part, 0x6273, 1) == bytes((data,)), "NV actual FWDATA store differs")
        elif kind == "idle-return":
            require(memory_dump(part, 0x6270, 1) == b"\x04", "NV returned from RAM while controller active")
        elif kind == "outcome":
            options, caller, expected_nv, quota, expected_mapping = data
            ram = memory_dump(part, 0, 0x1f00)
            iram = memory_dump(blocks[number+1], 0, 256)
            registers = memory_dump(blocks[number+2], 0x80, 128)
            d = ram[0x195:0x1a4]
            require(ram[0x295:0x315] == caller, "NV caller publication/tail differs")
            require(ram[0x318] == (0xaa if options["result"] == 13 else options["result"]),
                    f"NV public result differs: {ram[0x318]} expected {options['result']}")
            require(d[13:] == bytes(quota), "NV runtime erase accounting differs")
            if options["result"] in (0, 1):
                page = options["page"]; contents = expected_nv[page*2048:(page+1)*2048]
                require(d[:4] == contents[8:12] and d[5:8] == bytes((page, contents[6], 8)),
                        "NV selected generation/length/done phase differs")
            if options["result"] == 13:
                require(d[4] == 13 and d[9] == 10 and
                        ram[7] == (0 if options.get("mode") == "cut" else 7) and ram[0xd1] == 10,
                        "NV interrupted command became successful publication")
            require(ram[0x1e00:0x1e08] == b"NVR1\x01\x08\0\0" and
                    all(b == 0xa5 for a, b in enumerate(ram) if a not in allocated),
                    "NV escaped allocated/status XDATA")
            require(iram[128:] == b"\xc7"*128 and (options["result"] == 13 or registers[1] == 0x39),
                    "NV upper IRAM/alias/stack unwind failed")
            expected_sfr = sfr | {0xc7: expected_mapping}
            mismatches = [(a, registers[a-0x80], v) for a, v in expected_sfr.items() if registers[a-0x80] != v]
            require(not mismatches, f"NV changed guarded SFR: {mismatches}")
            require(memory_dump(blocks[number+4], 0xe7f0, 0x1020) == b"\x69"*16+expected_nv+b"\x69"*16,
                    "NV changed old/unselected/neighbor flash outside modeled commands")
        elif kind == "stopped":
            require(memory_dump(part, 0xc7, 1) == b"\x0a" and memory_dump(part, 0x194, 16)[5] == 13,
                    "NV escaped retained RAM stop after later idle")
    radio = memory_dump(blocks[n], 0x6000, 0x2000)
    require(all(b == peripheral.get(a, 0x69) for a, b in enumerate(radio, 0x6000)),
            "NV touched unowned peripheral/information storage")
    peaks = [int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "NV stack crossed upper IRAM")
    return max(peaks), call_count, command_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output/"nv_record_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("."+ext).read_text() for ext in ("cdb", "mem"))
    listings = {name: (args.output/f"nv_record_test.{name}.rst").read_text()
                for name in ("flash_exec", "flash", "flash_write", "nv_record")}
    allocated, erase_call = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings); check_alias(args.simulator)
    empty, body = b"\xff"*4096, bytes(i ^ 0x69 for i in range(128))
    old = record(3, body[:5])+record(2, body[:17])
    damaged = bytearray(old); damaged[12] ^= 1
    cases = [
        (empty, [dict(result=2)]),
        (empty, [dict(action=1, length=1, body=body[:1], page=0, generation=1, result=0),
                 dict(result=0, page=0), dict(reset=True, result=0, page=0)]),
        (old, [dict(action=1, length=128, body=body, page=1, generation=4, result=0), dict(result=0, page=1)]),
        (old, [dict(result=5, length=4)]),
        (bytes(damaged), [dict(result=1, page=1), dict(action=1, length=5, result=6),
                         dict(action=1, length=5, body=body[:5], recovery=1, page=0, generation=3, result=1),
                         dict(reset=True, result=0, page=0)]),
        (record(7, body[:5])+record(7, body[:5]), [dict(result=8), dict(action=1, length=5, result=8)]),
        (record(0xffffffff, body[:5])+record(0xfffffffe, body[:5]),
         [dict(result=0, page=0), dict(action=1, length=5, result=9)]),
        (b"\0"*4096, [dict(result=7), dict(action=1, length=5, recovery=1, result=7)]),
        (old, [dict(action=1, length=5, body=body[:5], page=1, generation=4, result=0, quotas=(31, 31)),
               dict(action=1, length=5, body=body[:5], page=0, generation=5, result=0),
               dict(action=1, length=5, result=10)]),
        (empty, [dict(action=1, length=5, body=body[:5], page=0, generation=1, mode="ignored", result=12),
                 dict(action=1, result=12, retained=True), dict(result=12, retained=True)]),
    ]
    unsupported = bytearray(old); unsupported[4] = 2
    cases.append((bytes(unsupported), [dict(result=14), dict(action=1, length=5, recovery=1, result=14)]))
    for at in (1, 2, 7, 8):
        cases.append((old, [dict(action=1, length=5, body=body[:5], page=1, generation=4, result=13,
                                mode="stuck", at=at)]))
    for at in range(1, 39):
        cases.append((old, [dict(action=1, length=128, body=body, page=1, generation=4, result=13,
                                mode="cut", at=at),
                            dict(reset=True, result=0 if at in (1, 38) else 1, page=1 if at == 38 else 0)]))
    for options, result in ((dict(pointer=0), 3), (dict(pointer=0x294), 4),
                            (dict(pointer=0x1f00), 4), (dict(pointer=0x1d81), 4),
                            (dict(length=0), 3), (dict(action=1, length=129), 3),
                            (dict(action=1, limit=0), 3), (dict(action=1, recovery=2), 3)):
        cases.append((empty, [dict(**options, result=result, benign=True)]))
    peak = calls = commands = 0
    for number, (initial, operations) in enumerate(cases):
        try:
            observed, count, physical = execute(args.simulator, path, image, allocated, erase_call, initial, operations)
        except (ValueError, KeyError) as exc:
            raise ValueError(f"NV sequence {number}: {exc}") from exc
        peak = max(peak, observed); calls += count; commands += physical
    print(f"NV record: {len(cases)} linked sequences, {calls} actual flash API calls/{commands} RAM commands; "
          f"whole CODE/private ABI, byte-identical published backend, commit/recovery/retained RAM stop and alias "
          f"guards PASS; 797 ordinary XDATA+64 reserved, peak SP {peak:#x}. Synthetic, not power-loss/counter evidence.")


if __name__ == "__main__":
    main()
