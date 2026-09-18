#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Passive RX linked execution against strict synthetic host-MMIO traces; no USB."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate, snapshot,
    snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS, READ_OFFSETS, READER_BYTES
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import instructions
from radio_rx_fixture import verify_fscal1_readback
from verify_firmware import cdb_address, parse_ihex, parse_symbols, peripheral_accesses, require


IMAGE_SIZE = 5214
IMAGE_SHA256 = "a69606bca743a8e660d824d920211dfdf9b3467f175f251e117c5c01539dd41e"
LENGTHS = PRNG_LENGTHS | {0x1c: 1, 0x23: 1, 0x2b: 1, 0x3c: 1, 0x52: 2, 0xa4: 1}
FIELDS = (
    "elapsed_ticks", "polls", "timebase_status", "phase", "writes", "verified", "actions", "sample_valid",
    "rx_enable", "fsm0", "signals", "rx_count", "tx_count", "rx_first", "rx_last", "rx_packet",
    "tx_first", "tx_last", "errors", "flags0", "flags1", "rssi_valid",
    "bytes_read", "phr", "rssi_raw", "crc_correlation", "discarded_bytes",
)
SIZES = (4, 2) + (1,) * 25
XREADS = (
    0x624a, 0x61e1, 0x6189, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61a9, 0x61b8, 0x61b9,
    0x618b, 0x6192, 0x6193, 0x619b, 0x619c, 0x619d, 0x619e, 0x619f, 0x61a1, 0x61a2, 0x6199,
)
SETTINGS = (0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f)
VALUES = bytes((0x40, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0, 0))
OBJECTS = (("frame", 0xcf, 128), ("diagnostics", 0x14f, 31), ("channel", 0x16e, 1),
           ("return", 0x16f, 1), ("timeout", 0x170, 4), ("limit", 0x174, 2))


def verify_code(image):
    require(set(image) == set(range(IMAGE_SIZE)), "RX exact CODE extent changed")
    require(hashlib.sha256(bytes(image[i] for i in range(IMAGE_SIZE))).hexdigest() == IMAGE_SHA256,
            "RX exact linked instructions/constants/runtime changed")


