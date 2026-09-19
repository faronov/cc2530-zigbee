#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check genuine linked MAC-TX instructions/ABI and execute synthetic events."""

import argparse
import hashlib
from pathlib import Path
import re
from tempfile import TemporaryDirectory

from boot_image import (
    ALIAS, check_alias, check_pc, simulate, snapshot_commands, verify_component_layout,
    memory_dump,
)
from verify_firmware import cdb_address, parse_ihex, parse_symbols, require, xdata_ranges

SIZE = 25388
DIGEST = "f658150863b450952fdb2e70a466f4c92691a117b40d03f2fe356a581bb51ef6"
PRIVATE_DIGEST = "7863fd66009ea481c458789f3b3a540319fff645c75c083456097186dcff0125"
CALLER_DIGEST = "37c47d910a8530b7a9d53d052d55c1838b3771ba247165416a2746093d4e184c"
PUBLIC_DIGEST = "d5b115694201f970b6e1b9b095e1a4df1e2661bbbc316e2b48f2b1a282a8d32e"
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
    "mac_frame": (4168, 7009, "6f42dd5789a73ec3024332b64ec3a61a5a29a4e5d541dbb8564aa35c0e6eb423",
                  ((0x62, 0x1bc3),)),
    "mac_tx": (3729, 5650, "b9923d41551ab54b15d2079d0312b73514e0466041cb0d13a344abe156f85e0c",
               ((0x1bc3, 0x31d5),)),
    "mac_tx_test": (6998, 11934, "1c3686695fbb7421ecf5cb5d19e8e22d120b731a8ed1c91a19cdaa5ef96bec30",
                    ((0, 6), (0x5f, 0x62), (0x31d5, 0x606a))),
}
ENTRY_POINTS = {
    "_mac_command_decode": ("mac_frame", 0x22a),
    "_mac_command_encode": ("mac_frame", 0x425),
    "_mac_beacon_decode": ("mac_frame", 0x8e0),
    "_mac_frame_decode": ("mac_frame", 0x1214),
    "_mac_frame_encode": ("mac_frame", 0x195c),
    "_mac_tx_init": ("mac_tx", 0x1c87),
    "_mac_tx_submit": ("mac_tx", 0x1d55),
    "_mac_tx_copy": ("mac_tx", 0x21af),
    "_mac_tx_step": ("mac_tx", 0x23fc),
    "_mac_tx_release": ("mac_tx", 0x3180),
    "_main": ("mac_tx_test", 0x5f45),
    "_mac_tx_done": ("mac_tx_test", 0x6066),
}
CALLER_OBJECTS = {
    "tx": (0x18e, 168), "saved": (0x236, 168), "event": (0x2de, 17),
    "action": (0x2ef, 22), "saved_action": (0x305, 22),
    "body": (0x31b, 125), "copy": (0x398, 125), "ack": (0x415, 4),
    "request_index": (0x41f, 1), "request_size": (0x420, 1),
    "request_body": (0x421, 3), "request_ready": (0x424, 4),
}


def validate_cdb(debug):
    require(re.search(r"[\x00-\x08\x0b-\x1f\x7f-\x9f\u2028\u2029]", debug) is None,
            "CDB contains a non-LF control/separator")


def read_cdb(path):
    debug = path.read_bytes().decode("utf-8")
    validate_cdb(debug)
    return debug


def abi_records(debug, pattern):
    return "\n".join(sorted(re.findall(pattern, debug, re.M))) + "\n"


def private_records(debug):
    return abi_records(debug, r"^[FSLT]:(?:X?F|L)(?:mac_frame|mac_tx)[.$][^\n]+$")


