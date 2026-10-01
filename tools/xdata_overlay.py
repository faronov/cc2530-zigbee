#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Opt-in, instruction-preserving XSEG alias prototype; not a flashing tool."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from join_smoke_analysis import call_graph, instructions, load
from join_smoke_image import identities, xdata
from verify_firmware import require
from xdata_lifetime import analyze, assembly_allocations, nonescape
from xdata_relocations import audit


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def verify_study(root, board, key_mode):
    require(board == "lg_esl29_rev03" and key_mode == "default-tc", "Unmeasured XDATA study profile")
    catalog = json.loads((Path(__file__).resolve().parents[1] /
                          "experiments/xdata/identities.json").read_bytes())
    profile = "overlay" if (root / "xdata-overlay.json").exists() else "baseline"
    require(identities(root) == catalog[profile], "Immutable XDATA study artifacts differ")
    from join_smoke_image import structure
    return structure(root)


def choose(analysis, module, limit):
    groups = [g for g in analysis["groups"] if g["owner"].split(".")[0] == module]
    best, saving = None, 0
    for count in range(2, min(limit, len(groups)) + 1):
        for selected in itertools.combinations(groups, count):
            owners = {g["owner"] for g in selected}
            if any(owners.intersection(g["conflicts"]) for g in selected):
                continue
            benefit = sum(g["bytes"] for g in selected) - max(g["bytes"] for g in selected)
            if benefit > saving:
                best, saving = selected, benefit
    require(best is not None and saving >= 32, "No >=32-byte mutually exclusive prototype")
    offsets = {}
    for group in best:
        cursor = 0
        for key in group["objects"]:
            row = next(r for r in analysis["objects"] if r["key"] == key)
            offsets[key] = cursor
            cursor += row["size"]
    return dict(module=module, width=max(g["bytes"] for g in best), offsets=offsets,
                saving=saving, owners=[g["owner"] for g in best])


def transform(raw, plan):
    """Only declarations change. A real XSEG reservation backs every alias."""
    text = raw.decode("ascii")
    require("_xdata_overlay_pool" not in text, "Already transformed")
    replacements, first = [], True
    found = set()
    for block in assembly_allocations(text):
        key = block["key"]
        if key not in plan["offsets"]:
            continue
        found.add(key)
        prefix = ""
        if first:
            prefix = "_xdata_overlay_pool:\n\t.ds " + str(plan["width"]) + "\n"
            first = False
        expression = "_xdata_overlay_pool+" + str(plan["offsets"][key])
        replacement = prefix + key + "==" + expression + "\n" + block["symbol"] + "=" + expression + "\n"
        replacements.append((block["start"], block["end"], replacement))
    require(found == plan["offsets"].keys(), "Missing or foreign overlay declaration")
    for start, end, replacement in reversed(replacements):
        text = text[:start] + replacement + text[end:]
    return text.encode("ascii")


