#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine linked TX/CCA + FIFO/timebase execution; synthetic only, NEVER flash."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, simulate, snapshot_commands,
    verify_component_layout,
)
from boot_timebase import GUARD_SFRS, READ_OFFSETS, READER_BYTES
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import FIFO_LENGTHS, instructions, verify_fifo_relocated
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

SIZE = 8526
DIGEST = "426080e0151dffd7f5b9ee4cf58a4d3962e7d0a4d7561359a8cda9970683a154"
PRIVATE_DIGEST = "5388a511fff5111edaf29c362411949d24268c64f1d334de02c00a9bd16a1ab0"
LENGTHS = PRNG_LENGTHS | FIFO_LENGTHS | {
    0: 1, 0x1c: 1, 0x23: 1, 0x2b: 1, 0x3c: 1, 0x49: 1, 0x52: 2, 0x6f: 1, 0xa4: 1,
}
FIELDS = (
    "elapsed_ticks", "polls", "phase", "writes", "verified", "actions", "sample_valid",
    "cca", "txdone", "radio_idle", "timebase_status", "errors", "flags0", "flags1",
    "rx_enable", "fsm0", "signals", "rssi_valid", "rx_count", "tx_count",
    "rx_first", "rx_last", "rx_packet", "tx_first", "tx_last",
)
SIZES = (4, 2) + (1,) * 23
FIFO_FIELDS = (
    "elapsed_ticks", "polls", "timebase_status", "strobes", "confirmed", "bytes_written",
    "bytes_verified", "errors", "rx_count", "tx_count", "rx_first", "rx_last",
    "rx_packet", "tx_first", "tx_last", "fifo_signals", "sample_valid",
)
FIFO_SIZES = (4, 2) + (1,) * 15
SETTINGS = (0x618a, 0x6180, 0x6182, 0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6196, 0x6197)
XREADS = (
    0x624a, 0x61e1, 0x6189, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61a9, 0x61b8, 0x61b9,
    0x6191, 0x618e, 0x618b, 0x6192, 0x6193, 0x6199, 0x619b, 0x619c, 0x619d,
    0x619e, 0x619f, 0x61a1, 0x61a2, 0x618a, 0x6080,
)
OBJECTS = {
    "guard": (173, 37), "fifo": (210, 21), "body": (231, 125),
    "action": (356, 1), "mode": (357, 1), "channel": (358, 1), "power": (359, 1),
    "length": (360, 1), "return": (361, 1), "limit": (362, 2),
    "target": (364, 2), "timeout": (366, 4),
}
INPUTS = ("action", "mode", "channel", "power", "length", "timeout", "limit", "target")
SFRS = {
    0xa8: "IEN0", 0xb8: "IEN1", 0x9a: "IEN2", 0xbe: "SLEEPCMD",
    0xc6: "CLKCONCMD", 0x9e: "CLKCONSTA", 0xbf: "RFERRF", 0xe9: "RFIRQF0",
    0x91: "RFIRQF1", 0xe1: "RFST", 0xd9: "RFD", 0x95: "ST0", 0x96: "ST1", 0x97: "ST2",
}


def verify_code(image):
    require(hashlib.sha256(code_bytes(image, SIZE)).hexdigest() == DIGEST,
            "TX complete instructions/constants/runtime changed")


def private_records(debug):
    return "\n".join(sorted(line for line in debug.splitlines() if re.match(
        r"^(?:S|L):L(?:timebase|radio_fifo|radio_tx)\.", line)))


def fields(debug, module, tag, names, sizes):
    records = re.findall(rf"^T:F{module}\${tag}\[(.*)\]$", debug, re.M)
    expected = [(sum(sizes[:i]), n, s) for i, (n, s) in enumerate(zip(names, sizes))]
    require(records, "Missing TX/FIFO structure ABI")
    for line in records:
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", line)
        require([(int(a), b, int(c)) for a, b, c in actual] == expected, "TX/FIFO field ABI changed")


