#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Compiler-metadata-only placement adapter and independent final-image proof."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from boot_mac_link_child_abi import allocations, full_locations
from join_smoke_analysis import call_graph, load
from join_smoke_image import CallerSchema, identities, xdata
from verify_firmware import require
from xdata_lifetime import (activation_graph, analyze, assembly_allocations,
                            compiler_metadata_identities, nonescape, platform_contract)
from xdata_pool_allocator import allocate
from xdata_relocations import audit, metadata


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def bind_inputs(root, directory, binding_file):
    binding = json.loads(binding_file.read_bytes())
    require(binding["version"] == 1 and identities(root) == binding["baseline"],
            "Preliminary image identity changed")
    require(compiler_metadata_identities(directory) == binding["metadata"],
            "Missing, stale or falsified compiler sidecar")
    require(sha(Path(__file__).with_name("xdata_platform_contract.json").read_bytes()) ==
            binding["contract_sha256"], "Platform contract identity changed")
    return binding


def regions(artifacts, study):
    _, symbols, debug, _, objects = artifacts
    ownership = xdata(*artifacts[1:])
    by_module = defaultdict(list)
    for row in study["objects"]:
        if row["key"]:
            by_module[row["module"]].append(row)
    result, runs, anchors = [], [], []
    for module in objects:
        rows = by_module[module]
        if not rows:
            continue
        start, end = ownership["modules"][module]
        result.append(dict(id=module, start=start, end=end, contiguous=True, relocatable=True,
                           shrinkable=module not in platform_contract()["frozen_modules"],
                           extent_source="linked XSEG .ds ledger and object area size"))
        current, previous = None, None
        for row in rows:
            retained = row["owner"] is None or row["compiler_class"] in {
                "GLOBAL", "FILE_STATIC", "STATIC_LOCAL", "UNKNOWN"}
            if retained:
                if current is not None:
                    current["fence_after"] = row["key"]
                current = None
                previous = row["key"]
                anchors.append(dict(key=row["key"], symbol=row["symbol"], module=module,
                                    size=row["size"], address=row["address"], shape=row["shape"],
                                    first=row["address"] == start,
                                    last=row["address"] + row["size"] == end))
            else:
                if current is None:
                    current = dict(id=f"{module}:{len(runs)}", module=module, keys=[],
                                   start=row["address"], end=row["address"],
                                   fence_before=previous, fence_after=None)
                    runs.append(current)
                current["keys"].append(row["key"])
                current["end"] = row["address"] + row["size"]
    return dict(regions=result, runs=runs, anchors=anchors, contract=platform_contract(),
                generated_header=CallerSchema(debug).header(symbols),
                constraints=len(result)+len(runs)+2*len(anchors)+len(platform_contract()["fixed_objects"])+3)


def population(study, model, graph, symbols, references, objects):
    edges, closure, cycles = activation_graph(graph, symbols)
    require(not cycles, "Recursive activation")
    decoded, _, functions, entries, targets, calls = graph
    by_owner = {".".join(ident[:2]): e for e, ident in entries.items()}
    contract = platform_contract()
    roots = {symbols[n] for n in contract["root_symbols"]}
    roots |= {e for e, ident in entries.items() if ident[0] in contract["root_modules"]}
    require(roots <= entries.keys(), "Unbound platform roots")
    code_areas = {a["name"] for raw in objects.values() for a in metadata(raw)[0] if a["flags"] & 0x20}
    byte_owner = {pc+i: owner for pc, owner in functions.items() for i in range(len(decoded[pc]))}
    control = {pc+i for pc in targets for i in range(1, len(decoded[pc]))}
    for pc in calls:
        if decoded[pc] == b"\x12" + symbols[contract["banked_call_entry"]].to_bytes(2, "big"):
            control.update((pc-5, pc-3, pc-1))
    for ref in references:
        if ref["target_area"] in code_areas and ref["source"] not in control and ref["value"] in byte_owner:
            roots.add(byte_owner[ref["value"]])
    live = roots | set().union(*(closure[e] for e in roots))
    membership = {key: run["id"] for run in model["runs"] for key in run["keys"]}
    rows = {r["key"]: r for r in study["objects"] if r["key"]}
    groups, rejected = [], []
    for g in study["groups"]:
        require(g["owner"] in by_owner, "Compiler owner missing from linked call graph")
        reason = None
        if by_owner[g["owner"]] not in live:
            reason = "unreachable owner retained unchanged; dead stripping excluded"
        regions_used = {membership.get(k) for k in g["objects"]}
        if None in regions_used or len(regions_used) != 1:
            reason = "activation group spans retained-object boundaries"
        if reason:
            rejected.append(dict(owner=g["owner"], bytes=g["bytes"], reason=reason))
            continue
        require(all(rows[k]["compiler_class"] in
                    {"LOCAL", "FIRST_ARGUMENT_HOME", "REGISTER_ARGUMENT_HOME", "COMPILER_TEMP",
                     "INLINE_RETURN_HOME"} for k in g["objects"]), "Nonlocal compiler storage selected")
        groups.append({**g, "region": regions_used.pop(),
                       "classes": sorted({rows[k]["compiler_class"] for k in g["objects"]}),
                       "alignment": 1,
                       "ancestors": sorted(o for o, e in by_owner.items() if by_owner[g["owner"]] in closure[e])})
    owners = {g["owner"] for g in groups}
    for g in groups:
        g["conflicts"] = sorted(set(g["conflicts"]) & owners)
    return groups, rejected


