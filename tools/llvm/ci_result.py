#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Fail-closed FULL segment, upload and final-receipt acceptance."""
import argparse
import json
from pathlib import Path
import sys

import tiers


def segment_status(receipt, returncode, segment):
    status = receipt["status"]
    if (receipt["tier"] != "full" or receipt["segment"] != segment
            or tiers.EXIT.get(status) != returncode):
        raise ValueError("segment receipt and process exit disagree, or receipt is stale")
    return status


def chain_segment(needs):
    if needs["toolchain"]["result"] != "success":
        raise ValueError("toolchain did not succeed")
    passed = None
    for number in range(1, 7):
        job = needs[f"full-{number}"]
        status = job.get("outputs", {}).get("status", "")
        if passed is not None:
            if job["result"] != "skipped" or status:
                raise ValueError("unexpected job after complete FULL")
        elif job["result"] != "success" or status not in ("SUSPENDED", "PASS"):
            raise ValueError(f"segment {number} failed, timed out, was cancelled, or did not upload its evidence")
        elif status == "PASS":
            passed = number
    if passed is None:
        raise ValueError("FULL still incomplete after the available segments")
    return passed


def accept_full(directory, firmware, compiler):
    r = tiers.load_receipt(directory/"receipt.json")
    selected = {s["name"] for s in r["selection"]}
    variant = r["identity"].get("variant", "production")
    profile = r["profile"]["simulator"]
    cases = profile["full_cases"]+profile["variant_cases"].get(variant, [])
    if (r["tier"] != "full" or r["status"] != "PASS"
            or r["profile"] != tiers.PROFILE
            or set(r["steps"]) != selected or any(s["status"] != "PASS" for s in r["steps"].values())
            or {f"sim-case{c}" for c in cases} != {n for n in selected if n.startswith("sim-case")}
            or (variant == "parent-recovery-policy-c" and (len(selected) != 31 or len(cases) != 19))
            or r["identity"]["firmware"]["commit"] != firmware
            or r["identity"]["firmware"]["dirty"]
            or r["identity"]["tooling"]["dirty"]
            or r["identity"]["compiler"]["llvm_commit"] != compiler
            or r["identity"]["compiler"]["llvm_diff"] is not None):
        raise ValueError("incomplete FULL or incorrect frozen source identity")
    measured = r["steps"]["admission"]["details"]["measured"]
    for path, field in (("fw/link/hw1.elf", "elf_sha256"), ("fw/audit/hw1.hex", "hex_sha256")):
        actual = tiers.sha_file(directory/path)
        if actual != measured[field] or actual != r["artifacts"][path]:
            raise ValueError(f"published image differs from sealed admission: {path}")
    for case in cases:
        ok, details = tiers.sim_check(directory/f"sims/case{case}.json", case, profile["terminals"][str(case)])()
        if not ok or details != r["steps"][f"sim-case{case}"]["details"]:
            raise ValueError(f"case{case} evidence differs from the sealed complete result")
    return r["receipt_sha256"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    segment = sub.add_parser("segment")
    segment.add_argument("receipt", type=Path)
    segment.add_argument("returncode", type=int)
    segment.add_argument("number", type=int)
    chain = sub.add_parser("chain")
    chain.add_argument("needs", type=json.loads)
    accept = sub.add_parser("accept")
    accept.add_argument("directory", type=Path)
    accept.add_argument("firmware")
    accept.add_argument("compiler")
    args = parser.parse_args()
    try:
        if args.command == "segment":
            status = segment_status(tiers.load_receipt(args.receipt), args.returncode, args.number)
            print(status)
            return 0 if status in ("PASS", "SUSPENDED") else 1
        if args.command == "chain":
            print(f"segment={chain_segment(args.needs)}")
        else:
            print(f"Accepted sealed FULL receipt {accept_full(args.directory, args.firmware, args.compiler)}")
        return 0
    except (ValueError, KeyError, OSError) as error:
        print(f"FULL acceptance rejected: {error}", file=sys.stderr)
        if args.command == "segment":
            print("FAIL")
        return 1


if __name__ == "__main__":
    sys.exit(main())