def verify(image, symbols, debug, memory, listings):
    verify_code(image)
    allocated = verify_component_layout(image, symbols, debug, memory, "radio_tx_test_result",
        ("timebase.c", "radio_fifo.c", "radio_tx.c", "test_radio_tx.c"), xdata_budget=512)
    require(hashlib.sha256(private_records(debug).encode()).hexdigest() == PRIVATE_DIGEST,
            "TX/FIFO/timebase complete compiler-private declarations/addresses changed")
    modules = {
        "timebase": (symbols["_timebase_read_awake_ticks24"], cdb_address(debug, "L:XG$timebase_expired$0$0")+1),
        "radio_fifo": (cdb_address(debug, "L:Fradio_fifo$observe$0$0"),
                       cdb_address(debug, "L:XG$radio_fifo_preload_init$0$0")+1),
        "radio_tx": (cdb_address(debug, "L:Fradio_tx$cca_settle$0$0"),
                     cdb_address(debug, "L:XG$radio_tx_cca_init$0$0")+1),
        "test_radio_tx": (symbols["_radio_tx_test_cycle"], cdb_address(debug, "L:XG$main$0$0")+1),
    }
    require(modules["radio_tx"] == (0xdd8, 0x1ef4), "TX private code extent changed")
    codes, covered = {}, set()
    for name, (start, end) in modules.items():
        code = instructions(image, start, end, LENGTHS); codes[name] = code
        listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
            r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listings[name], re.M)]
        startup = [(0, b"\x02\0\x06"), (0x5f, b"\x02\0\x03"), (3, b"\x02\x20\x59")]
        require((startup if name == "test_radio_tx" else []) + list(code.items()) == listed,
                "TX listing snapshot differs from linked instructions: "+name)
        if name == "test_radio_tx":
            require(not peripheral_accesses(code), "TX harness acquired MMIO")
            continue
        require(".area XSEG    (XDATA)" in listings[name], "Missing private allocation listing")
        segment = listings[name].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            region = set(range(int(a, 16), int(a, 16)+int(n)))
            require(region and not region & covered, "Overlapping compiler-private TX allocation")
            covered |= region
    require(covered == set(range(173)), "TX whole timebase/FIFO/TX prefix escaped its fence")
    fifo, _ = verify_fifo_relocated(image, symbols, debug)
    require(fifo == codes["radio_fifo"], "TX real FIFO dependency extent changed")
    code = codes["radio_tx"]
    require(bytes(image[a] for a in range(0xdd8, 0xddd)) == b"\0\0\0\0\x22",
            "TX CCA four-clock settle leaf changed")
    require(sum(raw == b"\x12\x0d\xd8" for raw in code.values()) == 1,
            "TX CCA lost its genuine call to the four-clock settle leaf")
    require([raw.hex() for _, raw, _ in peripheral_accesses(code)] == [
        "e5a8", "e5b8", "e59a", "acbe", "e5c6", "b59e02", "e5bf", "e5e9", "e591",
        "e5c6", "75913d", "75e1e9", "75e1e3", "75e1eb", "75e1ea",
    ], "TX exact command/flag/SFR instruction contract changed")
    sites = {}
    for name in ("radio_fifo", "radio_tx"):
        for pc, raw, reg in peripheral_accesses(codes[name]):
            require(symbols.get("_SOC_" + SFRS[reg]) == reg, "TX/FIFO SFR identity changed")
            write = raw[0] in (0x75, 0x89)
            # CJNE A,direct reads the operand without putting it in A.
            observed = reg if write or raw[0] == 0xb5 else raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0
            sites[pc] = ("w" if write else "r", reg, observed)
    static = []
    for pc, raw in code.items():
        if raw[0] != 0x90:
            continue
        a = int.from_bytes(raw[1:], "big")
        if a < 0x1e00:
            continue
        if code.get(pc+3) == b"\x93":
            require(a == 0x2144, "TX unexpected CODE constant reference"); continue
        if a == 0x618d:
            require(code.get(pc+3) == b"\x74\x80" and code.get(pc+5) == b"\xf0",
                    "TX soft shutdown lost exact RXMASKCLR80 write")
            sites[pc+5] = ("w", a, a)
        elif code.get(pc+3) == b"\xf0":
            require(pc == 0x1d5d and a == 0x618a and code.get(pc-3) == b"\x74\x01" and
                    code.get(pc-1) == b"\xf0" and code.get(pc-6) == b"\x90\0\x93",
                    "TX restore must write exact FRMCTRL1=01")
            sites[pc+3] = ("w", a, a)
        else:
            require(code.get(pc+3) == b"\xe0", "TX unexpected static XREG instruction")
            static.append(a); sites[pc+3] = ("r", a, 0xe0)
    require(tuple(static) == XREADS, "TX XREG/identity/FIFO whitelist changed")
    require(code.get(0xf0c) == b"\xe0" and code.get(0x17d2) == b"\xf0", "TX indexed MMIO changed")
    sites[0xf0c] = ("r", None, 0xe0); sites[0x17d2] = ("w", None, None)
    for pc, raw in fifo.items():
        if raw[0] == 0x90 and int.from_bytes(raw[1:], "big") >= 0x1e00:
            a = int.from_bytes(raw[1:], "big")
            require(fifo.get(pc+3) == b"\xe0" and 0x6180 <= a < 0x6200,
                    "FIFO composed static XREG changed")
            sites[pc+3] = ("r", a, 0xe0)
    for name, expected in (
        ("settings", b"".join(a.to_bytes(2, "little") for a in SETTINGS)),
        ("values", bytes((0, 12, 0, 21, 9, 0, 0, 5, 248, 26))),
    ):
        a = cdb_address(debug, f"L:Fradio_tx${name}$0_0$0")
        require(bytes(image[i] for i in range(a, a+len(expected))) == expected, "TX CODE profile changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader+i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "TX real Sleep Timer reader changed")
    for i, off in enumerate(READ_OFFSETS):
        require(symbols.get(f"_SOC_ST{i}") == 0x95+i, "TX ST0/1/2 SFR changed")
        sites[reader+off] = ("r", 0x95+i, 0xe0)
    for name in ("send", "cca"):
        require(f"F:G$radio_tx_{name}_init$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug,
                "TX byte-return ABI changed")
        require(re.search(rf"S:Lradio_tx.radio_tx_{name}_init\$diagnostics\$[^(]+\(\{{2\}}DX,ST", debug),
                "TX diagnostics is not an XDATA-qualified pointer")
    require(re.search(r"S:Lradio_fifo.radio_fifo_preload_init\$body\$[^(]+\(\{3\}DG,SC:U\)", debug),
            "TX FIFO body generic-pointer ABI changed")
    for module in ("radio_tx", "test_radio_tx"):
        fields(debug, module, "__00000000", FIELDS, SIZES)
    fields(debug, "radio_fifo", "__00000000", FIFO_FIELDS, FIFO_SIZES)
    fields(debug, "test_radio_tx", "__00000001", FIFO_FIELDS, FIFO_SIZES)
    fields(debug, "test_radio_tx", "__00000002", ("before", "d", "after"), (4, 29, 4))
    for name, (a, n) in OBJECTS.items():
        symbol = "_radio_tx_test_" + name
        declarations = re.findall(rf"^S:G\${symbol[1:]}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(symbols.get(symbol) == a and declarations and all(int(x) == n for x in declarations)
                and set(range(a, a+n)) <= allocated, "TX caller object layout/ABI changed")
    for name, a in {
        "_radio_tx_fault": 103, "_radio_tx_reserved_end": 172, "__gptrput_PARM_2": 373,
        "s_SSEG": 0x59, "l_XSEG": 374, "_radio_tx_test_before": 0x1ef4,
        "_radio_tx_test_done": 0x2057, "_radio_tx_test_cycle": 0x1ef4,
        "s_DSEG": 0, "l_DSEG": 126, "s_OSEG": 68, "l_OSEG": 21,
        "l_ISEG": 0, "s_BSEG_BYTES": 32, "l_BSEG_BYTES": 1, "l_BSEG": 6,
        "s_REG_BANK_0": 0, "l_REG_BANK_0": 8,
        "l_REG_BANK_1": 0, "l_REG_BANK_2": 0, "l_REG_BANK_3": 0,
    }.items():
        require(symbols.get(name) == a, "TX linked boundary/stack changed: "+name)
    require(image[0x1ef4] == image[0x2057] == 0, "TX checkpoints are not NOPs")
    return allocated, sites


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for a in image:
        with case.assertRaisesRegex(ValueError, "instructions"):
            verify_code(image | {a: image[a] ^ 1})
    for name, value in (
        ("_SOC_RFST", 0xd9), ("_SOC_RFIRQF1", 0xe9), ("_SOC_RFD", 0xe1),
        ("_radio_tx_reserved_end", 100), ("__gptrput_PARM_2", 400),
        ("_radio_tx_test_guard", 0x1f00), ("_radio_tx_test_result", 0x1f00),
        ("l_XSEG", 449), ("l_XABS", 1), ("l_XISEG", 1), ("l_PSEG", 1),
        ("s_SSEG", 128), ("__XPAGE", 0xa0), ("_radio_tx_test_done", 0),
        ("l_OSEG", 20), ("l_DSEG", 125), ("l_BSEG", 7), ("l_ISEG", 1), ("l_REG_BANK_1", 8),
    ):
        with case.assertRaises(ValueError):
            verify(image, symbols | {name: value}, debug, memory, listings)
    for old, new in (
        ("({29}ST", "({30}ST"), ("{12}S:S$txdone", "{13}S:S$txdone"),
        ("({2}DX,ST", "({3}DG,ST"), ("C$radio_tx.c$", "C$absent.c$"),
        ("L:Lradio_tx.operate$w$1_0$18:83", "L:Lradio_tx.operate$w$1_0$18:84"),
    ):
        require(old in debug, "TX ABI mutation did not apply")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    for name in listings:
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {name: ""})
    with case.assertRaises(ValueError):
        verify(image | {0x8000: 0}, symbols, debug, memory, listings)
    with case.assertRaises(ValueError):
        verify(image, symbols, debug, memory.replace("bytes available", "bytes absent"), listings)