def context(root, directory, binding_file):
    started = time.perf_counter()
    bind_inputs(root, directory, binding_file)
    parsing = time.perf_counter()-started
    study = analyze(root, directory)
    artifacts = load(root)
    graph = call_graph(*artifacts)
    references, _ = audit(root, artifacts, json.loads((root / "layout.json").read_bytes()))
    model = regions(artifacts, study)
    groups, rejected = population(study, model, graph, artifacts[1], references, artifacts[4])
    return dict(study=study, artifacts=artifacts, graph=graph, references=references,
                model=model, groups=groups, rejected=rejected,
                timings=dict(input_binding_seconds=parsing, analysis_seconds=time.perf_counter()-started))


def select(ctx):
    plans, metrics = allocate(ctx["groups"])
    groups = {g["owner"]: g for g in ctx["groups"]}
    rows = {r["key"]: r for r in ctx["study"]["objects"] if r["key"]}
    runs = {r["id"]: r for r in ctx["model"]["runs"]}
    for i, plan in enumerate(plans):
        plan.update(id=f"pool_{i}", symbol=f"_xdata_pool_{i}", module=runs[plan["region"]]["module"],
                    saving=plan["original_bytes"]-plan["width"], offsets={})
        for owner in plan["owners"]:
            offset = plan["group_offsets"][owner]
            for key in groups[owner]["objects"]:
                plan["offsets"][key] = offset
                offset += rows[key]["size"]
    return plans, metrics


def check_plans(ctx, plans):
    rows = {r["key"]: r for r in ctx["study"]["objects"] if r["key"]}
    groups = {g["owner"]: g for g in ctx["groups"]}
    runs = {r["id"]: r for r in ctx["model"]["runs"]}
    seen, used_owners, ids = set(), set(), set()
    for p in plans:
        require(p["id"] not in ids and p["symbol"] == "_xdata_"+p["id"], "Duplicate/invalid pool identity")
        ids.add(p["id"])
        require(p["region"] in runs and p["module"] == runs[p["region"]]["module"],
                "Pool has foreign owning region")
        keys, owners = set(p["offsets"]), set(p["owners"])
        require(keys and not keys & seen and len(owners) == len(p["owners"]) and not owners & used_owners,
                "Object or activation group assigned twice")
        seen |= keys
        used_owners |= owners
        require(owners <= groups.keys(), "Pool owner lacks lifetime/initialization/escape proof")
        require(keys == {k for owner in owners for k in groups[owner]["objects"]},
                "Pool omits or invents an activation-group object")
        require(keys <= set(runs[p["region"]]["keys"]), "Pool crosses private fence or module")
        require(type(p["width"]) is int and p["width"] > 0, "Invalid pool width")
        require(set(p["group_offsets"]) == owners, "Activation-group offsets have foreign owners")
        for owner in owners:
            cursor = p["group_offsets"][owner]
            require(type(cursor) is int, "Invalid activation-group offset")
            for key in groups[owner]["objects"]:
                require(p["offsets"][key] == cursor, "Activation-group offsets differ")
                cursor += rows[key]["size"]
        spans = defaultdict(set)
        for key in keys:
            row, off = rows[key], p["offsets"][key]
            require(row["candidate_for_overlay"] == "yes" and
                    row["compiler_class"] in {"LOCAL", "FIRST_ARGUMENT_HOME", "REGISTER_ARGUMENT_HOME",
                                             "COMPILER_TEMP", "INLINE_RETURN_HOME"} and
                    not row["compiler_isr"] and not row["compiler_reentrant"],
                    "Ineligible compiler storage in pool")
            require(type(off) is int and 0 <= off <= p["width"]-row["size"], "Pool exceeds owning region")
            span = set(range(off, off+row["size"]))
            require(not span & spans[row["owner"]], "Same-owner objects overlap")
            spans[row["owner"]] |= span
        for a in owners:
            for b in owners & set(groups[a]["conflicts"]):
                require(not spans[a] & spans[b], "Ancestor/descendant activations share bytes")
        require(set().union(*spans.values()) == set(range(p["width"])), "Pool contains unaccounted bytes")
        require(sum(rows[k]["size"] for k in keys) == p["original_bytes"] and
                p["original_bytes"]-p["width"] == p["saving"] > 0, "False claimed saving")
    return rows


