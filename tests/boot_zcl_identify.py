#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify and execute the genuine bounded Identify/ZCL composition; no RF."""

import argparse
import re
import unittest
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_nwk_candidates import label, listing_metrics, records
from boot_zcl_basic import canonical, private_span, sha
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("zcl_identify", "zcl_dispatch", "zcl_attributes", "zcl_frame", "zcl_value", "zcl_identify_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_zcl_identify.c",)
CODE_SIZE, CODE_BUDGET, XDATA_BUDGET = 21792, 24576, 1536
CODE_SHA = "d9b2bf29d2edf7c7994c28bf952b0707bbdda6bbd25d8ef2e0ff9dd22e3539fe"
CDB_SHA = "7b74474d63e1a0bd6786b0cb5fdbbc850e9e05398eb1bb7d4700411b305e6f2e"
MAP_SHA = "c60ca2fcd564c0db1c36155d1bd2a79210883c6c1ce3edbb5ca92754f1552552"
# Complete ordered instruction records and complete raw immediate snapshots.
LISTINGS = {
    "zcl_identify": (1617, 2487, "db4679b2ee83d4b1bb9eef6d68d3d71931198b8b8947033fcb944dafcfbfccf0"),
    "zcl_dispatch": (1800, 2864, "33af38cbd8b389d8fb0ebd4411cb36bbf73cf2a4f9c2bca6a0a662e5107dee21"),
    "zcl_attributes": (1284, 2086, "0523515a0c0ae0084e3f1b97f20ae6a43b5f567c75d4b382d8d200b893c9a171"),
    "zcl_frame": (663, 1173, "04885531b362fa26cc7aaa60e32a1db7cf52c0beec959bf7130f990cdf286b91"),
    "zcl_value": (1006, 1710, "ea0b9d01c41d2cc87ed183b93f9725b3839281f50147038f5509126f820718a0"),
    "zcl_identify_test": (5929, 10225, "a399ea9336016357f1527d8e8d2c7076307ee31c0bef49472d6c589f0d58a66d"),
}
LISTING_SHA = {
    "zcl_identify": "f1ecdfd9f7d97c1770d0a41022e0db712adcee6842f9f5200864cc2a3b2484a2",
    "zcl_dispatch": "240d4690a5da6bf0b59a1a4c798580c2668a03903a2d0c36613d71c4f741304b",
    "zcl_attributes": "656f95b358d058ba266f7f4d9dc9c24389adfb5d90856ff686e8756cb8c5e76f",
    "zcl_frame": "a4f1a0bdc5167217c529191c68abf022e56ad69058a12f975c1070f414d6363e",
    "zcl_value": "7d9da99ed59cf2ab3872463e22c387ebbb42107021f5005bc3ce41db931ca6ff",
    "zcl_identify_test": "1bfd11bf0e1377358b4718b1e2e59608ecfed7280f2bb6029e5949f3db935771",
}
# All area records, including zero areas, flags, addresses and ordering.
AREAS_SHA = {
    "zcl_identify": "9cab1af72f9aa5d7204675dfefdcd50b7e8fa2137027d0887ffa9a004fca05d8",
    "zcl_dispatch": "f1bdc58b1979b2daeccfc17c1518d80eab7a7700885f6cc5ed5ffc6ffb287bc1",
    "zcl_attributes": "c8a8963ce9e7fc1f3888df44194b6239379b0b04f44ed495dbe06d505632f11e",
    "zcl_frame": "0ce8e9216057ea5573059399bbdc0216189aaa8132365c419e209d01ac591177",
    "zcl_value": "29c2bbdac508133f443c8524a9afca7c7148c2a16e05b1e0a983a95629af03e8",
    "zcl_identify_test": "3018d404ee721bcdaaa8db3ac2dca9917665834b697cdd936b1c78a23fb5a3e1",
}
CALLER = {
    "ctx": (505, 8), "saved": (513, 8), "other": (521, 8), "info": (529, 10),
    "frame": (539, 9), "value": (548, 5), "request": (553, 102),
    "response": (655, 102), "encoded": (757, 1), "cases": (758, 2),
}
RUNTIME = {
    "__divuint_PARM_2": 790, "__divulong_PARM_2": 797,
    "___memcpy_PARM_2": 810, "___memcpy_PARM_3": 813,
    "_memset_PARM_2": 818, "_memset_PARM_3": 819, "__gptrput_PARM_2": 821,
    "__mulint_PARM_2": 822, "__mullong_PARM_2": 824,
    "_memcmp_PARM_2": 828, "_memcmp_PARM_3": 831,
}
PUBLIC = {
    "zcl_id_init": ("zcl_identify", 0x395),
    "zcl_id_tick": ("zcl_identify", 0x40d),
    "zcl_id_rx": ("zcl_identify", 0x699),
    "zcl_dispatch_unicast": ("zcl_dispatch", 0xf82),
    "zcl_attr_set_check": ("zcl_attributes", 0x1549),
    "zcl_read_attrs_unicast": ("zcl_attributes", 0x1793),
    "zcl_frame_decode": ("zcl_frame", 0x1d6f),
    "zcl_frame_encode": ("zcl_frame", 0x1f6f),
    "zcl_value_type_supported": ("zcl_value", 0x2342),
    "zcl_value_decode": ("zcl_value", 0x2426),
    "zcl_value_encode": ("zcl_value", 0x25c7),
}
PARAMS = {
    "_zcl_id_init_PARM_2": 10, "_zcl_id_tick_PARM_2": 17,
    "_zcl_id_rx_PARM_2": 83, "_zcl_id_rx_PARM_3": 87, "_zcl_id_rx_PARM_4": 90,
    "_zcl_id_rx_PARM_5": 92, "_zcl_id_rx_PARM_6": 95, "_zcl_id_rx_PARM_7": 97,
}
FIELDS = (
    ((0, "type", 1), (1, "flags", 1), (2, "manufacturer_code", 2), (4, "sequence", 1), (5, "command_id", 1)),
    ((0, "header", 6), (6, "ignored_control_bits", 1), (7, "payload_offset", 1), (8, "payload_length", 1)),
    ((0, "type", 1), (1, "string_non_value", 1), (2, "data", 3), (5, "data_length", 2)),
    ((0, "type", 1), (1, "data_offset", 1), (2, "data_length", 1), (3, "encoded_length", 1), (4, "non_value_pattern", 1)),
    ((0, "id", 2), (2, "readable", 1), (3, "value", 7)),
    ((0, "attributes", 3), (3, "count", 1), (4, "side", 1), (5, "manufacturer_specific", 1), (6, "manufacturer_code", 2)),
    ((0, "length", 1), (1, "command_id", 1), (2, "requested_count", 1), (3, "returned_count", 1)),
    ((0, "kind", 1), (1, "length", 1), (2, "command_id", 1), (3, "sequence", 1),
     (4, "requested_count", 1), (5, "returned_count", 1), (6, "discovery_complete", 1),
     (7, "default_command", 1), (8, "default_status", 1), (9, "default_raw_status", 1)),
    ((0, "stamp", 4), (4, "remaining", 2), (6, "phase", 2)),
)


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA,
            "Identify complete raw CDB changed (checked BEFORE decode)")
    debug = debug_raw.decode("ascii")
    require(CODE_SIZE <= CODE_BUDGET and sha(code_bytes(image, CODE_SIZE)) == CODE_SHA,
            "Identify complete CODE identity changed")
    require(sha(canonical(symbols)) == MAP_SHA, "Identify complete parsed map identity changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "zcl_id_result", SOURCES, xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 836 and symbols["s_SSEG"] == 0x54,
            "Identify ordinary storage/stack changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
                       memory) == [("0x0000", "0x0343", "836", "7680")],
            "Identify memory accounting changed")
    require(set(listings) == set(objects) == set(MODULES), "Identify composition changed")
    instructions, coverage = {}, set()
    for m in MODULES:
        require(sha(listings[m]) == LISTING_SHA[m], f"Identify complete immediate listing changed: {m}")
        text = listings[m].decode("ascii")
        require(listing_metrics(text) == LISTINGS[m], f"Identify ordered instruction records changed: {m}")
        for address, data in records(text):
            span = set(range(address, address + len(data)))
            require(not coverage & span and all(image.get(address+i) == b for i, b in enumerate(data)),
                    "Identify duplicate/overlapping/non-linked instructions")
            coverage.update(span)
            instructions[address] = data
        areas = re.findall(r"^A (\S+) size (\S+) flags (\S+) addr (\S+)$", objects[m], re.MULTILINE)
        require(sha(canonical(areas)) == AREAS_SHA[m], f"Identify exact module allocations changed: {m}")
    for i, expected in enumerate(FIELDS):
        found = re.findall(rf"^T:Fzcl_identify\$__{i:08d}\[(.*)\]$", debug, re.MULTILINE)
        require(len(found) == 1, "Identify missing/duplicate field record")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected, "Identify field ABI changed")
    for name, spec in (
        ("ctx", "{3}DG,ST__00000008:S"), ("now", "{4}SL:U"),
        ("request", "{3}DG,SC:U"), ("length", "{2}SI:U"),
        ("response", "{3}DG,SC:U"), ("capacity", "{2}SI:U"),
        ("info", "{3}DG,ST__00000007:S"), ("next", "{8}ST__00000008:S"),
    ):
        require(re.search(r"^S:Lzcl_identify.zcl_id_rx\$" + name + r"\$[^(]+\("
                          + re.escape(spec) + r"\),F,0,0$", debug, re.MULTILINE),
                f"Identify argument/staging ABI changed: {name}")
    require(len({n[:32] for n in PARAMS}) == len(PARAMS)
            and all(symbols.get(k) == v for k, v in PARAMS.items()), "Identify parameter map ABI changed")
    require(private_span(debug, "|".join(MODULES[:-1])) == set(range(505)),
            "Identify compiler-private prefix changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zcl_identify${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                f"Identify caller storage changed: {name}")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "Identify caller objects overlap")
        caller.update(span)
    require(caller == set(range(505, 760)) and private_span(debug, "test_zcl_identify") == set(range(760, 790)),
            "Identify caller/local/runtime boundaries changed")
    require(all(symbols.get(k) == v for k, v in RUNTIME.items()),
            "Identify libc/compiler scratch moved into caller storage")
    require(allocated == set(range(836)) | set(range(0x1e00, 0x1e08)),
            "Identify unaccounted ordinary/runtime/status allocation")
    for name, (m, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") == address
                and label(listings[m].decode("ascii"), name) == address and address in instructions,
                "Identify function address ABI changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "Identify result return ABI changed")
    require(symbols["_main"] == 0x505d and symbols["_zcl_id_done"] == 0x5096
            and code_bytes(image, CODE_SIZE)[0x5096:0x5099] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == 0x5099
            and instructions.get(0x5096) == b"\0", "Identify exact checkpoint changed")

    def calls(prefix):
        lo = cdb_address(debug, "L:" + prefix + "$0$0")
        hi = cdb_address(debug, "L:X" + prefix + "$0$0")
        return {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                if lo <= a <= hi and len(b) == 3 and b[0] == 0x12}

    for prefix, targets in (
        ("Fzcl_identify$advance", ("__divulong",)),
        ("G$zcl_id_init", ("_memset",)),
        ("Fzcl_identify$global", ("_zcl_dispatch_unicast",)),
        ("G$zcl_id_rx", ("_zcl_frame_decode", "_zcl_frame_encode")),
        ("G$zcl_dispatch_unicast", ("_zcl_attr_set_check", "_zcl_frame_decode", "_zcl_read_attrs_unicast", "_zcl_frame_encode")),
        ("G$zcl_read_attrs_unicast", ("_zcl_value_encode", "_zcl_frame_encode", "_zcl_frame_decode")),
        ("Fzcl_dispatch$discover", ("_zcl_value_type_supported", "_zcl_frame_encode")),
    ):
        require(all(symbols[t] in calls(prefix) for t in targets), f"Identify bypasses actual calls in {prefix}")
    require(0x62 in calls("G$zcl_id_tick") and {0x62, 0x4a1} <= calls("G$zcl_id_rx"),
            "Identify bypasses real countdown/global view")
    require(all(symbols["_" + n] in calls("Ftest_zcl_identify$golden_cases")
                for n in ("zcl_id_init", "zcl_id_rx", "zcl_id_tick",
                          "zcl_frame_decode", "zcl_frame_encode", "zcl_value_decode")),
            "Identify corpus bypasses genuine public functions")
    return allocated


def artifact_negatives(image, symbols, debug_raw, memory, listings, objects):
    count = 0
    case = unittest.TestCase()

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug_raw=debug_raw, memory=memory,
                    listings=listings, objects=objects)
        args.update(changes)
        with case.assertRaises(ValueError):
            verify(**args)
        count += 1

    for a in (0, CODE_SIZE-1, 0x62, 0x4a1, 0x5096, symbols["__divulong"],
              symbols["___memcpy"], *(v[1] for v in PUBLIC.values())):
        reject(image=image | {a: image[a] ^ 1})
    reject(image={a: b for a, b in image.items() if a != CODE_SIZE-1})
    reject(image=image | {CODE_SIZE: 0})
    for name in (*RUNTIME, *PARAMS, "_zcl_id_done", "_zcl_id_result", "s_SSEG", "l_XSEG"):
        reject(symbols=symbols | {name: symbols[name]+1})
    reject(symbols=symbols | {"unreviewed_extra_symbol": 0})
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fzcl_identify", b"S:Lzcl_identify",
                   b"L:Lzcl_identify", b"S:Ftest_zcl_identify", b"L:C$test_zcl_identify",
                   b"F:Fzcl_dispatch", b"S:Lzcl_value"):
        found = [line for line in debug_raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(found, f"Missing CDB negative prerequisite: {prefix!r}")
        reject(debug_raw=debug_raw.replace(found[0], b"", 1))
        reject(debug_raw=debug_raw + found[0])
    reject(debug_raw=debug_raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=debug_raw + b"\xff")
    reject(debug_raw=debug_raw.replace(b"{4}S:S$remaining", b"{3}S:S$remaining", 1))
    reject(memory=memory.replace("172 bytes available", "171 bytes available"))
    reject(memory=memory.replace("836", "837"))
    for m in MODULES:
        reject(listings=listings | {m: b""})
        lines = listings[m].splitlines(keepends=True)
        first, second = [i for i, line in enumerate(lines) if records(line.decode("ascii"))][:2]
        for op in ("delete", "duplicate", "reorder", "mutate"):
            changed = lines.copy()
            if op == "delete":
                del changed[first]
            elif op == "duplicate":
                changed.insert(first, lines[first])
            elif op == "reorder":
                changed[first], changed[second] = changed[second], changed[first]
            else:
                changed[first] = changed[first].replace(b" ", b"\t", 1)
            reject(listings=listings | {m: b"".join(changed)})
        reject(objects=objects | {m: ""})
        reject(objects=objects | {m: objects[m].replace("A XSEG size ", "A ISEG size ", 1)})
    reject(listings={m: v for m, v in listings.items() if m != "zcl_identify"})
    return count


def check_result(ram, iram, sfr, allocated):
    require(ram[0x1e00:0x1e08] == b"ZID1\x01\x08\0\0",
            "Identify target corpus failed/result ABI changed")
    require(ram[758:760] == b"\x49\0", "Identify skipped common scenarios")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Identify writes outside ordinary allocation/eight-byte result/status-tail guard")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x53,
            "Identify SP7C/IRAM alias guard or checkpoint unwind failed")
    require(all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Identify enabled interrupts")


def check_peak(text):
    found = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(found) == 1 and int(found[0], 16) == 0x70 <= 0x7c, "Identify full-run SP peak changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "zcl_identify_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text(encoding="ascii"))
    debug_raw = path.with_suffix(".cdb").read_bytes()
    memory = path.with_suffix(".mem").read_text(encoding="ascii")
    listings = {m: (args.output / f"zcl_identify_test.{m}.rst").read_bytes() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text(encoding="ascii") for m in MODULES}
    allocated = verify(image, symbols, debug_raw, memory, listings, objects)
    negatives = artifact_negatives(image, symbols, debug_raw, memory, listings, objects)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaises(ValueError):
        check_alias(args.simulator, False)
    start, stop = symbols["_main"], symbols["_zcl_id_done"]
    setup = [ALIAS, "fill xram 0 0x1eff 0xa5",
             "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0"]
    text = simulate(args.simulator, setup + [
        f"run 0 {start:#x}", "fill iram 0x7d 0xff 0xc7", f"run {start:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    check_result(ram, iram, sfr, allocated)
    full = simulate(args.simulator, setup + [f"run 0 {stop:#x}"] + snapshot_commands(1), path)
    check_pc(section(full, 1), stop)
    check_peak(section(full, 1))  # independent uninterrupted reset-to-checkpoint run
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 758), (0, 836),
                            (0, 0x1dff), (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0xa8-0x80)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated)
    for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x53"):
        with unittest.TestCase().assertRaises(ValueError):
            check_peak(bad)
    print(f"Identify: {CODE_SIZE}/{CODE_BUDGET} CODE, 836+64/{XDATA_BUDGET} reserved XDATA; "
          f"73 common scenarios, full-run SP70/cap7C, checkpoint SP53; complete raw CDB/map/CODE, "
          f"6 immediate snapshots, {negatives} artifact +9 guard +3 peak +1 alias negatives PASS "
          "(simulation only; no indicator, network or hardware time).")


if __name__ == "__main__":
    main()
