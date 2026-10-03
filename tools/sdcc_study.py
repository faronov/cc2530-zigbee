#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Read-only measurements, not image admission or an optimization proof."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import subprocess

from join_smoke_analysis import call_graph, instructions, load
from join_smoke_image import identities
from join_smoke_layout import link_symbols
from boot_mac_link_child_abi import full_locations
from verify_firmware import parse_ihex, require


def measure(root):
    target = root / "join_smoke_unverified"
    image = parse_ihex(target.with_suffix(".ihx").read_text("ascii"))
    symbols = link_symbols(target.with_suffix(".noi").read_text("ascii"),
                           target.with_suffix(".map").read_text("ascii"),
                           target.with_suffix(".ihx"))
    plan = json.loads((root / "placement.json").read_bytes())
    frames = {n: v for n, v in plan["object_areas"].items() if n.startswith("JF_")}
    banks = []
    for bank in range(8):
        size = 0x6800 if bank == 7 else 0x8000
        addresses = [a for a in image if a >> 16 == bank]
        first = bank * 0x10000 + (0x8000 if bank else 0)
        require(all(first <= a < first + size for a in addresses), "Bank overflow")
        banks.append({"bank": bank, "populated": len(addresses),
                      "capacity": size, "free": size - len(addresses)})
    analysis = root / "analysis.json"
    proof = json.loads(analysis.read_bytes()) if analysis.exists() else {}
    if proof:
        require(proof["layout_sha256"] ==
                hashlib.sha256((root / "layout.json").read_bytes()).hexdigest(),
                "Stale analysis report")
    stack = proof.get("static_stack", {}).get("roots", {})
    return {
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "layout": str(root),
        "code": len(image),
        "common": banks[0]["populated"],
        "banks": banks,
        "xdata": symbols["l_XSEG"],
        "physical_dseg": plan["object_areas"]["DSEG"],
        "linker_dseg_extent": symbols["l_DSEG"],
        "oseg": symbols["l_OSEG"],
        "bseg_bits": symbols["l_BSEG"],
        "bseg_bytes": symbols["l_BSEG_BYTES"],
        "stack": max((s["bytes"] for s in stack.values()), default=None),
        "bank_depth": max((s["bank_depth"] for s in stack.values()), default=None),
        "jf_count": len(frames),
        "jf_sum": sum(frames.values()),
        "jf_max": max(frames.values(), default=0),
        "jf_frames": dict(sorted(frames.items(), key=lambda p: (-p[1], p[0]))),
        "data_verified": proof.get("DATA_liveness_verified", False),
        "stack_verified": proof.get("static_stack_verified", False),
        "identities": identities(root),
        "simulation_or_hardware_claim": False,
    }


