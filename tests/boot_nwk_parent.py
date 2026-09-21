#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove the actual parent/collector/codec image; no radio or join model."""

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
from boot_nwk_candidates import listing_metrics, records
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("mac_frame", "nwk_beacon", "nwk_candidates", "nwk_parent", "nwk_parent_test")
SOURCES = ("mac_frame.c", "nwk_beacon.c", "nwk_candidates.c", "nwk_parent.c", "test_nwk_parent.c")
CODE_SIZE = 15580
CODE_SHA = "88d823c98f15096a7f464c1743006e4b0acb629414c332f5e46c6b78b494cdec"
CDB_SHA = "658b204473ab46ccc24b310d390e360b21d3d6521181500764bec359433b937b"
MAP_SHA = "77cbdcd02c72eb29e8b376c456726ebf572027e179e4bd57c16efb931d7815bc"
LISTINGS = {
    "mac_frame": (4253, 7136, "42e1472393ff92c6ed7917a2df5479d508c709bfa62a15d461d9da4b2378626b"),
    "nwk_beacon": (353, 601, "de3b7e7938b360eff7ef39decd263b78e0bde30d11e11874e7d35027a9ab68eb"),
    "nwk_candidates": (1519, 2334, "ce1e10a807a9b5471c687308d5faebcba0bf6635ff48c11b60357343254490b0"),
    "nwk_parent": (915, 1428, "3201ae9a2fac0c8656a6c896d6d78862ef5357b316ff2c58f354a4031d742242"),
    "nwk_parent_test": (1810, 3463, "ac37a0ae60216185e0ac6a32fd14b020fcd1aca1cd88372a97f1df17f284f524"),
}
OBJECTS = {
    "mac_frame": (7136, 216, 15, 10),
    "nwk_beacon": (601, 27, 9, 0),
    "nwk_candidates": (2334, 111, 4, 0),
    "nwk_parent": (1428, 57, 0, 0),
    "nwk_parent_test": (3522, 442, 0, 0),
}
CALLER = {
    "table": (411, 150), "saved_table": (561, 150),
    "policy": (711, 15), "saved_policy": (726, 15),
    "choice": (741, 37), "saved_choice": (778, 37),
    "body": (815, 26), "cases": (841, 4), "i": (845, 1), "j": (846, 1),
}


def verify(image, symbols, debug, memory, listings, objects):
    require(CODE_SIZE <= 16384 and hashlib.sha256(code_bytes(image, CODE_SIZE)).hexdigest() == CODE_SHA,
            "Parent complete CODE identity changed")
    require(hashlib.sha256(debug.encode("ascii")).hexdigest() == CDB_SHA,
            "Parent complete raw CDB identity changed")
    canonical = json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii")
    require(hashlib.sha256(canonical).hexdigest() == MAP_SHA, "Parent complete map identity changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "nwk_parent_test_result", SOURCES, xdata_budget=1024,
    )
    require(symbols["l_XSEG"] == 875 and symbols["s_SSEG"] == 0x2f,
            "Parent storage/stack identity changed")
    require(set(listings) == set(objects) == set(MODULES), "Parent composition changed")
    instructions = {}
    for module in MODULES:
        require(listing_metrics(listings[module]) == LISTINGS[module],
                f"Parent immediate instruction snapshot changed: {module}")
        for address, data in records(listings[module]):
            require(address not in instructions and
                    all(image.get(address+i) == value for i, value in enumerate(data)),
                    "Parent duplicated or non-linked instruction")
            instructions[address] = data
        areas = {name: int(size, 16) for name, size in re.findall(
            r"^A (\S+) size ([0-9A-Fa-f]+) flags \S+ addr \S+$", objects[module], re.MULTILINE)}
        code = sum(size for name, size in areas.items()
                   if name in ("HOME", "GSFINAL", "CSEG", "CONST") or name.startswith("GSINIT"))
        require((code, areas.get("XSEG", 0), areas.get("DSEG", 0), areas.get("OSEG", 0))
                == OBJECTS[module], f"Parent module allocation changed: {module}")
    spans = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_nwk_parent${name}$0_0$0"
        require(cdb_address(debug, "L:"+prefix) == address and
                f"S:{prefix}({{{size}}}" in debug, f"Parent caller ABI changed: {name}")
        span = set(range(address, address+size))
        require(not span & spans and span <= allocated, "Parent caller objects overlap")
        spans.update(span)
    require(spans == set(range(411, 847)), "Parent caller/private-prefix boundary changed")
    require(instructions.get(symbols["_nwk_parent_test_done"]) == b"\0" and
            symbols["_nwk_parent_test_done"] == 15047 and
            cdb_address(debug, "L:XG$main$0$0") == 15050, "Parent checkpoint changed")
    selector_calls = {int.from_bytes(data[1:], "big") for address, data in instructions.items()
                      if 10169 <= address < 11597 and len(data) == 3 and data[0] == 0x12}
    require(symbols["_nwk_candidates_get"] in selector_calls, "Parent bypasses real collector getter")
    caller_calls = {int.from_bytes(data[1:], "big") for address, data in instructions.items()
                    if 11597 <= address <= 15047 and len(data) == 3 and data[0] == 0x12}
    require(all(symbols[name] in caller_calls for name in
                ("_nwk_parent_select", "_nwk_candidates_init", "_nwk_candidates_consider")),
            "Parent corpus no longer calls actual selector/collector")
    return allocated