def public_records(debug):
    # Public declarations can repeat identically across translation units.
    # Keep complete raw records: a conflicting variant must change this set.
    return "\n".join(sorted(set(re.findall(r"^[FSL]:(?:X?G)\$[^\n]+$", debug, re.M)))) + "\n"


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
    validate_cdb(debug)
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
    require(hashlib.sha256(abi_records(
        debug, r"^[FSLT]:(?:X?F|L)test_mac_tx[.$][^\n]+$").encode()).hexdigest() == CALLER_DIGEST,
        "MAC-TX complete caller storage/helper/field ABI changed")
    require(hashlib.sha256(public_records(debug).encode()).hexdigest() == PUBLIC_DIGEST,
            "MAC-TX complete public declarations/entries/ends changed")
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
            require(set(declarations) == {f"F:G${name[1:]}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0"},
                    "MAC-TX public result ABI changed: " + name)
    require(symbols.get("__sdcc_program_startup") == 3
            and bytes(image[a] for a in range(3, 6))
            == bytes((2, ENTRY_POINTS["_main"][1] >> 8, ENTRY_POINTS["_main"][1] & 0xff)),
            "MAC-TX startup does not jump to the pinned main")
    done = ENTRY_POINTS["_mac_tx_done"][1]
    require(bytes(image[a] for a in range(done, done + 4)) == b"\x00\x80\xfe\x22",
            "MAC-TX final NOP/loop/return checkpoint changed")
    require(private == set(range(398)), "MAC-TX private/compiler prefix changed")
    for name, address, size in (("control", 0xcf, 43), ("input", 0xfa, 12)):
        require(cdb_address(debug, f"L:Fmac_tx${name}$0_0$0") == address
                and f"S:Fmac_tx${name}$0_0$0({{{size}}}" in debug
                and set(range(address, address + size)) <= private,
                "MAC-TX bounded staging storage changed")
    for name, (address, size) in CALLER_OBJECTS.items():
        declaration = re.findall(rf"^S:Ftest_mac_tx\${name}\$[^\n]*", debug, re.M)
        addresses = re.findall(rf"^L:Ftest_mac_tx\${name}\$[^:]+:([0-9A-F]+)$", debug, re.M)
        require(declaration and all(f"({{{size}}}" in s for s in declaration)
                and len(addresses) == 1 and int(addresses[0], 16) == address,
                "MAC-TX caller ABI missing/changed")
        region = set(range(address, address + size))
        require(not region & private and region <= allocated, "Caller overlaps private prefix/alias")
    require(sum(e - s for s, e in xdata_ranges(symbols)) == 1105
            and symbols["__gptrput_PARM_2"] == 0x444,
            "MAC-TX linked ordinary/generic-store storage changed")
    require(symbols["s_SSEG"] == 0x39, "MAC-TX stack reservation changed")
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


