#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify and execute the distinct synthetic interval MAC-TX composition."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from boot_image import (
    ALIAS, check_alias, check_pc, memory_dump, simulate, snapshot_commands,
    verify_component_layout,
)
from boot_mac_tx import instruction_records, rejected, sections
from verify_firmware import parse_ihex, parse_symbols, require, xdata_ranges

SIZE = 26797
IMAGE_SHA = "9b3188b25a30c1c5d47c6b95981ff89726fcb4e18e4a30240a99589a99df2991"
CDB_SHA = "ebc6de5abd9e1273a7ee4b3cfbab741bbec7a5cff24daa1c37d93317ccd99070"
MAP_SHA = "6d447a1a16c03a84b4018be6320c70eb66722ef5acb73680bcd1702b9e425946"
CASES = 52
LISTINGS = {
    "mac_frame": (4262, 7093, "b786900feeec7643055767014d7f05aa6695be92d65143f5ddb15a57fc0afad3",
                  ((98, 7191),)),
    "mac_tx": (7377, 11197, "621b9cdd907f4d336731de8d75e79476b013892d9ff7ec138164972c01211d18",
               ((7191, 18388),)),
    "test_mac_tx_interval": (
        4867, 7831, "6be256ad6762aca3dceb168161804d1f00efddd7b7343441ea7b2d14c2dd5e9d",
        ((0, 6), (95, 98), (18388, 22346), (22370, 26234))),
}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def verify(image, symbols, raw_debug, memory, listings):
    require(sha(raw_debug) == CDB_SHA, "Interval MAC complete raw CDB/ABI changed")
    require(len(image) == SIZE and set(image) == set(range(SIZE)) and SIZE <= 28672,
            "Interval MAC whole CODE extent/budget changed")
    require(sha(bytes(image[a] for a in range(SIZE))) == IMAGE_SHA,
            "Interval MAC complete linked CODE changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "Interval MAC linked symbols/allocations changed")
    debug = raw_debug.decode("utf-8")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "mac_tx_interval_result",
        ("mac_frame.c", "mac_tx.c", "test_mac_tx_interval.c"), xdata_budget=1536,
    )
    require(sum(end - start for start, end in xdata_ranges(symbols)) == 1285
            and symbols["s_SSEG"] == 0x4f,
            "Interval MAC ordinary/IRAM allocation changed")
    require(set(listings) == set(LISTINGS), "Interval MAC listing composition changed")
    covered = set()
    for module, (count, size, digest, ranges) in LISTINGS.items():
        records = instruction_records(listings[module])
        normalized = "".join(f"{at:06x}:{raw.hex()}\n" for at, raw in records).encode("ascii")
        require(len(records) == count and sha(normalized) == digest,
                "Interval MAC instruction identity/order changed: " + module)
        owned = set()
        for at, raw in records:
            region = set(range(at, at + len(raw)))
            require(not (region & owned or region & covered), "Interval MAC overlapping instructions")
            require(all(image.get(at + index) == byte for index, byte in enumerate(raw)),
                    "Interval MAC linked instruction/listing disagreement")
            owned |= region
        require(len(owned) == size
                and owned == {at for start, end in ranges for at in range(start, end)},
                "Interval MAC incomplete instruction coverage")
        covered |= owned
    done = re.findall(r"(?m)^\s*([0-9A-F]{6})\s+\d+\s+_mac_tx_interval_done:",
                      listings["test_mac_tx_interval"])
    require(len(done) == 1 and int(done[0], 16) == symbols["_mac_tx_interval_done"],
            "Interval MAC checkpoint label changed")
    return allocated


