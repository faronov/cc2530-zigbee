#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Opt-in primary overlay acceptance, including mandatory complete case0; no hardware."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from verify_firmware import require
from xdata_build import (ROOT, build, canonical, checked_receipt, executable, inputs,
                         sha, write_json)
from xdata_multipool import check_identities


def successful_join(report, expected):
    require(report["complete"] is True and report["phase"] == 5 and report["terminal"] == "serving",
            "Overlay release gate requires a complete READY/serving case0")
    require(type(report["case"]) is int and report["case"] == 0 and
            type(report["peak_sp"]) is int and 0x4f <= report["peak_sp"] <= 0x7c and
            report["board"] == "lg_esl29_rev03" and report["key_mode"] == "default-tc" and
            report["simulated"] is True and
            report["hardware_observed"] is False, "Wrong complete-join execution evidence")
    check_identities(report["identities"], expected, "Replayed release image")


def accept(output, sdcc, simulator, board, key_mode):
    root = build(output, sdcc, board, key_mode)
    compiler, _ = inputs(sdcc, board, key_mode)
    root, receipt = checked_receipt(root)
    simulator = executable(simulator)
    simulator_hash = sha(simulator)
    run = canonical(Path(tempfile.mkdtemp(prefix="acceptance-", dir=root.parent)))
    (run / "join-smoke-layout").symlink_to(Path("..") / "join-smoke-layout")
    phases = []
    environment = {k: v for k, v in os.environ.items()
                   if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES")}
    environment["PATH"] = str(compiler.parent) + os.pathsep + os.environ.get("PATH", os.defpath)

    def command(name, arguments):
        started = time.monotonic()
        print(f"Overlay acceptance: {name}", flush=True)
        with (run / (name + ".log")).open("w") as log:
            subprocess.run(list(map(str, arguments)), cwd=ROOT, env=environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        phases.append(dict(name=name, command=list(map(str, arguments)),
                           seconds=round(time.monotonic()-started, 3), result="PASS"))
        write_json(run / "progress.json", phases)

    make = ["make", "--no-print-directory", "-s", "-j2", f"BOARD={board}",
            f"JOIN_SMOKE_KEY_MODE={key_mode}", f"BUILD={run}", f"SDCC={compiler}"]
    adapter = run / "mac-adapter/mac_adapter_layout.h"
    command("adapter-header", [*make, adapter])
    require(adapter.is_file(), "Verified adapter header was not generated")
    header = root / "join_smoke_layout.h"
    require(header.is_file(), "Verified overlay header is missing")
    command("native-sanitized-shallow-deep-zcl", [
        *make, "-B", "-o", header, "-o", adapter, f"JOIN_SMOKE_LAYOUT_DIR={root}",
        "test-join-smoke-host", run / "host-join-smoke-vectors", run / "host-join-smoke-vectors-sanitize"])
    python = [sys.executable, "-B"]
    for name, layout, mode in (
            ("directed-baseline", root.parent / "preliminary/join-smoke-layout", "--baseline"),
            ("directed-overlay", root, "--xdata-overlay")):
        command(name, [*python, "tests/boot_xdata_pools.py", "--layout", layout, mode,
                       "--simulator", simulator, "--output", run / (name + ".json")])
    command("btr", [*python, "tests/boot_xdata_overlay.py", "--layout", root, "--xdata-overlay",
                    "--simulator", simulator, "--output", run / "btr.json"])
    replay = [*python, "tests/boot_join_smoke.py", "--output", run, "--board", board,
              "--key-mode", key_mode, "--simulator", simulator, "--xdata-overlay"]
    command("case2", [*replay, "--case", "2"])
    command("case0-prefix", [*replay, "--case", "0", "--limit", "4"])
    command("case0-complete", [*replay, "--case", "0"])
    report = json.loads((run / "join-replay-0.json").read_bytes())
    expected = json.loads((ROOT / "experiments/xdata/multipool/identities.json").read_bytes())["overlay"]
    successful_join(report, expected)
    _, after = checked_receipt(root)
    _, current = inputs(sdcc, board, key_mode)
    require(after == receipt and current == receipt["inputs"],
            "Overlay inputs changed during acceptance; no release evidence published")
    require(sha(simulator) == simulator_hash, "Simulator changed during acceptance")
    result = dict(version=1, result="PASS", layout=str(root), receipt_sha256=sha(root.parent / "receipt.json"),
                  simulator_sha256=simulator_hash, phases=phases, identities=expected,
                  full_join=report, hardware_observed=False)
    write_json(run / "acceptance.json", result)
    print(f"Overlay offline acceptance PASS: {run}/acceptance.json; hardware NOT RUN")
    return run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdcc", help="Explicit development compiler; default is the pinned release")
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--board", required=True)
    parser.add_argument("--key-mode", required=True)
    args = parser.parse_args()
    os.chdir(ROOT)
    try:
        accept(args.output, args.sdcc, args.simulator, args.board, args.key_mode)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Overlay acceptance FAILED: {error}; inspect the generation's acceptance-*/*.log",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
