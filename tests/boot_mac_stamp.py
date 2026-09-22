#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute genuine delayed-sample projection; no peripheral or capture model."""
import argparse
import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_mac_epoch import GUARDS, sha, storage_inventory
from boot_nwk_candidates import listing_metrics, records
from verify_firmware import code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("mac_epoch", "mac_stamp", "mac_stamp_test")
SIZE, XDATA, CHECKS, PEAK = 5169, 167, 23769, 0x3a
CODE_SHA = "b1d280b94831bfb45ea65d9866ee3fb2f9b1e0ce162c60f6f94124dff4c2df5b"
CDB_SHA = "11c3cb68b8b1719870455a9ca6d27146cef9dbbff2d2dae0cc32ae7690b21a20"
MAP_SHA = "d0871a5bbb52c319b4901ceea756a98fb7afd4b930d507d9e3d03f22adf77f13"
LISTINGS = {
    "mac_epoch": (643, 954, "af972028de085a191358349cb4ddf5fca4bb5e59a500f3e02e2e6df891447b7b"),
    "mac_stamp": (242, 397, "3b8cbe86ad4cd7073d033771098cd2d3712a1f25400cc75ff716f7580d69287a"),
    "mac_stamp_test": (2002, 3291, "f633f3a414614702f806791e9e258c8d597fd0d70e4b5de926b56feb98b3fabe"),
}
STORAGE = {
    "mac_epoch": "b511ebdb6bd480bdb33d233b14cf470368b4167f8e5cf55a73bc961050780240",
    "mac_stamp": "7936be3e694ae0f5aa04b970e5fc53acaa7f3e39ea3e68a82f56dde66cb1d9a4",
    "mac_stamp_test": "aee9246bb59ad6e47bc6d9c55b4181643aa137a3c1d161325aa0bb2d0a6c57c5",
}
OBJECTS = {
    "mac_epoch": ((954, 22, 14, 0), "e8e1c7fe9a93a5962b7adcca606bbc4f4fb5ca44a292d76b6cc623ef4b75b724"),
    "mac_stamp": ((397, 31, 1, 0), "ce06d977575a028060c1aaf8a75e7d52acf67eb36d31b0f99074f8f6280cf971"),
    "mac_stamp_test": ((3291, 94, 0, 0), "95aaaa6e6be4e2cc10ac67af1d5650447bb74c613a84671274c4c2292aeb1e0d"),
}


def read_cdb(path):
    raw = path.read_bytes()
    require(sha(raw) == CDB_SHA, "MAC stamp complete raw CDB identity changed")
    return raw.decode("ascii")


