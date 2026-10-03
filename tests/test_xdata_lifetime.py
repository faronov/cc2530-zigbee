#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Negative checks against the measured objects, not invented accepted outputs."""
import argparse
import copy
import json
from pathlib import Path
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from join_smoke_analysis import call_graph, load
from verify_firmware import require
from xdata_lifetime import analyze, nonescape
from xdata_overlay import transform, verify
from xdata_relocations import records


def rejected(action, message):
    try:
        action()
    except ValueError as error:
        require(message in str(error), "Wrong rejection: " + str(error))
    else:
        raise AssertionError("Mutation unexpectedly accepted: " + message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args()
    study = analyze(args.baseline)
    require(study["xseg"] == 7676 and study["function_scoped"] == 2849, "Baseline resource mismatch")
    require(study["categories"]["runtime_shared"] == {"bytes": 26, "objects": 11},
            "Runtime homes must not include anonymous bytes in parameter extents")
    result = verify(args.candidate)
    require(result["ordinary"] == 7637 and result["saving"] == 39, "Actual saving differs")
    manifest_path = args.candidate / "xdata-overlay.json"
    original = json.loads(manifest_path.read_bytes())
    read = Path.read_bytes

    def check_manifest(change, message):
        mutated = copy.deepcopy(original)
        change(mutated)

        def altered(path):
            return json.dumps(mutated).encode() if path == manifest_path else read(path)
        with patch.object(Path, "read_bytes", altered), patch("xdata_overlay.analyze", return_value=study):
            rejected(lambda: verify(args.candidate), message)

    keys = list(original["plan"]["offsets"])
    check_manifest(lambda m: m["plan"]["offsets"].__setitem__(keys[0], -1), "exceeds reservation")
    check_manifest(lambda m: m["plan"]["offsets"].__setitem__(keys[1], 0), "same activation")
    check_manifest(lambda m: m["plan"].__setitem__("width", 2), "exceeds reservation")
    check_manifest(lambda m: m["plan"].__setitem__("saving", 40), "l_XSEG reduction")
    check_manifest(lambda m: m["candidate_identities"].__setitem__("ihx", "0"*64), "image identity")
    check_manifest(lambda m: m["baseline_identities"].__setitem__("cdb", "0"*64), "baseline identity")
    receive = next(r for r in study["objects"] if r["owner"] == "nwk_aps.nwk_aps_receive"
                   and r["candidate_for_overlay"] == "yes" and r["size"] <= 6)

    def conflicting(m):
        m["plan"]["offsets"][receive["key"]] = 0
        m["plan"]["owners"].append(receive["owner"])
    check_manifest(conflicting, "Simultaneously live")
    unsafe = next(r for r in study["objects"] if r["module"] == "nwk_aps"
                  and "_PARM_" in r["symbol"])

    def escaped(m):
        m["plan"]["offsets"][unsafe["key"]] = 0
        if unsafe["owner"] not in m["plan"]["owners"]:
            m["plan"]["owners"].append(unsafe["owner"])
    check_manifest(escaped, "lacks a nonescape proof")
    graph = call_graph(*load(args.baseline))
    row = next(r for r in study["objects"] if r["key"] == keys[0])
    refs = [(pc, graph[0][pc], "") for pc in row["references"]]
    mutated = (dict(graph[0]), *graph[1:])
    after_address = refs[0][0] + 3
    mutated[0][after_address] = b"\xe5\x82"
    safe, reason, _ = nonescape(row, mutated, {row["key"]: refs}, load(args.baseline)[2])
    require(not safe and "address read" in reason, "DPTR address escape was accepted")
    raw = (args.baseline / "nwk_aps.asm").read_bytes()
    output = transform(raw, original["plan"])
    require(output.count(b"\t.ds 6\n") > 0 and b"_xdata_overlay_pool:" in output,
            "Missing physical XSEG reservation")
    rejected(lambda: transform(output, original["plan"]), "Already transformed")
    rejected(lambda: list(records(b"XH3\nT 00 00 00 00 00\nR 00 00 00 00 04 03 00 00\n")),
             "Unsupported relocation mode")
    rejected(lambda: list(records(b"XH3\nT 00 00 00 00\nR 00 00 00 00 00 03 00 00\n")),
             "Out-of-range")
    print("XDATA inventory, real 39-byte reduction and 12 negative checks PASS")


if __name__ == "__main__":
    main()
