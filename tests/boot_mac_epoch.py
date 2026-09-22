#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the actual fractional epoch arithmetic; no timer or radio model."""

import argparse
import hashlib
import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_nwk_candidates import listing_metrics, records
from verify_firmware import code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require


MODULES = ("mac_epoch", "mac_epoch_test")
SIZE, XDATA, CHECKS, PEAK = 5442, 119, 5766, 0x2f
CODE_SHA = "f3b736e20ab790097034cb4e0676dc37279033c18d2bda0a5444a0a1f58e58f5"
CDB_SHA = "3ccacc99c08c9a3780a249573ae829c2957b2ec592fb9c8ec15962c6249ab4ad"
MAP_SHA = "9f327fed260bbd8f69b59dd1d18a8a1fa4191bfc9966c50dda2a5d7bc84da118"
LISTINGS = {
    "mac_epoch": (643, 954, "af972028de085a191358349cb4ddf5fca4bb5e59a500f3e02e2e6df891447b7b"),
    "mac_epoch_test": (2349, 3808, "434070f869908e7c1dd01c807c7c429346cda3617d851814d15508daa42ae97e"),
}
OBJECTS = {
    "mac_epoch": ((954, 22, 14, 0), "e8e1c7fe9a93a5962b7adcca606bbc4f4fb5ca44a292d76b6cc623ef4b75b724"),
    "mac_epoch_test": ((3961, 77, 0, 0), "ae1afb3f4a585f547b3fd448a2b5f69f0ade657cb89b3f7f09277a3edc695ba2"),
}
STORAGE = {
    "mac_epoch": "b511ebdb6bd480bdb33d233b14cf470368b4167f8e5cf55a73bc961050780240",
    "mac_epoch_test": "e77140da7007a459648bcca9d17ba049ac6b3104544f20569fe4f00654e33f22",
}
GUARDS = {0x80: 0x5a, 0x90: 0xa5, 0xa0: 0x69, 0xa8: 0, 0xb8: 0, 0x9a: 0,
          0xc6: 0x88, 0x9e: 0x88, 0x94: 13, 0xc3: 0, 0xe1: 0, 0xd9: 0x69}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_cdb(path):
    raw = path.read_bytes()
    require(sha(raw) == CDB_SHA, "MAC epoch complete raw CDB identity changed")
    return raw.decode("ascii")


def storage_inventory(text):
    return "\n".join(line.strip() for line in text.splitlines()
                     if re.search(r"\b_[\w]+:|\.ds \d+$|\.area ", line))


