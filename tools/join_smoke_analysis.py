#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Inspect a bound resource candidate; never authorize execution or flashing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from boot_nwk_candidates import INSTRUCTION, records
from boot_security_resident import branch
from join_smoke_layout import RUNTIME_OBJECTS, link_symbols, resources
from join_smoke_stack import analyze_stack
from verify_banked_join import data_liveness, direct_accesses, transfers
from verify_firmware import cdb_address, parse_ihex, require


RUNTIME = ("___memcpy", "_memset", "__gptrput", "__gptrget",
           "__mulint", "__mullong", "_memcmp", "___gptr_cmp")
RESERVATIONS = set(range(8, 0x1e)) | set(range(0x2b, 0x46))
OVERLAY = set(range(0x46, 0x50))
PHYSICAL_DATA = {"join_smoke_iram_low": (8, 22), "join_smoke_iram_high": (0x2b, 27),
                 "banked_depth": (0x1e, 1), "banked_fault": (0x1f, 1)}


def load(root):
    report = json.loads((root / "layout.json").read_text("ascii"))
    target = root / "join_smoke_unverified.ihx"
    require(report["resource_link_completed"] and not report["accepted_image"],
            "Expected an explicitly unaccepted resource candidate")
    raw = {}
    for suffix, digest in report["artifacts"].items():
        require(suffix in (".ihx", ".map", ".mem", ".cdb", ".noi"), "Unexpected artifact kind")
        raw[suffix] = target.with_suffix(suffix).read_bytes()
        require(hashlib.sha256(raw[suffix]).hexdigest() == digest, f"Changed linked artifact: {suffix}")
    require(set(raw) == {".ihx", ".map", ".mem", ".cdb", ".noi"}, "Incomplete linked artifacts")
    objects, listings = {}, {}
    require(report["objects"].keys() == report["listings"].keys(), "Object/listing inventory differs")
    for module, digest in report["objects"].items():
        require(re.fullmatch(r"\w+", module), "Invalid module identity")
        objects[module] = (root / f"{module}.rel").read_bytes()
        listings[module] = (root / f"join_smoke_unverified.{module}.rst").read_bytes()
        require(hashlib.sha256(objects[module]).hexdigest() == digest
                and hashlib.sha256(listings[module]).hexdigest() == report["listings"][module],
                f"Changed object or relocated snapshot: {module}")
    spill = (root / "spill-manifest.json").read_bytes()
    require(hashlib.sha256(spill).hexdigest() == report["spill_manifest"], "Changed spill manifest")
    image = parse_ihex(raw[".ihx"].decode("ascii"))
    symbols = link_symbols(raw[".noi"].decode("ascii"), raw[".map"].decode("ascii"), target)
    require(all(report[key] == value for key, value in
                resources(image, symbols, raw[".mem"].decode("ascii")).items()),
            "Resource report disagrees with actual artifacts")
    debug = raw[".cdb"].decode("ascii")
    require(re.findall(r"^M:(\w+)$", debug, re.M) == list(objects), "CDB module inventory differs")
    if "runtime_objects" in report:
        require(list(report["runtime_objects"]) == [m for _, m in RUNTIME_OBJECTS],
                "Unexpected explicit runtime inventory")
        for module, digest in report["runtime_objects"].items():
            raw = (root / "runtime" / (module + ".rel")).read_bytes()
            require(hashlib.sha256(raw).hexdigest() == digest, "Changed runtime object: " + module)
    return image, symbols, debug, listings, objects


# Banked read-only tables, placed by SDCC after the module's code in its own
# area (common CODE has no room). Only these named objects may be non-code.
CONSTANT_TABLES = {"zcl_sensor": ("attributes", "defaults", "manufacturer", "model")}
DATA_RECORD = re.compile(r"^\s*([0-9A-Fa-f]{6})\s+((?:[0-9A-Fa-f]{2}\s+)+)\s*\d+\s+"
                         r"\t(?:\.byte|\.db|\.ascii)\s")
CONTINUATION = re.compile(r"^\s+((?:[0-9A-Fa-f]{2}\s+)*[0-9A-Fa-f]{2})\s*$")