def verify_image(image, symbols, debug, memory, listing):
    allocated = verify_component_layout(image, symbols, debug, memory, "radio_rx_test_result",
                                         ("timebase.c", "radio_rx.c", "test_radio_rx.c"))
    verify_code(image)
    start = cdb_address(debug, "L:Fradio_rx$ordinary$0$0")
    end = cdb_address(debug, "L:XG$radio_rx_receive_init$0$0") + 1
    require((start, end) == (0x1f6, 0x137d), "RX module extent changed")
    code = instructions(image, start, end, LENGTHS)
    listed = {int(m[1], 16): bytes.fromhex(m[2]) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.MULTILINE)}
    require(code == listed, "RX listing differs from actual instructions")
    accesses = peripheral_accesses(code)
    require([data.hex() for _, data, _ in accesses] == [
        "e5a8", "e5b8", "e59a", "acbe", "e5c6", "e59e", "e5bf", "e5e9", "e591",
        "e5c6", "75e1e3", "e5d9", "e5d9", "75e1ed",
    ], "RX SFR effects or exactly-once RFD reads changed")
    sites = {}
    for pc, data, reg in accesses:
        require(symbols.get("_SOC_" + {0xa8: "IEN0", 0xb8: "IEN1", 0x9a: "IEN2", 0xbe: "SLEEPCMD",
            0xc6: "CLKCONCMD", 0x9e: "CLKCONSTA", 0xbf: "RFERRF", 0xe9: "RFIRQF0",
            0x91: "RFIRQF1", 0xe1: "RFST", 0xd9: "RFD"}[reg]) == reg, "RX SFR symbol changed")
        sites[pc] = ("w" if data[0] == 0x75 else "r", reg,
                     ("iram", data[0] - 0xa8) if 0xa8 <= data[0] <= 0xaf else
                     ("sfr", reg if data[0] == 0x75 else 0xe0))
    static_reads = []
    for pc, data in code.items():
        if data[0] != 0x90 or int.from_bytes(data[1:], "big") < 0x1e00:
            continue
        address = int.from_bytes(data[1:], "big")
        if address == 0x618d:
            require(code.get(pc + 3) == b"\x74\x80" and code.get(pc + 5) == b"\xf0",
                    "RX soft-stop is not an exact RXMASKCLR80 write")
            sites[pc + 5] = ("w", address, ("xram", address))
        else:
            require(code.get(pc + 3) == b"\xe0", "RX unexpected static XREG operation")
            static_reads.append(address)
            sites[pc + 3] = ("r", address, ("sfr", 0xe0))
    require(tuple(static_reads) == XREADS, "RX XREG/identity/RAM whitelist changed")
    # These are the sole table-indexed MMIO instructions, not ordinary MOVX scratch.
    require(code.get(0x3a1) == b"\xe0" and code.get(0xc0b) == b"\xf0",
            "RX reviewed dynamic MMIO sites changed")
    sites[0x3a1] = ("r", None, ("sfr", 0xe0))
    sites[0xc0b] = ("w", None, ("xram", None))
    for name, expected in (("settings", b"".join(a.to_bytes(2, "little") for a in SETTINGS)),
                           ("values", VALUES)):
        address = cdb_address(debug, f"L:Fradio_rx${name}$0_0$0")
        require(bytes(image[a] for a in range(address, address + len(expected))) == expected,
                "RX passive configuration table changed")
    verify_fscal1_readback(code, 0x3a1, 0x21, cdb_address(debug, "L:Fradio_rx$values$0_0$0"))
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader + i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "RX real timebase reader changed")
    for i, offset in enumerate(READ_OFFSETS):
        require(symbols.get(f"_SOC_ST{i}") == 0x95 + i, "RX timebase SFR changed")
        sites[reader + offset] = ("r", 0x95 + i, ("sfr", 0xe0))
    require("F:G$radio_rx_receive_init$0_0$0({2}DF,SC:U),Z,0,0,0,0,0" in debug,
            "RX byte-return ABI changed")
    for name in ("output", "d"):
        require(re.search(rf"S:Lradio_rx.radio_rx_receive_init\${name}\$[^(]+\(\{{2\}}DX,ST", debug),
                "RX caller pointer is not XDATA")
    expected = [(sum(SIZES[:i]), name, size) for i, (name, size) in enumerate(zip(FIELDS, SIZES))]
    for module in ("radio_rx", "test_radio_rx"):
        for suffix, fields in (("00", [(0, "length", 1), (1, "rssi_raw", 1),
                                      (2, "correlation", 1), (3, "body", 125)]), ("01", expected)):
            records = re.findall(rf"^T:F{module}\$__000000{suffix}\[(.*)\]$", debug, re.MULTILINE)
            require(records, "Missing RX output ABI")
            for record in records:
                actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
                require([(int(a), b, int(c)) for a, b, c in actual] == fields, "RX output ABI changed")
    for name, address, size in OBJECTS:
        symbol = "_radio_rx_test_" + name
        records = re.findall(rf"^S:G\${symbol[1:]}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(symbols.get(symbol) == address and records and all(int(n) == size for n in records),
                "RX caller object ABI changed")
        require(set(range(address, address + size)) <= allocated, "RX caller object is not allocated")
    require(symbols["_radio_rx_fault"] == 0x19 and symbols["_radio_rx_reserved_end"] == 0xce
            and symbols["__gptrput_PARM_2"] == 0x176 and symbols["s_SSEG"] == 0x34,
            "RX private ownership/helper/stack layout changed")
    for name, address in (("before", 0x137d), ("done", 0x13ce)):
        require(symbols["_radio_rx_test_" + name] == address and image[address] == 0,
                "RX checkpoint is not the reviewed NOP")
    require(symbols["_radio_rx_test_cycle"] == symbols["_radio_rx_test_before"], "RX cycle ABI changed")
    return allocated, sites


def check_rejections(image, symbols, debug, memory, listing):
    case = unittest.TestCase()
    code = instructions(image, 0x1f6, 0x137d, LENGTHS)
    check_fscal1_rejections(code, 0x3a1, 0x21, cdb_address(debug, "L:Fradio_rx$values$0_0$0"))
    for address in image:
        changed = dict(image)
        changed[address] ^= 1
        with case.assertRaisesRegex(ValueError, "instructions"):
            verify_code(changed)
    for name, value in (("_SOC_RFD", 0xe1), ("_SOC_RFST", 0xd9), ("_SOC_RFIRQF0", 0xbf),
                        ("_radio_rx_test_result", 0x1f00), ("_radio_rx_test_frame", 0x6000),
                        ("_radio_rx_reserved_end", 0x80), ("__gptrput_PARM_2", 0x400),
                        ("l_XSEG", 449), ("l_XABS", 1), ("l_PSEG", 1), ("l_XISEG", 1),
                        ("__XPAGE", 0xa0), ("s_SSEG", 0x80), ("_radio_rx_test_done", 0)):
        with case.assertRaises(ValueError):
            verify_image(image, symbols | {name: value}, debug, memory, listing)
    for changed in (debug.replace("({31}ST", "({32}ST"),
                    debug.replace("{27}S:S$phr", "{28}S:S$phr"),
                    debug.replace("({2}DX,ST", "({3}DG,ST"),
                    debug.replace("C$radio_rx.c$", "C$missing.c$")):
        require(changed != debug, "RX ABI mutation did not apply")
        with case.assertRaises(ValueError):
            verify_image(image, symbols, changed, memory, listing)
    for im, mem, lst in ((image | {0x8000: 0}, memory, listing),
                         (image, memory.replace("bytes available", "bytes absent"), listing),
                         (image, memory, "")):
        with case.assertRaises(ValueError):
            verify_image(im, symbols, debug, mem, lst)


