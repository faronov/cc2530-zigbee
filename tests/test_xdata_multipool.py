#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Fail-closed mutations of bound metadata and the actual multi-pool image."""
import argparse
from contextlib import ExitStack
import copy
import json
from pathlib import Path
import re
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from join_smoke_analysis import call_graph, instructions, load
from test_xdata_lifetime import rejected
from verify_firmware import require
from xdata_lifetime import nonescape
from xdata_multipool import (bind_inputs, check_plans, context, population, select,
                            verify, verify_physical)
from xdata_relocations import audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.candidate / "xdata-overlay.json").read_bytes())
    baseline, directory, binding = map(Path, (manifest["baseline"], manifest["metadata"], manifest["bindings"]))
    with ExitStack() as guards:
        for name in ("parameter_names", "legacy_scopes", "legacy_classification", "legacy_caller_written"):
            guards.enter_context(patch("xdata_lifetime."+name, side_effect=AssertionError(name+" forbidden")))
        ctx = context(baseline, directory, binding)
        plans = manifest["plans"]
        require(select(ctx)[0] == plans == select(ctx)[0], "Nondeterministic allocation")
        with patch("xdata_multipool.context", return_value=ctx):
            proof = verify(args.candidate)
    candidate, graph = load(args.candidate), None
    graph = call_graph(*candidate)
    rows = {r["key"]: r for r in ctx["study"]["objects"] if r["key"]}
    selected = [rows[k] for p in plans for k in p["offsets"]]
    successes = []

    def reject(name, action, message):
        rejected(action, message)
        successes.append(name)

    sidecar = next(directory.rglob(selected[0]["module"]+".xdata.json"))
    original_metadata = json.loads(sidecar.read_bytes())
    index = next(i for i, r in enumerate(original_metadata["objects"]) if r["cdb_key"] == selected[0]["key"])
    read = Path.read_bytes
    changes = [
        ("falsified compiler owner", lambda d: d["objects"][index].update(owner="foreign")),
        ("falsified storage class", lambda d: d["objects"][index].update(**{"class": "PARAM_CALLER_WRITTEN"})),
        ("stale sidecar", lambda d: d.update(version=0)),
        ("duplicate metadata object", lambda d: d["objects"].append(d["objects"][index])),
        ("wrong symbol width", lambda d: d["objects"][index].update(size=999)),
    ]
    for name, change in changes:
        data = copy.deepcopy(original_metadata)
        change(data)
        with patch.object(Path, "read_bytes", lambda p: json.dumps(data).encode() if p == sidecar else read(p)):
            reject(name, lambda: bind_inputs(baseline, directory, binding), "falsified compiler sidecar")
    rglob = Path.rglob
    with patch.object(Path, "rglob", lambda p, pattern: (q for q in rglob(p, pattern) if q != sidecar)):
        reject("missing sidecar", lambda: bind_inputs(baseline, directory, binding), "falsified compiler sidecar")
    study = copy.deepcopy(ctx["study"])
    study["groups"][0]["owner"] = "unknown.owner"
    reject("owner/callgraph mismatch", lambda: population(study, ctx["model"], ctx["graph"],
           ctx["artifacts"][1], ctx["references"], ctx["artifacts"][4]), "missing from linked call graph")

    def logical(name, change, message):
        changed = copy.deepcopy(plans)
        change(changed)
        reject(name, lambda: check_plans(ctx, changed), message)

    group_map = {g["owner"]: g for g in ctx["groups"]}
    pair = next((i, a, b) for i, p in enumerate(plans) for a in p["owners"]
                for b in p["owners"] if b in group_map[a]["conflicts"])

    def interfere(changed):
        i, a, b = pair
        p = changed[i]
        cursor = p["group_offsets"][a]
        p["group_offsets"][b] = cursor
        for key in group_map[b]["objects"]:
            p["offsets"][key] = cursor
            cursor += rows[key]["size"]

    logical("ancestor/descendant bytes overlap", interfere, "activations share bytes")
    for name, field, value in (("caller-written parameter", "compiler_class", "PARAM_CALLER_WRITTEN"),
                               ("ISR owner", "compiler_isr", True),
                               ("reentrant owner", "compiler_reentrant", True)):
        old = selected[0][field]
        selected[0][field] = value
        try:
            reject(name, lambda: check_plans(ctx, plans), "Ineligible compiler storage")
        finally:
            selected[0][field] = old
    original_graph = ctx["graph"]
    local = next(r for r in selected if original_graph[0][min(r["references"])+3] != b"\xe0")
    refs = [(pc, original_graph[0][pc], "") for pc in local["references"]]
    first = min(local["references"])
    decoded = dict(original_graph[0])
    decoded[first+3] = b"\xe5\x82"
    safe, reason, _ = nonescape(local, (decoded, *original_graph[1:]), {local["key"]: refs}, ctx["artifacts"][2])
    require(not safe and "address read" in reason, "Escaping home admitted")
    successes.append("escaped home")
    initialization = None
    for row in selected:
        pc = min(row["references"])+3
        for _ in range(8):
            raw = original_graph[0].get(pc)
            if raw == b"\xf0":
                initialization = row, pc
                break
            if raw is None:
                break
            pc += len(raw)
        if initialization:
            break
    require(initialization is not None, "No real entry initialization site")
    row, pc = initialization
    decoded = dict(original_graph[0])
    decoded[pc] = b"\xe0"
    refs = [(p, original_graph[0][p], "") for p in row["references"]]
    safe, reason, _ = nonescape(row, (decoded, *original_graph[1:]), {row["key"]: refs}, ctx["artifacts"][2])
    require(not safe and "read before" in reason, "Uninitialized home admitted")
    successes.append("uninitialized home")

    def cross_fence(changed):
        p = next(p for p in changed if sum(q["module"] == p["module"] for q in ctx["model"]["runs"]) > 1)
        other = next(r for r in ctx["model"]["runs"] if r["module"] == p["module"] and r["id"] != p["region"])
        p["region"] = other["id"]

    logical("pool crossing private fence", cross_fence, "crosses private fence")
    logical("illegal cross-module overlap", lambda p: p[0].update(module=p[-1]["module"]), "foreign owning region")
    logical("false claimed saving", lambda p: p[0].update(saving=p[0]["saving"]+1), "False claimed saving")

    def outside(changed):
        p = changed[0]
        owner = p["owners"][0]
        cursor = p["width"]
        p["group_offsets"][owner] = cursor
        for key in group_map[owner]["objects"]:
            p["offsets"][key] = cursor
            cursor += rows[key]["size"]

    logical("pool outside region", outside, "Pool exceeds owning region")
    logical("object assigned twice", lambda p: p.append({**copy.deepcopy(p[0]),
            "id": "pool_999", "symbol": "_xdata_pool_999"}), "assigned twice")
    logical("false group offset", lambda p: p[0]["group_offsets"].update(
            {p[0]["owners"][0]: p[0]["width"]}), "Activation-group offsets differ")

    def physical(name, *, symbols=None, debug=None, listings=None, message):
        changed = (candidate[0], candidate[1] if symbols is None else symbols,
                   candidate[2] if debug is None else debug,
                   candidate[3] if listings is None else listings, candidate[4])
        reject(name, lambda: verify_physical(ctx, plans, changed, graph), message)

    # Only repeat immutable lifetime facts for identical graph/reference/row inputs.
    lifetime_cache = {}

    def cached_lifetime(row, actual_graph, references, debug):
        require(actual_graph is graph and debug == candidate[2], "Changed cached lifetime evidence")
        key = (json.dumps(row, sort_keys=True),
               tuple((k, tuple(v)) for k, v in sorted(references.items())))
        if key not in lifetime_cache:
            lifetime_cache[key] = nonescape(row, actual_graph, references, debug)
        return lifetime_cache[key]

    with patch("xdata_multipool.nonescape", side_effect=cached_lifetime):
        verify_physical(ctx, plans, candidate, graph)
        physical("unchanged l_XSEG", symbols={**candidate[1], "l_XSEG": ctx["artifacts"][1]["l_XSEG"]},
                 message="l_XSEG saving/byte coverage")
        physical("broken CRT XSEG coverage", symbols={**candidate[1], "s_XISEG": 0}, message="Broken CRT")
        fence = next(a for a in ctx["model"]["anchors"] if a["symbol"] in candidate[1]
                     and a["symbol"].endswith("_reserved_end"))
        physical("altered fence symbol", symbols={**candidate[1], fence["symbol"]: candidate[1][fence["symbol"]]+1},
                 message="Altered private fence symbol")
        for name, overlap in (("two pools overlapping physically", True), ("incomplete byte ledger", False)):
            p, target = proof["pools"][-1], proof["pools"][0]
            listings = dict(candidate[3])
            text = listings[p["module"]].decode("ascii")
            pattern = r"(?m)^(\s*)"+f'{p["start"]:06X}'+r"(\s+\d+\s+\.ds "+str(p["width"])+r")$"
            replacement = (lambda m: m[1]+f'{target["start"]:06X}'+m[2]) if overlap else ""
            text, count = re.subn(pattern, replacement, text)
            require(count == 1, "Missing real pool declaration")
            listings[p["module"]] = text.encode("ascii")
            physical(name, listings=listings, message="Physical pools overlap" if overlap else "Incomplete physical")

    key = selected[0]["key"]
    debug, count = re.subn(r"(?m)^L:"+re.escape(key)+r":([0-9A-Fa-f]+)$",
                          lambda m: f"L:{key}:{int(m[1],16)+1:X}", candidate[2])
    require(count == 1, "Missing actual CDB home")
    physical("wrong CDB allocation address", debug=debug, message="Wrong CDB allocation address")
    module = plans[0]["module"]
    listings = dict(candidate[3])
    pc, (raw, _) = next(iter(instructions(listings[module].decode("ascii")).items()))
    text, count = re.subn(r"(?m)^(\s*"+f"{pc:06X}"+r"\s+)"+f"{raw[0]:02X}",
                         r"\g<1>73", listings[module].decode("ascii"), count=1)
    require(count == 1, "Missing actual instruction")
    listings[module] = text.encode("ascii")
    reject("unknown indirect edge", lambda: call_graph(candidate[0], candidate[1], candidate[2],
           listings, candidate[4]), "Unexpected indirect transfer")
    image = dict(candidate[0])
    ret = next(pc for pc, (raw, _) in instructions(candidate[3][module].decode("ascii")).items() if raw == b"\x22")
    image[ret] = 0
    reject("instruction mutation outside relocations", lambda: audit(args.candidate, (image, *candidate[1:]),
           json.loads((args.candidate / "layout.json").read_bytes())), "Relocation/emitted CODE mismatch")
    source = args.candidate / (module+".asm")
    changed = read(source).replace(b"\tret\n", b"\tnop\n", 1)
    require(changed != read(source), "Missing source opcode mutation")
    with patch.object(Path, "read_bytes", lambda p: changed if p == source else read(p)), \
            patch("xdata_multipool.context", return_value=ctx):
        reject("assembly instruction mutation", lambda: verify(args.candidate), "Non-declaration instruction")
    result = dict(count=len(successes), negative_cases=successes, ordinary=proof["ordinary"],
                  saving=proof["saving"], pools=len(plans), deterministic=True,
                  legacy_ownership_helpers_called=False, physical_lifetime_cache="test-only identical inputs")
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
