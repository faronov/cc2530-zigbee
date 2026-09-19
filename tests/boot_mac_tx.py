#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check genuine linked MAC-TX instructions/ABI and execute synthetic events."""

import argparse
import hashlib
from pathlib import Path
import re

from boot_image import (
    ALIAS, check_alias, check_pc, simulate, snapshot_commands, verify_component_layout,
    memory_dump,
)
from verify_firmware import cdb_address, parse_ihex, parse_symbols, require, xdata_ranges

SIZE = 28342
DIGEST = "5aef7879004504b02bc8ce2e0d4b649b4aa37e925d039efcd3b04d162993ab37"
PRIVATE_DIGEST = "1fc0a2e73350c02c66fa3dab6596c8e4352423d275ef6d5c08282e12e5d937af"
# Dedicated composition, NOT changes to codec/platform/board budgets.
CODE_BUDGET = 28672
XDATA_BUDGET = 1280
MODULES = ("mac_frame", "mac_tx", "mac_tx_test")
INSTRUCTION_RE = re.compile(
    r"^[ \t]+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})[ \t]+\[[ \t]*\d+\]", re.M)
# Ordered normalized records are "six-hex-address:lowercase-byte-hex\n".
# Pin every reviewed instruction, not an arbitrary surviving subset of a listing.
# The harness also contains non-CSEG startup instructions in listing order.
LISTING_PROOFS = {
    "mac_frame": (4168, 7009, "bcdb6cdd4b8a011b54ccb3ee26726d4ff013d69fba113b70b51037c50038841a",
                  ((0x62, 0x1bc3),)),
    "mac_tx": (5796, 8861, "19bf4007eac51ec46f6afbd9082f227e1ee945b52dfff2af1bfc6c7b781286d8",
               ((0x1bc3, 0x3e60),)),
    "mac_tx_test": (6898, 11765, "e657482b19ee550ce689c973d99f7134136780d3ae24d0f2dc5aae66b146e4f2",
                    ((0, 6), (0x5f, 0x62), (0x3e60, 0x6c4c))),
}
ENTRY_POINTS = {
    "_mac_command_decode": ("mac_frame", 0x22a),
    "_mac_command_encode": ("mac_frame", 0x425),
    "_mac_beacon_decode": ("mac_frame", 0x8e0),
    "_mac_frame_decode": ("mac_frame", 0x1214),
    "_mac_frame_encode": ("mac_frame", 0x195c),
    "_mac_tx_init": ("mac_tx", 0x1c05),
    "_mac_tx_submit": ("mac_tx", 0x1cd3),
    "_mac_tx_copy": ("mac_tx", 0x2244),
    "_mac_tx_step": ("mac_tx", 0x2748),
    "_mac_tx_release": ("mac_tx", 0x3e0b),
    "_main": ("mac_tx_test", 0x6b27),
    "_mac_tx_done": ("mac_tx_test", 0x6c48),
}
CALLER_OBJECTS = {
    "tx": (0x163, 168), "saved": (0x20b, 168), "event": (0x2b3, 17),
    "action": (0x2c4, 22), "saved_action": (0x2da, 22),
    "body": (0x2f0, 125), "copy": (0x36d, 125), "ack": (0x3ea, 4),
}


def private_records(debug):
    return "\n".join(sorted(line for line in debug.splitlines()
                           if re.match(r"^[FSL]:(?:X?F|L)(?:mac_frame|mac_tx)[.$]", line)))


def field_abi(debug, tag, names, sizes):
    records = re.findall(rf"^T:Fmac_tx\${tag}\[(.*)\]$", debug, re.M)
    require(records, "Missing MAC-TX field ABI")
    expected = [(sum(sizes[:i]), n, s) for i, (n, s) in enumerate(zip(names, sizes))]
    for record in records:
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
        require([(int(a), n, int(s)) for a, n, s in actual] == expected,
                "MAC-TX field ABI changed")


