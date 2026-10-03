#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Relocation-complete closed-image reachability; does not remove functions."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

from join_smoke_analysis import call_graph, function_frames, load
from verify_firmware import require
from xdata_lifetime import activation_graph, inventory
from xdata_relocations import audit


def analyze(root):
    artifacts = load(root)
    image, symbols, debug, listings, objects = artifacts
    graph = call_graph(*artifacts)
    edges, closure, cycles = activation_graph(graph, symbols)
    require(not cycles, "Unreviewed recursive reachability")
    rows = inventory(root, artifacts)
    references, evidence = audit(root, artifacts, json.loads((root / "layout.json").read_bytes()))
    decoded, _, functions, entries, targets, calls = graph
    byte_owner = {pc + i: owner for pc, owner in functions.items() for i in range(len(decoded[pc]))}
    control_operands = set()
    for pc, target in targets.items():
        control_operands.update(range(pc + 1, pc + len(decoded[pc])))
        if pc in calls and decoded[pc] == b"\x12" + symbols["__sdcc_banked_call"].to_bytes(2, "big"):
            control_operands.update((pc - 5, pc - 3, pc - 1))
    roots = {symbols[n] for n in ("_main", "__sdcc_external_startup",
                                 "__sdcc_banked_call", "__sdcc_banked_ret")}
    # Runtime/banker and flash-execution entry points remain externally rooted,
    # even when no ordinary CALL currently targets an exported helper.
    roots |= {e for e, ident in entries.items() if ident[0] in ("banked", "flash_exec")}
    taken = {}
    for ref in references:
        if ref["target_area"] not in ("CSEG", "HOME") and not (
                ref["target_area"] or "").startswith("JS_"):
            continue
        owner = byte_owner.get(ref["value"])
        if owner is None or ref["source"] in control_operands:
            continue
        roots.add(owner)
        taken.setdefault(owner, []).append(ref["source"])
    live = roots | set().union(*(closure[e] for e in roots))
    frames, _, _, _ = function_frames(symbols, debug, listings, graph)
    sizes = Counter()
    for pc, owner in functions.items():
        sizes[owner] += len(decoded[pc])
    bits = Counter()
    for owner, size in re.findall(r"^S:L([^$]+)\$[^(]+\(\{(\d+)\}SB0\$0:S\),H,0,0$", debug, re.M):
        bits[owner] += int(size)
    dead, spans = [], set()
    for entry, (module, name, _) in entries.items():
        if entry in live:
            continue
        owner = module + "." + name
        owned = [r for r in rows if r["owner"] == owner]
        storage = set().union(*(set(range(r["address"], r["address"] + r["size"])) for r in owned))
        external = [ref for ref in references if ref["target_area"] == "XSEG"
                    and ref["value"] in storage and byte_owner.get(ref["source"]) in live]
        require(not external, "Apparently dead function homes are referenced by live code")
        spans |= storage
        dead.append({"owner": owner, "entry": entry, "code": sizes[entry],
                     "xdata": len(storage), "data_frame": len(frames.get(entry, set())),
                     "bseg_bits": bits[owner], "homes": [r["key"] for r in owned]})
    return {"roots": [":".join(entries[e][:2]) for e in sorted(roots)],
            "address_taken": {":".join(entries[e][:2]): pcs for e, pcs in taken.items()},
            "relocation_audit": evidence, "functions": dead,
            "code": sum(r["code"] for r in dead), "xdata": len(spans),
            "data_frame_sum": sum(r["data_frame"] for r in dead),
            "bseg_bits": sum(r["bseg_bits"] for r in dead),
            "physical_data_saving": 0,
            "removal_prototype": False,
            "scope": "Reachability of this closed foreground firmware, not arbitrary calls to exported C APIs. "
                     "DATA frames already overlap real reservations, so their sum is not physical savings. "
                     "Removing functions requires separate compiler-metadata/area/fence proofs; this tool does not strip."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layout", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.layout)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("code", "xdata", "data_frame_sum", "bseg_bits")}))


if __name__ == "__main__":
    main()
