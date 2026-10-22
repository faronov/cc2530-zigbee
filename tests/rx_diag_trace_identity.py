#!/usr/bin/env python3
"""Prove that CC2530_RX_DIAG leaves the radio driver's hardware access sequence unchanged.

Each --radio/--attempt pair is (flag-off binary, flag-on binary) built from the same host
test. Every deterministic --vector MMIO transcript must be byte-identical, and for radio
pairs the FNV digest of all MMIO/XREG accesses (printed before the RX_DIAG-only cases,
covering the overflow/salvage/flush cases too) must match. Synthetic host model only.
"""
import argparse
import re
import subprocess
import sys

RADIO_VECTOR = ["0x600", "0x700", "0x500", "0x1d00"]
ATTEMPT_VECTOR = ["0xe00", "0xd00", "0xc00", "0x500", "0x700", "0xb00", "0x510", "0x530",
                  "0x710", "0x900", "0x1200"]


def run(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if out.returncode:
        raise SystemExit(f"{' '.join(cmd)} failed ({out.returncode}):\n{out.stdout}{out.stderr}")
    return out.stdout


def count(text, pattern):
    m = re.search(pattern, text)
    if not m:
        raise SystemExit(f"missing summary /{pattern}/ in host test output")
    return int(m[1])


def compare(off, on, vector, summary, digest):
    a, b = run([off]), run([on])
    n = count(a, summary)
    if count(b, summary) != n:
        raise SystemExit(f"{off} and {on} ran different scenario counts")
    if digest:
        da, db = (re.findall(r"MMIO digest before RX_DIAG-only cases: ([0-9a-f]{8})", t) for t in (a, b))
        if len(da) != 1 or da != db:
            raise SystemExit(f"MMIO digest differs: {off}={da} {on}={db}")
    for i in range(n):
        if run([off, "--vector", str(i)] + vector) != run([on, "--vector", str(i)] + vector):
            raise SystemExit(f"--vector {i} MMIO transcript differs between {off} and {on}")
    print(f"RX_DIAG trace identity: {on}: {n} vectors" + (f", digest {da[0]}" if digest else "") + " identical")
    return n


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--radio", nargs=2, action="append", default=[], metavar=("OFF", "ON"))
    p.add_argument("--attempt", nargs=2, action="append", default=[], metavar=("OFF", "ON"))
    args = p.parse_args(argv)
    if not args.radio and not args.attempt:
        p.error("nothing to compare")
    for off, on in args.radio:
        compare(off, on, RADIO_VECTOR, r"AUTOACK: (\d+) linked scenarios", True)
    for off, on in args.attempt:
        compare(off, on, ATTEMPT_VECTOR, r"MAC attempt: (\d+) sequences", False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
