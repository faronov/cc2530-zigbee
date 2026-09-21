#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove the actual seven-module Basic/ZCL/write composition, without board linkage."""

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


MODULES = ("zcl_basic", "zcl_dispatch", "zcl_write", "zcl_attributes", "zcl_frame", "zcl_value", "zcl_basic_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_zcl_basic.c",)
CODE_SIZE, CODE_BUDGET, XDATA_BUDGET = 21098, 24576, 1536
CODE_SHA = "d32ee56a12c9f6677ff0d0dced68bd2195677e683244d80c1472b5eabb6840c4"
CDB_SHA = "62e24ec07510e15027f7dbb5f3a940eba65f13568704b261e32df1ca8519d5ee"
MAP_SHA = "bc4c38ac249400c06999398f187ba33467faada6f1373d29ca2b09e6548af0f9"
# Complete ordered instruction metrics AND complete raw immediate snapshots.
LISTINGS = {
    "zcl_basic": (1501, 2120, "ad9a5ab577b3aef1446ed77e00b8c0a6b2dd781788dfe3405e07c8e20f75c806"),
    "zcl_dispatch": (1942, 3093, "781fc340c649376fe7da70df72e46fcb81342b6eafcec6ce00338bb77bf1e2d4"),
    "zcl_write": (1016, 1552, "704d8a21244b368aea839bb4f5bf1a9f4a30e82339928580e26158f2a64f2873"),
    "zcl_attributes": (1284, 2086, "3a8c5590eb8a2f8f3aadec39f144799fb72bd0a511f0579ae1e0e7a0b50dfec1"),
    "zcl_frame": (663, 1173, "d483a18c39ebb70edbfc4744911b50873036beb058ba62204438b70f3f05d0ac"),
    "zcl_value": (1006, 1710, "136a1e5f2dc78747ccc41e57b8d04b147372c0c93ea53b14d461a48e1de1f097"),
    "zcl_basic_test": (4519, 8209, "90f63c034eb55a547d746fa77f5938090e261e567df2cc74ba3a5cb6381e3d4e"),
}
LISTING_SHA = {
    "zcl_basic": "a5904021c38c88f8831b4f808bde77adadbb84138c23fe2e4e500346349b38bf",
    "zcl_dispatch": "bef372caa1cc89c81f6aabb0c76151ca997c8e4d3efdd0f8652dae5886e2503f",
    "zcl_write": "e85a2459d3412027c0334eceb5edcfdf7c4bd41f2e1b959853357758b3186958",
    "zcl_attributes": "58eceed286846daac16b70f8af7f38850df9f7ae691b577dc313f87ac5055867",
    "zcl_frame": "f69ea57129e6c12affc05cbf9822cb7a8e235a0dc95bd28216a1cff9b034472d",
    "zcl_value": "5415edfb87b8ba9036d053f18727a203588fdf20fdf8c2dc5812f05757d9e25c",
    "zcl_basic_test": "c7f662ebec3a046f9acbfc1875d86115426335b8c8888a94dca96c263a75d766",
}
# Hash ALL parsed area records (including zero areas, flags, addresses, order).
AREAS_SHA = {
    "zcl_basic": "f948cccbc180dd5dae33a7ad36183c132aee283f36d9dccaa96c74c1faafe36a",
    "zcl_dispatch": "0c44f299eab89d9a8cf66c129d2093b09b4238ae39dbe7e43004940ad8b93590",
    "zcl_write": "8ae20fa1fb009f575b1e70ac88d52ccf08397ef524b76eb3c6825796d8849d70",
    "zcl_attributes": "c8a8963ce9e7fc1f3888df44194b6239379b0b04f44ed495dbe06d505632f11e",
    "zcl_frame": "0ce8e9216057ea5573059399bbdc0216189aaa8132365c419e209d01ac591177",
    "zcl_value": "29c2bbdac508133f443c8524a9afca7c7148c2a16e05b1e0a983a95629af03e8",
    "zcl_basic_test": "c984105d2a3e6cf6cab16de33faa91abee749dff066711791cdf4339ad4760d1",
}
CALLER = {
    "basic": (515, 152), "saved": (667, 152), "config": (819, 13),
    "info": (832, 10), "read_info": (842, 4), "frame": (846, 9), "value": (855, 5),
    "request": (860, 102), "response": (962, 102), "text": (1064, 33),
    "encoded_length": (1097, 1), "cases": (1098, 2),
}
RUNTIME = {
    "__divuint_PARM_2": 1113, "___memcpy_PARM_2": 1120, "___memcpy_PARM_3": 1123,
    "_memset_PARM_2": 1128, "_memset_PARM_3": 1129, "__gptrput_PARM_2": 1131,
    "__mulint_PARM_2": 1132, "_memcmp_PARM_2": 1134, "_memcmp_PARM_3": 1137,
}
PUBLIC = {
    "zcl_basic_init": ("zcl_basic", 0x90),
    "zcl_dispatch_unicast": ("zcl_dispatch", 0xe41),
    "zcl_wr_handle": ("zcl_write", 5357),
    "zcl_attr_set_check": ("zcl_attributes", 6909),
    "zcl_read_attrs_unicast": ("zcl_attributes", 7495),
    "zcl_frame_decode": ("zcl_frame", 8995),
    "zcl_frame_encode": ("zcl_frame", 9507),
    "zcl_value_type_supported": ("zcl_value", 10486),
    "zcl_value_decode": ("zcl_value", 10714),
    "zcl_value_encode": ("zcl_value", 11131),
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


def verify_write_abi(debug, symbols, instructions):
    """Shared internal five-byte write intent, generic pointers and real calls."""
    fields = re.findall(r"^T:Fzcl_write\$__00000008\[(.*)\]$", debug, re.MULTILINE)
    require(len(fields) == 1, "Write intent field record missing/duplicated")
    actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", fields[0])
    require(tuple((int(o), n, int(s)) for o, n, s in actual) ==
            ((0, "id", 2), (2, "value", 2), (4, "written", 1)), "Write intent field ABI changed")
    spans = set()
    for number, name, spec, size in (
        (None, "set", "{3}DG,ST__00000005:S", 3),
        (2, "frame", "{3}DG,ST__00000001:S", 3),
        (3, "payload", "{3}DG,SC:U", 3), (4, "response", "{3}DG,SC:U", 3),
        (5, "capacity", "{2}SI:U", 2), (6, "info", "{3}DG,ST__00000007:S", 3),
        (7, "edit", "{3}DG,ST__00000008:S", 3),
    ):
        found = re.findall(r"^S:(Lzcl_write.zcl_wr_handle\$" + name
                           + r"\$[^(]+)\(" + re.escape(spec) + r"\),F,0,0$", debug, re.MULTILINE)
        require(len(found) == 1, f"Write argument ABI changed: {name}")
        address = cdb_address(debug, "L:" + found[0])
        span = set(range(address, address + size))
        require(not span & spans and span <= private_span(debug, "zcl_write"),
                "Write parameter aliases caller or other arguments")
        spans.update(span)
        if number:
            require(symbols[f"_zcl_wr_handle_PARM_{number}"] == address, "Write parameter map ABI changed")
    require("F:G$zcl_wr_handle$0_0$0({2}DF,SC:U),Z,0,0,0,0,0\n" in debug,
            "Write result ABI changed")
    lo = cdb_address(debug, "L:G$zcl_wr_handle$0$0")
    hi = cdb_address(debug, "L:XG$zcl_wr_handle$0$0")
    calls = {int.from_bytes(b[1:], "big") for a, b in instructions.items()
             if lo <= a <= hi and len(b) == 3 and b[0] == 0x12}
    require(all(symbols[n] in calls for n in ("_zcl_value_decode", "_zcl_frame_encode")),
            "Write path bypasses real value/frame codecs")


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
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 1142 and symbols["s_SSEG"] == 0x46,
            "Basic storage/stack extent changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
                       memory) == [("0x0000", "0x0475", "1142", "7680")],
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
    verify_write_abi(debug, symbols, instructions)
    require(private_span(debug, "|".join(MODULES[:-1])) == set(range(515)),
            "Basic compiler-private prefix changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zcl_basic${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                f"Basic caller ABI changed: {name}")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "Basic caller objects overlap")
        caller.update(span)
    require(caller == set(range(515, 1100)) and private_span(debug, "test_zcl_basic") == set(range(1100, 1113)),
            "Basic caller/local/runtime boundary changed")
    require(all(symbols.get(k) == v for k, v in RUNTIME.items()),
            "Basic libc/compiler scratch moved into caller storage")
    # The 29-byte runtime suffix includes non-public libc/compiler locals;
    # complete CODE/map + exact module allocations pin these as well.
    require(allocated == set(range(1142)) | set(range(0x1e00, 0x1e08)),
            "Basic unaccounted ordinary/runtime/status allocation")
    for name, (m, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") == address
                and label(listings[m].decode("ascii"), name) == address and address in instructions,
                "Basic public symbol/listing ABI changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "Basic result return ABI changed")
    require(symbols["_main"] == 20018 and symbols["_zcl_basic_test_done"] == 20074
            and code_bytes(image, CODE_SIZE)[20074:20077] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == 20077
            and instructions.get(20074) == b"\0", "Basic exact checkpoint changed")

    def calls(start, end):
        return {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                if start <= a < end and len(b) == 3 and b[0] == 0x12}

    for name, targets in (
        ("zcl_basic_init", ("___memcpy", "_memset")),
        ("zcl_dispatch_unicast", ("_zcl_attr_set_check", "_zcl_frame_decode", "_zcl_read_attrs_unicast", "_zcl_frame_encode", "_zcl_wr_handle")),
        ("zcl_read_attrs_unicast", ("_zcl_frame_decode", "_zcl_value_encode", "_zcl_frame_encode")),
    ):
        found = calls(symbols["_" + name], cdb_address(debug, f"L:XG${name}$0$0") + 1)
        require(all(symbols[t] in found for t in targets), f"Basic real calls absent in {name}")
    require(all(symbols[t] in calls(0x9f6, 0xe41) for t in ("_zcl_value_type_supported", "_zcl_frame_encode")),
            "Basic discovery bypasses actual value/frame implementation")
    require(all(symbols["_" + name] in calls(11878, 20074) for name in PUBLIC
                if name not in ("zcl_value_encode", "zcl_value_type_supported", "zcl_wr_handle")),
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

    for a in (0, CODE_SIZE-1, 20074, symbols["___memcpy"], *(v[1] for v in PUBLIC.values())):
        reject(image=image | {a: image[a] ^ 1})
    reject(image={a: b for a, b in image.items() if a != CODE_SIZE-1})
    reject(image=image | {CODE_SIZE: 0})
    for name in (*RUNTIME, "_zcl_basic_test_done", "s_SSEG", "l_XSEG", "_zcl_basic_test_result"):
        reject(symbols=symbols | {name: symbols[name]+1})
    reject(symbols=symbols | {"unreviewed_extra_symbol": 0})
    # All record kinds, including private/helper/source-line records, matter.
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fzcl_dispatch", b"S:Lzcl_basic",
                   b"L:Lzcl_basic", b"S:Ftest_zcl_basic", b"L:C$test_zcl_basic",
                   b"F:G$zcl_wr_handle", b"S:Lzcl_write", b"T:Fzcl_write"):
        matches = [line for line in debug_raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(matches, f"Missing raw CDB negative prerequisite: {prefix!r}")
        reject(debug_raw=debug_raw.replace(matches[0], b"", 1))
        reject(debug_raw=debug_raw + matches[0])
    reject(debug_raw=debug_raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=debug_raw + b"\xff")  # Must fail raw proof before ASCII decode.
    reject(debug_raw=debug_raw.replace(b"({152}ST", b"({151}ST", 1))
    reject(memory=memory.replace("186 bytes available", "185 bytes available"))
    reject(memory=memory.replace("1142", "1143"))
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
    require(ram[0x1e00:0x1e08] == b"ZBA1\x01\x08\0\0", f"Basic target result: {ram[0x1e00:0x1e08].hex()}")
    require(ram[1098:1100] == b"\x72\x01", f"Basic skipped common cases: {ram[1098:1100].hex()}")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Basic writes outside ordinary allocation/eight-byte result (including status tail)")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x45,
            "Basic SP7C/IRAM alias guard or checkpoint unwind failed")
    require(all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Basic enabled interrupts")


def check_peak(text):
    found = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(found) == 1 and int(found[0], 16) == 0x5a <= 0x7c, f"Basic full-run SP peak changed: {found}")


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
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 1098), (0, 1142),
                            (0, 0x1dff), (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0xa8-0x80)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated)
    for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x45"):
        with unittest.TestCase().assertRaises(ValueError):
            check_peak(bad)
    print(f"Basic: {CODE_SIZE}/{CODE_BUDGET} CODE, 1142+64/{XDATA_BUDGET} reserved XDATA; "
          f"370 common cases, full-run SP5A/cap7C, checkpoint SP45; complete raw CDB/map/CODE, "
          f"7 immediate snapshots, {negatives} artifact +9 guard +3 peak +1 alias negatives PASS "
          "(simulation only; no endpoint, radio or security).")


if __name__ == "__main__":
    main()
