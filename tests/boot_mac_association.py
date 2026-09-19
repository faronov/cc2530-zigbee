#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify and execute the real Association Response context/codec composition."""

import argparse
from functools import lru_cache
import hashlib
from pathlib import Path
import re

from boot_image import (
    ALIAS, check_alias, check_pc, memory_dump, simulate, snapshot_commands,
    verify_component_layout,
)
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require

MODULES = ("mac_frame", "mac_association", "mac_association_test")
SOURCES = ("mac_frame.c", "mac_association.c", "test_mac_association.c")
# Independent standalone corpus budget, never an increase to existing components.
CODE_BUDGET, XDATA_BUDGET = 16384, 1024
SIZE, XDATA, PRIVATE_END, CALLER_END = 14022, 675, 0x110, 0x28f
DIGEST = "18921a1d34eab51efc32993ea0513b32c6e2d454bb238406f9ddf88775e3e115"
PRIVATE = "8a41a37b099aa0ff9c5620e981d7d28568ee0f4d95543d940aeb0c7481a985fc"
CALLER_DIGEST = "bf59b3e2ef5acb1578445c855c2acce591bee50d8d599854d16a4d9d01529f5a"
FIELDS = "75cc77b4721e8ca829854b73fa5b772634cc7a6568f9be75f8a1cfe4df9838f4"
LISTINGS = {
    "mac_frame": (4168, 7009, "38ee1522e79fa71f00b1faf3b1bcc40c1c7a1aba2e652373f1559d8dd4bf165e",
                  ((0x62, 0x1bc3),)),
    "mac_association": (1715, 2674, "6b3d9bf8e917b9a61d3bf65f06950af5ec9bf4fda1d660abfbdf8a85f3a3667e",
                        ((0x1bc3, 0x2635),)),
    "mac_association_test": (2166, 3719, "2828458b61d8816bd73ac5c400a6d0c88dc430d6bd23cbd733f454f03be21daa",
                             ((0, 6), (0x5f, 0x62), (0x2635, 0x2adc), (0x2b20, 0x34f7))),
}
# CODE including startup/constants, XSEG, DSEG, OSEG, BSEG bits.
OBJECTS = {"mac_frame": (7009, 207, 15, 10, 1),
           "mac_association": (2674, 65, 25, 0, 1),
           "mac_association_test": (3812, 383, 0, 0, 0)}
# Module, entry, exact CDB end (RET instruction).
PUBLIC = {
    "mac_command_decode": ("mac_frame", 0x22a, 0x424),
    "mac_command_encode": ("mac_frame", 0x425, 0x63a),
    "mac_beacon_decode": ("mac_frame", 0x8e0, 0x947),
    "mac_frame_decode": ("mac_frame", 0x1214, 0x1686),
    "mac_frame_encode": ("mac_frame", 0x195c, 0x1bc2),
    "mac_association_init": ("mac_association", 0x1c05, 0x1c78),
    "mac_association_start": ("mac_association", 0x1c79, 0x1f4c),
    "mac_association_step": ("mac_association", 0x1f4d, 0x25a4),
    "mac_association_take": ("mac_association", 0x25a5, 0x2634),
    "main": ("mac_association_test", 0x34bb, 0x34f6),
}
DONE = 0x34f3
CALLER = {
    "ctx": (0x110, 73), "saved": (0x159, 73), "request": (0x1a2, 30),
    "event": (0x1c0, 20), "record": (0x1d4, 27), "before": (0x1ef, 27),
    "body": (0x20a, 126), "observation": (0x288, 1), "expected": (0x289, 1),
    "scenario": (0x28a, 1), "now": (0x28b, 4),
}
INSTRUCTION = re.compile(
    r"^\s*([0-9A-Fa-f]{6})\s+((?:[0-9A-Fa-f]{2}\s+)+)"
    r"\[\s*\d+\]\s+\d+\s+\S.*$", re.M)
DATA = re.compile(r"^\s*([0-9A-Fa-f]{6})\s+([0-9A-Fa-f]{2})\s+\d+\s+\.db\s+\S.*$", re.M)


def digest(text):
    return hashlib.sha256(text.encode("ascii")).hexdigest()


def abi(debug, pattern):
    return "\n".join(sorted(re.findall(pattern, debug, re.M))) + "\n"


