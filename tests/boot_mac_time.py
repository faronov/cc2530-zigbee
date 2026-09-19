#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine SDCC MAC Timer transactions under explicit synthetic latches; NEVER flash."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unittest

from boot_image import ALIAS, check_alias, check_pc, marker, memory_dump, simulate, snapshot_commands, verify_component_layout
from boot_timebase import GUARD_SFRS, READ_OFFSETS, READER_BYTES
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import FIFO_LENGTHS, instructions
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

SIZE = 2999
DIGEST = "3caf16cb1d4010f02b32c9d448aab19eb816137f4e4d5c9be5088d37bc78b336"
PRIVATE_DIGEST = "7b2dc17c795498472384142f39d7b707dcafeccfd7ea815c006644d2af96455b"
LENGTHS = PRNG_LENGTHS | FIFO_LENGTHS | {0: 1, 0x49: 1, 0x52: 2, 0xa4: 1}
SIZES = (4, 2, 2, 1, 1, 1, 1, 1, 1)
FIELDS = ("elapsed_ticks", "polls", "discarded", "result", "phase", "control", "select", "irq_flags", "timebase_status")
OBJECTS = {
    "output": (97, 14), "action": (111, 1), "return": (112, 1), "target": (113, 2),
    "limit": (115, 2), "timeout": (117, 4), "diagnostic": (121, 2),
}
INPUTS = ("action", "target", "limit", "timeout")
PUBLIC_APIS = {
    "timebase_read_awake_ticks24": (0x62, "SL:U"),
    "timebase_deadline_after": (0xba, "SC:U"),
    "timebase_expired": (0x14e, "SC:U"),
    "mac_time_init": (0xa1e, "SC:U"),
    "mac_time_read_live": (0xa6d, "SC:U"),
    "mac_time_diagnostic": (0xac5, "DX,ST__00000001:S"),
}
SFRS = {
    0xa8: "IEN0", 0xb8: "IEN1", 0x9a: "IEN2", 0xbe: "SLEEPCMD",
    0xc6: "CLKCONCMD", 0x9e: "CLKCONSTA", 0xd6: "DMAARM", 0xd7: "DMAREQ", 0xbf: "RFERRF",
    0x94: "T2CTRL", 0xc3: "T2MSEL", 0xa1: "T2IRQF", 0xa2: "T2M0", 0xa3: "T2M1",
    0xa4: "T2MOVF0", 0xa5: "T2MOVF1", 0xa6: "T2MOVF2", 0xa7: "T2IRQM", 0x9c: "T2EVTCFG",
}
INITIAL_WRITES = [(0x9c, 0x77), (0x94, 8), (0xc3, 0x22), (0xa2, 0), (0xa3, 2),
                  (0xa4, 255), (0xa5, 255), (0xa6, 255), (0xc3, 0), (0x94, 9)]


def verify_code(image):
    require(hashlib.sha256(code_bytes(image, SIZE)).hexdigest() == DIGEST,
            "MAC time complete instructions/constants/runtime changed")


def fields(debug, module, tag, names, sizes):
    records = re.findall(rf"^T:F{module}\${tag}\[(.*)\]$", debug, re.M)
    expected = [(sum(sizes[:i]), n, s) for i, (n, s) in enumerate(zip(names, sizes))]
    require(records, "Missing MAC time structure ABI")
    for record in records:
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
        require([(int(a), b, int(c)) for a, b, c in actual] == expected, "MAC time field ABI changed")