def check_fscal1_rejections(code, read, scratch, values):
    end = verify_fscal1_readback(code, read, scratch, values)
    case = unittest.TestCase()
    # Bypass the image hash deliberately to exercise the independent mask
    # analysis itself, including the index, both masks and the comparison.
    for pc, data in code.items():
        if not read <= pc < end:
            continue
        for i in range(len(data)):
            changed = bytearray(data); changed[i] ^= 1
            with case.assertRaisesRegex(ValueError, "FSCAL1"):
                verify_fscal1_readback(code | {pc: bytes(changed)}, read, scratch, values)


def check_fscal1_trace(v, result):
    """Independent expected FSCAL1 events/results within shared-model replay."""
    if not v["name"].startswith("FSCAL1 "):
        return
    events = v["events"]
    require(v["initial"]["25006"] == 0x2b, "FSCAL1 trace lost reset VCO_CURR/reserved history")
    require([e for e in events if e[:2] == ["w", 0x61ae]] == [["w", 0x61ae, 0]],
            "FSCAL1 must still be written exactly once as 00")
    strobes = [(i, e[2]) for i, e in enumerate(events) if e[:2] == ["w", 0xe1]]
    if v["name"] == "FSCAL1 configuration low bits":
        require(not strobes and result == 7 and
                [e[2] for e in events if e[:2] == ["r", 0x61ae]] == [0x31],
                "FSCAL1 configuration low-bit failure was waived")
        return
    require(strobes and strobes[0][1] == 0xe3, "Missing genuine E3 in FSCAL1 trace")
    on = strobes[0][0]
    before = [e[2] for e in events[:on] if e[:2] == ["r", 0x61ae]]
    after = [e[2] for e in events[on:] if e[:2] == ["r", 0x61ae]]
    require(before == [0]*3 and after, "FSCAL1 write/entry readback history changed")
    if v["name"] == "FSCAL1 postcal low bits":
        require(len(after) == 1 and after[0] in (0x31, 0x32, 0x33) and result == 7 and
                strobes == [(on, 0xe3)] and not any(e[:2] == ["r", 0xd9] for e in events),
                "FSCAL1 postcal low-bit failure published/read/continued")
    else:
        expected = {"FSCAL1 postcal 30": 0x30, "FSCAL1 postcal FC": 0xfc}[v["name"]]
        require(set(after) == {expected} and result == 0 and [n for _, n in strobes] == [0xe3, 0xed],
                "FSCAL1 reserved bits blocked the genuine receive/stop/flush")