def trace_contract(step):
    events = step["events"]
    strobes = [v for k, a, v in events if (k, a) == ("w", 0xe1)]
    require(not any(k == "r" and a == 0xd9 for k, a, _ in events), "TX unexpectedly consumed RX frames")
    if step["action"] == 2 and step["result"] == 0:
        require([v for k, a, v in events if (k, a) == ("w", 0xd9)] ==
                [step["length"]+2]+[i ^ 0x69 for i in range(step["length"])] and not strobes,
                "Real FIFO preload lost exact PHR/payload/no-TX contract")
    if step["action"] < 2:
        require(all(v in (0xe3, 0xe9, 0xea, 0xeb) for v in strobes) and
                not any(k == "w" and a == 0xd9 for k, a, _ in events),
                "TX/CCA bypassed explicit FIFO ownership or added cleanup/retry")
        if step["action"] == 1:
            require(all(v in (0xe3, 0xeb) for v in strobes), "CCA-only issued TX")
        if step["result"] == 14:
            require(not any(k == "w" for k, _, _ in events), "Unprepared TX acquired a hardware write")
        if step["result"] <= 2:
            expected = [0xe3, 0xeb] if step["action"] else [0xe9] if step["mode"] == 1 else [0xe3, 0xea]
            require(strobes == expected and not step["fault"], "TX successful command sequence changed")
    if step["action"] == 3:
        require(strobes in ([], [0xed], [0xee], [0xed, 0xee]), "FIFO cleanup acquired RF/retry")