def verify(image, symbols, debug, memory, listings):
    verify_code(image)
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_time_test_result",
        ("timebase.c", "mac_time.c", "test_mac_time.c"), xdata_budget=512)
    private = "\n".join(sorted(l for l in debug.splitlines() if re.match(
        r"^(?:S|L):L(?:timebase|mac_time)\.", l)))
    require(hashlib.sha256(private.encode()).hexdigest() == PRIVATE_DIGEST,
            "MAC time entire compiler-private ABI/declarations/addresses changed")
    for name, (address, abi) in PUBLIC_APIS.items():
        require(symbols.get("_"+name) == address and
                cdb_address(debug, f"L:G${name}$0$0") == address,
                "MAC time public entry address changed: "+name)
        records = re.findall(rf"^F:G\${name}\$[^\n]*$", debug, re.M)
        require(set(records) == {f"F:G${name}$0_0$0({{2}}DF,{abi}),Z,0,0,0,0,0"},
                "MAC time public declaration ABI changed: "+name)
    modules = {"timebase": (0x62, 0x1f6), "mac_time": (0x1f6, 0xac9), "test_mac_time": (0xac9, 0xb98)}
    require(cdb_address(debug, "L:Fmac_time$observe$0$0") == 0x1f6 and
            cdb_address(debug, "L:XG$mac_time_diagnostic$0$0")+1 == 0xac9 and
            cdb_address(debug, "L:XG$main$0$0")+1 == 0xb98, "MAC time code extent changed")
    codes, covered = {}, set()
    for name, (start, end) in modules.items():
        code = instructions(image, start, end, LENGTHS); codes[name] = code
        listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listings[name], re.M)]
        startup = [(0, b"\x02\0\x06"), (0x5f, b"\x02\0\x03"), (3, b"\x02\x0b\x50")]
        require((startup if name == "test_mac_time" else []) + list(code.items()) == listed,
                "MAC time listing snapshot differs from genuine instructions: "+name)
        if name == "test_mac_time":
            require(not peripheral_accesses(code), "MAC time test caller acquired MMIO"); continue
        require(".area XSEG    (XDATA)" in listings[name], "Missing MAC time private allocation listing")
        section = listings[name].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", section, re.M):
            region = set(range(int(address, 16), int(address, 16)+int(size)))
            require(region and not region & covered, "MAC time private allocations overlap")
            covered |= region
    require(covered == set(range(97)), "MAC time complete private prefix escaped its fence")
    code = codes["mac_time"]
    accesses = peripheral_accesses(code)
    require([code.get(pc) for pc in (0x76b, 0x76e, 0x771, 0x774, 0x777)] ==
            [b"\x75\xa2\0", b"\x75\xa3\x02", b"\x75\xa4\xff", b"\x75\xa5\xff", b"\x75\xa6\xff"],
            "MAC time consecutive low-first period writes changed")
    require([raw.hex() for _, raw, _ in accesses] == [
        "e5a8", "e5b8", "e59a", "aebe", "e5d6", "e5d7", "e5c6", "b59e02", "e5bf",
        "e5a7", "e594", "e5c3", "e5a1", "b59c02", "e5c6",
        "e5a2", "e5a3", "e5a4", "e5a5", "e5a6",
        "f59c", "f594", "f5c3", "75a200", "75a302", "75a4ff", "75a5ff", "75a6ff",
        "e5a2", "e5a3", "e5a4", "e5a5", "e5a6", "f5c3", "f594",
        "e5a2", "e5a3", "e5a4", "e5a5", "e5a6",
    ], "MAC time exact SFR/order/side-effect contract changed")
    sites = {}
    for pc, raw, reg in accesses:
        require(symbols.get("_SOC_"+SFRS[reg]) == reg, "MAC time SFR address changed")
        write = raw[0] in (0x75, 0xf5)
        observed = reg if write or raw[0] == 0xb5 else raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0
        sites[pc] = ("w" if write else "r", reg, observed)
    xreads = []
    for pc, raw in code.items():
        if raw[0] == 0x90 and int.from_bytes(raw[1:], "big") >= 0x1e00:
            address = int.from_bytes(raw[1:], "big")
            require(code.get(pc+3) == b"\xe0", "MAC time acquired an XREG write")
            xreads.append(address); sites[pc+3] = ("r", address, 0xe0)
    require(xreads == [0x624a, 0x61e1, 0x618b, 0x6192, 0x6193, 0x61a3, 0x61a4, 0x61a5],
            "MAC time chip/radio quiescence whitelist changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(reader == 0x62 and bytes(image[reader+i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "MAC time real Sleep Timer reader changed")
    for i, offset in enumerate(READ_OFFSETS):
        require(symbols.get(f"_SOC_ST{i}") == 0x95+i, "MAC time ST0/1/2 ABI changed")
        sites[reader+offset] = ("r", 0x95+i, 0xe0)
    # Exactly one live destructive-read site, and no other Timer2 read between
    # it and the saved-byte FF decision. Whole CODE pins the branches/retry path.
    require([(pc, raw) for pc, raw, reg in accesses if pc >= 0x8a3 and reg in range(0xa2, 0xa7)] ==
            [(0x8a3, b"\xe5\xa2"), (0x8e8, b"\xe5\xa3"), (0x8ee, b"\xe5\xa4"),
             (0x8f4, b"\xe5\xa5"), (0x8fa, b"\xe5\xa6")], "MAC time live latch read sites changed")
    require(re.search(r"S:Lmac_time.mac_time_read_live\$output\$[^(]+\(\{2\}DX,ST", debug),
            "MAC time output lost XDATA-qualified ABI")
    require(bytes(image[i] for i in range(0xac5, 0xac9)) == b"\x90\0\x1c\x22",
            "MAC time diagnostic accessor changed")
    for module in ("mac_time", "test_mac_time"):
        fields(debug, module, "__00000000", ("fine", "periods"), (2, 4))
        fields(debug, module, "__00000001", FIELDS, SIZES)
    fields(debug, "test_mac_time", "__00000002", ("before", "value", "after"), (4, 6, 4))
    fields(debug, "mac_time", "__00000002",
           ("start", "previous", "deadline", "limit", "control", "select", "event"), (4, 4, 4, 2, 1, 1, 1))
    for name, a in {"saved_clock": 27, "status": 28, "staged": 42, "work": 48}.items():
        require(cdb_address(debug, f"L:Fmac_time${name}$0_0$0") == a, "MAC time private object changed")
    for name, (address, size) in OBJECTS.items():
        symbol = "_mac_time_test_"+name
        records = re.findall(rf"^S:G\${symbol[1:]}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(symbols.get(symbol) == address and records and all(int(x) == size for x in records)
                and set(range(address, address+size)) <= allocated, "MAC time caller allocation/ABI changed")
    for name, expected in {
        "_mac_time_fault": 25, "_mac_time_ready": 26, "_mac_time_reserved_end": 96,
        "__gptrput_PARM_2": 123, "_mac_time_test_cycle": 0xac9, "_mac_time_test_before": 0xac9,
        "_mac_time_test_done": 0xb4e, "_main": 0xb50, "l_XSEG": 124,
        "s_DSEG": 0, "l_DSEG": 110, "s_OSEG": 11, "l_OSEG": 3, "l_ISEG": 0,
        "s_BSEG_BYTES": 32, "l_BSEG_BYTES": 1, "l_BSEG": 1, "s_SSEG": 33,
        "s_REG_BANK_0": 0, "l_REG_BANK_0": 8, "l_REG_BANK_1": 0, "l_REG_BANK_2": 0, "l_REG_BANK_3": 0,
    }.items():
        require(symbols.get(name) == expected, "MAC time boundary changed: "+name)
    require(image[0xac9] == image[0xb4e] == 0, "MAC time checkpoint is not NOP")
    return allocated, sites


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for name, (address, abi) in PUBLIC_APIS.items():
        with case.assertRaisesRegex(ValueError, "public entry"):
            verify(image, symbols | {"_"+name: address+1}, debug, memory, listings)
        record = f"L:G${name}$0$0:{address:X}"
        require(record in debug, "MAC time public-address mutation did not apply")
        with case.assertRaisesRegex(ValueError, "public entry"):
            verify(image, symbols, debug.replace(record, f"L:G${name}$0$0:{address+1:X}"),
                   memory, listings)
        record = f"F:G${name}$0_0$0({{2}}DF,{abi}),Z,0,0,0,0,0"
        require(record in debug, "MAC time public-declaration mutation did not apply")
        wrong = f"F:G${name}$0_0$0({{2}}DF,SV:S),Z,0,0,0,0,0"
        for altered in (debug.replace(record, wrong), debug+"\n"+wrong+"\n"):
            with case.assertRaisesRegex(ValueError, "public declaration"):
                verify(image, symbols, altered, memory, listings)
    for pc in image:
        with case.assertRaisesRegex(ValueError, "instructions"):
            verify_code(image | {pc: image[pc] ^ 1})
    for name, value in (
        ("_SOC_T2M0", 0xa3), ("_SOC_T2MOVF0", 0xa5), ("_SOC_T2CTRL", 0x95),
        ("_mac_time_reserved_end", 70), ("__gptrput_PARM_2", 200), ("_mac_time_test_result", 0x1f00),
        ("_mac_time_test_output", 0x6000), ("l_XSEG", 449), ("l_XABS", 1), ("l_XISEG", 1), ("l_PSEG", 1),
        ("s_SSEG", 128), ("l_DSEG", 111), ("l_OSEG", 4), ("l_REG_BANK_1", 8), ("__XPAGE", 0xa0),
    ):
        with case.assertRaises(ValueError):
            verify(image, symbols | {name: value}, debug, memory, listings)
    for old, new in (
        ("({6}ST", "({7}ST"), ("{2}S:S$periods", "{3}S:S$periods"),
        ("({2}DX,ST", "({3}DG,ST"), ("C$mac_time.c$", "C$missing.c$"),
        ("L:Fmac_time$status$0_0$0:1C", "L:Fmac_time$status$0_0$0:1D"),
    ):
        require(old in debug, "MAC time mutation did not apply")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    for name in listings:
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {name: ""})
        rows = listings[name].splitlines(keepends=True)
        indices = [i for i, row in enumerate(rows)
                   if re.match(r"^\s+[0-9A-F]{6} (?:[0-9A-F]{2} ){1,3}\s+\[\s*\d+\]", row)]
        require(len(indices) >= 2, "MAC time listing mutation lacks instruction records")
        first, second = indices[:2]
        swapped = rows.copy()
        swapped[first], swapped[second] = swapped[second], swapped[first]
        for altered in (rows[:first]+rows[first+1:], rows[:first]+[rows[first]]+rows[first:], swapped):
            with case.assertRaisesRegex(ValueError, "listing snapshot"):
                verify(image, symbols, debug, memory, listings | {name: "".join(altered)})
    with case.assertRaises(ValueError):
        verify(image | {0x8000: 0}, symbols, debug, memory, listings)
    with case.assertRaises(ValueError):
        verify(image, symbols, debug, memory.replace("bytes available", "bytes absent"), listings)


