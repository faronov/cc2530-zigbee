#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Pin and execute the offline collector/real-codec composition; no radio model."""

import argparse
import hashlib
import re
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, memory_dump, simulate, snapshot_commands,
    verify_component_layout,
)
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("mac_frame", "nwk_beacon", "nwk_candidates", "nwk_candidates_test")
SOURCES = ("mac_frame.c", "nwk_beacon.c", "nwk_candidates.c", "test_nwk_candidates.c")
# Separate composed-corpus budgets, NOT increases to existing codec budgets.
CODE_BUDGET = 20480
XDATA_BUDGET = 1280
# Codec lowering reviewed in boot_mac_tx: -43 CODE/-3 object DATA, unchanged
# collector/caller ABI, ordinary XDATA and actual linked stack start.
CODE_SIZE = 18433
CODE_SHA256 = '857e1f5f9cc670610268987417e192cfc8909dbfe9d335b387704816982a650f'
PRIVATE_SHA256 = '94d4e5046b120832e25918832f42c538d96d39973ccebf33239c18a09a4e789e'
CALLER_SHA256 = '0eed6c75e32427ef35a592f4ec3473c925ea66819302c4e2e89992d5451109ad'
FIELDS_SHA256 = 'ff3aa08d250d6bd3d54c9a021d1a81dfe64fbea00541d7cebae177a3cdda8014'
PUBLIC = {'mac_command_decode': ('mac_frame', 554),
 'mac_command_encode': ('mac_frame', 1018),
 'mac_beacon_decode': ('mac_frame', 2229),
 'mac_frame_decode': ('mac_frame', 5771),
 'mac_frame_encode': ('mac_frame', 6576),
 'nwk_beacon_decode': ('nwk_beacon', 7191),
 'nwk_candidates_init': ('nwk_candidates', 8014),
 'nwk_candidates_consider': ('nwk_candidates', 8590),
 'nwk_candidates_get': ('nwk_candidates', 9923),
 'main': ('nwk_candidates_test', 17833),
 'mac_frame_decode_profile': ('mac_frame', 4601)}
DONE = 17889
# Per module: complete ordered instruction count, byte coverage, digest.
LISTINGS = {'mac_frame': (4262, 7093, 'b5348652d47961d79d551262faa9f5e17a45e0a5de97d99b32c44a41b72e0f3a'),
 'nwk_beacon': (353, 601, 'e5ad4cc3fbcb7b4c7031d32e92b28a5b53de49ef3f675c5b11c7d3118ee7867b'),
 'nwk_candidates': (1519, 2334, '09db951b18b9e2dc6a0900b163e436c14544aca716a2499f982a6f2d33632930'),
 'nwk_candidates_test': (4173, 7776, '1246096cda54985da5643cea67afcab1fb6df9530a017b14c43d16a93756c644')}
# CODE (including CONST/startup contributions), XSEG, DSEG, OSEG.
OBJECTS = {'mac_frame': (7093, 216, 12, 10),
 'nwk_beacon': (601, 27, 9, 0),
 'nwk_candidates': (2334, 111, 4, 0),
 'nwk_candidates_test': (7846, 612, 0, 0)}
CALLER_OBJECTS = {'table': (354, 150),
 'saved': (504, 150),
 'entry': (654, 36),
 'saved_entry': (690, 36),
 'header': (726, 26),
 'body': (752, 126),
 'payload': (878, 75),
 'length': (953, 1),
 'i': (954, 1),
 'j': (955, 1),
 'k': (956, 1),
 'mode': (957, 1),
 'shorts': (958, 1),
 'extendeds': (959, 1),
 'position': (960, 1)}
INSTRUCTION = re.compile(
    r"^\s*([0-9A-Fa-f]{6})\s+((?:[0-9A-Fa-f]{2}\s+)+)"
    r"\[\s*\d+\]\s*\d+\s+\S.*$", re.MULTILINE,
)


def digest(text):
    return hashlib.sha256(text.encode("ascii")).hexdigest()