def metadata_negatives(debug, check, directory):
    count = 0

    def reject(changed, name):
        nonlocal count
        rejected(lambda: check(changed), name)
        count += 1

    prefixes = [
        f"{kind}:{scope}mac_tx${name}$"
        for name in ("load_control", "save_control")
        for kind, scope in (("F", "F"), ("S", "F"), ("L", "F"), ("L", "XF"))
    ] + [
        f"{kind}:Fmac_tx${name}$" for name in ("control", "input") for kind in ("S", "L")
    ] + ["T:Fmac_tx$__00000007[", "T:Fmac_tx$__00000008["]
    for prefix in prefixes:
        found = [line for line in debug.split("\n") if line.startswith(prefix)]
        require(len(found) == 1, "Staging/helper negative needs one complete record: " + prefix)
        record = found[0]
        reject(debug.replace(record, record + "!", 1), "malformed complete private record")
        reject(debug + "\n" + record + "\n", "private duplicate multiplicity")
    parameters = re.findall(r"^S:Lmac_tx\.[^\n]*\(\{3\}DG,[^\n]+$", debug, re.M)
    require(parameters, "Generic-pointer negatives need real parameter/local declarations")
    for record in parameters:
        reject(debug + "\n" + record.replace("({3}DG,", "({2}DX,", 1) + "\n",
               "conflicting generic-pointer declaration")
    public = public_records(debug)
    declarations = [line for line in public.split("\n")
                    if line.startswith(("F:G$", "S:G$")) and "DF," in line]
    require(declarations, "Public-return negatives need real declarations")
    for record in declarations:
        result = "SC:U" if "DF,SV:S" in record else "SV:S"
        changed = re.sub(r"(DF,)[^)]+", r"\g<1>" + result, record, count=1)
        require(changed != record, "Public-return negative did not apply")
        reject(debug + "\n" + changed + "\n", "conflicting F/S public return declaration")
    check(debug + "\n" + public)
    record = next(line for line in debug.split("\n") if line.startswith("F:Fmac_tx$load_control$"))
    hostile = record.replace("$load_control$", "$wrong_control$")
    with TemporaryDirectory(prefix="cdb-negative-", dir=directory) as temporary:
        path = Path(temporary) / "metadata.cdb"
        for separator in ("\0", "\r", "\v", "\f", "\x1c", "\x1d", "\x1e",
                          "\x1f", "\x7f", "\x85", "\u2028", "\u2029"):
            reject(debug.replace(record, record + separator, 1), "raw record separator")
            changed = debug + "\n" + separator + hostile + "\n"
            reject(changed, "separator-prefixed hostile helper")
            path.write_bytes(changed.encode("utf-8"))
            rejected(lambda: check(read_cdb(path)), "raw CDB artifact loader")
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "mac_tx_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = read_cdb(path.with_suffix(".cdb"))
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / ("mac_tx_test." + m + ".rst")).read_text() for m in MODULES}
    allocated = verify(image, symbols, debug, memory, listings)
    metadata_count = metadata_negatives(
        debug, lambda changed: verify(image, symbols, changed, memory, listings), args.output,
    )
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
    for old, new in (
        ("S:Ftest_mac_tx$request_body$0_0$0({3}", "S:Ftest_mac_tx$request_body$0_0$0({2}"),
        ("L:Ftest_mac_tx$request_ready$0_0$0:424", "L:Ftest_mac_tx$request_ready$0_0$0:425"),
    ):
        require(old in debug, "Request-caller negative did not apply")
        rejected(lambda: verify(image, symbols, debug.replace(old, new), memory, listings),
                 "changed request-caller pointer/storage ABI")
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
    require(ram[CALLER_OBJECTS["request_index"][0]] == 5,
            "MAC-TX did not execute every acknowledged-request layout")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "MAC-TX wrote outside allocated XDATA/status")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[128:] == b"\xc7" * 128,
            "MAC-TX stack did not unwind or crossed the upper-IRAM guard")
    require(all(sfr[a - 0x80] == 0 for a in (0xa8, 0xb8, 0x9a)),
            "MAC-TX enabled interrupts")
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", indexed[1])
    require(len(peaks) == 1 and int(peaks[0], 16) == 0x5a
            and int(peaks[0], 16) <= 0x7c, "MAC-TX reviewed stack peak/budget changed")
    ordinary = sum(end - start for start, end in xdata_ranges(symbols))
    print(f"MAC-TX: {SIZE} CODE SHA256={DIGEST}; {ordinary}+64/{XDATA_BUDGET} XDATA; "
          f"private prefix=0..397; stack=39..ff peak={int(peaks[0], 16):02x}.")
    for module in MODULES:
        areas = re.findall(r"^A (\S+) size ([0-9A-F]+) flags", (args.output / (module + ".rel")).read_text(), re.M)
        print(module, {name: int(size, 16) for name, size in areas if int(size, 16)})
        print("instruction records/covered bytes:", LISTING_PROOFS[module][:2])
    print(f"Whole linked CODE/private/public ABI, {metadata_count} new metadata negatives, "
          "complete ordered instruction records, "
          "drop/duplicate/reorder and checkpoint negatives, genuine compiled vectors, alias negative control, "
          "allocation and upper-IRAM guards PASS. Synthetic only; never flash.")


if __name__ == "__main__":
    main()
