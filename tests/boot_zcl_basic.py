#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove the actual six-module Basic/ZCL composition, without board linkage."""

import argparse
import hashlib
import json
import re
import unittest
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_nwk_candidates import label, listing_metrics, records
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("zcl_basic", "zcl_dispatch", "zcl_attributes", "zcl_frame", "zcl_value", "zcl_basic_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_zcl_basic.c",)
CODE_SIZE, CODE_BUDGET, XDATA_BUDGET = 17978, 24576, 1536
CODE_SHA = "58fa0b4dcb96bba289bd02b8117f8b7718a5ffe1704beece1f7060857418b2c0"
CDB_SHA = "185f0d79fc0176fb89e50d208e3fb4004f5df7bcab53e092a6838bed07ea7644"
MAP_SHA = "4b1c3adcb7324d454592d0800ec62cd7417628674d5f83d8a8a824aef979ad26"
# Complete ordered instruction metrics AND complete raw immediate snapshots.
LISTINGS = {
    "zcl_basic": (1501, 2120, "714a97d7466ca0473af03af04c03e4dc3701aad28cf64fbf53f39d657daef59f"),
    "zcl_dispatch": (1800, 2864, "3ef1f7cddd92c672ab50f720bba5ae074d2c493bf32d1c417914b3861be9c11f"),
    "zcl_attributes": (1284, 2086, "e839184023c0d9d1531ef7873e594bdf0dd58ded6be591b9bcc9e69325f798d9"),
    "zcl_frame": (663, 1173, "b6ccabd48278973c2b505ce8d46729a9538243e38be703a92d3df563e2e61aac"),
    "zcl_value": (1006, 1710, "ed13b886b8eaf70967b71775ed38db87dda03d5116b3e933ad58649389cd90fa"),
    "zcl_basic_test": (3835, 6963, "547afaa13786fcd8e5a424497c3513c2ee1764871218617d938da276d74bf7a3"),
}
LISTING_SHA = {
    "zcl_basic": "dc23338cbe36b2cb5ade29adc74c44a2a3f9a6db2f7bf938aaab6f225866f8d4",
    "zcl_dispatch": "f0f135c13ccd25eb8e1fd16838c8636538ea0f8645f2d9d14de08457f054c989",
    "zcl_attributes": "d4b27069418ce22202ee26725d7e7e48a7475edb91036110b7630d0b9fdf3ca7",
    "zcl_frame": "93173e16dc48b4d06270c379c2a07b216d5c9f6ad62637cb7b9720b726d0f57f",
    "zcl_value": "4570fad6d6dbd78b0b0428a5796c22c5283e23cafe20831b50a6b97481bb3920",
    "zcl_basic_test": "b18f16c755d33cab8efe6a04702494bf57c926edf164b1d3e7c33d7532cc8383",
}
# Hash ALL parsed area records (including zero areas, flags, addresses, order).
AREAS_SHA = {
    "zcl_basic": "f948cccbc180dd5dae33a7ad36183c132aee283f36d9dccaa96c74c1faafe36a",
    "zcl_dispatch": "f1bdc58b1979b2daeccfc17c1518d80eab7a7700885f6cc5ed5ffc6ffb287bc1",
    "zcl_attributes": "c8a8963ce9e7fc1f3888df44194b6239379b0b04f44ed495dbe06d505632f11e",
    "zcl_frame": "0ce8e9216057ea5573059399bbdc0216189aaa8132365c419e209d01ac591177",
    "zcl_value": "29c2bbdac508133f443c8524a9afca7c7148c2a16e05b1e0a983a95629af03e8",
    "zcl_basic_test": "1c715fe1baf4d43b9bf0e94939ca98eb2820cb322e52e36a1cf6a5a39765a713",
}
CALLER = {
    "basic": (370, 152), "saved": (522, 152), "config": (674, 13),
    "info": (687, 10), "read_info": (697, 4), "frame": (701, 9), "value": (710, 5),
    "request": (715, 102), "response": (817, 102), "text": (919, 33),
    "encoded_length": (952, 1), "cases": (953, 2),
}
RUNTIME = {
    "__divuint_PARM_2": 967, "___memcpy_PARM_2": 974, "___memcpy_PARM_3": 977,
    "_memset_PARM_2": 982, "_memset_PARM_3": 983, "__gptrput_PARM_2": 985,
    "__mulint_PARM_2": 986, "_memcmp_PARM_2": 988, "_memcmp_PARM_3": 991,
}
PUBLIC = {
    "zcl_basic_init": ("zcl_basic", 0x90),
    "zcl_dispatch_unicast": ("zcl_dispatch", 0xe41),
    "zcl_attr_set_check": ("zcl_attributes", 0x1408),
    "zcl_read_attrs_unicast": ("zcl_attributes", 0x1652),
    "zcl_frame_decode": ("zcl_frame", 0x1c2e),
    "zcl_frame_encode": ("zcl_frame", 0x1e2e),
    "zcl_value_type_supported": ("zcl_value", 0x2201),
    "zcl_value_decode": ("zcl_value", 0x22e5),
    "zcl_value_encode": ("zcl_value", 0x2486),
}
FIELDS = (
    ((0, "type", 1), (1, "flags", 1), (2, "manufacturer_code", 2), (4, "sequence", 1), (5, "command_id", 1)),
    ((0, "header", 6), (6, "ignored_control_bits", 1), (7, "payload_offset", 1), (8, "payload_length", 1)),
    ((0, "type", 1), (1, "string_non_value", 1), (2, "data", 3), (5, "data_length", 2)),
    ((0, "type", 1), (1, "data_offset", 1), (2, "data_length", 1), (3, "encoded_length", 1), (4, "non_value_pattern", 1)),
    ((0, "id", 2), (2, "readable", 1), (3, "value", 7)),
    ((0, "attributes", 3), (3, "count", 1), (4, "side", 1), (5, "manufacturer_specific", 1), (6, "manufacturer_code", 2)),
    ((0, "length", 1), (1, "command_id", 1), (2, "requested_count", 1), (3, "returned_count", 1)),
    ((0, "data", 3), (3, "length", 1)),
    ((0, "manufacturer", 4), (4, "model", 4), (8, "software", 4), (12, "power_source", 1)),
    ((0, "set", 8), (8, "attributes", 60), (68, "strings", 80), (148, "scalars", 4)),
)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("ascii")