def verify(image, symbols, debug, memory, listings, objects):
    require(SIZE <= 8192 and sha(code_bytes(image, SIZE)) == CODE_SHA,
            "MAC epoch complete CODE/runtime/constants changed")
    require(sha(debug.encode("ascii")) == CDB_SHA, "MAC epoch complete metadata changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii")) == MAP_SHA,
            "MAC epoch complete map changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "mac_epoch_test_result",
        ("mac_epoch.c", "test_mac_epoch.c"), xdata_budget=512,
    )
    require(symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == 0x21,
            "MAC epoch actual storage/stack changed")
    require(set(listings) == set(objects) == set(MODULES), "MAC epoch composition changed")
    covered = set()
    for module in MODULES:
        text = listings[module]
        require(listing_metrics(text) == LISTINGS[module], "MAC epoch ordered instructions changed")
        require(sha(storage_inventory(text).encode("ascii")) == STORAGE[module],
                "MAC epoch full allocation/entry/helper inventory changed")
        code = records(text)
        require(not peripheral_accesses(dict(code)), "MAC epoch gained peripheral instructions")
        for address, data in code:
            span = set(range(address, address + len(data)))
            require(not covered & span and all(image.get(address+i) == v for i, v in enumerate(data)),
                    "MAC epoch missing/overlapping/non-linked instruction")
            covered.update(span)
        areas = {n: int(s, 16) for n, s in re.findall(
            r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$", objects[module], re.M)}
        extent = sum(s for n, s in areas.items()
                     if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, areas.get("XSEG", 0), areas.get("DSEG", 0), areas.get("OSEG", 0)) ==
                OBJECTS[module][0], "MAC epoch object resource accounting changed")
        require(objects[module].startswith(";!FILE ") and
                sha(objects[module].split("\n", 1)[1].encode("ascii")) == OBJECTS[module][1],
                "MAC epoch complete relocatable object changed")
    caller = dict(records(listings["mac_epoch_test"]))
    for name in ("start", "step"):
        call = b"\x12" + symbols["_mac_epoch_" + name].to_bytes(2, "big")
        require(call in caller.values(), "MAC epoch corpus bypasses real public entry")
    require(caller.get(symbols["_mac_epoch_test_done"]) == b"\0",
            "MAC epoch genuine checkpoint changed")
    return allocated


def rejected(function):
    try:
        function()
    except ValueError:
        return
    raise ValueError("MAC epoch negative control accepted")


def negatives(image, symbols, debug, memory, listings, objects, directory):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory,
                    listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changes)))
        count += 1

    for address in image:
        reject(image=image | {address: image[address] ^ 1})
    reject(image=image | {SIZE: 0})
    reject(image={a: v for a, v in image.items() if a != SIZE - 1})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    for record in debug.splitlines():
        if record.startswith(("F:", "S:", "L:", "T:")):
            reject(debug=debug.replace(record + "\n", "", 1))
            reject(debug=debug + record + "\n")
            reject(debug=debug.replace(record, record + "!", 1))
    reject(memory=memory.replace("223 bytes available", "222 bytes available"))
    with TemporaryDirectory(prefix="mac-epoch-cdb-", dir=directory) as temporary:
        path = Path(temporary) / "raw.cdb"
        for raw in (debug.replace("\n", "\r\n"), debug + "\n", debug + "\0"):
            path.write_bytes(raw.encode("ascii"))
            rejected(lambda: read_cdb(path))
            count += 1
    for module in MODULES:
        lines = listings[module].splitlines(keepends=True)
        positions = [i for i, line in enumerate(lines) if records(line)]
        first, second = positions[:2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = lines.copy()
            if operation == "drop":
                del changed[first]
            elif operation == "duplicate":
                changed.insert(first, changed[first])
            else:
                changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {module: "".join(changed)})
        reject(listings=listings | {module: listings[module].replace(".ds ", ".lost ", 1)})
        reject(objects=objects | {module: objects[module].replace("A XSEG size ", "A XSEG lost ", 1)})
    return count


def check_result(ram, iram, sfr, allocated, symbols):
    require(ram[0x1e00:0x1e06] == b"MEP1\x01\x08", "MAC epoch status ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"Actual SDCC epoch corpus failed at C line {failure}")
    address = symbols["_mac_epoch_test_checks"]
    require(int.from_bytes(ram[address:address+4], "little") == CHECKS, "MAC epoch skipped target checks")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "MAC epoch wrote unused/status-tail/alias XDATA")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x20,
            "MAC epoch SP7C/upper IRAM guard or unwind failed")
    require(all(sfr[a-0x80] == v for a, v in GUARDS.items()), "MAC epoch mutated GPIO/clock/timer/RF")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_epoch_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = read_cdb(path.with_suffix(".cdb"))
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / f"mac_epoch_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings, objects)
    count = negatives(image, symbols, debug, memory, listings, objects, args.output)
    require(count == 15821, "MAC epoch artifact negative inventory changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}",
                "fill iram 0x7d 0xff 0xc7"]
    commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in GUARDS.items()]
    stop = symbols["_mac_epoch_test_done"]
    text = simulate(args.simulator, commands + [f"run {symbols['_main']:#x} {stop:#x}"] +
                    snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    state = snapshot(text, 1)
    check_result(*state, allocated, symbols)
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", section(text, 1))
    require(len(peaks) == 1 and int(peaks[0], 16) == PEAK <= 0x7c,
            "MAC epoch full-run high-water changed")
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, symbols["_mac_epoch_test_checks"]),
                            (0, 0x1dff), (1, 0x7d), (2, 1), (2, 0)):
        changed = [bytearray(s) for s in state]
        changed[region][address] ^= 1
        rejected(lambda: check_result(*changed, allocated, symbols))
    print(f"MAC epoch: {CHECKS} genuine target checks; {SIZE}/8192 CODE; "
          f"{XDATA}+64/512 XDATA; full-run SP={PEAK:02X}/7C; "
          f"{count} artifact + 7 snapshot + 1 alias negatives PASS. "
          "Preserved fractional phase, not captured PHY time or a MAC adapter.")


if __name__ == "__main__":
    main()