def execute(simulator, path, symbols, allocated, sites, vector):
    before, done = symbols["_radio_tx_test_before"], symbols["_radio_tx_test_done"]
    current = GUARD_SFRS | {int(a): v for a, v in vector["initial"].items()}
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
    commands += [f"break {pc:#x}" for pc in list(sites)+[before, done]]
    commands += [f"run {symbols['_main']:#x} {before:#x}"]
    records, number = [], 10
    for index, step in enumerate(vector["steps"]):
        trace_contract(step)
        if index:
            commands += ["step 1", "run"]
        commands += [marker(number), "state"]
        start = number; number += 1
        inputs = {}
        for name in INPUTS:
            a, n = OBJECTS[name]
            value = step[name]
            if name == "target":
                value = {65535: OBJECTS["guard"][0]+4, 65534: symbols["__gptrput_PARM_2"],
                         65533: symbols["_radio_tx_reserved_end"]}.get(value, value)
            inputs[name] = value.to_bytes(n, "little")
            commands.append(f"set memory xram {a:#x} "+" ".join(hex(b) for b in inputs[name]))
        commands.append("step 1")
        first = number
        for kind, a, value in step["events"]:
            memory = "sfr" if a < 256 else "xram"
            commands += ["run", marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r":
                commands.append(f"set memory {memory} {a:#x} {value:#x}")
            commands += [marker(number+1), "step 1"]
            if kind == "r":
                commands += ["dump /h sfr 0xe0 0xe0", "dump /h iram 4 4", f"dump /h {memory} {a:#x} {a:#x}"]
            else:
                commands.append(f"dump /h {memory} {a:#x} {a:#x}")
            commands.append(marker(number+2)); number += 3
            current[a] = value
        commands += ["run"] + snapshot_commands(number)
        commands += [marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5)]
        records.append((step, start, first, number, dict(current), inputs))
        number += 6
    require(number < 65536, "TX transcript exceeds bounded marker namespace")
    text = simulate(simulator, commands, path)
    # Index once, including snapshots: no full-transcript search per MMIO event.
    pieces = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    markers = [int(pieces[i], 16) for i in range(1, len(pieces), 2)]
    require(len(markers) == len(set(markers)), "Duplicate TX simulator marker")
    blocks = dict(zip(markers, pieces[2::2]))
    peak = 0
    for step, start, first, final, current, inputs in records:
        check_pc(blocks[start], before)
        for i, (kind, a, value) in enumerate(step["events"]):
            n = first+3*i
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", blocks[n])
            require(match is not None and int(match[1], 16) in sites, vector["name"]+": unplanned MMIO stop")
            pc = int(match[1], 16)
            actual_kind, actual_address, observed = sites[pc]
            require(kind == actual_kind and actual_address in (a, None), vector["name"]+": wrong MMIO sequence")
            regs = memory_dump(blocks[n], 0x81, 3); peak = max(peak, regs[0])
            if a >= 256:
                require(regs[1:] == a.to_bytes(2, "little"), "TX actual MOVX address changed")
                if actual_address is None: require(a in SETTINGS, "TX indexed access escaped profile")
            if observed is None: observed = a
            require(memory_dump(blocks[n+1], observed, 1)[0] == value, vector["name"]+": wrong MMIO value")
        check_pc(blocks[final], done)
        ram = memory_dump(blocks[final], 0, 0x1f00)
        iram = memory_dump(blocks[final+1], 0, 256); sfr = memory_dump(blocks[final+2], 0x80, 128)
        require(ram[0x1e00:0x1e08] == b"TXO1\x01\x08\0\0", "TX status ABI changed")
        require(ram[symbols["_radio_tx_test_return"]] == step["result"] and
                ram[symbols["_radio_tx_fault"]] == step["fault"], "TX return/fault differs from native")
        for name, sizes in (("diagnostics", SIZES), ("fifo", FIFO_SIZES)):
            a = OBJECTS["guard"][0]+4 if name == "diagnostics" else OBJECTS[name][0]
            require(len(step[name]) == len(sizes), "TX serialized native diagnostic ABI changed")
            expected = b"".join(v.to_bytes(s, "little") for v, s in zip(step[name], sizes))
            require(ram[a:a+len(expected)] == expected, vector["name"]+": "+name+" publication mismatch")
        a = OBJECTS["guard"][0]
        require(ram[a:a+4] == ram[a+33:a+37] == b"\x69"*4, "TX diagnostic overrun")
        a = OBJECTS["body"][0]
        require(ram[a:a+125] == bytes(i ^ 0x69 for i in range(125)), "TX changed caller payload/tail")
        for name, data in inputs.items():
            a = OBJECTS[name][0]
            require(ram[a:a+len(data)] == data, "TX changed caller inputs")
        require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
                "TX changed unallocated/reserved/status/alias XDATA")
        require(iram[128:] == b"\xc7"*128 and sfr[1] == symbols["s_SSEG"]+1 and peak < 128,
                "TX upper-IRAM/stack guard/unwind failed")
        for a, v in current.items():
            if a < 256: require(sfr[a-0x80] == v, f"TX changed guarded SFR {a:02x}")
        radio = memory_dump(blocks[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(radio, 0x6000)),
                "TX changed RF RAM/registers beyond explicit synthetic effects")
    return peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "radio_tx_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_text(); memory = path.with_suffix(".mem").read_text()
    listings = {name: (args.output / f"radio_tx_test.{name}.rst").read_text()
                for name in ("timebase", "radio_fifo", "radio_tx", "test_radio_tx")}
    allocated, sites = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    result = subprocess.run([str(args.output / "host-radio-tx-tests"), "--vectors"],
                            capture_output=True, text=True, timeout=15, check=True)
    vectors = [json.loads(line) for line in result.stdout.splitlines()]
    require(len(vectors) == 51, "TX native scenario inventory changed")
    peak = max(execute(args.simulator, path, symbols, allocated, sites, v) for v in vectors)
    operations = sum(len(v["steps"]) for v in vectors)
    events = sum(len(s["events"]) for v in vectors for s in v["steps"])
    print(f"TX/CCA: {len(image)} CODE, {len(allocated)-8} ordinary XDATA +64 reserved; "
          f"stack start {symbols['s_SSEG']:02X}, MMIO peak {peak:02X}. "
          f"{len(vectors)} sequences/{operations} calls/{events} genuine linked MMIO events; "
          "whole CODE/ABI/prefix, published FIFO equivalence, alias/stack guards PASS. "
          "Synthetic only, NOT RF/timing/board evidence; NEVER flash.")


if __name__ == "__main__":
    main()