def transform(raw, plans):
    text, replacements, seen = raw.decode("ascii"), [], set()
    by_key = {key: p for p in plans for key in p["offsets"]}
    require(len(by_key) == sum(len(p["offsets"]) for p in plans), "Object assigned twice")
    for p in plans:
        require(p["symbol"] not in text, "Pool symbol already present")
    found = set()
    for block in assembly_allocations(text):
        p = by_key.get(block["key"])
        if p is None:
            continue
        prefix = ""
        if p["id"] not in seen:
            prefix = p["symbol"] + ":\n\t.ds " + str(p["width"]) + "\n"
            seen.add(p["id"])
        expression = p["symbol"] + "+" + str(p["offsets"][block["key"]])
        replacements.append((block["start"], block["end"],
                             prefix+block["key"]+"=="+expression+"\n"+block["symbol"]+"="+expression+"\n"))
        found.add(block["key"])
    require(found == by_key.keys(), "Missing physical declaration")
    for start, end, replacement in reversed(replacements):
        text = text[:start] + replacement + text[end:]
    return text.encode("ascii")


def verify(root):
    started = time.perf_counter()
    manifest = json.loads((root / "xdata-overlay.json").read_bytes())
    require(manifest["version"] == 3, "Wrong multi-pool schema")
    baseline, directory, binding = map(Path, (manifest["baseline"], manifest["metadata"], manifest["bindings"]))
    require(baseline.resolve() != root.resolve() and not (baseline / "xdata-overlay.json").exists(),
            "Invalid preliminary image")
    ctx = context(baseline, directory, binding)
    require(sha(binding.read_bytes()) == manifest["binding_sha256"], "Input bindings changed")
    require(ctx["model"] == manifest["regions"], "Region model differs from linked reconstruction")
    plans = manifest["plans"]
    check_plans(ctx, plans)
    original = ctx["artifacts"]
    by_module = defaultdict(list)
    for p in plans:
        by_module[p["module"]].append(p)
    for module in original[4]:
        expected = transform((baseline / (module+".asm")).read_bytes(), by_module[module])
        require((root / (module+".asm")).read_bytes() == expected, "Non-declaration instruction/source mutation")
        require((root / (module+".adb")).read_bytes() == (baseline / (module+".adb")).read_bytes(),
                "CDB type/ABI declaration changed")
    candidate = load(root)
    require(list(candidate[4]) == list(original[4]), "Link module order changed")
    require([l for l in original[2].splitlines() if not l.startswith("L:")] ==
            [l for l in candidate[2].splitlines() if not l.startswith("L:")], "CDB type/ABI changed")
    with tempfile.TemporaryDirectory(prefix="multipool-reassemble-") as temporary:
        for module in original[4]:
            if by_module[module]:
                source = Path(temporary) / (module+".asm")
                source.write_bytes((root / source.name).read_bytes())
                subprocess.run(["sdas8051", "-plosgff", str(source)], check=True, capture_output=True)
                require(source.with_suffix(".rel").read_bytes() == candidate[4][module],
                        "Actual pool object differs from independent assembly")
            else:
                require(candidate[4][module] == original[4][module], "Unselected object changed")
    require(identities(root)["runtime"] == identities(baseline)["runtime"], "Runtime storage/code changed")
    graph = call_graph(*candidate)
    require(graph[2:] == ctx["graph"][2:], "Linked call graph/entries/control flow changed")
    refs, relocation = audit(root, candidate, json.loads((root / "layout.json").read_bytes()))
    allowed = {r["source"]+i for r in ctx["references"]+refs for i in range(r["width"])}
    require(original[0].keys() == candidate[0].keys() and
            all(original[0][pc] == candidate[0][pc] or pc in allowed for pc in original[0]),
            "Instruction bytes changed outside relocation operands")
    physical = verify_physical(ctx, plans, candidate, graph)
    physical["witnesses"] = overlap_witnesses(ctx, physical["pools"], candidate, graph)
    require(identities(root) == manifest["candidate_identities"], "Immutable candidate identity differs")
    return dict(**physical, relocation_audit=relocation, source_ownership_inference=False, dead_saving=0,
                verification_seconds=time.perf_counter()-started)