def constant_tables(image, symbols, listings):
    """Return the exact linked bytes of each allowed module's trailing tables."""
    result = {}
    for module, names in CONSTANT_TABLES.items():
        area = "JS_" + module
        start, end = symbols["s_" + area], symbols["s_" + area] + symbols["l_" + area]
        text = listings[module].decode("ascii")
        first = re.search(rf"^\s*([0-9A-Fa-f]{{6}})\s+\d+\s+_{names[0]}:\s*$", text, re.M)
        require(first is not None, "Missing banked constant table: " + names[0])
        tail = text[first.start():text.rindex("\n", 0, text.index("\t.area XINIT", first.start()))]
        data, pc, labels = {}, int(first[1], 16), []
        for line in tail.splitlines():
            label = re.fullmatch(r"\s*([0-9A-Fa-f]{6})\s+\d+\s+_(\w+):\s*", line)
            record = DATA_RECORD.match(line)
            more = CONTINUATION.fullmatch(line)
            if label:
                require(int(label[1], 16) == pc, "Banked constant table is not contiguous")
                labels.append(label[2])
            elif record:
                require(int(record[1], 16) == pc, "Banked constant record is not contiguous")
                raw = bytes.fromhex(record[2])
            elif more:
                raw = bytes.fromhex(more[1])
            else:
                require(re.fullmatch(r"\s*(?:[0-9A-Fa-f]{6}\s+)?\d+\s+\w+\$\w+\$\w+\$\w+ == \.\s*",
                                     line) is not None, "Unexpected banked constant listing record")
                continue
            if not label:
                for octet in raw:
                    require(image.get(pc) == octet, "Banked constant differs from linked CODE")
                    data[pc] = octet
                    pc += 1
        require(tuple(labels) == names,
                "Banked constant inventory differs")
        require(pc == end and min(data) >= start, "Banked constants do not end the area")
        result[area] = (min(data), end)
    return result


def call_graph(image, symbols, debug, listings, objects):
    areas = ["CSEG"]
    tables = constant_tables(image, symbols, listings)
    for name, value in symbols.items():
        if name.startswith("l_JS_") and value and name[2:] not in tables:
            areas.append(name[2:])
    indirect = [(pc, raw) for listing in listings.values()
                for pc, raw in records(listing.decode("ascii")) if raw[0] in (0x73, 0x32)]
    require(len(indirect) == 1 and indirect[0][1] == b"\x73",
            "Unexpected indirect transfer or interrupt return")
    pc = indirect[0][0]
    work, ram = symbols["_flash_exec_work"], symbols["_flash_exec_ram"]
    expected = (bytes((0x7a, work & 255, 0x7b, work >> 8, 0x90))
                + (ram + 0x8000).to_bytes(2, "big") + b"\xe4\x73")
    require((pc, b"\x73") in tuple(records(listings["flash_exec"].decode("ascii")))
            and bytes(image[a] for a in range(pc - 8, pc + 1)) == expected,
            "Indirect transfer is not the fixed XMAP flash engine entry")
    if symbols["___memcpy_PARM_2"] == 0:
        first_source = min(pc for listing in listings.values()
                           for pc, _ in records(listing.decode("ascii"))
                           if symbols["s_CSEG"] <= pc < 0x8000)
        runtime_spans = ((symbols["___gptr_cmp"], symbols["s_GSINIT0"]),
                         (symbols["s_CSEG"], first_source),
                         (symbols["__gptrget"], symbols["s_CSEG"] + symbols["l_CSEG"]))
    else:
        runtime_spans = ((symbols["___gptr_cmp"], symbols["s_GSINIT0"]),
                         (symbols["___memcpy"], symbols["s_CSEG"] + symbols["l_CSEG"]))
    graph = transfers(image, symbols, listings, debug, modules=tuple(objects),
                      areas=tuple(areas), library=RUNTIME, indirect_sites=tuple(indirect),
                      runtime_spans=runtime_spans)
    symbolic_transfers(symbols, listings, graph[4])
    for area, (first, end) in tables.items():
        code = set()
        module = area[3:]
        for pc, raw in records(listings[module].decode("ascii")):
            require(pc + len(raw) <= first, "Banked code overlaps its constants")
            code |= set(range(pc, pc + len(raw)))
        require(code | set(range(first, end)) == set(range(symbols["s_" + area], end)),
                "Incomplete actual join CODE decode")
    return graph