def negatives(image, symbols, raw_debug, memory, listings):
    count = 0

    def reject(label, **changed):
        nonlocal count
        arguments = dict(image=image, symbols=symbols, raw_debug=raw_debug, memory=memory, listings=listings)
        arguments.update(changed)
        rejected(lambda: verify(**arguments), label)
        count += 1

    for name in ("_mac_tx_interval_init", "_mac_tx_interval_submit", "_mac_tx_interval_copy",
                 "_mac_tx_interval_step", "_mac_tx_interval_release", "_mac_tx_step",
                 "_mac_frame_decode", "_main", "_mac_tx_interval_done"):
        changed = dict(image)
        changed[symbols[name]] ^= 1
        reject("changed public instruction: " + name, image=changed)
    for at in (0, SIZE - 1):
        changed = dict(image)
        del changed[at]
        reject("truncated CODE", image=changed)
    changed = dict(image)
    changed[SIZE] = 0
    reject("extended CODE", image=changed)
    for name in symbols:
        changed = dict(symbols)
        changed[name] ^= 1
        reject("changed symbol/allocation: " + name, symbols=changed)
    records = raw_debug.splitlines(keepends=True)
    position = 0
    for record in records:
        if record.startswith((b"F:", b"S:", b"L:", b"T:")):
            reject("changed complete declaration/entry/end/type",
                   raw_debug=raw_debug[:position] + b"!" + raw_debug[position + 1:])
        position += len(record)
    reject("missing final raw byte", raw_debug=raw_debug[:-1])
    reject("CRLF normalization", raw_debug=raw_debug.replace(b"\n", b"\r\n"))
    reject("extra helper declaration", raw_debug=raw_debug + b"S:Fmac_tx$unreviewed$0_0$0({1}SC:U),F,0,0\n")
    for module in listings:
        records = listings[module].splitlines(keepends=True)
        indices = [index for index, line in enumerate(records) if instruction_records(line)]
        require(len(indices) > 1, "Interval listing negatives need real instructions")
        for label, replace in (
            ("missing", records[:indices[0]] + records[indices[0] + 1:]),
            ("duplicate", records[:indices[0]] + [records[indices[0]]] + records[indices[0]:]),
        ):
            changed = dict(listings)
            changed[module] = "".join(replace)
            reject(label + " instruction", listings=changed)
        records[indices[0]], records[indices[1]] = records[indices[1]], records[indices[0]]
        changed = dict(listings)
        changed[module] = "".join(records)
        reject("reordered instructions", listings=changed)
    changed = dict(listings)
    changed["test_mac_tx_interval"] = re.sub(
        r"(?m)^(\s*)[0-9A-F]{6}(\s+\d+\s+_mac_tx_interval_done:)",
        r"\g<1>000001\g<2>", changed["test_mac_tx_interval"])
    reject("changed checkpoint", listings=changed)
    reject("missing stack accounting", memory=memory.replace("Stack starts at:", "No stack:"))
    return count


def check_execution(text, symbols, allocated, selected):
    indexed = sections(text)
    check_pc(indexed[1], symbols["_mac_tx_interval_done"])
    ram = memory_dump(indexed[1], 0, 0x1f00)
    iram = memory_dump(indexed[2], 0, 256)
    sfr = memory_dump(indexed[3], 0x80, 128)
    require(ram[0x1e00:0x1e06] == b"MTI1\x01\x08", "Interval MAC result ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"Interval MAC case{selected} failed at C line{failure}")
    require(ram[symbols["_mac_tx_interval_case"]] == selected, "Interval MAC selected case changed")
    require(all(value == 0xa5 for at, value in enumerate(ram) if at not in allocated),
            "Interval MAC wrote outside allocated XDATA/status")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[0x7d:] == b"\xc7" * 131,
            "Interval MAC stack did not unwind or exceeded SP7C")
    require(all(sfr[at - 0x80] == 0 for at in (0xa8, 0xb8, 0x9a, 0x92)),
            "Interval MAC enabled interrupts or changed DPS")
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", indexed[1])
    require(len(peaks) == 1 and int(peaks[0], 16) == 0x67,
            "Interval MAC reviewed stack peak changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_tx_interval_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_bytes()
    memory = path.with_suffix(".mem").read_text()
    listings = {name: (args.output / f"mac_tx_interval_test.{name}.rst").read_text()
                for name in LISTINGS}
    allocated = verify(image, symbols, debug, memory, listings)
    count = negatives(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, alias=False), "missing physical alias")
    for selected in range(CASES):
        text = simulate(args.simulator, [
            ALIAS, "fill xram 0 0x1eff 0xa5",
            f"run 0 {symbols['_main']:#x}",
            f"set memory xram {symbols['_mac_tx_interval_case']:#x} {selected}",
            "fill iram 0x7d 0xff 0xc7",
            f"run {symbols['_main']:#x} {symbols['_mac_tx_interval_done']:#x}",
        ] + snapshot_commands(1), path)
        check_execution(text, symbols, allocated, selected)
    print(f"Interval MAC: {SIZE}/28672 CODE; 1285+64/1536 XDATA; SP67/7C; "
          f"{CASES} genuine case groups; {count} artifact negatives. Synthetic only; never flash.")


if __name__ == "__main__":
    main()