def trace_contract(step):
    writes = [(a, v) for k, a, v in step["events"] if k == "w"]
    if step["action"]:
        require(not writes, "MAC time live read acquired a hardware write")
    else:
        require(writes == INITIAL_WRITES[:len(writes)], "MAC time altered init order/value or added cleanup")
        if step["result"] == 1: require(writes == INITIAL_WRITES, "MAC time init skipped actual configuration")
    control, select, discard, expected = (13 if step["action"] else 2), 0, 0, 0xa2
    for kind, address, value in step["events"]:
        if address == 0x94: control = value
        if address == 0xc3: select = value
        if kind != "r" or not control & 4 or select or address not in range(0xa2, 0xa7):
            continue
        require(address == expected, "MAC time repeated/skipped a destructive live read")
        if address == 0xa2 and value == 255: discard += 1
        else: expected = 0xa2 if address == 0xa6 else address+1
    if step["events"] and step["diagnostics"][4] >= 7:
        require(discard == step["diagnostics"][2], "MAC time FF discard accounting changed")


def execute(simulator, path, symbols, allocated, sites, vector):
    before, done = symbols["_mac_time_test_before"], symbols["_mac_time_test_done"]
    current = GUARD_SFRS | {int(a): v for a, v in vector["initial"].items()}
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
    commands += [f"break {pc:#x}" for pc in list(sites)+[before, done]]
    commands += [f"run {symbols['_main']:#x} {before:#x}"]
    records, number = [], 10
    for index, step in enumerate(vector["steps"]):
        trace_contract(step)
        if index: commands += ["step 1", "run"]
        commands += [marker(number), "state"]; start = number; number += 1
        inputs = {}
        for name in INPUTS:
            address, size = OBJECTS[name]; value = step[name]
            if name == "target":
                value = {65535: 101, 65534: 123, 65533: 96}.get(value, value)
            inputs[name] = value.to_bytes(size, "little")
            commands.append(f"set memory xram {address:#x} "+" ".join(hex(b) for b in inputs[name]))
        commands.append("step 1"); first = number
        previous = None
        for kind, address, value in step["events"]:
            memory = "sfr" if address < 256 else "xram"
            # uCsim run skips a breakpoint at the CURRENT PC. The period's
            # five actual adjacent MOV instructions must each be stepped,
            # without run accidentally executing the next peripheral write.
            adjacent = previous is not None and previous[0] == kind == "w" and \
                0xa2 <= previous[1] < 0xa6 and address == previous[1]+1
            if not adjacent: commands.append("run")
            commands += [marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r": commands.append(f"set memory {memory} {address:#x} {value:#x}")
            commands += [marker(number+1), "step 1"]
            if kind == "r":
                commands += ["dump /h sfr 0xe0 0xe0", "dump /h iram 0 7"]
            commands.append(f"dump /h {memory} {address:#x} {address:#x}")
            commands.append(marker(number+2)); number += 3; current[address] = value
            previous = (kind, address)
        commands += ["run"] + snapshot_commands(number)
        commands += [marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5)]
        records.append((step, start, first, number, dict(current), inputs)); number += 6
    require(number < 65536, "MAC time trace marker budget exceeded")
    text = simulate(simulator, commands, path)
    # A single index for ALL event and full-memory blocks, not quadratic scans.
    pieces = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    numbers = [int(pieces[i], 16) for i in range(1, len(pieces), 2)]
    require(len(numbers) == len(set(numbers)), "Duplicate MAC time simulator marker")
    blocks = dict(zip(numbers, pieces[2::2])); peak = 0
    for step, start, first, final, current, inputs in records:
        check_pc(blocks[start], before)
        for i, (kind, address, value) in enumerate(step["events"]):
            n = first+3*i
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", blocks[n])
            require(match is not None and int(match[1], 16) in sites, vector["name"]+": unplanned MMIO stop")
            actual_kind, actual_address, observed = sites[int(match[1], 16)]
            require((kind, address) == (actual_kind, actual_address),
                    f"{vector['name']}: action{step['action']} event{i} PC{int(match[1],16):04X}: "
                    f"wanted {kind}:{address:04X}, got {actual_kind}:{actual_address:04X}")
            regs = memory_dump(blocks[n], 0x81, 3); peak = max(peak, regs[0])
            if address >= 256:
                require(regs[1:] == address.to_bytes(2, "little"), "MAC time actual MOVX address changed")
            require(memory_dump(blocks[n+1], observed, 1)[0] == value, "MAC time actual MMIO value mismatch")
        check_pc(blocks[final], done)
        ram = memory_dump(blocks[final], 0, 0x1f00)
        iram = memory_dump(blocks[final+1], 0, 256); sfr = memory_dump(blocks[final+2], 0x80, 128)
        require(ram[0x1e00:0x1e08] == b"MTI1\x01\x08\0\0", "MAC time status ABI changed")
        require(ram[25] == step["fault"] and ram[26] == step["ready"] and ram[112] == step["result"],
                "MAC time public/retained state mismatch")
        require(ram[121:123] == b"\x1c\0", "MAC time accessor is not the real private diagnostic")
        expected = b"".join(v.to_bytes(n, "little") for v, n in zip(step["stamp"], (2, 4)))
        require(ram[101:107] == expected, vector["name"]+": coherent stamp/nonpublication mismatch")
        require(ram[97:101] == ram[107:111] == b"\xa5"*4, "MAC time caller guard overwritten")
        require(len(step["diagnostics"]) == len(SIZES), "MAC time diagnostic serialization changed")
        expected = b"".join(v.to_bytes(n, "little") for v, n in zip(step["diagnostics"], SIZES))
        require(ram[28:42] == expected, vector["name"]+": diagnostic ABI mismatch")
        for name, value in inputs.items():
            address = OBJECTS[name][0]
            require(ram[address:address+len(value)] == value, "MAC time changed caller inputs")
        require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
                "MAC time changed unallocated/status/alias XDATA")
        require(iram[128:] == b"\xc7"*128 and sfr[1] == symbols["s_SSEG"]+1 and peak < 128,
                "MAC time upper IRAM/stack guard/unwind failed")
        for address, value in current.items():
            if address < 256:
                require(sfr[address-0x80] == value, f"MAC time changed guarded SFR {address:02x}")
        radio = memory_dump(blocks[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(radio, 0x6000)),
                "MAC time changed radio registers/RAM beyond explicit synthetic effects")
    return peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_time_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_text(); memory = path.with_suffix(".mem").read_text()
    listings = {n: (args.output / f"mac_time_test.{n}.rst").read_text()
                for n in ("timebase", "mac_time", "test_mac_time")}
    allocated, sites = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    result = subprocess.run([str(args.output / "host-mac-time-tests"), "--vectors"],
                            capture_output=True, text=True, timeout=15, check=True)
    vectors = [json.loads(line) for line in result.stdout.splitlines()]
    require(len(vectors) == 48, "MAC time trace inventory changed")
    peak = max(execute(args.simulator, path, symbols, allocated, sites, v) for v in vectors)
    # Real negative control: emulate an INCORRECT MOVF0 re-latch, but retain the
    # known T2M0-instant oracle. Range-valid torn time must not pass this proof.
    broken = copy.deepcopy(next(v for v in vectors if v["name"].startswith("latched overflow FF is")))
    events = broken["steps"][1]["events"]
    point = next(i for i, e in enumerate(events) if e == ["r", 0xa4, 255])
    events[point][2] = 0
    require(events[point+1] == ["r", 0xa5, 0x12], "Torn-latch negative lost its carry boundary")
    events[point+1][2] = 0x13
    with unittest.TestCase().assertRaisesRegex(ValueError, "coherent stamp"):
        execute(args.simulator, path, symbols, allocated, sites, broken)
    count = sum(len(v["steps"]) for v in vectors)
    events = sum(len(s["events"]) for v in vectors for s in v["steps"])
    print(f"MAC time: {len(image)} CODE, {len(allocated)-8} ordinary XDATA +64 reserved; "
          f"stack start {symbols['s_SSEG']:02X}, MMIO peak {peak:02X}. "
          f"{len(vectors)} sequences/{count} calls/{events} genuine linked MMIO events; "
          "whole CODE/ABI/prefix/listing, coherent latch/FF, torn-latch negative, alias/stack PASS. "
          "Synthetic only; not calibrated time, capture, RF or hardware evidence. NEVER flash.")


if __name__ == "__main__":
    main()