def instructions(text):
    return {int(m[1], 16): (bytes.fromhex(m[2]), re.split(r"\]\s*\d+\s+", m[0], 1)[1].strip())
            for m in INSTRUCTION.finditer(text)}


def symbolic_transfers(symbols, listings, targets):
    """A wrong-bank call can hit another valid entry; require the intended symbol."""
    checked = 0
    for module, listing in listings.items():
        text = listing.decode("ascii")
        local = {}
        for address, name in re.findall(r"^\s*([0-9A-F]{6})\s+\d+\s+(_\w+):$", text, re.M):
            require(name not in local, "Duplicate module-local label")
            local[name] = int(address, 16)
        rows = instructions(text)
        for pc, (raw, asm) in rows.items():
            if branch(pc, raw) is None:
                continue
            match = re.search(r"(?<=[\s,])(_\w+)$", asm)
            if not match:
                continue
            name = match[1]
            if name == "__sdcc_banked_call":
                setup = [rows.get(pc - offset, (None, ""))[1] for offset in (6, 4, 2)]
                first = re.fullmatch(r"mov\s+r0,#(_\w+)", setup[0])
                require(first is not None, "Missing symbolic banked-call setup")
                name = first[1]
                require(all(re.fullmatch(rf"mov\s+r{index},#\({name} >> {shift}\)", row)
                            for index, shift, row in ((1, 8, setup[1]), (2, 16, setup[2]))),
                        "Inconsistent symbolic banked-call setup")
            require(name in local or name in symbols, f"Unresolved transfer symbol: {module}:{name}")
            intended = local[name] if name in local else symbols[name]
            actual = targets.get(pc, branch(pc, raw))
            require(actual == intended,
                    f"Wrong linked transfer: {module}:{pc:x} -> {actual:x}, {name} is {intended:x}")
            checked += 1
    return checked