def private_records(debug):
    return abi(debug, r"^[FSL]:(?:X?F|L)(?:mac_frame|mac_association)[.$][^\n]+$")


def caller_records(debug):
    return abi(debug, r"^[FSL]:(?:X?F|L)test_mac_association[.$][^\n]+$")


def field_records(debug):
    return abi(debug, r"^T:F[^\n]+$")


@lru_cache(maxsize=6)
def records(text):
    return tuple((int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION.finditer(text))


def label(text, name):
    found = re.findall(rf"^\s*([0-9A-Fa-f]{{6}})\s+\d+\s+_{re.escape(name)}:\s*$", text, re.M)
    require(len(found) == 1, "Missing/duplicate label: " + name)
    return int(found[0], 16)


def verify(image, symbols, debug, memory, listings, objects):
    raw = code_bytes(image, SIZE)
    require(SIZE <= CODE_BUDGET and hashlib.sha256(raw).hexdigest() == DIGEST, "Whole CODE changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_association_result",
                                        SOURCES, xdata_budget=XDATA_BUDGET)
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == XDATA
            and symbols["s_SSEG"] == 0x44 and symbols["__gptrput_PARM_2"] == 0x29a,
            "Exact allocation/stack/runtime changed")
    require(digest(private_records(debug)) == PRIVATE, "Full private ABI changed")
    require(digest(caller_records(debug)) == CALLER_DIGEST, "Full caller ABI changed")
    require(digest(field_records(debug)) == FIELDS, "Full field ABI changed")
    require(set(listings) == set(objects) == set(MODULES), "Incomplete module set")
    starts, calls, private = {}, {}, set()
    for module in MODULES:
        text = listings[module]
        found = records(text)
        count, size, expected, ranges = LISTINGS[module]
        require(len(found) == count and digest("".join(f"{a:06x}:{b.hex()}\n" for a, b in found))
                == expected, "Ordered instruction set changed: " + module)
        covered = set()
        starts[module], calls[module] = set(), set()
        for address, data in found:
            span = set(range(address, address + len(data)))
            require(not covered.intersection(span), "Duplicate/overlapping instructions")
            require(all(image.get(address + i) == b for i, b in enumerate(data)), "Relocated bytes differ")
            covered.update(span)
            starts[module].add(address)
            if data[0] in (2, 0x12) and len(data) == 3:
                calls[module].add(int.from_bytes(data[1:], "big"))
        require(len(covered) == size
                and covered == {a for low, high in ranges for a in range(low, high)},
                "Incomplete instruction byte coverage")
        if module != "mac_association_test":
            segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                span = set(range(int(address, 16), int(address, 16) + int(size)))
                require(span and not private.intersection(span), "Private allocation overlaps")
                private.update(span)
        areas = {n: int(s, 16) for n, s in re.findall(
            r"^A (\S+) size ([0-9A-Fa-f]+) flags \S+ addr \S+$", objects[module], re.M)}
        code = sum(s for n, s in areas.items()
                   if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((code,) + tuple(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))
                == OBJECTS[module], "Object CODE/private extent changed")
    require(private == set(range(PRIVATE_END)), "Private prefix coverage changed")
    # Real compiler switch table: 34 low bytes, then 34 high bytes. The caller's
    # 25 CODE input bytes also have complete ordered .db records, not instructions.
    data = [(int(m[1], 16), int(m[2], 16)) for m in DATA.finditer(listings["mac_association_test"])]
    require([a for a, _ in data] == list(range(0x2adc, 0x2b20)) + list(range(0x36ad, 0x36c6))
            and all(image[a] == b for a, b in data), "Complete CODE table/constant records changed")
    require(all((image[0x2adc + i] | image[0x2afe + i] << 8) in starts["mac_association_test"]
                for i in range(34)), "Switch table target is not an instruction")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_mac_association${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address
                and f"S:{prefix}({{{size}}}" in debug, "Caller extent changed")
        span = set(range(address, address + size))
        require(not span.intersection(private | caller) and span <= allocated, "Caller overlap/unallocated")
        caller.update(span)
    require(caller == set(range(PRIVATE_END, CALLER_END)), "Caller prefix changed")
    for name, (module, address, end) in PUBLIC.items():
        require(symbols.get("_" + name) == address and label(listings[module], name) == address
                and address in starts[module] and end in starts[module] and image[end] == 0x22
                and cdb_address(debug, f"L:G${name}$0$0") == address
                and cdb_address(debug, f"L:XG${name}$0$0") == end, "Public entry/end ABI changed: " + name)
        declaration = (f"F:G${name}$0_0$0({{2}}DF,SV:S),C,0,0,0,0,0" if name == "main"
                       else f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0")
        require(set(re.findall(rf"^F:G\${re.escape(name)}\$[^\n]*$", debug, re.M)) == {declaration},
                "Conflicting/missing public return ABI")
    require(symbols.get("_mac_association_done") == DONE
            and label(listings["mac_association_test"], "mac_association_done") == DONE
            and DONE in starts["mac_association_test"] and raw[DONE:DONE + 4] == b"\0\x80\xfe\x22",
            "Exact checkpoint changed")
    require(raw[3:6] == bytes((2, PUBLIC["main"][1] >> 8, PUBLIC["main"][1] & 255)),
            "Startup target changed")
    require(all(PUBLIC[n][1] in calls["mac_association"]
                for n in ("mac_frame_decode", "mac_command_decode")), "Real decoder composition missing")
    require(all(PUBLIC[n][1] in calls["mac_association_test"]
                for n in PUBLIC if n.startswith("mac_association_")), "Real caller entries missing")
    return allocated


def rejected(call, name):
    try:
        call()
    except ValueError:
        return
    raise ValueError("Negative control accepted: " + name)


def negatives(image, symbols, debug, memory, listings, objects):
    count = 0

    def reject(name, **changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory,
                    listings=listings, objects=objects)
        args.update(changes)
        rejected(lambda: verify(**args), name)
        count += 1

    reject("missing CODE", image={a: b for a, b in image.items() if a != SIZE - 1})
    reject("extra CODE", image={**image, SIZE: 0})
    corrupt = dict(image); corrupt[PUBLIC["mac_association_step"][1]] ^= 1
    reject("mutated CODE", image=corrupt)
    for old, new in (
        ("S:Lmac_association.mac_association_step$ctx$", "S:Lmac_association.mac_association_step$wrong$"),
        ("F:Fmac_association$valid$", "F:Fmac_association$wrong$"),
        ("L:Fmac_association$valid$", "L:Fmac_association$wrong$"),
        ("L:XFmac_association$valid$", "L:XFmac_association$wrong$"),
        ("S:Ftest_mac_association$ctx$0_0$0({73}", "S:Ftest_mac_association$ctx$0_0$0({72}"),
        ("F:Ftest_mac_association$corpus$", "F:Ftest_mac_association$wrong$"),
        ("{72}S:S$phase", "{71}S:S$phase"),
        ("S:S$body$0_0$0({3}DG", "S:S$body$0_0$0({2}DX"),
    ):
        require(old in debug, "ABI negative did not apply")
        reject("ABI", debug=debug.replace(old, new, 1))
    # Full private/caller/field sets reject dropped AND duplicate records.
    for get in (private_records, caller_records, field_records):
        line = get(debug).splitlines()[0]
        reject("dropped record", debug=debug.replace(line + "\n", "", 1))
        reject("duplicate record", debug=debug + "\n" + line + "\n")
    repeated = []
    for name, (_, address, end) in PUBLIC.items():
        reject("public map address", symbols={**symbols, "_" + name: address + 1})
        declaration = re.findall(rf"^F:G\${name}\$[^\n]*$", debug, re.M)[0]
        other = declaration.replace("DF,SV:S", "DF,SC:U") if name == "main" else declaration.replace("DF,SC:U", "DF,SV:S")
        reject("conflicting return", debug=debug + "\n" + other + "\n")
        repeated.append(declaration)
        for record, value in ((f"L:G${name}$0$0", address), (f"L:XG${name}$0$0", end)):
            for suffix in (f"{value + 1:X}", f"{value:X}:garbage"):
                reject("conflicting/malformed public address", debug=debug + f"\n{record}:{suffix}\n")
            repeated.append(f"{record}:{value:X}")
    # Identical public address/return duplicates are allowed; conflicts are not.
    verify(image, symbols, debug + "\n" + "\n".join(repeated) + "\n", memory, listings, objects)
    reject("checkpoint map", symbols={**symbols, "_mac_association_done": DONE + 1})
    for module in MODULES:
        lines = listings[module].splitlines(keepends=True)
        indexes = [i for i, line in enumerate(lines) if INSTRUCTION.fullmatch(line.rstrip("\n"))]
        first, second = indexes[len(indexes) // 2:len(indexes) // 2 + 2]
        for op in ("drop", "duplicate", "reorder", "mutate"):
            changed = list(lines)
            if op == "drop": del changed[first]
            elif op == "duplicate": changed.insert(first, lines[first])
            elif op == "reorder": changed[first], changed[second] = changed[second], changed[first]
            else:
                changed[first] = re.sub(r"^(\s*[0-9A-Fa-f]{6}\s+)([0-9A-Fa-f]{2})",
                                       lambda m: m[1] + f"{int(m[2], 16) ^ 1:02X}", changed[first])
            reject(op + " listing", listings={**listings, module: "".join(changed)})
    text = listings["mac_association_test"]
    lines = text.splitlines(keepends=True)
    indexes = [i for i, line in enumerate(lines) if DATA.fullmatch(line.rstrip("\n"))]
    for index in (indexes[0], indexes[-1]):  # table and CODE fixture
        reject("dropped data record", listings={**listings, "mac_association_test":
               "".join(lines[:index] + lines[index + 1:])})
        reject("duplicated data record", listings={**listings, "mac_association_test":
               "".join(lines[:index] + [lines[index]] + lines[index:])})
    reject("checkpoint label", listings={**listings, "mac_association_test":
           text.replace("_mac_association_done:", "_wrong_done:", 1)})
    text = objects["mac_association"]
    require("A XSEG size 41 " in text, "Object negative did not apply")
    reject("object extent", objects={**objects, "mac_association":
           text.replace("A XSEG size 41 ", "A XSEG size 40 ", 1)})
    reject("alias allocation", symbols={**symbols, "s_XSEG": 0x1f00})
    require("188 bytes available" in memory, "Stack negative did not apply")
    reject("stack extent", memory=memory.replace("188 bytes available", "187 bytes available"))
    return count


def run(simulator, path, symbols, allocated):
    start = PUBLIC["main"][1]
    text = simulate(simulator, [ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
        f"run 0 {start:#x}", "fill iram 0x80 0xff 0xc7", f"run {start:#x} {DONE:#x}",
    ] + snapshot_commands(1), path)
    # Index the long transcript once. Shared simulate keeps its 15-second timeout.
    marks = list(re.finditer(r"^0x2530([0-9a-fA-F]{4})\r?$", text, re.M))
    require([int(m[1], 16) for m in marks] == [1, 2, 3, 4], "Snapshot markers changed")
    parts = {int(a[1], 16): text[a.end():b.start()] for a, b in zip(marks, marks[1:])}
    check_pc(parts[1], DONE)
    ram = memory_dump(parts[1], 0, 0x1f00)
    iram = memory_dump(parts[2], 0, 256)
    sfr = memory_dump(parts[3], 0x80, 128)
    require(ram[0x1e00:0x1e06] == b"ASR1\x01\x08", "Result ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(not failure, f"Genuine C corpus failed at line {failure}")
    require(ram[CALLER["scenario"][0]] == 34, "Incomplete scenario corpus")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "Unallocated/status-tail write")
    require(iram[128:] == b"\xc7" * 128 and sfr[1] == 0x43, "Upper-IRAM/unwind guard failed")
    require(all(sfr[a - 0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Interrupts enabled")
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", parts[1])
    require(len(peaks) == 1 and int(peaks[0], 16) <= 0x7c, "Stack cap exceeded")
    return int(peaks[0], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_association_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix(ext).read_text() for ext in (".cdb", ".mem"))
    listings = {m: (args.output / f"mac_association_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings, objects)
    count = negatives(image, symbols, debug, memory, listings, objects)
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, alias=False), "missing alias")
    peak = run(args.simulator, path, symbols, allocated)
    print(f"Association context: {SIZE} CODE; {XDATA}+64 XDATA; context73; SP{peak:02X}; "
          f"34 genuine scenarios, complete ABI/listings, {count}+1 negatives PASS. Offline only.")


if __name__ == "__main__":
    main()
