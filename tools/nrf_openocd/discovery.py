# SPDX-License-Identifier: BSD-3-Clause
"""Explicit genuine-libjaylink discovery proof; only the libusb boundary is synthetic."""

import argparse
import json
from pathlib import Path
import re

if __package__:
    from . import offline
else:
    import offline

EXPECTED_CASES = 131097  # Two 16-bit sweeps, 24 lifecycle cases, one genuine old-library control.
EXPECTED_PROCESSES = 34
PROOF_SOURCES = ("discovery.py", "discovery_probe.c", "fake_libusb.c", "fake_libusb.h", "sandbox.c")


def exercise(workspace):
    workspace = Path(workspace)
    offline.validate_workspace(workspace)
    lock = json.loads((offline.HERE / "dependencies.json").read_text())["libjaylink"]
    real = workspace / "library/lib/libjaylink.so.0"
    baseline = workspace / "pre-usb-1025/libjaylink.so.0"
    if offline.sha256(baseline) != lock["baseline_library_sha256"]:
        raise ValueError("unexpected pre-correction control library")
    for library in (real, baseline):
        if offline.needed(library) != {"libusb-1.0.so.0", "libc.so.6"}:
            raise ValueError("unexpected genuine-library dependencies")
    output = workspace / "usb-discovery-tests"
    output.mkdir(exist_ok=True)
    fake = output / "libusb-1.0.so.0"
    probe = output / "discovery-probe"
    offline.command([
        "/usr/bin/gcc", *offline.STRICT, "-fPIC", "-shared", "-Wl,-z,defs",
        "-Wl,-soname,libusb-1.0.so.0",
        "-I" + str(workspace / "deps/usr/include/libusb-1.0"),
        offline.HERE / "fake_libusb.c", offline.HERE / "sandbox.c", "-o", fake,
    ])
    offline.command([
        "/usr/bin/gcc", *offline.STRICT, "-I" + str(workspace / "library/include"),
        offline.HERE / "discovery_probe.c", real, fake, "-o", probe,
    ])
    if offline.needed(fake) != {"libc.so.6"}:
        raise ValueError("fake libusb unexpectedly links a backend")
    if offline.needed(probe) != {"libjaylink.so.0", "libusb-1.0.so.0", "libc.so.6"}:
        raise ValueError("probe is not linked to the genuine library and strict boundary")
    results = []

    def run(library, arguments, count):
        env = {"LD_LIBRARY_PATH": str(output) + ":" + str(library.parent)}
        # The loader's list mode does not enter library constructors/program code.
        listing = offline.command(
            ["/lib64/ld-linux-x86-64.so.2", "--list", probe],
            env={"PATH": "/usr/bin:/bin", "LC_ALL": "C", "HOME": "/nonexistent",
                 **env, "LD_PRELOAD": str(fake), "LD_BIND_NOW": "1"},
        )
        if "libjaylink.so.0 => " + str(library) + " (" not in listing:
            raise ValueError("wrong genuine library selected")
        if str(fake) + " (" not in listing:
            raise ValueError("fake libusb not selected")
        rc, text, seconds = offline.synthetic_run(probe, fake, variables=env, arguments=arguments)
        match = re.search(r"^cases=(\d+) admitted=(\d+)$", text, re.M)
        if rc or not match or int(match[1]) != count:
            raise AssertionError((arguments, rc, text))
        result = {
            "arguments": arguments, "cases": count, "admitted": int(match[2]),
            "library_sha256": offline.sha256(library), "seconds": seconds, "output": text,
        }
        results.append(result)
        return int(match[2])

    if run(baseline, ["baseline"], 1) != 0:
        raise AssertionError("unmodified library unexpectedly admits PID 1025")
    for axis, expected_admitted in (("pids", 21), ("vendors", 1)):
        admitted = sum(run(real, [axis, str(first), "4096"], 4096)
                       for first in range(0, 65536, 4096))
        if admitted != expected_admitted:
            raise AssertionError((axis, admitted))
    run(real, ["special"], 24)
    total = sum(result["cases"] for result in results)
    if total != EXPECTED_CASES or len(results) != EXPECTED_PROCESSES:
        raise AssertionError("discovery corpus count changed")
    evidence = {
        "schema": 1, "cases": total, "processes": len(results),
        "worst_seconds": max(result["seconds"] for result in results),
        "library_sha256": offline.sha256(real),
        "baseline_sha256": offline.sha256(baseline),
        "patch_sha256": lock["patch"]["sha256"],
        "fake_sha256": offline.sha256(fake), "probe_sha256": offline.sha256(probe),
        "test_sources": {name: offline.sha256(offline.HERE / name) for name in PROOF_SOURCES},
        "results": results,
    }
    (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return evidence


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    evidence = exercise(parser.parse_args().workspace)
    print(json.dumps({k: v for k, v in evidence.items() if k != "results"}, indent=2))