def overlap_witnesses(ctx, pools, candidate, graph):
    rows = {r["key"]: r for r in ctx["study"]["objects"] if r["key"]}
    groups = {g["owner"]: g for g in ctx["groups"]}
    witnesses = []
    for p in pools:
        pairs = []
        for a in sorted(p["offsets"]):
            for b in sorted(p["offsets"]):
                if a >= b or rows[a]["owner"] == rows[b]["owner"]:
                    continue
                lo = max(p["offsets"][a], p["offsets"][b])
                hi = min(p["offsets"][a]+rows[a]["size"], p["offsets"][b]+rows[b]["size"])
                if lo < hi:
                    pairs.append((a, b, lo, hi))
        require(pairs, "Physical pool saves no shared bytes")
        a, b, lo, hi = pairs[0]
        require(rows[b]["owner"] not in groups[rows[a]["owner"]]["conflicts"],
                "Witness owners interfere")
        homes = []
        for key in (a, b):
            row = {**rows[key], "address": p["start"]+p["offsets"][key]}
            refs = [(pc, graph[0][pc], "") for pc in row["references"]]
            evidence = {}
            safe, reason, _ = nonescape(row, graph, {key: refs}, candidate[2], evidence=evidence)
            require(safe, "Witness lifetime proof failed: "+reason)
            homes.append({k: row[k] for k in ("key", "owner", "symbol", "compiler_class", "size", "address")} |
                         dict(proof=reason, accesses={kind: sorted(sites) for kind, sites in evidence.items()}))
        witnesses.append(dict(pool=p["id"], start=p["start"]+lo, end=p["start"]+hi, homes=homes,
                              relation="Neither owner is a transitive ancestor of the other"))
    return witnesses


def verify_physical(ctx, plans, candidate, graph):
    rows = check_plans(ctx, plans)
    original = ctx["artifacts"]
    locations, old_locations = full_locations(candidate[2]), full_locations(original[2])
    require(locations.keys() == old_locations.keys() and
            all(locations[k] == v for k, v in old_locations.items() if k not in rows),
            "Non-XDATA debug identity changed")
    cursor, bases = 0, {}
    selected = {key: p for p in plans for key in p["offsets"]}
    for row in ctx["study"]["objects"]:
        p = selected.get(row["key"])
        if p:
            if p["id"] not in bases:
                bases[p["id"]] = cursor
                cursor += p["width"]
            expected = bases[p["id"]] + p["offsets"][row["key"]]
        else:
            expected = cursor
            cursor += row["size"]
        if row["key"]:
            require(locations[row["key"]] == expected, "Wrong CDB allocation address")
        else:
            require(all(candidate[1][s] == expected for s in row["linker_aliases"]), "Runtime prefix changed")
        if p:
            relocated = {**row, "address": expected}
            use = [(pc, graph[0][pc], "") for pc in row["references"]]
            safe, reason, _ = nonescape(relocated, graph, {row["key"]: use}, candidate[2])
            require(safe, "Final escape/initialization failure: " + reason)
    contract = ctx["model"]["contract"]
    physical, extents, at = set(range(contract["runtime_prefix_bytes"])), {}, contract["runtime_prefix_bytes"]
    for module, listing in candidate[3].items():
        ranges = allocations(listing.decode("ascii")).get("XSEG", [])
        spans = set()
        for address, size in ranges:
            span = set(range(address, address+size))
            require(size > 0 and not span & (physical | spans), "Physical pools overlap")
            spans |= span
        area = next(a for a in metadata(candidate[4][module])[0] if a["name"] == "XSEG")
        require(len(spans) == area["size"] and spans == set(range(at, at+len(spans))),
                "Incomplete physical byte ledger")
        extents[module] = [at, at+len(spans)]
        physical |= spans
        at += len(spans)
    symbols = candidate[1]
    require(physical == set(range(cursor)) and at == cursor == symbols["l_XSEG"] ==
            original[1]["l_XSEG"]-sum(p["saving"] for p in plans), "l_XSEG saving/byte coverage differs")
    require(symbols["s_XSEG"] == contract["ordinary_start"] and symbols["s_XISEG"] == cursor and
            cursor <= contract["ordinary_limit"] and all(symbols["l_"+a] == 0 for a in contract["empty_areas"]),
            "Broken CRT XSEG coverage")
    for name, (address, _) in contract["fixed_objects"].items():
        require(symbols[name] == address, "Fixed status/reserved object moved")
    for lo, hi in contract["forbidden_ranges"]:
        require(not physical.intersection(range(lo, hi)), "Forbidden alias storage allocated")
    anchor_end = contract["runtime_prefix_bytes"]
    for a in ctx["model"]["anchors"]:
        address = locations[a["key"]]
        start, end = extents[a["module"]]
        require(address >= anchor_end and start <= address < address+a["size"] <= end and
                (not a["first"] or address == start) and (not a["last"] or address+a["size"] == end),
                "Private fence/retained-object ordering changed")
        if a["symbol"] in symbols:
            require(symbols[a["symbol"]] == address, "Altered private fence symbol")
        anchor_end = address+a["size"]
    occupied, output = set(), []
    for p in plans:
        base = {locations[k]-off for k, off in p["offsets"].items()}
        require(base == {bases[p["id"]]}, "Actual pool base differs")
        base = base.pop()
        span = set(range(base, base+p["width"]))
        require(not span & occupied, "Two physical pools overlap")
        occupied |= span
        require((base, p["width"]) in allocations(candidate[3][p["module"]].decode("ascii"))["XSEG"],
                "Missing real pool reservation")
        lo, hi = extents[p["module"]]
        require(lo <= base < base+p["width"] <= hi, "Pool outside owning physical region")
        for row in ctx["study"]["objects"]:
            if row["key"] and row["key"] not in p["offsets"]:
                address = locations[row["key"]]
                require(not span.intersection(range(address, address+row["size"])),
                        "Partial overlap outside declared pool")
        output.append({**p, "start": base, "end": base+p["width"]})
    return dict(ordinary=cursor, saving=original[1]["l_XSEG"]-cursor, pools=output, regions=extents,
                generated_header=CallerSchema(candidate[2]).header(symbols))


