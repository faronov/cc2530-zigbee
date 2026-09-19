#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Pin and execute the real offline scan/TX/collector composition, without RF."""

import argparse
import hashlib
import re
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, memory_dump, simulate, snapshot_commands,
    verify_component_layout,
)
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require

MODULES = ("mac_frame", "mac_tx", "nwk_beacon", "nwk_candidates", "mac_scan", "mac_scan_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_mac_scan.c",)
# A new composition budget only. Existing MAC-TX/codec/platform caps are unchanged.
CODE_BUDGET, XDATA_BUDGET = 32768, 2048
SIZE, XDATA, PRIVATE_END = 32715, 1449, 0x23c
DIGEST = "de6e8e45caa3b86681b45ea9bf2894d6151791630307c3b50b416caff755eb42"
PRIVATE_DIGEST = "17324a1661a6bbaf985a17813d69d18c146eda41666fd2dbd335d25a1fd045ea"
CALLER_DIGEST = "4dbbea8827d3a02b9bc7cb28ea4b1fed6b318a628e3082d5be0a12977c7b69ea"
FIELD_DIGEST = "d773e2e566d7f7ff106cf3c3ac72d438f31bccf4d812644edffe13b3427feaef"
LISTINGS = {
    "mac_frame": (4168, 7009, "4271478eabc4318c0a65388cdd6272ab918c849e682b214be73be81e61beb516"),
    "mac_tx": (5851, 8925, "6b6aaff1df3233c02bafb7ef1c356cbf160a5b551286cb4303b99d79eb535a26"),
    "nwk_beacon": (353, 601, "bd766740a2a26b4ec63fac202605b95b027efbb35c33e41602a2b29449feb9aa"),
    "nwk_candidates": (1519, 2334, "89c8de3cff79c68f2313e7f4f8eb9901736c697b1e20780d0b7f4e295d28666a"),
    "mac_scan": (6230, 8590, "36cc8a86e89dcbb1fa73e3718f7347a99c5da46bd7d277b492e42e70db84f321"),
    "mac_scan_test": (2686, 4483, "a4ab247faddd8128c2092403e2abb5a715f654bb66ed97d75a7f66fb80c1f49a"),
}
OBJECTS = {
    "mac_frame": (7009, 207, 15, 10),
    "mac_tx": (8925, 148, 33, 5),
    "nwk_beacon": (601, 27, 9, 0),
    "nwk_candidates": (2334, 111, 4, 0),
    "mac_scan": (8598, 79, 4, 0),
    "mac_scan_test": (4581, 851, 4, 0),
}
ENTRIES = {
    "mac_command_decode": ("mac_frame", 0x022a), "mac_command_encode": ("mac_frame", 0x0425),
    "mac_beacon_decode": ("mac_frame", 0x08e0), "mac_frame_decode": ("mac_frame", 0x1214),
    "mac_frame_encode": ("mac_frame", 0x195c),
    "mac_tx_init": ("mac_tx", 0x1c05), "mac_tx_submit": ("mac_tx", 0x1cd3),
    "mac_tx_copy": ("mac_tx", 0x2284), "mac_tx_step": ("mac_tx", 0x2788),
    "mac_tx_release": ("mac_tx", 0x3e4b), "nwk_beacon_decode": ("nwk_beacon", 0x3ea0),
    "nwk_candidates_init": ("nwk_candidates", 0x41d7),
    "nwk_candidates_consider": ("nwk_candidates", 0x4417),
    "nwk_candidates_get": ("nwk_candidates", 0x494c),
    "mac_scan_init": ("mac_scan", 0x4eff), "mac_scan_start": ("mac_scan", 0x4f53),
    "mac_scan_step": ("mac_scan", 0x5504), "mac_scan_get": ("mac_scan", 0x6a16),
    "mac_scan_release": ("mac_scan", 0x6aab), "main": ("mac_scan_test", 0x6ba5),
}
DONE = 0x7d1b
CALLER = {
    "scan": (0x23c, 212), "saved": (0x310, 212), "tx": (0x3e4, 168),
    "request": (0x48c, 16), "event": (0x49c, 23), "action": (0x4b3, 23),
    "before": (0x4ca, 23), "tx_event": (0x4e1, 17), "tx_action": (0x4f2, 22),
    "entry": (0x508, 36), "body": (0x52c, 44), "copy": (0x558, 36),
    "length": (0x57c, 1), "scenario": (0x57d, 1), "injected": (0x57e, 1),
    "received": (0x57f, 1), "dsn": (0x580, 1), "j": (0x581, 1),
    "was_radio": (0x582, 1), "iterations": (0x583, 2), "failure": (0x585, 2),
    "now": (0x587, 4), "floor_at": (0x58b, 4),
}
INSTRUCTION = re.compile(
    r"^\s*([0-9A-Fa-f]{6})\s+((?:[0-9A-Fa-f]{2}\s+)+)"
    r"\[\s*\d+\]\s+\d+\s+\S.*$", re.MULTILINE,
)