def artifact_negatives(image, symbols, debug, memory, listings, objects):
    case = unittest.TestCase()
    count = 0

    def reject(**changes):
        nonlocal count
        arguments = dict(image=image, symbols=symbols, debug=debug, memory=memory,
                         listings=listings, objects=objects)
        with case.assertRaises(ValueError):
            verify(**(arguments | changes))
        count += 1

    for address in (0, CODE_SIZE-1, symbols["_main"], symbols["_nwk_parent_select"],
                    symbols["_nwk_candidates_get"], symbols["_nwk_candidates_consider"],
                    symbols["_nwk_beacon_decode"], symbols["_mac_frame_decode"]):
        reject(image=image | {address: image[address] ^ 1})
    reject(image={a: b for a, b in image.items() if a != CODE_SIZE-1})
    reject(image=image | {CODE_SIZE: 0})
    for name in ("_nwk_parent_select", "_nwk_parent_test_done", "s_SSEG", "l_XSEG",
                 "_nwk_parent_test_result", "__gptrput_PARM_2"):
        reject(symbols=symbols | {name: symbols[name]+1})
    for prefix in ("F:", "S:", "L:", "T:", "S:Ftest_nwk_parent", "S:Lnwk_parent",
                   "L:Ftest_nwk_parent", "F:Fmac_frame", "L:XG$nwk_parent_select"):
        matches = [line for line in debug.splitlines() if line.startswith(prefix)]
        require(matches, f"Missing CDB negative prerequisite: {prefix}")
        reject(debug=debug.replace(matches[0]+"\n", "", 1))
    reject(debug=debug.replace("\n", "\r\n"))
    reject(memory=memory.replace("209 bytes available", "208 bytes available"))
    for module in MODULES:
        reject(listings=listings | {module: ""})
        lines = listings[module].splitlines(keepends=True)
        positions = [i for i, line in enumerate(lines) if records(line)]
        require(len(positions) >= 2, "Missing instruction negative prerequisite")
        for operation in ("delete", "duplicate", "reorder"):
            changed = lines.copy()
            first, second = positions[:2]
            if operation == "delete":
                del changed[first]
            elif operation == "duplicate":
                changed.insert(first, lines[first])
            else:
                changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {module: "".join(changed)})
        reject(objects=objects | {module: ""})
    reject(listings={m: text for m, text in listings.items() if m != "nwk_parent"})
    return count


def check_result(ram, iram, sfr, allocated):
    require(ram[0x1e00:0x1e06] == b"NWP1\x01\x08", "Parent result ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"Actual SDCC parent corpus failed at C line {failure}")
    require(int.from_bytes(ram[841:845], "little") == 42, "Parent skipped common cases")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "Parent wrote outside ordinary allocation/status (including reserved tail)")
    require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == 0x2e,
            "Parent SP7C/IRAM alias guard or unwind failed")
    require(sfr[0xa8-0x80] == sfr[0xb8-0x80] == sfr[0x9a-0x80] == 0,
            "Parent enabled interrupts")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output/"nwk_parent_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_bytes().decode("ascii")
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output/f"nwk_parent_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output/f"{m}.rel").read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings, objects)
    negatives = artifact_negatives(image, symbols, debug, memory, listings, objects)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaises(ValueError):
        check_alias(args.simulator, False)
    start, stop = symbols["_main"], symbols["_nwk_parent_test_done"]
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
        f"run 0 {start:#x}", "fill iram 0x7d 0xff 0xc7", f"run {start:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    check_result(ram, iram, sfr, allocated)
    peak = re.search(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", section(text, 1))
    require(peak is not None and int(peak[1], 16) == 0x4d, "Parent full-run SP peak changed")
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 841), (0, 0x1dff),
                            (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0xa8-0x80)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated)
    print(f"NWK parent: {CODE_SIZE}/16384 CODE, 875+64/1024 XDATA, 42 real-codec "
          f"cases, peak SP4D/cap7C; complete ABI/5 snapshots, {negatives} artifact "
          "+8 result +1 alias negatives PASS (simulation only; no join).")


if __name__ == "__main__":
    main()