def execution(simulator, path, symbols, allocated, sites, v):
    check_fscal1_trace(v, v["result"])
    before, done = (symbols["_radio_rx_test_" + name] for name in ("before", "done"))
    current = GUARD_SFRS | {int(a): n for a, n in v["initial"].items()}
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7"]
    for address, value in current.items():
        commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
    commands.append(f"run {symbols['_main']:#x} {before:#x}")
    for name, size in (("channel", 1), ("timeout", 4), ("limit", 2)):
        commands.append(f"set memory xram {symbols['_radio_rx_test_' + name]:#x} " +
                        " ".join(hex(b) for b in v[name].to_bytes(size, "little")))
    output, diag = symbols["_radio_rx_test_frame"], symbols["_radio_rx_test_diagnostics"]
    commands.append(f"fill xram {diag:#x} {diag + 30:#x} 0x69")
    commands += [f"break {pc:#x}" for pc in list(sites) + [before, done]]
    commands.append("step 1")
    for index, (kind, address, value) in enumerate(v["events"]):
        number = 10 + 3 * index
        memory = "sfr" if address < 256 else "xram"
        commands += ["run", marker(number), "state", "dump /h sfr 0x81 0x83"]
        if kind == "r":
            commands.append(f"set memory {memory} {address:#x} {value:#x}")
        commands += [marker(number + 1), "step 1"]
        if kind == "r":
            commands += ["dump /h sfr 0xe0 0xe0", "dump /h iram 4 4"]
        else:
            commands.append(f"dump /h {memory} {address:#x} {address:#x}")
        commands.append(marker(number + 2))
        current[address] = value
    final = 12 + 3 * len(v["events"])
    commands += ["run"] + snapshot_commands(final)
    commands += [marker(final + 4), "dump /h xram 0x6000 0x63ff", marker(final + 5)]
    if v["fault"]:
        commands += ["step 1", "run", marker(final + 6), "state", marker(final + 7)]
        for name, size in (("channel", 1), ("timeout", 4), ("limit", 2)):
            address = symbols["_radio_rx_test_" + name]
            commands.append(f"fill xram {address:#x} {address + size - 1:#x} 0")
        commands += ["step 1", "run"] + snapshot_commands(final + 8)
    text = simulate(simulator, commands, path)
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.MULTILINE)
    numbers = [int(parts[i], 16) for i in range(1, len(parts), 2)]
    require(len(numbers) == len(set(numbers)), "Duplicate RX simulator marker")
    blocks = dict(zip(numbers, parts[2::2]))
    peak = 0
    for index, (kind, address, value) in enumerate(v["events"]):
        number = 10 + 3 * index
        require(all(n in blocks for n in range(number, number + 3)), "Missing RX simulator event")
        match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", blocks[number])
        require(match is not None and int(match[1], 16) in sites, v["name"] + ": unplanned MMIO stop")
        pc = int(match[1], 16)
        actual_kind, actual_address, (_, observed) = sites[pc]
        require(kind == actual_kind and actual_address in (None, address), v["name"] + ": wrong MMIO sequence")
        regs = memory_dump(blocks[number], 0x81, 3)
        peak = max(peak, regs[0])
        if address >= 256:
            require(regs[1:] == address.to_bytes(2, "little"), v["name"] + ": wrong actual MOVX address")
            if actual_address is None:
                require(address in SETTINGS, "RX dynamic MMIO outside passive whitelist")
        observed = address if observed is None else observed
        require(memory_dump(blocks[number + 1], observed, 1)[0] == value, v["name"] + ": MMIO value mismatch")
    check_pc(section(text, final), done)
    ram, iram, sfr = snapshot(text, final)
    require(ram[0x1e00:0x1e08] == b"RXO1\x01\x08\0\0", "RX result ABI mismatch")
    require(ram[symbols["_radio_rx_test_return"]] == v["result"]
            and ram[symbols["_radio_rx_fault"]] == v["fault"], v["name"] + ": result/fault mismatch")
    require(ram[output:output + 128] == bytes.fromhex(v["frame"]), v["name"] + ": publication/tail mismatch")
    expected = b"".join(value.to_bytes(size, "little") for value, size in zip(v["diagnostics"], SIZES))
    require(len(v["diagnostics"]) == len(SIZES) and ram[diag:diag + 31] == expected,
            v["name"] + ": diagnostic ABI mismatch")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "RX wrote unallocated/status/alias XDATA")
    require(iram[128:] == b"\xc7" * 128 and sfr[1] == symbols["s_SSEG"] + 1 and peak < 128,
            "RX IRAM stack guard/unwind failed")
    for address, value in current.items():
        if address < 256:
            require(sfr[address - 0x80] == value, f"RX changed guarded SFR {address:02x}")
    radio = memory_dump(section(text, final + 4), 0x6000, 1024)
    require(all(byte == current.get(address, 0x69) for address, byte in enumerate(radio, 0x6000)),
            "RX changed radio RAM/config outside explicit synthetic effects")
    for name, size in (("channel", 1), ("timeout", 4), ("limit", 2)):
        address = symbols["_radio_rx_test_" + name]
        require(ram[address:address + size] == v[name].to_bytes(size, "little"), "RX changed caller input")
    if v["fault"]:
        check_pc(section(text, final + 6), before)
        check_pc(section(text, final + 8), done)
        repeated, repeat_iram, repeat_sfr = snapshot(text, final + 8)
        require(repeated[output:diag + 31] == ram[output:diag + 31]
                and repeated[symbols["_radio_rx_test_return"]] == v["result"]
                and repeat_iram[128:] == b"\xc7" * 128 and repeat_sfr[1] == sfr[1],
                "RX retained fault changed output/diagnostics/result/stack")
    return peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "radio_rx_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix(s).read_text() for s in (".cdb", ".mem"))
    listing = (args.output / "radio_rx.rst").read_text()
    allocated, sites = verify_image(image, symbols, debug, memory, listing)
    check_rejections(image, symbols, debug, memory, listing)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    result = subprocess.run([str(args.output / "host-radio-rx-tests"), "--vectors"],
                            capture_output=True, text=True, timeout=15, check=True)
    vectors = [json.loads(line) for line in result.stdout.splitlines()]
    require(len(vectors) == 33, "RX host trace scenarios changed")
    peak = max(execution(args.simulator, path, symbols, allocated, sites, v) for v in vectors)
    print(f"Passive RX: {len(image)} CODE, {len(allocated) - 8} ordinary XDATA +64 reserved; "
          f"stack start {symbols['s_SSEG']:02X}, observed MMIO peak {peak:02X}. "
          f"{len(vectors)} linked traces, complete CODE/ABI mutation rejection, alias/stack guards PASS. "
          "Synthetic only: not RF, hardware or timing evidence; NEVER flash this image.")


if __name__ == "__main__":
    main()