def digest(text):
    return hashlib.sha256(text.encode("ascii")).hexdigest()


def abi_records(debug, pattern):
    return "\n".join(sorted(re.findall(pattern, debug, re.MULTILINE))) + "\n"


def private_records(debug):
    return abi_records(
        debug, r"^[FSL]:(?:X?F|L)(?:mac_frame|mac_tx|nwk_beacon|nwk_candidates|mac_scan)[.$][^\n]+$",
    )


def caller_records(debug):
    return abi_records(debug, r"^[SL]:[FL]test_mac_scan[.$][^\n]+$")


def field_records(debug):
    return abi_records(debug, r"^T:F[^\n]+$")


def records(text):
    return [(int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION.finditer(text)]


def listing_metrics(text):
    found = records(text)
    return (len(found), sum(len(data) for _, data in found),
            digest("".join(f"{address:06x}:{data.hex()}\n" for address, data in found)))


def label(text, name):
    found = re.findall(rf"^\s*([0-9A-Fa-f]{{6}})\s+\d+\s+_{name}:\s*$", text, re.MULTILINE)
    require(len(found) == 1, f"Missing/duplicate checkpoint/entry label: {name}")
    return int(found[0], 16)


def verify(image, symbols, debug, memory, listings, objects):
    raw = code_bytes(image, SIZE)
    require(SIZE <= CODE_BUDGET and hashlib.sha256(raw).hexdigest() == DIGEST,
            "Whole scan CODE hash changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "mac_scan_result", SOURCES, xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == XDATA
            and symbols["s_SSEG"] == 0x59 and symbols["__gptrput_PARM_2"] == 0x59a,
            "Exact ordinary/stack/runtime allocation changed")
    require(digest(private_records(debug)) == PRIVATE_DIGEST, "Private ABI changed")
    require(digest(caller_records(debug)) == CALLER_DIGEST, "Caller ABI changed")
    require(digest(field_records(debug)) == FIELD_DIGEST, "Whole field ABI changed")
    require(set(listings) == set(objects) == set(MODULES), "Incomplete module set")
    starts, calls, private = {}, {}, set()
    for module in MODULES:
        text = listings[module]
        require(listing_metrics(text) == LISTINGS[module], "Complete instruction records changed: " + module)
        starts[module] = set()
        covered = set()
        calls[module] = set()
        for address, data in records(text):
            span = set(range(address, address + len(data)))
            require(not span.intersection(covered), "Duplicate/overlapping instruction coverage")
            require(all(image.get(address + i) == byte for i, byte in enumerate(data)),
                    "Relocated instruction differs from CODE")
            covered.update(span)
            starts[module].add(address)
            if data[0] in (0x02, 0x12) and len(data) == 3:
                calls[module].add(int.from_bytes(data[1:], "big"))
        if module != "mac_scan_test":
            section = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            for address, size in re.findall(
                r"^\s+([0-9A-Fa-f]{6})\s+\d+\s+\.ds (\d+)$", section, re.MULTILINE,
            ):
                span = set(range(int(address, 16), int(address, 16) + int(size)))
                require(span and not span.intersection(private), "Compiler-private overlap")
                private.update(span)
        areas = {name: int(size, 16) for name, size in re.findall(
            r"^A (\S+) size ([0-9A-Fa-f]+) flags \S+ addr \S+$", objects[module], re.MULTILINE,
        )}
        code = sum(size for name, size in areas.items()
                   if name in ("HOME", "GSFINAL", "CSEG", "CONST") or name.startswith("GSINIT"))
        require((code, areas.get("XSEG", 0), areas.get("DSEG", 0), areas.get("OSEG", 0))
                == OBJECTS[module], "Object extent/ABI changed: " + module)
    require(private == set(range(PRIVATE_END)), "Private prefix coverage changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_mac_scan${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address
                and f"S:{prefix}({{{size}}}" in debug, "Caller object ABI changed: " + name)
        span = set(range(address, address + size))
        require(not span.intersection(private | caller) and span <= allocated, "Caller storage overlap")
        caller.update(span)
    require(caller == set(range(PRIVATE_END, 0x58f)), "Caller coverage changed")
    for name, (module, address) in ENTRIES.items():
        require(symbols.get("_" + name) == address and label(listings[module], name) == address
                and cdb_address(debug, f"L:G${name}$0$0") == address
                and address in starts[module], "Entry/checkpoint address ABI changed: " + name)
        declaration = (f"F:G${name}$0_0$0({{2}}DF,SV:S),C,0,0,0,0,0" if name == "main"
                       else f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0")
        declarations = set(re.findall(rf"^F:G\${re.escape(name)}\$[^\n]*$", debug, re.MULTILINE))
        require(declarations == {declaration}, "Public return ABI changed: " + name)
    require(symbols["_mac_scan_done"] == DONE and label(listings["mac_scan_test"], "mac_scan_done") == DONE
            and DONE in starts["mac_scan_test"] and raw[DONE:DONE + 4] == b"\0\x80\xfe\x22"
            and cdb_address(debug, "L:XG$main$0$0") == DONE + 3, "Exact final checkpoint ABI changed")
    require(raw[3:6] == bytes((2, ENTRIES["main"][1] >> 8, ENTRIES["main"][1] & 255)),
            "Startup target changed")
    for module, names in (
        ("mac_scan", ("mac_tx_submit", "mac_tx_release", "nwk_candidates_init",
                      "nwk_candidates_consider", "nwk_candidates_get")),
        ("mac_scan_test", ("mac_tx_init", "mac_tx_step", "mac_tx_copy",
                           "mac_scan_init", "mac_scan_start", "mac_scan_step",
                           "mac_scan_get", "mac_scan_release")),
        ("mac_tx", ("mac_frame_decode",)),
        ("nwk_candidates", ("mac_frame_decode", "mac_beacon_decode", "nwk_beacon_decode")),
    ):
        require(all(ENTRIES[name][1] in calls[module] for name in names),
                "Required real composition call missing: " + module)
    require(ENTRIES["mac_tx_step"][1] not in calls["mac_scan"]
            and ENTRIES["mac_tx_init"][1] not in calls["mac_scan"],
            "Serialized pump boundary or persistent DSN ownership changed")
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

    reject("CODE missing", image={a: b for a, b in image.items() if a != SIZE - 1})
    reject("CODE extra", image={**image, SIZE: 0})
    damaged = dict(image); damaged[ENTRIES["mac_scan_step"][1]] ^= 1
    reject("CODE mutation", image=damaged)
    for name in tuple(ENTRIES) + ("mac_scan_done",):
        changed = dict(symbols)
        changed["_" + name] = next(a for a, b in image.items()
                                  if b == 0 and a != symbols["_" + name])
        reject("entry moved to arbitrary NOP: " + name, symbols=changed)
    for old, new in (
        ("S:Lmac_scan.mac_scan_step$scan$", "S:Lmac_scan.mac_scan_step$wrong$"),
        ("S:Fmac_scan$beacon_request$0_0$0({8}", "S:Fmac_scan$beacon_request$0_0$0({7}"),
        ("L:Fmac_scan$beacon_request$0_0$0:7F61", "L:Fmac_scan$beacon_request$0_0$0:7F62"),
        ("F:Fmac_scan$stop$", "F:Fmac_scan$wrong_stop$"),
        ("L:XFmac_scan$stop$", "L:XFmac_scan$wrong_stop$"),
        ("{199}S:S$token", "{198}S:S$token"),
        ("({212}ST", "({211}ST"),
        ("F:G$mac_scan_init$0_0$0({2}DF,SC:U)", "F:G$mac_scan_init$0_0$0({2}DF,SV:S)"),
        ("L:G$mac_scan_step$0$0:5504", "L:G$mac_scan_step$0$0:5505"),
        ("L:XG$main$0$0:7D1E", "L:XG$main$0$0:7D1F"),
    ):
        require(old in debug, "Debug negative did not apply")
        reject("debug ABI", debug=debug.replace(old, new, 1))
    duplicates = []
    for name in ENTRIES:
        declarations = re.findall(rf"^F:G\${re.escape(name)}\$[^\n]*$", debug, re.MULTILINE)
        require(len(set(declarations)) == 1, "Conflicting-return negative needs a valid original")
        original = declarations[0]
        conflicting = (original.replace("DF,SV:S", "DF,SC:U") if name == "main"
                       else original.replace("DF,SC:U", "DF,SV:S"))
        require(conflicting != original, "Conflicting-return negative did not apply")
        reject("appended conflicting return: " + name, debug=debug + "\n" + conflicting + "\n")
        duplicates.append(original)
    # Identical repeated records remain valid; only a conflicting declaration
    # must fail. Exercise all20 entries together without changing any ABI digest.
    verify(image, symbols, debug + "\n" + "\n".join(duplicates) + "\n", memory, listings, objects)
    for module in MODULES:
        lines = listings[module].splitlines(keepends=True)
        indices = [i for i, line in enumerate(lines) if INSTRUCTION.fullmatch(line.rstrip("\n"))]
        first, second = indices[len(indices) // 2:len(indices) // 2 + 2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = list(lines)
            if operation == "drop": del changed[first]
            elif operation == "duplicate": changed.insert(first, lines[first])
            else: changed[first], changed[second] = changed[second], changed[first]
            altered = dict(listings); altered[module] = "".join(changed)
            reject(operation + " record: " + module, listings=altered)
    altered = dict(listings)
    altered["mac_scan_test"] = listings["mac_scan_test"].replace("_mac_scan_done:", "_wrong_done:", 1)
    reject("checkpoint label", listings=altered)
    altered = dict(objects)
    require("A XSEG size 4F " in altered["mac_scan"], "Object negative did not apply")
    altered["mac_scan"] = altered["mac_scan"].replace("A XSEG size 4F ", "A XSEG size 4E ", 1)
    reject("object extent", objects=altered)
    changed = dict(symbols); changed["s_XSEG"] = 0x1f00
    reject("aliased XDATA", symbols=changed)
    require("167 bytes available" in memory, "Stack negative did not apply")
    reject("stack report", memory=memory.replace("167 bytes available", "166 bytes available"))
    return count


def run(simulator, path, symbols, allocated):
    start = symbols["_main"]
    text = simulate(simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
        f"run 0 {start:#x}", "fill iram 0x80 0xff 0xc7", f"run {start:#x} {DONE:#x}",
    ] + snapshot_commands(1), path)
    # Index the transcript exactly once.
    marks = list(re.finditer(r"^0x2530([0-9a-fA-F]{4})\r?$", text, re.MULTILINE))
    require([int(m[1], 16) for m in marks] == [1, 2, 3, 4], "Snapshot markers changed")
    sections = {int(a[1], 16): text[a.end():b.start()] for a, b in zip(marks, marks[1:])}
    check_pc(sections[1], DONE)
    ram = memory_dump(sections[1], 0, 0x1f00)
    iram = memory_dump(sections[2], 0, 256)
    sfr = memory_dump(sections[3], 0x80, 128)
    require(ram[0x1e00:0x1e06] == b"SCN1\x01\x08", "Result ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(not failure, f"Real scan corpus failed at C line {failure}")
    require(ram[CALLER["scenario"][0]] == 28, "Did not execute the complete scenario corpus")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Unallocated/status-tail write")
    require(iram[128:] == b"\xc7" * 128 and sfr[1] == 0x58, "Upper IRAM/unwind guard failed")
    require(sfr[0xa8 - 0x80] == sfr[0xb8 - 0x80] == sfr[0x9a - 0x80] == 0,
            "Interrupts became enabled")
    peak = re.search(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", sections[1])
    require(peak is not None and int(peak[1], 16) == 0x7a
            and int(peak[1], 16) <= 0x7c, "Reviewed stack high-water/cap changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_scan_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix(ext).read_text() for ext in (".cdb", ".mem"))
    listings = {m: (args.output / f"mac_scan_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings, objects)
    count = negatives(image, symbols, debug, memory, listings, objects)
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, alias=False), "missing alias")
    run(args.simulator, path, symbols, allocated)
    print(f"MAC scan: {SIZE} CODE, {XDATA}+64 XDATA, context212, SP7A; "
          f"28 real composition scenarios, whole ABI/listings and {count}+1 negatives "
          "PASS (simulation only).")


if __name__ == "__main__":
    main()