def private_span(debug, module_pattern):
    span = set()
    for match in re.finditer(
        r"^S:(L(?:" + module_pattern + r")\.[^(\n]+)\(\{(\d+)\}[^\n]*\),F,0,0$",
        debug, re.MULTILINE,
    ):
        if re.search(r"^L:" + re.escape(match[1]) + ":", debug, re.MULTILINE):
            start = cdb_address(debug, "L:" + match[1])
            span.update(range(start, start + int(match[2])))
    return span


def verify(image, symbols, debug_raw, memory, listings, objects):
    # Hash raw bytes BEFORE any decoding/newline normalization/filtering.
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA,
            "Basic complete raw CDB identity changed")
    debug = debug_raw.decode("ascii")
    require(CODE_SIZE <= CODE_BUDGET and sha(code_bytes(image, CODE_SIZE)) == CODE_SHA,
            "Basic complete CODE identity changed")
    require(sha(canonical(symbols)) == MAP_SHA, "Basic complete parsed map identity changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "zcl_basic_test_result", SOURCES, xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 996 and symbols["s_SSEG"] == 0x46,
            "Basic storage/stack extent changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
                       memory) == [("0x0000", "0x03e3", "996", "7680")],
            "Basic external-RAM memory accounting changed")
    require(set(listings) == set(objects) == set(MODULES), "Basic module set changed")
    instructions, coverage = {}, set()
    for m in MODULES:
        raw = listings[m]
        require(sha(raw) == LISTING_SHA[m], f"Basic complete immediate snapshot changed: {m}")
        text = raw.decode("ascii")
        require(listing_metrics(text) == LISTINGS[m], f"Basic ordered instructions changed: {m}")
        for address, data in records(text):
            span = set(range(address, address + len(data)))
            require(not span & coverage and all(image.get(address+i) == b for i, b in enumerate(data)),
                    "Basic duplicate/overlapping/non-linked instructions")
            coverage.update(span)
            instructions[address] = data
        areas = re.findall(r"^A (\S+) size (\S+) flags (\S+) addr (\S+)$", objects[m], re.MULTILINE)
        require(sha(canonical(areas)) == AREAS_SHA[m], f"Basic complete module allocations changed: {m}")
    for i, expected in enumerate(FIELDS):
        found = re.findall(rf"^T:Fzcl_basic\$__{i:08d}\[(.*)\]$", debug, re.MULTILINE)
        require(len(found) == 1, "Basic field record missing/duplicated")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected, "Basic field ABI changed")
    for name, suffix in (("model", "09"), ("config", "08")):
        require(re.search(rf"^S:Lzcl_basic.zcl_basic_init\${name}\$[^(]+\(\{{3\}}DG,ST__000000{suffix}:S\),F,0,0$",
                          debug, re.MULTILINE), "Basic generic pointer ABI changed")
    require(private_span(debug, "|".join(MODULES[:-1])) == set(range(370)),
            "Basic compiler-private prefix changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zcl_basic${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                f"Basic caller ABI changed: {name}")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "Basic caller objects overlap")
        caller.update(span)
    require(caller == set(range(370, 955)) and private_span(debug, "test_zcl_basic") == set(range(955, 967)),
            "Basic caller/local/runtime boundary changed")
    require(all(symbols.get(k) == v for k, v in RUNTIME.items()),
            "Basic libc/compiler scratch moved into caller storage")
    # The 29-byte runtime suffix includes non-public libc/compiler locals;
    # complete CODE/map + exact module allocations pin these as well.
    require(allocated == set(range(996)) | set(range(0x1e00, 0x1e08)),
            "Basic unaccounted ordinary/runtime/status allocation")
    for name, (m, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") == address
                and label(listings[m].decode("ascii"), name) == address and address in instructions,
                "Basic public symbol/listing ABI changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "Basic result return ABI changed")
    require(symbols["_main"] == 0x425f and symbols["_zcl_basic_test_done"] == 0x4297
            and code_bytes(image, CODE_SIZE)[0x4297:0x429a] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == 0x429a
            and instructions.get(0x4297) == b"\0", "Basic exact checkpoint changed")

    def calls(start, end):
        return {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                if start <= a < end and len(b) == 3 and b[0] == 0x12}

    for name, targets in (
        ("zcl_basic_init", ("___memcpy", "_memset")),
        ("zcl_dispatch_unicast", ("_zcl_attr_set_check", "_zcl_frame_decode", "_zcl_read_attrs_unicast", "_zcl_frame_encode")),
        ("zcl_read_attrs_unicast", ("_zcl_frame_decode", "_zcl_value_encode", "_zcl_frame_encode")),
    ):
        found = calls(symbols["_" + name], cdb_address(debug, f"L:XG${name}$0$0") + 1)
        require(all(symbols[t] in found for t in targets), f"Basic real calls absent in {name}")
    require(all(symbols[t] in calls(0x9f6, 0xe41) for t in ("_zcl_value_type_supported", "_zcl_frame_encode")),
            "Basic discovery bypasses actual value/frame implementation")
    require(all(symbols["_" + name] in calls(0x2771, 0x4297) for name in PUBLIC
                if name not in ("zcl_value_encode", "zcl_value_type_supported")),
            "Basic corpus bypasses actual public functions")
    return allocated


def artifact_negatives(image, symbols, debug_raw, memory, listings, objects):
    case, count = unittest.TestCase(), 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug_raw=debug_raw, memory=memory,
                    listings=listings, objects=objects)
        args.update(changes)
        with case.assertRaises(ValueError):
            verify(**args)
        count += 1

    for a in (0, CODE_SIZE-1, 0x4297, symbols["___memcpy"], *(v[1] for v in PUBLIC.values())):
        reject(image=image | {a: image[a] ^ 1})
    reject(image={a: b for a, b in image.items() if a != CODE_SIZE-1})
    reject(image=image | {CODE_SIZE: 0})
    for name in (*RUNTIME, "_zcl_basic_test_done", "s_SSEG", "l_XSEG", "_zcl_basic_test_result"):
        reject(symbols=symbols | {name: symbols[name]+1})
    reject(symbols=symbols | {"unreviewed_extra_symbol": 0})
    # All record kinds, including private/helper/source-line records, matter.
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fzcl_dispatch", b"S:Lzcl_basic",
                   b"L:Lzcl_basic", b"S:Ftest_zcl_basic", b"L:C$test_zcl_basic"):
        matches = [line for line in debug_raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(matches, f"Missing raw CDB negative prerequisite: {prefix!r}")
        reject(debug_raw=debug_raw.replace(matches[0], b"", 1))
        reject(debug_raw=debug_raw + matches[0])
    reject(debug_raw=debug_raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=debug_raw + b"\xff")  # Must fail raw proof before ASCII decode.
    reject(debug_raw=debug_raw.replace(b"({152}ST", b"({151}ST", 1))
    reject(memory=memory.replace("186 bytes available", "185 bytes available"))
    reject(memory=memory.replace("996", "997"))
    for m in MODULES:
        reject(listings=listings | {m: b""})
        lines = listings[m].splitlines(keepends=True)
        indexes = [i for i, line in enumerate(lines) if records(line.decode("ascii"))]
        first, second = indexes[:2]
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
    reject(listings={m: v for m, v in listings.items() if m != "zcl_basic"})
    return count


def check_result(ram, iram, sfr, allocated):
    require(ram[0x1e00:0x1e08] == b"ZBA1\x01\x08\0\0", "Basic actual target corpus failed/result ABI changed")
    require(ram[953:955] == b"\x38\x01", "Basic skipped common cases")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Basic writes outside ordinary allocation/eight-byte result (including status tail)")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x45,
            "Basic SP7C/IRAM alias guard or checkpoint unwind failed")
    require(all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Basic enabled interrupts")


def check_peak(text):
    found = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(found) == 1 and int(found[0], 16) == 0x5a <= 0x7c, "Basic full-run SP peak changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "zcl_basic_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text(encoding="ascii"))
    debug_raw = path.with_suffix(".cdb").read_bytes()
    memory = path.with_suffix(".mem").read_text(encoding="ascii")
    listings = {m: (args.output / f"zcl_basic_test.{m}.rst").read_bytes() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text(encoding="ascii") for m in MODULES}
    allocated = verify(image, symbols, debug_raw, memory, listings, objects)
    negatives = artifact_negatives(image, symbols, debug_raw, memory, listings, objects)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaises(ValueError):
        check_alias(args.simulator, False)
    start, stop = symbols["_main"], symbols["_zcl_basic_test_done"]
    setup = [ALIAS, "fill xram 0 0x1eff 0xa5",
             "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0"]
    text = simulate(args.simulator, setup + [
        f"run 0 {start:#x}", "fill iram 0x7d 0xff 0xc7", f"run {start:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    check_result(ram, iram, sfr, allocated)
    # Independent uninterrupted reset-to-checkpoint run, NOT checkpoint SP.
    full = simulate(args.simulator, setup + [f"run 0 {stop:#x}"] + snapshot_commands(1), path)
    check_pc(section(full, 1), stop)
    check_peak(section(full, 1))
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 953), (0, 996),
                            (0, 0x1dff), (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0xa8-0x80)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated)
    for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x45"):
        with unittest.TestCase().assertRaises(ValueError):
            check_peak(bad)
    print(f"Basic: {CODE_SIZE}/{CODE_BUDGET} CODE, 996+64/{XDATA_BUDGET} reserved XDATA; "
          f"312 common cases, full-run SP5A/cap7C, checkpoint SP45; complete raw CDB/map/CODE, "
          f"6 immediate snapshots, {negatives} artifact +9 guard +3 peak +1 alias negatives PASS "
          "(simulation only; no endpoint, radio or security).")


if __name__ == "__main__":
    main()