def analyze(root):
    image, symbols, debug, listings, objects = load(root)
    graph = call_graph(image, symbols, debug, listings, objects)
    decoded, owners, functions, entries, targets, calls = graph
    sizes = Counter()
    edges = defaultdict(set)
    for pc, entry in functions.items():
        sizes[entry] += len(decoded[pc])
        if pc in calls:
            edges[entry].add(calls[pc])
        elif targets.get(pc) in entries and targets[pc] != entry:
            edges[entry].add(targets[pc])
    roots = {symbols[n] for n in ("_main", "__sdcc_external_startup",
                                 "__sdcc_banked_call", "__sdcc_banked_ret")}
    roots |= {e for e, identity in entries.items()
              if identity[:2] == ("flash_exec", "flash_exec_template")}
    require(len(roots) == 5, "Missing reviewed reachability root")
    # Address materialization adds edges too; bank calls already have call edges.
    global_entries = {n: v for n, v in symbols.items() if v in entries}
    rows = {}
    for module, listing in listings.items():
        rows[module] = instructions(listing.decode("ascii"))
        local = {"_" + name: e for e, (m, name, _) in entries.items() if m == module}
        for pc, (_, asm) in rows[module].items():
            if pc not in functions or "#" not in asm:
                continue
            for name in re.findall(r"_\w+", asm.split("#", 1)[1]):
                target = local.get(name, global_entries.get(name))
                if target in entries:
                    edges[functions[pc]].add(target)
    live, pending = set(), list(roots)
    while pending:
        entry = pending.pop()
        if entry in live:
            continue
        live.add(entry)
        pending.extend(edges[entry] - live)
    dead = [{"module": entries[e][0], "function": entries[e][1],
             "entry": e, "bytes": sizes[e]}
            for e in entries if e not in live]
    locations = full_locations(debug)
    homes = defaultdict(set)
    for key, owner, size in re.findall(
            r"^S:(L([^$]+)\$[^(]+)\(\{(\d+)\}.*\),F,0,0$", debug, re.M):
        if key in locations:
            homes[owner].update(range(locations[key], locations[key] + int(size)))
    dead_homes = set()
    for row in dead:
        dead_homes |= homes[row["module"] + "." + row["function"]]
    patterns = defaultdict(list)
    counts = Counter()
    for module, module_rows in rows.items():
        dptr, previous = None, None
        branch_entries = set(targets.values()) | set(entries)
        for pc, (raw, asm) in sorted(module_rows.items()):
            normalized = re.sub(r"\s+", " ", asm).strip()
            counts[normalized.split(" ", 1)[0]] += 1
            if pc in branch_entries:
                dptr = None
            function = (":".join(entries[functions[pc]][:2])
                        if pc in functions else module + ":startup")

            def found(name, removable):
                patterns[name].append({"module": module, "function": function,
                                       "pc": pc, "bytes": removable, "asm": asm})

            if raw[0] == 0x90:
                if dptr == raw[1:]:
                    found("same_DPTR_immediate_in_basic_block", 3)
                dptr = raw[1:]
            elif (raw[0] in (0x12, 0x22, 0x32, 0x73, 0xa3) or
                  pc in targets or re.search(r"\b(?:dpl|dph|dptr)\s*,", asm)):
                dptr = None
            if "_sloc" in asm:
                found("spill_access_cost_not_removable_estimate", 0)
            if (raw[0] in (0x02, 0x80) and targets.get(pc) in decoded
                    and decoded[targets[pc]] == b"\x22"
                    and functions.get(pc) == functions.get(targets[pc])):
                found("jump_to_local_plain_RET", len(raw) - 1)
            if "__gptr" in asm and raw[0] == 0x12:
                found("generic_pointer_call_cost_not_removable_estimate", 0)
            if "__sdcc_banked_call" in asm:
                found("bank_call_sites_9byte_setup_call", 0)
            if previous and previous[0] + len(previous[1]) == pc:
                ppc, praw, pasm = previous
                if praw[0] == 0x12 and raw[0] == 0x22:
                    found("near_call_then_RET_tailcall_candidate", 1)
                if (raw[0] == 0x22 and praw[0] in (0x02, 0x80) and
                        targets.get(ppc) == pc):
                    found("jump_to_next_RET", len(praw))
                if (praw == raw and raw[0] in (0x74, 0xc3, 0xe4) and
                        pc not in branch_entries):
                    found("adjacent_duplicate_accumulator_or_carry_set", len(raw))
            previous = pc, raw, normalized
    return {
        "instructions": len(decoded), "functions": len(entries), "calls": len(calls),
        "mnemonics": dict(counts.most_common()),
        "patterns": {
            name: {"count": len(rows), "local_upper_bound_bytes": sum(r["bytes"] for r in rows),
                   "modules": dict(Counter(r["module"] for r in rows).most_common()),
                   "examples": rows[:15]}
            for name, rows in patterns.items()
        },
        "unreachable_from_reviewed_roots": sorted(dead, key=lambda row: -row["bytes"]),
        "unreachable_code_bytes_upper_bound": sum(row["bytes"] for row in dead),
        "function_scoped_xdata_bytes": len(set().union(*homes.values())),
        "unreachable_function_scoped_xdata_upper_bound": len(dead_homes),
        "xdata_caveat": "Includes parameter/local homes; not a lifetime or escape proof.",
        "gc_caveat": "Symbolic reachability estimate only; not relocation GC or admission.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layout", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--analyze", action="store_true")
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    result = measure(args.layout)
    if args.analyze:
        result["assembly_analysis"] = analyze(args.layout)
    if args.baseline:
        baseline = json.loads(args.baseline.read_bytes())
        result["delta"] = {
            key: result[key] - baseline[key] for key in
            ("code", "common", "xdata", "physical_dseg", "oseg", "bseg_bits",
             "stack", "bank_depth", "jf_count", "jf_sum")
            if result[key] is not None and baseline[key] is not None
        }
        result["bank_free_delta"] = [a["free"] - b["free"]
                                     for a, b in zip(result["banks"], baseline["banks"])]
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
    print(json.dumps({k: v for k, v in result.items()
                      if k not in ("identities", "jf_frames", "assembly_analysis")}))


if __name__ == "__main__":
    main()