def prepare(baseline, directory, binding, root, analysis_only=False):
    ctx = context(baseline, directory, binding)
    plans, metrics = select(ctx)
    check_plans(ctx, plans)
    report = dict(plans=plans, metrics=metrics, timings=ctx["timings"], regions=ctx["model"],
                  rejected=ctx["rejected"], eligible_homes=sum(
                      r["candidate_for_overlay"] == "yes" for r in ctx["study"]["objects"]),
                  eligible_bytes=sum(r["size"] for r in ctx["study"]["objects"]
                                     if r["candidate_for_overlay"] == "yes"),
                  groups=ctx["groups"])
    if analysis_only:
        root.write_text(json.dumps(report, separators=(",", ":"))+"\n")
        return metrics
    require(not root.exists(), "Candidate path already exists")
    root.mkdir(parents=True)
    original = ctx["artifacts"]
    by_module = defaultdict(list)
    for p in plans:
        by_module[p["module"]].append(p)
    for module in original[4]:
        (root / (module+".asm")).write_bytes(transform((baseline / (module+".asm")).read_bytes(), by_module[module]))
        shutil.copyfile(baseline / (module+".adb"), root / (module+".adb"))
        subprocess.run(["sdas8051", "-plosgff", str(root / (module+".asm"))], check=True, capture_output=True)
    shutil.copyfile(baseline / "spill-manifest.json", root / "spill-manifest.json")
    placement = json.loads((baseline / "data-placement.json").read_bytes())
    placement["objects"] = {m: sha((root / (m+".rel")).read_bytes()) for m in original[4]}
    (root / "data-placement.json").write_text(json.dumps(placement, indent=2)+"\n")
    started = time.perf_counter()
    subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("join_smoke_layout.py")),
                    "--output", str(root), "--data-placement", str(root / "data-placement.json"),
                    "--modules", *original[4]], check=True)
    report["timings"]["relink_seconds"] = time.perf_counter()-started
    (root / "allocation.json").write_text(json.dumps(report, separators=(",", ":"))+"\n")
    manifest = dict(version=3, baseline=str(baseline), metadata=str(directory), bindings=str(binding),
                    binding_sha256=sha(binding.read_bytes()), candidate_identities=identities(root),
                    plans=plans, regions=ctx["model"])
    (root / "xdata-overlay.json").write_text(json.dumps(manifest, separators=(",", ":"))+"\n")
    return verify(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--bindings", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--analysis-only", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    result = verify(args.output) if args.verify else prepare(
        args.baseline, args.metadata, args.bindings, args.output, args.analysis_only)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
