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
from boot_zcl_basic import canonical, private_span, sha, verify_write_abi
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("zcl_identify", "zcl_dispatch", "zcl_write", "zcl_attributes", "zcl_frame", "zcl_value", "zcl_identify_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_zcl_identify.c",)
CODE_SIZE, CODE_BUDGET, XDATA_BUDGET = 24340, 24576, 1536
CODE_SHA = "54b291f43bf19a306b0314722553d2f5e089472072072db1de91654d0e6fee25"
CDB_SHA = "ef854655c9db54eb9c8003eb72de73568742f51a38e254ac176bfb558f9d2fc7"
MAP_SHA = "33f9d6aeace45c227ac0e097f4080e8489e498aa7d709ce0ef0475d9f27c6f5d"
# Complete ordered instruction records and complete raw immediate snapshots.
LISTINGS = {
    "zcl_identify": (1967, 3001, "2b45a55114162da1f835a3692925da35b06d2f28049f1999e8c1b0763fbfedd9"),
    "zcl_dispatch": (1942, 3093, "925b03b609154a47ec5fd1c73e682785ed664d6beb4e70689ff2ab50ac40e164"),
    "zcl_write": (1016, 1552, "f80d82a2cd99a1ad7692e8734ebb2b623ac23a45bd9f897fa80d67e45b861f0d"),
    "zcl_attributes": (1284, 2086, "c53c1837ebb5d026261ec4512fc685f9084919d94a716acf20b8ab2462cf3fa1"),
    "zcl_frame": (663, 1173, "c1ebe6ba035ab2659d99d716a646466ea7d5218c553c7b16da58651577574d7e"),
    "zcl_value": (1006, 1710, "a93dac05e24a953813abebb61895e38702c56d8ec317d7aee35e37e6032995d4"),
    "zcl_identify_test": (5556, 9864, "f02615fa4708d9308b39e2a508410d23f227eb9d90b7c31e0402f110232b661b"),
}
LISTING_SHA = {
    "zcl_identify": "e1330c368b78cac650f12f37a127a4a6e526987ff5ff7a2956f64baf108ac588",
    "zcl_dispatch": "545ce18ea22df45db8ea9b841c1c27ad8bf053a9fddd5819801b44a699742ef6",
    "zcl_write": "e06ff3a9215eb168120b80504bf4c1b113b099ec3b66d4522cad3427c6718448",
    "zcl_attributes": "8ee98f4aef1535b2a1d1448a9c1de7e4571059946d95934d5cf8e9c0a416051a",
    "zcl_frame": "396b6a5623aefbd35b68f0a0984cd5cc005c79f1a6008c77de485ad0cb32a5f8",
    "zcl_value": "3bfebdcfb5079b2f39f01b5a1986d9d6aa549ddec18a76d4c583859f3b6d1d2d",
    "zcl_identify_test": "60f3626b6367c421fd77f00a7d97c42f4a2d3a81649427aab54d9ed887dbb1b8",
}
# All area records, including zero areas, flags, addresses and ordering.
AREAS_SHA = {
    "zcl_identify": "7595499c488219c8f0ba3273b6ad21d068c82e40097b3ad67f32693a77715f4d",
    "zcl_dispatch": "0c44f299eab89d9a8cf66c129d2093b09b4238ae39dbe7e43004940ad8b93590",
    "zcl_write": "8ae20fa1fb009f575b1e70ac88d52ccf08397ef524b76eb3c6825796d8849d70",
    "zcl_attributes": "c8a8963ce9e7fc1f3888df44194b6239379b0b04f44ed495dbe06d505632f11e",
    "zcl_frame": "0ce8e9216057ea5573059399bbdc0216189aaa8132365c419e209d01ac591177",
    "zcl_value": "29c2bbdac508133f443c8524a9afca7c7148c2a16e05b1e0a983a95629af03e8",
    "zcl_identify_test": "ed2f8a20faa9960150879844fbb917f8831b49725389502ab51564f32b4d2e1f",
}
CALLER = {
    "ctx": (656, 8), "saved": (664, 8), "other": (672, 8), "info": (680, 10),
    "frame": (690, 9), "value": (699, 5), "request": (704, 102),
    "response": (806, 102), "encoded": (908, 1), "cases": (909, 2),
}
RUNTIME = {
    "__divuint_PARM_2": 960, "__divulong_PARM_2": 967,
    "___memcpy_PARM_2": 980, "___memcpy_PARM_3": 983,
    "_memset_PARM_2": 988, "_memset_PARM_3": 989, "__gptrput_PARM_2": 991,
    "__mulint_PARM_2": 992, "__mullong_PARM_2": 994,
    "_memcmp_PARM_2": 998, "_memcmp_PARM_3": 1001,
}
PUBLIC = {
    "zcl_id_init": ("zcl_identify", 963),
    "zcl_id_tick": ("zcl_identify", 1083),
    "zcl_id_rx": ("zcl_identify", 2258),
    "zcl_dispatch_unicast": ("zcl_dispatch", 4530),
    "zcl_wr_handle": ("zcl_write", 6238),
    "zcl_attr_set_check": ("zcl_attributes", 7790),
    "zcl_read_attrs_unicast": ("zcl_attributes", 8376),
    "zcl_frame_decode": ("zcl_frame", 9876),
    "zcl_frame_encode": ("zcl_frame", 10388),
    "zcl_value_type_supported": ("zcl_value", 11367),
    "zcl_value_decode": ("zcl_value", 11595),
    "zcl_value_encode": ("zcl_value", 12012),
}
PARAMS = {
    "_zcl_id_init_PARM_2": 10, "_zcl_id_tick_PARM_2": 17,
    "_zcl_id_rx_PARM_2": 89, "_zcl_id_rx_PARM_3": 93, "_zcl_id_rx_PARM_4": 96,
    "_zcl_id_rx_PARM_5": 98, "_zcl_id_rx_PARM_6": 101, "_zcl_id_rx_PARM_7": 103,
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
    ((0, "id", 2), (2, "value", 2), (4, "written", 1)),
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
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 1006 and symbols["s_SSEG"] == 0x54,
            "Identify ordinary storage/stack changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
                       memory) == [("0x0000", "0x03ed", "1006", "7680")],
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
    verify_write_abi(debug, symbols, instructions)
    require(private_span(debug, "|".join(MODULES[:-1])) == set(range(656)),
            "Identify compiler-private prefix changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zcl_identify${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                f"Identify caller storage changed: {name}")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "Identify caller objects overlap")
        caller.update(span)
    require(caller == set(range(656, 911)) and private_span(debug, "test_zcl_identify") == set(range(911, 960)),
            "Identify caller/local/runtime boundaries changed")
    require(all(symbols.get(k) == v for k, v in RUNTIME.items()),
            "Identify libc/compiler scratch moved into caller storage")
    require(allocated == set(range(1006)) | set(range(0x1e00, 0x1e08)),
            "Identify unaccounted ordinary/runtime/status allocation")
    for name, (m, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") == address
                and label(listings[m].decode("ascii"), name) == address and address in instructions,
                "Identify function address ABI changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "Identify result return ABI changed")
    require(symbols["_main"] == 22553 and symbols["_zcl_id_done"] == 22610
            and code_bytes(image, CODE_SIZE)[22610:22613] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == 22613
            and instructions.get(22610) == b"\0", "Identify exact checkpoint changed")

    def calls(prefix):
        lo = cdb_address(debug, "L:" + prefix + "$0$0")
        hi = cdb_address(debug, "L:X" + prefix + "$0$0")
        return {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                if lo <= a <= hi and len(b) == 3 and b[0] == 0x12}

    for prefix, targets in (
        ("Fzcl_identify$advance", ("__divulong",)),
        ("G$zcl_id_init", ("_memset",)),
        ("Fzcl_identify$global", ("_zcl_dispatch_unicast", "_zcl_wr_handle")),
        ("G$zcl_id_rx", ("_zcl_frame_decode", "_zcl_frame_encode")),
        ("G$zcl_dispatch_unicast", ("_zcl_attr_set_check", "_zcl_frame_decode", "_zcl_read_attrs_unicast", "_zcl_frame_encode", "_zcl_wr_handle")),
        ("G$zcl_read_attrs_unicast", ("_zcl_value_encode", "_zcl_frame_encode", "_zcl_frame_decode")),
        ("Fzcl_dispatch$discover", ("_zcl_value_type_supported", "_zcl_frame_encode")),
        ("Ftest_zcl_identify$rx", ("_zcl_id_rx",)),
    ):
        require(all(symbols[t] in calls(prefix) for t in targets), f"Identify bypasses actual calls in {prefix}")
    require(0x90 in calls("G$zcl_id_tick") and {0x90, 0x4cf} <= calls("G$zcl_id_rx"),
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

    for a in (0, CODE_SIZE-1, 0x90, 0x4cf, 22610, symbols["__divulong"],
              symbols["___memcpy"], *(v[1] for v in PUBLIC.values())):
        reject(image=image | {a: image[a] ^ 1})
    reject(image={a: b for a, b in image.items() if a != CODE_SIZE-1})
    reject(image=image | {CODE_SIZE: 0})
    for name in (*RUNTIME, *PARAMS, "_zcl_id_done", "_zcl_id_result", "s_SSEG", "l_XSEG"):
        reject(symbols=symbols | {name: symbols[name]+1})
    reject(symbols=symbols | {"unreviewed_extra_symbol": 0})
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fzcl_identify", b"S:Lzcl_identify",
                   b"L:Lzcl_identify", b"S:Ftest_zcl_identify", b"L:C$test_zcl_identify",
                   b"F:Fzcl_dispatch", b"S:Lzcl_value",
                   b"F:G$zcl_wr_handle", b"S:Lzcl_write", b"T:Fzcl_write"):
        found = [line for line in debug_raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(found, f"Missing CDB negative prerequisite: {prefix!r}")
        reject(debug_raw=debug_raw.replace(found[0], b"", 1))
        reject(debug_raw=debug_raw + found[0])
    reject(debug_raw=debug_raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=debug_raw + b"\xff")
    reject(debug_raw=debug_raw.replace(b"{4}S:S$remaining", b"{3}S:S$remaining", 1))
    reject(memory=memory.replace("172 bytes available", "171 bytes available"))
    reject(memory=memory.replace("1006", "1007"))
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
            f"Identify target result: {ram[0x1e00:0x1e08].hex()}")
    require(ram[909:911] == b"\x66\0", f"Identify skipped common scenarios: {ram[909:911].hex()}")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Identify writes outside ordinary allocation/eight-byte result/status-tail guard")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x53,
            "Identify SP7C/IRAM alias guard or checkpoint unwind failed")
    require(all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Identify enabled interrupts")


def check_peak(text):
    found = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    # Both-board CI 35657733337 observed 6E with all 102 cases and guards
    # passing; compact real-call staging and its immediate listing reviewed.
    require(len(found) == 1 and int(found[0], 16) == 0x6e <= 0x7c, f"Identify full-run SP peak changed: {found}")


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
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 909), (0, 1006),
                            (0, 0x1dff), (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0xa8-0x80)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated)
    for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x53"):
        with unittest.TestCase().assertRaises(ValueError):
            check_peak(bad)
    print(f"Identify: {CODE_SIZE}/{CODE_BUDGET} CODE, 1006+64/{XDATA_BUDGET} reserved XDATA; "
          f"102 common scenarios, full-run SP6E/cap7C, checkpoint SP53; complete raw CDB/map/CODE, "
          f"7 immediate snapshots, {negatives} artifact +9 guard +3 peak +1 alias negatives PASS "
          "(simulation only; no indicator, network or hardware time).")


if __name__ == "__main__":
    main()