def function_frames(symbols, debug, listings, graph):
    """Bind each compiler spill to its actual function, not its whole module."""
    decoded, owners, functions, entries, _, _ = graph
    identities = {(module, name): entry for entry, (module, name, _) in entries.items()}
    require(len(identities) == len(entries), "Duplicate function identity")
    frame_ids = {entry: entry for entry, (module, name, _) in entries.items()
                 if module != "banked" or name == "banked_code_read"}
    frames = {entry: set() for entry in frame_ids}
    locations, areas, declared, bits = {}, {}, {}, {}
    bit_storage = set()
    pattern = re.compile(
        r"^\s+[0-9A-F]{6}\s+\d+\s+(L(\w+)\.(\w+)\$sloc(\d+)\$(\d+)_(\d+)\$0)==\.\n"
        r"\s+([0-9A-F]{6})\s+\d+\s+(_\w+):\n"
        r"\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", re.M)
    for module, listing in listings.items():
        text = listing.decode("ascii")
        require(not re.search(r"#\(?_\w+_sloc\d+", text), "Compiler DATA address escapes to an indirect user")
        labels = {}
        for segment in re.split(r"\.area\s+", text)[1:]:
            area = re.match(r"\w+", segment)[0]
            if not area.startswith("JF_") and area not in ("OSEG", "BSEG"):
                continue
            matches = tuple(pattern.finditer(segment))
            require(len(matches) == len(re.findall(r"\s\.ds\s+\d+", segment)),
                    "Unclassified compiler frame allocation")
            allocated = set()
            for match in matches:
                key, owner, name, number, block, level, address, label, storage, size = match.groups()
                address, size = int(address, 16), int(size)
                require(owner == module and (module, name) in identities
                        and label == f"_{name}_sloc{number}_{level}_{block}"
                        and address == int(storage, 16)
                        and 1 <= size <= 4, "Spill/listing/CDB identity differs")
                entry = identities[module, name]
                span = set(range(address, address + size))
                require(label not in labels, "Duplicate spill label")
                labels[label] = entry
                if area == "BSEG":
                    require(size == 1 and key not in bits and not span & bit_storage
                            and span <= set(range(symbols["l_BSEG"])), "Bit spills overlap/escape")
                    bits[key] = size
                    bit_storage |= span
                    continue
                require(address == cdb_address(debug, "L:" + key), "Spill CDB address differs")
                require(entry in frames and not frames[entry] & span,
                        "Function spill declarations overlap")
                if area == "OSEG":
                    require(span <= OVERLAY, "Compiler overlay escapes physical ownership")
                else:
                    require(area == f"JF_{module}_{name}" and span <= RESERVATIONS,
                            "Function spill escapes its reserved area")
                    require(key not in declared, "Duplicate compiler DATA declaration")
                    declared[key] = (address, size)
                base, width = symbols["s_" + area], symbols["l_" + area]
                require(span <= set(range(base, base + width)), "Spill escapes linked area")
                areas[area] = (base, width)
                frames[entry] |= span
                allocated |= span
                for address in span:
                    locations[entry, address] = (area, address - base)
            if area.startswith("JF_"):
                base, width = symbols["s_" + area], symbols["l_" + area]
                require(matches and allocated == set(range(base, base + width)),
                        "Undeclared function-area bytes")
        for pc, (raw, asm) in instructions(text).items():
            for label in re.findall(r"_\w+_sloc\d+_\d+_\d+", asm):
                require(label in labels and functions.get(pc) == labels[label],
                        f"Instruction uses unowned compiler spill: {module}:{pc:x}:{label}")
            require(raw[0] & 0xfe not in (0x06, 0x16, 0x26, 0x36, 0x46, 0x56, 0x66,
                                          0x76, 0x86, 0x96, 0xa6, 0xb6, 0xc6, 0xd6, 0xe6, 0xf6),
                    "Unreviewed indirect IRAM access in source CODE")
    direct = {}
    for key, size in re.findall(r"^S:(L[^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M):
        require(key not in direct, "Duplicate CDB compiler DATA declaration")
        direct[key] = (cdb_address(debug, "L:" + key), int(size))
    require(direct == declared, "CDB and listing compiler DATA inventories differ")
    bit_records = re.findall(r"^S:(L[^(]+)\(\{(\d+)\}SB0\$0:S\),H,0,0$", debug, re.M)
    require(len(bit_records) == len(bits) and {key: int(size) for key, size in bit_records} == bits
            and bit_storage == set(range(symbols["l_BSEG"])),
            "CDB and listing bit inventories differ")
    require({name[2:] for name in symbols if name.startswith("l_JF_")}
            == {name for name in areas if name.startswith("JF_")}, "Function-area inventory differs")
    return frames, frame_ids, locations, areas


def physical_data(symbols, debug, listings, graph):
    require((symbols["s_REG_BANK_0"], symbols["l_REG_BANK_0"], symbols["s_BSEG"],
             symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"]) == (0, 8, 0, 0x20, 11)
            and 80 < symbols["l_BSEG"] <= 88, "Register/bit DATA reservations changed")
    require(all(symbols["l_" + area] == 0 for area in
                ("REG_BANK_1", "REG_BANK_2", "REG_BANK_3", "ISEG", "IABS", "BIT_BANK")),
            "Unreviewed physical IRAM storage")
    retained = re.findall(r"^S:([FG][^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M)
    require({(key, int(size)) for key, size in retained}
            == {(f"G${name}$0_0$0", size) for name, (_, size) in PHYSICAL_DATA.items()},
            "Retained DATA entered compiler spill reservations")
    require(all(symbols["_" + name] == base for name, (base, _) in PHYSICAL_DATA.items()),
            "Physical DATA owner moved")
    allocations = []
    for module, listing in listings.items():
        for segment in re.split(rb"\.area\s+", listing)[1:]:
            if segment.startswith(b"DSEG "):
                allocations.extend((module, int(base, 16), int(size)) for base, size in
                                   re.findall(rb"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M))
    require(sorted(allocations) == sorted(
        (("banked" if name.startswith("banked_") else name), base, size)
        for name, (base, size) in PHYSICAL_DATA.items()), "Actual physical DATA backing differs")
    decoded, owners = graph[:2]
    for pc, raw in decoded.items():
        reads, writes = direct_accesses(raw)
        ram = {a for a in reads | writes if a < 0x80}
        require(ram <= set(range(8)) | RESERVATIONS | OVERLAY | {0x1e, 0x1f},
                "Direct access reaches unowned DATA or bit backing")
        require(not writes & {0x1e, 0x1f} or owners[pc] == "banked",
                "Non-banker instruction writes retained bank state")


def analyze_data(symbols, debug, listings, graph, *, constraints=None):
    physical_data(symbols, debug, listings, graph)
    frames, frame_ids, locations, areas = function_frames(symbols, debug, listings, graph)
    result = data_liveness(symbols, *graph, frames, frame_ids, RESERVATIONS, OVERLAY,
                           constraints=constraints)
    return result, locations, areas


def solve_data(constraints, locations, areas):
    """Place whole function areas under byte-level, live-across-call inequalities."""
    forbidden = {name: {} for name in areas}

    def location(owner, address):
        if owner == "libc":
            require(address in OVERLAY, "Runtime write outside fixed overlay")
            return "OSEG", address - 0x46
        require((owner, address) in locations, "Constraint lacks a declared byte owner")
        return locations[owner, address]

    for pc, after, writes in constraints:
        for live in after:
            left, a = location(*live)
            for write in writes:
                right, b = location(*write)
                if left == right:
                    require(a != b, f"Unplaceable fixed-area clobber across call {pc:x}")
                else:
                    forbidden[left].setdefault(right, set()).add(b - a)
                    forbidden[right].setdefault(left, set()).add(a - b)
    domains = {}
    for name, (base, size) in areas.items():
        if name == "OSEG":
            require((base, size) == (0x46, 10), "Fixed overlay moved")
            domains[name] = (base,)
        else:
            require(name.startswith("JF_") and 0 < size <= 27, "Unreviewed placement area")
            choices = tuple(a for a in sorted(RESERVATIONS)
                            if set(range(a, a + size)) <= RESERVATIONS)
            domains[name] = tuple(sorted(choices, key=lambda a: (a != base, a)))
    nodes = 0

    def search(pending, assigned):
        nonlocal nodes
        nodes += 1
        require(nodes <= 100000, "DATA placement search exhausted its explicit work limit")
        if not pending:
            return assigned
        name = min(pending, key=lambda n: (len(pending[n]), -len(forbidden[n]), -areas[n][1], n))
        for base in pending[name]:
            remaining = {}
            for other, choices in pending.items():
                if other == name:
                    continue
                differences = forbidden[name].get(other, set())
                remaining[other] = tuple(a for a in choices if base - a not in differences)
                if not remaining[other]:
                    break
            else:
                result = search(remaining, assigned | {name: base})
                if result is not None:
                    return result
        return None

    solution = search(domains, {})
    require(solution is not None, "Live compiler DATA cannot fit its physical reservations")
    return {name: base for name, base in solution.items() if name != "OSEG"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--solve-data", type=Path, help="write an unverified placement for a subsequent real link")
    mode.add_argument("--check-data", action="store_true", help="reject every live-byte collision in this link")
    parser.add_argument("--check-stack", action="store_true", help="bound actual foreground and startup stack paths")
    args = parser.parse_args()
    report_path = args.output / "analysis.json"
    report_path.unlink(missing_ok=True)
    require(not args.check_stack or args.check_data, "Stack checking requires the strict linked DATA check")
    image, symbols, debug, listings, objects = load(args.output)
    graph = call_graph(image, symbols, debug, listings, objects)
    report = {"instructions": len(graph[0]), "functions": len(graph[3]), "calls": len(graph[5]),
              "accepted_image": False, "DATA_liveness_verified": False, "stack_verified": False,
              "static_stack_verified": False,
              "simulated": False, "hardware_observed": False,
              "layout_sha256": hashlib.sha256((args.output / "layout.json").read_bytes()).hexdigest()}
    if args.solve_data or args.check_data:
        constraints = [] if args.solve_data else None
        result, locations, areas = analyze_data(symbols, debug, listings, graph, constraints=constraints)
        report["DATA_functions"], report["DATA_byte_pairs"] = result
        if args.solve_data:
            solution = {"objects": {m: hashlib.sha256(raw).hexdigest() for m, raw in objects.items()},
                        "placements": solve_data(constraints, locations, areas),
                        "source_layout_sha256": report["layout_sha256"]}
            args.solve_data.write_text(json.dumps(solution, indent=2) + "\n", encoding="ascii")
        else:
            report["DATA_liveness_verified"] = True
    if args.check_stack:
        report["static_stack"] = analyze_stack(image, symbols, graph, RUNTIME)
        report["static_stack_verified"] = True
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
