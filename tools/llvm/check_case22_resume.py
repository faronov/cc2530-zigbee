#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Real reference-s51 case22 prefix equivalence; never a complete case22 verdict."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import statistics
import signal
import subprocess
import sys
import time

from tiers import digest, load_receipt

HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "vectors", "simulator", "output", "compiler-receipt"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--expected-elf", required=True)
    parser.add_argument("--expected-compiler", required=True)
    parser.add_argument("--cpus", type=int, default=2, help="restrict benchmark to this many allowed CPUs")
    args = parser.parse_args()
    cpus = sorted(os.sched_getaffinity(0))[:args.cpus]
    if not cpus:
        parser.error("--cpus must be positive")
    os.sched_setaffinity(0, cpus)
    args.output.mkdir(parents=True, exist_ok=False)
    if sha(args.elf) != args.expected_elf:
        raise ValueError("Not the requested frozen ELF")
    compiler = load_receipt(args.compiler_receipt)
    if (compiler["status"] != "PASS" or
            compiler["identity"]["compiler"]["llvm_commit"] != args.expected_compiler):
        raise ValueError("Not the requested sealed compiler evidence")
    base = [sys.executable, "-B", str(HERE/"llvm_join_replay.py"),
            "--elf", str(args.elf.resolve()), "--vectors", str(args.vectors.resolve()),
            "--simulator", str(args.simulator.resolve()), "--case", "22", "--limit", "3"]
    clean = {k: v for k, v in os.environ.items() if k not in
             ("RESUME", "RESUME_SHA256", "SNAP_DIR", "SNAP_EVERY", "STOP_AT_TIME", "STOP_AFTER_SECONDS")}

    def run(name, extra=None, expected=0, stop_signal=False):
        out = args.output/name
        out.mkdir()
        env = clean | {"SNAP_DIR": str(out/"snap"), "SNAP_EVERY": "1",
                       "REPLAY_PERFORMANCE": str(out/"performance.jsonl"),
                       "PYTHONUNBUFFERED": "1", "SIM_TIMEOUT": "6000",
                       "REPLAY_IDENTITY": digest({"compiler": compiler["identity"]["compiler"],
                                                  "elf": args.expected_elf, "experiment": sha(__file__)})} | (extra or {})
        with (out/"console.log").open("w") as log:
            proc = subprocess.Popen(base+["--report", str(out/"report.json")], env=env,
                                    stdout=log, stderr=subprocess.STDOUT)
            if stop_signal:
                deadline = time.monotonic()+180
                performance = out/"performance.jsonl"
                while proc.poll() is None:
                    rows = [json.loads(s) for s in performance.read_text().splitlines()] if performance.exists() else []
                    if any(r["kind"] == "chunk" and r["step"] == 2 for r in rows):
                        proc.send_signal(signal.SIGTERM)
                        break
                    if time.monotonic() >= deadline:
                        proc.kill()
                        proc.wait()
                        raise TimeoutError("case22 did not reach its signal checkpoint test")
                    time.sleep(0.1)
            try:
                rc = proc.wait(timeout=1200)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise
        if rc != expected:
            raise RuntimeError(f"{name}: exit {rc}, expected {expected}; see {out/'console.log'}")
        if expected == 1:
            if "Snapshot differs from its sealed segment receipt" not in (out/"console.log").read_text():
                raise ValueError("Negative test did not fail on the requested snapshot-integrity check")
            return None
        return json.loads((out/"report.json").read_text())

    uninterrupted = run("uninterrupted")
    boundary = run("boundary", {"RESUME": str(args.output/"uninterrupted/snap/case22-step1.pkl"),
                               "STOP_AT_TIME": str(time.time()-1)}, expected=75)
    if boundary["next_step"] != 2 or boundary["peripheral_stops"] != 0:
        raise ValueError("Did not obtain the requested actual case22 caller checkpoint")
    intra = run("intra", {"RESUME": boundary["resume"], "RESUME_SHA256": boundary["snapshot_sha256"],
                         "STOP_AT_TIME": str(time.time()-1)}, expected=75)
    if intra["next_step"] != 2 or not 0 < intra["peripheral_stops"] < uninterrupted["peripheral_stops"]:
        raise ValueError("Did not suspend inside the long caller step")
    resumed = run("resumed", {"RESUME": intra["resume"], "RESUME_SHA256": intra["snapshot_sha256"]})
    functional = lambda r: {k: v for k, v in r.items() if k != "seconds"}
    if functional(uninterrupted) != functional(resumed):
        raise ValueError("Segmented execution differs from uninterrupted execution")
    run("wrong-digest", {"RESUME": intra["resume"], "RESUME_SHA256": "0"*64}, expected=1)
    signalled = run("signal", {"RESUME": boundary["resume"],
                              "RESUME_SHA256": boundary["snapshot_sha256"]}, expected=75, stop_signal=True)
    after_signal = run("after-signal", {"RESUME": signalled["resume"],
                                      "RESUME_SHA256": signalled["snapshot_sha256"]})
    if functional(after_signal) != functional(uninterrupted):
        raise ValueError("SIGTERM checkpoint/resume changed MCU execution")

    measurements = {}
    for workers in (1, 2, 4):
        started = time.perf_counter()
        with ThreadPoolExecutor(max_workers=workers) as pool:
            reports = list(pool.map(lambda i: run(f"workers{workers}-{i}"), range(workers)))
        if any(functional(r) != functional(uninterrupted) for r in reports):
            raise ValueError("Concurrent reference execution changed observations")
        elapsed = time.perf_counter()-started
        measurements[str(workers)] = {"seconds": elapsed, "cases": workers,
                                     "events_per_second": sum(r["peripheral_stops"] for r in reports)/elapsed}
    startup = []
    for _ in range(5):
        started = time.perf_counter()
        subprocess.run([str(args.simulator), "-q", "-c", "-"], input="quit\n",
                       capture_output=True, text=True, check=True, timeout=30)
        startup.append(time.perf_counter()-started)
    metrics = {}
    for name in ("uninterrupted", "boundary", "intra", "resumed"):
        rows = [json.loads(s) for s in (args.output/name/"performance.jsonl").read_text().splitlines()]
        metrics[name] = {kind: {"count": len(part), "seconds": sum(r["seconds"] for r in part),
                                "max_seconds": max(r["seconds"] for r in part)}
                         for kind in ("simulator", "snapshot", "chunk")
                         if (part := [r for r in rows if r["kind"] == kind])}
    result = {"status": "PASS", "scope": "case22 first three caller steps only; NOT complete case22",
              "elf_sha256": sha(args.elf), "simulator_sha256": sha(args.simulator),
              "vector_sha256": sha(args.vectors/"v22.json"), "replay_sha256": sha(HERE/"llvm_join_replay.py"),
              "compiler_receipt_sha256": sha(args.compiler_receipt), "compiler_commit": args.expected_compiler,
              "uninterrupted": uninterrupted, "resumed": resumed, "boundary": boundary, "intra": intra,
              "concurrency": measurements, "performance": metrics, "signal_checkpoint": signalled,
              "cpu_affinity": cpus,
              "startup_seconds": {"median": statistics.median(startup), "max": max(startup)}}
    (args.output/"proof.json").write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