def records(text):
    return [(int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION.finditer(text)]


def listing_metrics(text):
    found = records(text)
    normalized = "".join(f"{address:06x}:{data.hex()}\n" for address, data in found)
    return len(found), sum(len(data) for _, data in found), digest(normalized)


def abi_records(debug, pattern):
    # Sort complete records, preserving duplicates. No silent dropping of records.
    return "\n".join(sorted(re.findall(pattern, debug, re.MULTILINE))) + "\n"


def private_records(debug):
    return abi_records(debug, r"^[SL]:L(?:mac_frame|nwk_beacon|nwk_candidates)\.[^\n]+$")


def caller_records(debug):
    return abi_records(debug, r"^[SL]:[FL]test_nwk_candidates[.$][^\n]+$")


def field_records(debug):
    return abi_records(debug, r"^T:F[^\n]+$")


def label(text, name):
    found = re.findall(rf"^\s*([0-9A-Fa-f]{{6}})\s+\d+\s+_{name}:$", text, re.MULTILINE)
    require(len(found) == 1, f"Missing/duplicate listing label: {name}")
    return int(found[0], 16)


def field_layout(debug, suffix, expected):
    found = re.findall(rf"^T:Fnwk_candidates\$__000000{suffix}\[(.*)\]$", debug, re.MULTILINE)
    require(len(found) == 1, "Missing/duplicate collector field ABI")
    actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
    require(tuple((int(offset), name, int(size)) for offset, name, size in actual) == expected,
            "Collector field ABI changed")


def verify(image, symbols, debug, memory, listings, objects):
    raw = code_bytes(image, CODE_SIZE)
    require(len(raw) <= CODE_BUDGET and hashlib.sha256(raw).hexdigest() == CODE_SHA256,
            "Whole collector CODE digest changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "nwk_candidates_test_result", SOURCES,
        xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 988
            and symbols["s_SSEG"] == 0x2f, "Composed XDATA/stack extent changed")
    require(symbols.get("__gptrput_PARM_2") == 0x3d1, "Runtime scratch address changed")
    require(digest(private_records(debug)) == PRIVATE_SHA256, "Private storage ABI changed")
    require(digest(caller_records(debug)) == CALLER_SHA256, "Caller storage ABI changed")
    require(digest(field_records(debug)) == FIELDS_SHA256, "Field ABI digest changed")
    field_layout(debug, "00", (
        (0, "stack_profile", 1), (1, "router_capacity", 1), (2, "device_depth", 1),
        (3, "end_device_capacity", 1), (4, "extended_pan_id", 8),
        (12, "tx_offset", 4), (16, "update_id", 1),
    ))
    field_layout(debug, "01", (
        (0, "channel", 1), (1, "pan_id", 2), (3, "address_mode", 1),
        (4, "coordinator", 8), (12, "superframe", 2), (14, "sequence", 1),
        (15, "mac_flags", 1), (16, "gts_permit", 1), (17, "short_pending", 1),
        (18, "extended_pending", 1), (19, "network", 17),
    ))
    field_layout(debug, "02", (
        (0, "version", 1), (1, "channel_mask", 4), (5, "count", 1), (6, "entries", 144),
    ))
    # The actual addressed XDATA records cover the complete compiler-private prefix.
    # Some CDB declarations describe register-only values and have no L: record.
    private = set()
    for match in re.finditer(
        r"^S:(L(?:mac_frame|nwk_beacon|nwk_candidates)\.[^(\n]+)"
        r"\(\{(\d+)\}[^\n]*\),F,0,0$", debug, re.MULTILINE,
    ):
        if re.search(rf"^L:{re.escape(match[1])}:", debug, re.MULTILINE):
            start = cdb_address(debug, "L:" + match[1])
            private.update(range(start, start + int(match[2])))
    require(private == set(range(354)), "Compiler-private XDATA prefix changed")
    caller = set()
    for name, (address, size) in CALLER_OBJECTS.items():
        prefix = f"Ftest_nwk_candidates${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address
                and f"S:{prefix}({{{size}}}" in debug, f"Caller object ABI changed: {name}")
        span = set(range(address, address + size))
        require(not span.intersection(private | caller), "Caller storage overlaps")
        caller.update(span)
    require(caller == set(range(354, 961)), "Caller object coverage changed")
    # Pin all module instructions, not a conveniently chosen subset of lines.
    instructions = {}
    require(set(listings) == set(objects) == set(MODULES), "Module set changed")
    for module in MODULES:
        text = listings[module]
        require(listing_metrics(text) == LISTINGS[module],
                f"Canonical instruction records changed: {module}")
        for address, data in records(text):
            require(address not in instructions, "Duplicate instruction address")
            require(all(image.get(address + i) == byte for i, byte in enumerate(data)),
                    "Instruction bytes differ from linked CODE")
            instructions[address] = data
        areas = dict((name, int(size, 16)) for name, size in re.findall(
            r"^A (\S+) size ([0-9A-Fa-f]+) flags \S+ addr \S+$", objects[module], re.MULTILINE,
        ))
        code = sum(size for name, size in areas.items()
                   if name in ("HOME", "GSFINAL", "CSEG", "CONST") or name.startswith("GSINIT"))
        require((code, areas.get("XSEG", 0), areas.get("DSEG", 0), areas.get("OSEG", 0))
                == OBJECTS[module], f"Object storage/extent changed: {module}")
    for name, (module, address) in PUBLIC.items():
        require(symbols.get("_" + name) == address
                and cdb_address(debug, f"L:G${name}$0$0") == address
                and label(listings[module], name) == address and address in instructions,
                f"Public/checkpoint address ABI changed: {name}")
        declaration = (f"F:G${name}$0_0$0({{2}}DF,SV:S),C,0,0,0,0,0" if name == "main"
                       else f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0")
        require(declaration in debug, f"Public return ABI changed: {name}")
    require(symbols.get("_nwk_candidates_test_done") == DONE
            and label(listings["nwk_candidates_test"], "nwk_candidates_test_done") == DONE
            and instructions.get(DONE) == b"\0" and raw[DONE:DONE + 3] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == DONE + 3,
            "Exact checkpoint ABI changed")
    # All three real decoder calls are inside the collector's consider extent.
    calls = {int.from_bytes(data[1:], "big") for address, data in instructions.items()
             if PUBLIC["nwk_candidates_consider"][1] <= address < PUBLIC["nwk_candidates_get"][1]
             and len(data) == 3 and data[0] == 0x12}
    require(all(PUBLIC[name][1] in calls
                for name in ("mac_frame_decode", "mac_beacon_decode", "nwk_beacon_decode")),
            "Collector no longer composes all three real decoders")
    return allocated


def rejected(call, name):
    try:
        call()
    except ValueError:
        return
    raise ValueError(f"Negative control was accepted: {name}")


def negative_controls(image, symbols, debug, memory, listings, objects):
    count = 0

    def reject(name, **changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory,
                    listings=listings, objects=objects)
        args.update(changes)
        rejected(lambda: verify(**args), name)
        count += 1

    reject("missing CODE", image={k: v for k, v in image.items() if k != CODE_SIZE - 1})
    reject("extra CODE", image={**image, CODE_SIZE: 0})
    damaged = dict(image)
    damaged[PUBLIC["nwk_candidates_consider"][1]] ^= 1
    reject("changed instruction", image=damaged)
    for name in tuple(PUBLIC) + ("nwk_candidates_test_done",):
        damaged_symbols = dict(symbols)
        damaged_symbols["_" + name] += 1
        reject("map address " + name, symbols=damaged_symbols)
    damaged_symbols = dict(symbols)
    damaged_symbols["_nwk_candidates_test_done"] = next(
        address for address, byte in image.items() if byte == 0 and address != DONE)
    reject("arbitrary NOP checkpoint", symbols=damaged_symbols)
    for old, new in (
        ("{5}S:S$count", "{4}S:S$count"),
        ("({150}ST", "({149}ST"),
        ("S:Lnwk_candidates.nwk_candidates_consider$table$", "S:Lnwk_candidates.nwk_candidates_consider$bad$"),
        ("F:G$nwk_candidates_init$0_0$0({2}DF,SC:U)", "F:G$nwk_candidates_init$0_0$0({2}DF,SV:S)"),
        (f"L:G$nwk_candidates_consider$0$0:{PUBLIC['nwk_candidates_consider'][1]:X}",
         f"L:G$nwk_candidates_consider$0$0:{PUBLIC['nwk_candidates_consider'][1] + 1:X}"),
        (f"L:XG$main$0$0:{DONE + 3:X}", f"L:XG$main$0$0:{DONE + 4:X}"),
    ):
        require(old in debug, "CDB negative mutation did not apply")
        reject("debug ABI", debug=debug.replace(old, new, 1))
    for module in MODULES:
        lines = listings[module].splitlines(keepends=True)
        positions = [i for i, line in enumerate(lines) if INSTRUCTION.fullmatch(line.rstrip("\n"))]
        require(len(positions) > 2, "Cannot mutate listing instructions")
        first, second = positions[:2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = list(lines)
            if operation == "drop":
                del changed[first]
            elif operation == "duplicate":
                changed.insert(first, lines[first])
            else:
                changed[first], changed[second] = changed[second], changed[first]
            bad_listings = dict(listings)
            bad_listings[module] = "".join(changed)
            reject(operation + " instruction " + module, listings=bad_listings)
    bad_listings = dict(listings)
    bad_listings["nwk_candidates_test"] = listings["nwk_candidates_test"].replace(
        "_nwk_candidates_test_done:", "_not_the_checkpoint:", 1)
    reject("checkpoint label", listings=bad_listings)
    damaged_symbols = dict(symbols)
    damaged_symbols["s_XSEG"] = 0x1f00
    reject("aliased ordinary XDATA", symbols=damaged_symbols)
    reject("stack report", memory=memory.replace("209 bytes available", "208 bytes available"))
    return count


def indexed_sections(text):
    # Index the long transcript once, instead of rescanning it for each dump.
    boundaries = list(re.finditer(r"^0x2530([0-9a-fA-F]{4})\r?$", text, re.MULTILINE))
    require([int(boundary[1], 16) for boundary in boundaries] == [1, 2, 3, 4],
            "Missing/duplicate/reordered snapshot markers")
    return {int(left[1], 16): text[left.end() + 1:right.start()]
            for left, right in zip(boundaries, boundaries[1:])}


def run_corpus(simulator, path, symbols, allocated):
    start = symbols["_main"]
    text = simulate(simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
        f"run 0 {start:#x}", "fill iram 0x80 0xff 0xc7", f"run {start:#x} {DONE:#x}",
    ] + snapshot_commands(1), path)
    sections = indexed_sections(text)
    require(set(sections) == {1, 2, 3}, "Missing/duplicate corpus snapshots")
    check_pc(sections[1], DONE)
    ram = memory_dump(sections[1], 0, 0x1f00)
    iram = memory_dump(sections[2], 0, 256)
    sfr = memory_dump(sections[3], 0x80, 128)
    require(ram[0x1e00:0x1e06] == b"NCD1\x01\x08", "Collector status ABI changed")
    line = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(line == 0, f"Linked C collector assertion failed at line {line}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "Write outside ordinary storage/eight-byte status (including reserved tail)")
    require(iram[128:] == b"\xc7" * 128 and sfr[1] == 0x2e,
            "Upper IRAM guard or stack unwind failed")
    require(sfr[0xa8 - 0x80] == sfr[0xb8 - 0x80] == sfr[0x9a - 0x80] == 0,
            "Interrupts became enabled")
    peak = re.search(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", sections[1])
    require(peak is not None and int(peak[1], 16) == 0x4e, "Reviewed peak stack usage changed")
    return int(peak[1], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "nwk_candidates_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_text()
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / f"nwk_candidates_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings, objects)
    negatives = negative_controls(image, symbols, debug, memory, listings, objects)
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, alias=False), "missing simulator alias")
    peak = run_corpus(args.simulator, path, symbols, allocated)
    print(f"NWK candidates: {len(image)} CODE, 988+64 reserved XDATA, context150/entry36; "
          f"peak SP {peak:02x}; complete ABI/listings and {negatives}+1 negatives, "
          "real linked corpus/alias/storage/stack PASS (simulation only).")


if __name__ == "__main__":
    main()