def verify(root):
    manifest = json.loads((root / "xdata-overlay.json").read_bytes())
    baseline = Path(manifest["baseline"])
    require(baseline.resolve() != root.resolve() and not (baseline / "xdata-overlay.json").exists(),
            "Invalid overlay baseline")
    require(identities(baseline) == manifest["baseline_identities"], "Overlay baseline identity changed")
    original = load(baseline)
    xdata(original[1], original[2], original[3], original[4])
    study = analyze(baseline)
    plan = manifest["plan"]
    rows = {r["key"]: r for r in study["objects"] if r["key"]}
    selected = [rows[k] for k in plan["offsets"]]
    require(selected and {r["owner"] for r in selected} == set(plan["owners"]),
            "Overlay owner manifest differs")
    groups = {g["owner"]: g for g in study["groups"]}
    for row in selected:
        require(row["candidate_for_overlay"] == "yes" and row["module"] == plan["module"],
                "Overlay home lacks a nonescape proof")
        require(type(plan["offsets"][row["key"]]) is int and
                0 <= plan["offsets"][row["key"]] <= plan["width"] - row["size"],
                "Overlay object exceeds reservation")
    for owner in plan["owners"]:
        require(not set(plan["owners"]).intersection(groups[owner]["conflicts"]),
                "Simultaneously live overlay owners")
        occupied = set()
        for row in selected:
            if row["owner"] == owner:
                span = set(range(plan["offsets"][row["key"]], plan["offsets"][row["key"]] + row["size"]))
                require(not span & occupied, "Overlapping objects of the same activation")
                occupied |= span
    for module in original[4]:
        raw = (baseline / (module + ".asm")).read_bytes()
        expected = transform(raw, plan) if module == plan["module"] else raw
        require((root / (module + ".asm")).read_bytes() == expected,
                "Transformation changed instructions or unrelated declarations")
        require((root / (module + ".adb")).read_bytes() == (baseline / (module + ".adb")).read_bytes(),
                "CDB declaration input changed")
    candidate = load(root)
    for m in original[4]:
        if m != plan["module"]:
            require(candidate[4][m] == original[4][m], "Unrelated object changed")
    with tempfile.TemporaryDirectory(prefix="xdata-reassemble-") as directory:
        source = Path(directory) / (plan["module"] + ".asm")
        source.write_bytes((root / source.name).read_bytes())
        subprocess.run(["sdas8051", "-plosgff", str(source)], check=True, capture_output=True)
        require(source.with_suffix(".rel").read_bytes() == candidate[4][plan["module"]],
                "Transformed assembly does not reproduce the actual object")
    graph = call_graph(*candidate)
    _, relocation_summary = audit(root, candidate, json.loads((root / "layout.json").read_bytes()))
    # The assembler input is independently reconstructed above; the relocation
    # audit binds that layout's object bytes to the complete final image.
    require(graph[2:] == call_graph(*original)[2:], "Linked function/branch/call graph changed")
    from boot_mac_link_child_abi import full_locations
    locations = full_locations(candidate[2])
    delta = sum(r["size"] for r in selected) - plan["width"]
    require(delta == plan["saving"] >= 32 and
            candidate[1]["l_XSEG"] == original[1]["l_XSEG"] - delta,
            "No real l_XSEG reduction")
    module = plan["module"]
    first = min(r["address"] for r in selected)
    removed, inserted, pool = 0, False, first
    expected_spans = set()
    for row in study["objects"]:
        if row["key"] in plan["offsets"]:
            expected = pool + plan["offsets"][row["key"]]
            removed += row["size"]
            inserted = True
        else:
            expected = row["address"] - removed + (plan["width"] if inserted else 0)
        if row["key"]:
            require(locations[row["key"]] == expected, "Exact relocated CDB home address differs")
        else:
            require(all(candidate[1][n] == expected for n in row["linker_aliases"]), "Runtime prefix moved")
        expected_spans.update(range(expected, expected + row["size"]))
        if row["key"] in plan["offsets"]:
            relocated = {**row, "address": expected}
            references = [(pc, graph[0][pc], "") for pc in row["references"]]
            safe, reason, _ = nonescape(relocated, graph, {row["key"]: references}, candidate[2])
            require(safe, "Linked overlay nonescape failed: " + reason)
    require(expected_spans == set(range(candidate[1]["l_XSEG"])), "Physical XDATA ledger has a hole")
    # Every relative branch and every instruction size must be unchanged.
    before, after = {}, {}
    for m in original[3]:
        before.update(instructions(original[3][m].decode("ascii")))
        after.update(instructions(candidate[3][m].decode("ascii")))
    require(before.keys() == after.keys() and all(before[p][1] == after[p][1] for p in before),
            "Executable assembly changed")
    require(manifest["candidate_identities"] == identities(root), "Overlay image identity changed")
    return {"ordinary": candidate[1]["l_XSEG"], "saving": delta,
            "pool": [pool, pool + plan["width"]], "owners": plan["owners"],
            "objects": len(selected), "relocation_audit": relocation_summary,
            "baseline_identities": manifest["baseline_identities"]}


def prepare(baseline, root, module, limit):
    require(not root.exists(), "Candidate output already exists")
    study = analyze(baseline)
    plan = choose(study, module, limit)
    original = load(baseline)
    xdata(original[1], original[2], original[3], original[4])
    root.mkdir(parents=True)
    for m in original[4]:
        raw = (baseline / (m + ".asm")).read_bytes()
        (root / (m + ".asm")).write_bytes(transform(raw, plan) if m == module else raw)
        shutil.copyfile(baseline / (m + ".adb"), root / (m + ".adb"))
        subprocess.run(["sdas8051", "-plosgff", str(root / (m + ".asm"))], check=True)
    shutil.copyfile(baseline / "spill-manifest.json", root / "spill-manifest.json")
    placement = json.loads((baseline / "data-placement.json").read_bytes())
    placement["objects"] = {m: sha((root / (m + ".rel")).read_bytes()) for m in original[4]}
    (root / "data-placement.json").write_text(json.dumps(placement, indent=2) + "\n")
    subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("join_smoke_layout.py")),
                    "--output", str(root), "--data-placement", str(root / "data-placement.json"),
                    "--modules", *original[4]], check=True)
    manifest = {"baseline": str(baseline), "baseline_identities": study["identities"], "plan": plan,
                "candidate_identities": identities(root)}
    (root / "xdata-overlay.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return verify(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--module", default="nwk_aps")
    parser.add_argument("--limit", type=int, default=10)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    require(args.verify or args.baseline is not None, "Missing baseline")
    print(json.dumps(verify(args.output) if args.verify else
                     prepare(args.baseline, args.output, args.module, args.limit), indent=2))


if __name__ == "__main__":
    main()
