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
from boot_zcl_basic import canonical, private_span, sha, verify_value_abi, verify_write_abi
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("zcl_identify", "zcl_dispatch", "zcl_write", "zcl_attributes", "zcl_frame", "zcl_value", "zcl_identify_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_zcl_identify.c",)
CODE_SIZE, CODE_BUDGET, XDATA_BUDGET = 23338, 24576, 1536
CODE_SHA = "c5694cf8a75ec7053b2d5d29476981f737fbb323e5e75b2d39db3f0eb48c54ba"
CDB_SHA = "a5f4c90d2bc17506bd506220993a9c662229f1e17e16e71d906c79e03b5814c1"
MAP_SHA = "e4d57955f21f7029b2728c78356361a6496d312ae28d7f4f0d09c3aefed65d28"
# Complete ordered instruction records and complete raw immediate snapshots.
LISTINGS = {
    "zcl_identify": (1967, 3001, "3f2e2b10883d582e347c8f4e1ca5f5026e9ac86f3235e58587a266732de1358b"),
    "zcl_dispatch": (1702, 2725, "83c7cc7d8e3f7b6bfde71e531780f3670937b4d979f5216f7221a5fad9a2c8a9"),
    "zcl_write": (1016, 1552, "17e604f21d94a7fee313ab832da2cebbeea7d49c4ca6dc9c8c014922bbf06854"),
    "zcl_attributes": (1217, 1958, "c2d622abc80f0a54383f2c125f6f80e79452e69d6d96ee8a8fcad5811049be70"),
    "zcl_frame": (663, 1173, "8a06ffc31bce9f4bc38cea78a776d5adf74172dbf54ae2b08202e352018f0c41"),
    "zcl_value": (768, 1345, "63d7aa6ba9126e71087dfa58db51588cabedd67419997f8b094a636c53de3e27"),
    "zcl_identify_test": (5556, 9864, "a9bc73c2f51dd1111dd4868af9ffe8927cf2889ac4adfed626aa797616709b8b"),
}
LISTING_SHA = {
    "zcl_identify": "661024b65a5b2b05485cca6ebe510e04e6c771d79638838c3ede67fa8ba6334e",
    "zcl_dispatch": "40a08499ab587962aea12d05ce4120f3015c33066af0e3a29ed15dd48618d93b",
    "zcl_write": "756adb4fcc19134b0cb15a5f4a5e7ca8c07ec7a0d206b473f14d5b048ac7b1c5",
    "zcl_attributes": "9bd715c4d8f3b8c243beba97d91ab7e9a767abab1d4f3239a1e5a25e0aa65940",
    "zcl_frame": "de75d5c90f05693981afc53916d7956acf7124d1a772856761278b8c26185ba3",
    "zcl_value": "385ff1321a9e96209405b183f30aebe8b21c8d10e6e187a06862ac0d7c7f8f0e",
    "zcl_identify_test": "f2b1e13390d107a85a147fc2405dd1850f3dcb847bc60a6ed5200792643cd28d",
}
# All area records, including zero areas, flags, addresses and ordering.
AREAS_SHA = {
    "zcl_identify": "7595499c488219c8f0ba3273b6ad21d068c82e40097b3ad67f32693a77715f4d",
    "zcl_dispatch": "b4276a4d6d12cb88ff09a3af8296b257c0987dc124166ca5a021161aea080d61",
    "zcl_write": "8ae20fa1fb009f575b1e70ac88d52ccf08397ef524b76eb3c6825796d8849d70",
    "zcl_attributes": "25bd59cba20900d19452fe5e886feef5ff9309e66a211857be2bc581e6feaf29",
    "zcl_frame": "0ce8e9216057ea5573059399bbdc0216189aaa8132365c419e209d01ac591177",
    "zcl_value": "6099af83c6e82e925c1722a9b773713e4d6fedcea0d5055151157c08638971bd",
    "zcl_identify_test": "ed2f8a20faa9960150879844fbb917f8831b49725389502ab51564f32b4d2e1f",
}
CALLER = {
    "ctx": (650, 8), "saved": (658, 8), "other": (666, 8), "info": (674, 10),
    "frame": (684, 9), "value": (693, 5), "request": (698, 102),
    "response": (800, 102), "encoded": (902, 1), "cases": (903, 2),
}
RUNTIME = {
    "__divulong_PARM_2": 954,
    "___memcpy_PARM_2": 967, "___memcpy_PARM_3": 970,
    "_memset_PARM_2": 975, "_memset_PARM_3": 976, "__gptrput_PARM_2": 978,
    "__mulint_PARM_2": 979, "__mullong_PARM_2": 981,
    "_memcmp_PARM_2": 985, "_memcmp_PARM_3": 988,
}
PUBLIC = {
    "zcl_id_init": ("zcl_identify", 963),
    "zcl_id_tick": ("zcl_identify", 1083),
    "zcl_id_rx": ("zcl_identify", 2258),
    "zcl_dispatch_unicast": ("zcl_dispatch", 4367),
    "zcl_wr_handle": ("zcl_write", 5870),
    "zcl_attr_set_check": ("zcl_attributes", 7422),
    "zcl_read_attrs_unicast": ("zcl_attributes", 7915),
    "zcl_frame_decode": ("zcl_frame", 9380),
    "zcl_frame_encode": ("zcl_frame", 9892),
    "zcl_value_type_supported": ("zcl_value", 10699),
    "zcl_value_decode": ("zcl_value", 10840),
    "zcl_value_encode": ("zcl_value", 11272),
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
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 993 and symbols["s_SSEG"] == 0x4e,
            "Identify ordinary storage/stack changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
                       memory) == [("0x0000", "0x03e0", "993", "7680")],
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
    verify_value_abi(debug, instructions)
    require(private_span(debug, "|".join(MODULES[:-1])) == set(range(650)),
            "Identify compiler-private prefix changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zcl_identify${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                f"Identify caller storage changed: {name}")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "Identify caller objects overlap")
        caller.update(span)
    require(caller == set(range(650, 905)) and private_span(debug, "test_zcl_identify") == set(range(905, 954)),
            "Identify caller/local/runtime boundaries changed")
    require(all(symbols.get(k) == v for k, v in RUNTIME.items()),
            "Identify libc/compiler scratch moved into caller storage")
    require("__divuint_PARM_2" not in symbols, "Identify regained unused division scratch")
    require(allocated == set(range(993)) | set(range(0x1e00, 0x1e08)),
            "Identify unaccounted ordinary/runtime/status allocation")
    for name, (m, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") == address
                and label(listings[m].decode("ascii"), name) == address and address in instructions,
                "Identify function address ABI changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "Identify result return ABI changed")
    require(symbols["_main"] == 21692 and symbols["_zcl_id_done"] == 21749
            and code_bytes(image, CODE_SIZE)[21749:21752] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == 21752
            and instructions.get(21749) == b"\0", "Identify exact checkpoint changed")

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

    for a in (0, CODE_SIZE-1, 0x90, 0x4cf, 21749, symbols["__divulong"],
              symbols["___memcpy"], *(v[1] for v in PUBLIC.values())):
        reject(image=image | {a: image[a] ^ 1})
    reject(image={a: b for a, b in image.items() if a != CODE_SIZE-1})
    reject(image=image | {CODE_SIZE: 0})
    for name in (*RUNTIME, *PARAMS, "_zcl_id_done", "_zcl_id_result", "s_SSEG", "l_XSEG"):
        reject(symbols=symbols | {name: symbols[name]+1})
    reject(symbols=symbols | {"__divuint_PARM_2": 954})
    reject(symbols=symbols | {"unreviewed_extra_symbol": 0})
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fzcl_identify", b"S:Lzcl_identify",
                   b"L:Lzcl_identify", b"S:Ftest_zcl_identify", b"L:C$test_zcl_identify",
                   b"F:Fzcl_dispatch", b"S:Lzcl_value",
                   b"F:G$zcl_wr_handle", b"S:Lzcl_write", b"T:Fzcl_write",
                   b"F:Fzcl_value$value_shape", b"S:Lzcl_value.non_value_pattern"):
        found = [line for line in debug_raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(found, f"Missing CDB negative prerequisite: {prefix!r}")
        reject(debug_raw=debug_raw.replace(found[0], b"", 1))
        reject(debug_raw=debug_raw + found[0])
    reject(debug_raw=debug_raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=debug_raw + b"\xff")
    reject(debug_raw=debug_raw.replace(b"{4}S:S$remaining", b"{3}S:S$remaining", 1))
    reject(memory=memory.replace("178 bytes available", "177 bytes available"))
    reject(memory=memory.replace("993", "994"))
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
    require(ram[903:905] == b"\x66\0", f"Identify skipped common scenarios: {ram[903:905].hex()}")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Identify writes outside ordinary allocation/eight-byte result/status-tail guard")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x4d,
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
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 903), (0, 993),
                            (0, 0x1dff), (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0xa8-0x80)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated)
    for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x4d"):
        with unittest.TestCase().assertRaises(ValueError):
            check_peak(bad)
    print(f"Identify: {CODE_SIZE}/{CODE_BUDGET} CODE, 993+64/{XDATA_BUDGET} reserved XDATA; "
          f"102 common scenarios, full-run SP6E/cap7C, checkpoint SP4D; complete raw CDB/map/CODE, "
          f"7 immediate snapshots, {negatives} artifact +9 guard +3 peak +1 alias negatives PASS "
          "(simulation only; no indicator, network or hardware time).")


if __name__ == "__main__":
    main()