def instruction_records(listing):
    return [(int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION_RE.finditer(listing)]


def verify_instructions(module, listing, image):
    records = instruction_records(listing)
    count, size, digest, ranges = LISTING_PROOFS[module]
    normalized = "".join(f"{at:06x}:{raw.hex()}\n" for at, raw in records)
    require(len(records) == count
            and hashlib.sha256(normalized.encode("ascii")).hexdigest() == digest,
            "MAC-TX canonical instruction records/order changed: " + module)
    covered = set()
    for at, raw in records:
        region = set(range(at, at + len(raw)))
        require(not region & covered, "MAC-TX duplicate/overlapping instruction record")
        require(all(image.get(at + i) == byte for i, byte in enumerate(raw)),
                "MAC-TX relocated listing disagrees with linked instructions")
        covered |= region
    require(len(covered) == size
            and covered == {a for start, end in ranges for a in range(start, end)},
            "MAC-TX complete instruction coverage changed: " + module)
    return {at for at, _ in records}


def verify(image, symbols, debug, memory, listings):
    require(len(image) == SIZE and set(image) == set(range(SIZE))
            and SIZE <= CODE_BUDGET, "MAC-TX whole CODE extent/budget changed")
    require(hashlib.sha256(bytes(image[a] for a in range(SIZE))).hexdigest() == DIGEST,
            "MAC-TX whole CODE instructions/constants/runtime changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "mac_tx_result",
        ("mac_frame.c", "mac_tx.c", "test_mac_tx.c"), xdata_budget=XDATA_BUDGET,
    )
    require(hashlib.sha256(private_records(debug).encode()).hexdigest() == PRIVATE_DIGEST,
            "MAC-TX compiler-private storage/ABI changed")
    field_abi(debug, "__00000004",
              ("frame", "last", "deadline", "at", "tx_end", "ready_at", "generation", "stop_at",
               "steps", "phase", "next_dsn", "length", "ack_requested", "nb", "be", "retries",
               "outcome", "transmissions", "uncertain", "pending", "retry_pending", "stop_steps"),
              (125,) + (4,) * 7 + (2,) + (1,) * 13)
    field_abi(debug, "__00000005",
              ("generation", "stamp", "bytes", "length", "kind", "retry", "nb", "value"),
              (4, 4, 3, 2, 1, 1, 1, 1))
    field_abi(debug, "__00000006",
              ("generation", "at", "until", "kind", "retry", "nb", "length", "ack_requested",
               "phase", "outcome", "transmissions", "uncertain", "pending"),
              (4, 4, 4) + (1,) * 10)
    require(set(listings) == set(MODULES), "MAC-TX listing module set changed")
    private = set()
    instruction_starts = {}
    for module, listing in listings.items():
        instruction_starts[module] = verify_instructions(module, listing, image)
        if module != "mac_tx_test":
            segment = listing.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            for address, size in re.findall(
                    r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                region = set(range(int(address, 16), int(address, 16) + int(size)))
                require(region and not region & private, "Overlapping codec/MAC-TX compiler storage")
                private |= region
    for name, (module, address) in ENTRY_POINTS.items():
        require(symbols.get(name) == address and address in instruction_starts[module],
                "MAC-TX linked entry/checkpoint address changed: " + name)
        labels = re.findall(
            rf"^[ \t]+([0-9A-F]{{6}})[ \t]+\d+[ \t]+{re.escape(name)}:[ \t]*$",
            listings[module], re.M)
        require(labels == [f"{address:06X}"], "MAC-TX entry/checkpoint listing label changed: " + name)
        if name != "_mac_tx_done":
            require(cdb_address(debug, f"L:G${name[1:]}$0$0") == address,
                    "MAC-TX entry CDB disagrees with map/instructions: " + name)
        if name.startswith("_mac_tx_") and name != "_mac_tx_done":
            declarations = re.findall(rf"^F:G\${name[1:]}\$.*$", debug, re.M)
            require(declarations == [f"F:G${name[1:]}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0"],
                    "MAC-TX public result ABI changed: " + name)
    require(symbols.get("__sdcc_program_startup") == 3
            and bytes(image[a] for a in range(3, 6))
            == bytes((2, ENTRY_POINTS["_main"][1] >> 8, ENTRY_POINTS["_main"][1] & 0xff)),
            "MAC-TX startup does not jump to the pinned main")
    done = ENTRY_POINTS["_mac_tx_done"][1]
    require(bytes(image[a] for a in range(done, done + 4)) == b"\x00\x80\xfe\x22",
            "MAC-TX final NOP/loop/return checkpoint changed")
    require(private == set(range(355)), "MAC-TX private/compiler prefix changed")
    for name, (address, size) in CALLER_OBJECTS.items():
        declaration = re.findall(rf"^S:Ftest_mac_tx\${name}\$[^\n]*", debug, re.M)
        addresses = re.findall(rf"^L:Ftest_mac_tx\${name}\$[^:]+:([0-9A-F]+)$", debug, re.M)
        require(declaration and all(f"({{{size}}}" in s for s in declaration)
                and len(addresses) == 1 and int(addresses[0], 16) == address,
                "MAC-TX caller ABI missing/changed")
        region = set(range(address, address + size))
        require(not region & private and region <= allocated, "Caller overlaps private prefix/alias")
    require(sum(e - s for s, e in xdata_ranges(symbols)) == 1053
            and symbols["__gptrput_PARM_2"] == 0x410,
            "MAC-TX linked ordinary/generic-store storage changed")
    require(symbols["s_SSEG"] == 0x5a, "MAC-TX stack reservation changed")
    return allocated


def sections(text):
    """Index each long transcript once, not repeated full-text section scans."""
    marks = list(re.finditer(r"^0x2530([0-9a-f]{4})\r?$", text, re.M))
    return {int(a[1], 16): text[a.end():b.start()] for a, b in zip(marks, marks[1:])}


def rejected(call, label):
    try:
        call()
    except (ValueError, AssertionError):
        return
    raise ValueError("Missing negative-control rejection: " + label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_tx_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_text()
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / ("mac_tx_test." + m + ".rst")).read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, alias=False), "missing IRAM alias")
    corrupt = dict(image)
    corrupt[symbols["_mac_tx_step"]] ^= 1
    rejected(lambda: verify(corrupt, symbols, debug, memory, listings), "changed linked CODE")
    rejected(lambda: verify(image, symbols, debug.replace(
        "S:Lmac_tx.mac_tx_step$tx$", "S:Lmac_tx.mac_tx_step$wrong$"), memory, listings),
        "changed private declaration")
    for old, new in (
        ("F:Fmac_frame$command_policy$", "F:Fmac_frame$wrong_command_policy$"),
        ("L:Fmac_tx$stop$0$0:", "L:Fmac_tx$wrong_stop$0$0:"),
        ("L:XFmac_tx$stop$0$0:", "L:XFmac_tx$wrong_stop$0$0:"),
    ):
        require(old in debug, "Private-helper negative did not apply")
        rejected(lambda: verify(image, symbols, debug.replace(old, new), memory, listings),
                 "changed private helper declaration/entry/end")
    rejected(lambda: verify(image, symbols, debug.replace(
        "S:S$generation$0_0$0({4}", "S:S$generation$0_0$0({3}"), memory, listings),
        "changed structure field")
    rejected(lambda: verify(image, symbols, debug.replace(
        "F:G$mac_tx_init$0_0$0({2}DF,SC:U)", "F:G$mac_tx_init$0_0$0({2}DF,SV:S)"),
        memory, listings), "old void initializer ABI")
    changed = dict(listings)
    changed["mac_tx"] = re.sub(
        r"(?m)^(\s+[0-9A-F]{6} )([0-9A-F]{2}) ",
        lambda m: m[1] + f"{int(m[2], 16) ^ 1:02X} ", changed["mac_tx"], count=1)
    rejected(lambda: verify(image, symbols, debug, memory, changed), "changed linked listing")
    for module in MODULES:
        listing = listings[module]
        lines = listing.splitlines(keepends=True)
        indices = [i for i, line in enumerate(lines) if INSTRUCTION_RE.match(line)]
        index, next_index = indices[len(indices) // 2:len(indices) // 2 + 2]
        for label, replacement in (
                ("dropped", lines[:index] + lines[index + 1:]),
                ("duplicated", lines[:index] + [lines[index]] + lines[index:])):
            changed = dict(listings)
            changed[module] = "".join(replacement)
            rejected(lambda: verify(image, symbols, debug, memory, changed),
                     label + " instruction record: " + module)
        reordered = list(lines)
        reordered[index], reordered[next_index] = reordered[next_index], reordered[index]
        changed = dict(listings)
        changed[module] = "".join(reordered)
        rejected(lambda: verify(image, symbols, debug, memory, changed),
                 "reordered instruction records: " + module)
    for name, (_, address) in ENTRY_POINTS.items():
        changed_symbols = dict(symbols)
        # A different zero byte used to satisfy the weak "done points to NOP" test.
        changed_symbols[name] = next(a for a, byte in image.items() if byte == 0 and a != address)
        rejected(lambda: verify(image, changed_symbols, debug, memory, listings),
                 "changed map entry/checkpoint: " + name)
        if name != "_mac_tx_done":
            changed_debug = re.sub(
                rf"(?m)^(L:G\${re.escape(name[1:])}\$0\$0:)[0-9A-Fa-f]+$",
                lambda m: m[1] + f"{address + 1:X}", debug)
            rejected(lambda: verify(image, symbols, changed_debug, memory, listings),
                     "changed CDB entry: " + name)
    changed = dict(listings)
    changed["mac_tx_test"] = re.sub(
        r"(?m)^([ \t]+)[0-9A-F]{6}([ \t]+\d+[ \t]+_mac_tx_done:)",
        r"\g<1>000001\g<2>", changed["mac_tx_test"])
    rejected(lambda: verify(image, symbols, debug, memory, changed), "changed checkpoint label")
    stop = symbols["_mac_tx_done"]
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
        f"run {symbols['_main']:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    indexed = sections(text)
    check_pc(indexed[1], stop)
    ram = memory_dump(indexed[1], 0, 0x1f00)
    iram = memory_dump(indexed[2], 0, 256)
    sfr = memory_dump(indexed[3], 0x80, 128)
    require(ram[0x1e00:0x1e06] == b"MTX1\x01\x08", "MAC-TX result ABI mismatch")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"MAC-TX compiled test failed at C line {failure}")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "MAC-TX wrote outside allocated XDATA/status")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[128:] == b"\xc7" * 128,
            "MAC-TX stack did not unwind or crossed the upper-IRAM guard")
    require(all(sfr[a - 0x80] == 0 for a in (0xa8, 0xb8, 0x9a)),
            "MAC-TX enabled interrupts")
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", indexed[1])
    require(len(peaks) == 1 and int(peaks[0], 16) <= 0x7c, "MAC-TX stack peak exceeded budget")
    ordinary = sum(end - start for start, end in xdata_ranges(symbols))
    print(f"MAC-TX: {SIZE} CODE SHA256={DIGEST}; {ordinary}+64/{XDATA_BUDGET} XDATA; "
          f"private prefix=0..354; stack=5a..ff peak={int(peaks[0], 16):02x}.")
    for module in MODULES:
        areas = re.findall(r"^A (\S+) size ([0-9A-F]+) flags", (args.output / (module + ".rel")).read_text(), re.M)
        print(module, {name: int(size, 16) for name, size in areas if int(size, 16)})
        print("instruction records/covered bytes:", LISTING_PROOFS[module][:2])
    print("Whole linked CODE/private/public ABI, complete ordered instruction records, "
          "drop/duplicate/reorder and checkpoint negatives, genuine compiled vectors, alias negative control, "
          "allocation and upper-IRAM guards PASS. Synthetic only; never flash.")


if __name__ == "__main__":
    main()