def verify(image, symbols, debug, memory, listings, objects):
    require(SIZE <= 8192 and sha(code_bytes(image, SIZE)) == CODE_SHA,
            "MAC stamp complete CODE/runtime/constants changed")
    require(sha(debug.encode("ascii")) == CDB_SHA, "MAC stamp complete metadata changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "MAC stamp complete map changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_stamp_test_result",
        ("mac_epoch.c", "mac_stamp.c", "test_mac_stamp.c"), xdata_budget=256)
    require(symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == 0x21,
            "MAC stamp storage/stack changed")
    require(set(listings) == set(objects) == set(MODULES), "MAC stamp composition changed")
    covered = set()
    for module in MODULES:
        text = listings[module]
        require(listing_metrics(text) == LISTINGS[module] and
                sha(storage_inventory(text).encode("ascii")) == STORAGE[module],
                "MAC stamp ordered instructions/allocation/entry/helper inventory changed")
        code = records(text)
        require(not peripheral_accesses(dict(code)) and not any(
            raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400
            for _, raw in code), "MAC stamp gained peripheral instructions")
        for address, raw in code:
            span = set(range(address, address+len(raw)))
            require(not covered & span and all(image.get(address+i) == v for i, v in enumerate(raw)),
                    "MAC stamp missing/overlapping/non-linked instruction")
            covered.update(span)
        areas = {n: int(s, 16) for n, s in re.findall(
            r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$", objects[module], re.M)}
        extent = sum(s for n, s in areas.items()
                     if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, areas.get("XSEG", 0), areas.get("DSEG", 0), areas.get("OSEG", 0)) ==
                OBJECTS[module][0], "MAC stamp object accounting changed")
        require(objects[module].startswith(";!FILE ") and
                sha(objects[module].split("\n", 1)[1].encode("ascii")) == OBJECTS[module][1],
                "MAC stamp complete relocatable object changed")
    caller = dict(records(listings["mac_stamp_test"]))
    for name in ("mac_epoch_start", "mac_epoch_step", "mac_stamp_project"):
        require(b"\x12" + symbols["_"+name].to_bytes(2, "big") in caller.values(),
                "MAC stamp corpus bypasses real API")
    step = b"\x12" + symbols["_mac_epoch_step"].to_bytes(2, "big")
    require(sum(raw == step for _, raw in records(listings["mac_stamp"])) == 3,
            "MAC stamp must reuse all three real epoch checks")
    require(caller.get(symbols["_mac_stamp_test_done"]) == b"\0", "MAC stamp genuine checkpoint changed")
    return allocated


def rejected(function):
    try:
        function()
    except ValueError:
        return
    raise ValueError("MAC stamp negative accepted")


def negatives(image, symbols, debug, memory, listings, objects, directory):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory, listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changes))); count += 1

    for address in image:
        reject(image=image | {address: image[address] ^ 1})
    reject(image=image | {SIZE: 0})
    reject(image={a: v for a, v in image.items() if a != SIZE-1})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    for line in debug.splitlines():
        if line.startswith(("F:", "S:", "L:", "T:")):
            reject(debug=debug.replace(line+"\n", "", 1))
            reject(debug=debug+line+"\n")
            reject(debug=debug.replace(line, line+"!", 1))
    reject(memory=memory.replace("223 bytes available", "222 bytes available"))
    with TemporaryDirectory(prefix="mac-stamp-cdb-", dir=directory) as temporary:
        path = Path(temporary) / "raw.cdb"
        for raw in (debug.replace("\n", "\r\n"), debug+"\n", debug+"\0"):
            path.write_bytes(raw.encode("ascii")); rejected(lambda: read_cdb(path)); count += 1
    for module in MODULES:
        text = listings[module]; lines = text.splitlines(keepends=True)
        positions = [i for i, line in enumerate(lines) if records(line)]
        first, second = positions[:2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = lines.copy()
            if operation == "drop": del changed[first]
            elif operation == "duplicate": changed.insert(first, changed[first])
            else: changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {module: "".join(changed)})
        reject(listings=listings | {module: text.replace(".ds ", ".lost ", 1)})
        reject(objects=objects | {module: objects[module].replace("A XSEG size ", "A XSEG lost ", 1)})
    return count


def check_result(ram, iram, sfr, allocated, symbols):
    require(ram[0x1e00:0x1e06] == b"MSP1\x01\x08", "MAC stamp result ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"Genuine MAC stamp corpus failed at C line {failure}")
    address = symbols["_mac_stamp_test_checks"]
    require(int.from_bytes(ram[address:address+4], "little") == CHECKS, "MAC stamp target check count changed")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "MAC stamp unused/status-tail/alias XDATA write")
    require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == 0x20, "MAC stamp stack/alias cap or unwind changed")
    require(all(sfr[a-128] == v for a, v in GUARDS.items()), "MAC stamp changed GPIO/clock/timer/RF")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args(); path = args.output / "mac_stamp_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = read_cdb(path.with_suffix(".cdb")); memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / f"mac_stamp_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings, objects)
    count = negatives(image, symbols, debug, memory, listings, objects, args.output)
    require(count == 15644, "MAC stamp artifact rejection inventory changed")
    check_alias(args.simulator); rejected(lambda: check_alias(args.simulator, False))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}",
                "fill iram 0x7d 0xff 0xc7"]
    commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in GUARDS.items()]
    stop = symbols["_mac_stamp_test_done"]
    text = simulate(args.simulator, commands + [f"run {symbols['_main']:#x} {stop:#x}"] +
                    snapshot_commands(1), path)
    check_pc(section(text, 1), stop); state = snapshot(text, 1)
    check_result(*state, allocated, symbols)
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", section(text, 1))
    require(len(peaks) == 1 and int(peaks[0], 16) == PEAK <= 0x7c, "MAC stamp full-run stack changed")
    for region, address in ((0, 0x1e00), (0, 0x1e06), (0, symbols["_mac_stamp_test_checks"]),
                            (0, 0x1dff), (1, 0x7d), (2, 1), (2, 0)):
        changed = [bytearray(s) for s in state]; changed[region][address] ^= 1
        rejected(lambda: check_result(*changed, allocated, symbols))
    print(f"MAC stamp: {CHECKS} genuine target checks; {SIZE}/8192 CODE, {XDATA}+64/256 XDATA; "
          f"full-run SP={PEAK:02X}/7C; {count} artifact + 7 snapshot + 1 alias negatives PASS. "
          "Temporal projection, NOT hardware capture validity or freshness.")


if __name__ == "__main__":
    main()
