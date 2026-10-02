#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Bind real compiler sidecars to the independent image inventory, fail closed."""
import argparse
import copy
import json
from pathlib import Path
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_firmware import require
from xdata_lifetime import analyze, assembly_allocations, compiler_ownership
from xdata_overlay import choose


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    legacy = analyze(args.layout)
    with patch("xdata_lifetime.parameter_names", side_effect=AssertionError("Source parsing forbidden")):
        actual = analyze(args.layout, args.metadata)
    old = {r["key"]: r for r in legacy["objects"] if r["key"]}
    new = {r["key"]: r for r in actual["objects"] if r["key"]}
    require(old.keys() == new.keys(), "Inventory coverage changed")
    for key in old:
        for field in ("owner", "address", "size", "symbol", "category", "candidate_for_overlay"):
            require(old[key][field] == new[key][field], f"Reconciliation mismatch: {key} {field}")
    require(choose(legacy, "nwk_aps", 10) == choose(actual, "nwk_aps", 10),
            "Metadata selected a different physical pool")
    safe = [r for r in new.values() if r["candidate_for_overlay"] == "yes"]
    require((len(safe), sum(r["size"] for r in safe)) == (492, 1000),
            "Conservative eligibility changed")
    module = "nwk_aps"
    sidecar = next(args.metadata.rglob(module + ".xdata.json"))
    manifest = json.loads(sidecar.read_bytes())
    text = (args.layout / (module + ".asm")).read_text("ascii")
    blocks = assembly_allocations(text)
    first = next(i for i, r in enumerate(manifest["objects"]) if r["class"] == "FIRST_ARGUMENT_HOME")
    parm = next(i for i, r in enumerate(manifest["objects"]) if r["class"] == "PARAM_CALLER_WRITTEN")
    read = Path.read_bytes

    def altered(row, field, value):
        def change(data):
            data["objects"][row][field] = value
        return change

    mutations = [
        ("module", lambda d: d.update(module="foreign")),
        ("schema", lambda d: d.update(version=2)),
        ("size", altered(first, "size", 999)),
        ("owner", altered(first, "owner", "foreign")),
        ("entry", altered(first, "owner_symbol", "_foreign")),
        ("symbol", altered(first, "symbol", "_foreign")),
        ("key", altered(first, "cdb_key", "G$foreign$0_0$0")),
        ("global", altered(first, "class", "GLOBAL")),
        ("parameter", altered(parm, "class", "LOCAL")),
        ("area", altered(first, "area", "XISEG")),
        ("flags", altered(first, "owner_isr", "false")),
        ("address", altered(first, "address", 5)),
        ("class", altered(first, "class", "UNREVIEWED")),
        ("missing", lambda d: d["objects"].pop(first)),
        ("duplicate", lambda d: d["objects"].append(d["objects"][first])),
    ]
    for label, change in mutations:
        data = copy.deepcopy(manifest)
        change(data)
        with patch.object(Path, "read_bytes",
                          lambda p: json.dumps(data).encode() if p == sidecar else read(p)):
            try:
                compiler_ownership(args.metadata, module, blocks, text)
            except ValueError:
                pass
            else:
                raise AssertionError("Accepted malformed compiler metadata: " + label)
    for field, value in (("class", "UNKNOWN"), ("class", "STATIC_LOCAL"),
                         ("owner_isr", True), ("owner_reentrant", True)):
        data = copy.deepcopy(manifest)
        data["objects"][first][field] = value
        with patch.object(Path, "read_bytes",
                          lambda p: json.dumps(data).encode() if p == sidecar else read(p)), \
                patch("xdata_lifetime.parameter_names", side_effect=AssertionError("Source parsing forbidden")):
            rows = analyze(args.layout, args.metadata)["objects"]
            row = next(r for r in rows if r["key"] == data["objects"][first]["cdb_key"])
            require(row.get("compiler_"+field.removeprefix("owner_")) == value,
                    "Conservative metadata flag was lost")
            require(row["candidate_for_overlay"] == "no", "Conservative class admitted for overlay")
    result = dict(objects=len(new), function_objects=sum(bool(r["owner"]) for r in new.values()),
                  function_bytes=actual["function_scoped"], owner_mismatches=0,
                  eligible_objects=len(safe), eligible_bytes=sum(r["size"] for r in safe),
                  source_parsing=False, negative_cases=[name for name, _ in mutations],
                  selected_plan=choose(actual, "nwk_aps", 10))
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
